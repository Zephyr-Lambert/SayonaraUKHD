/* Aspect-ratio correction.

   The game always projects its 3D scene for 16:9 and lets the output stretch it. Here the
   camera constants are observed as the CPU uploads them (CreateBuffer / Map+Unmap /
   UpdateSubresource), validated, and stored on the buffer as private data. Just before each
   3D draw into the gameplay target, a corrected copy of the matrices for the real display
   aspect is bound to VS slot b0, and the game's buffer is restored right after the draw.
   The camera itself never moves; only the projection's horizontal/vertical scale changes.
   No GPU readback is involved. See aspect_policy.h for the matrix math. */
#include "aspect_policy.h"

static BOOL aspectEnabled;
static float displayAspect; /* width/height of the game's output viewport; 0 until seen */

static const GUID cameraDataTag = {
    0x7106d459, 0x5607, 0x486f, {0x96, 0xb2, 0xb4, 0x4b, 0x25, 0xb3, 0xd9, 0x01}}; /* validated */
static const GUID outputTag = {0x7106d459, 0x5607, 0x486f, {0x96, 0xb2, 0xb4, 0x4b, 0x25, 0xb3, 0xd9, 0x02}};
static const GUID rawCameraTag = {
    0x7106d459, 0x5607, 0x486f, {0x96, 0xb2, 0xb4, 0x4b, 0x25, 0xb3, 0xd9, 0x03}}; /* unvalidated */

/* Open WRITE maps of camera-sized constant buffers, so the data can be read at Unmap. */
struct Mapping {
    ID3D11DeviceContext *c;
    ID3D11Resource *r;
    void *data;
};
static struct Mapping mappedCamera[64];

/* 192 bytes = main camera (projection, view, combined); 64 bytes = rope/line matrix. */
static BOOL cameraBuffer(ID3D11Resource *r) {
    ID3D11Buffer *b = NULL;
    if (!r || FAILED(ID3D11Resource_QueryInterface(r, &bufferIID, (void **)&b)))
        return FALSE;
    D3D11_BUFFER_DESC d;
    ID3D11Buffer_GetDesc(b, &d);
    ID3D11Buffer_Release(b);
    return (d.ByteWidth == 192 || d.ByteWidth == 64) && d.BindFlags == D3D11_BIND_CONSTANT_BUFFER;
}

/* Stores (or clears, when data is NULL) the CPU copy of a camera buffer's contents. */
static void rememberCamera(ID3D11Resource *r, const void *data) {
    ID3D11Buffer *b = NULL;
    if (FAILED(ID3D11Resource_QueryInterface(r, &bufferIID, (void **)&b)))
        return;
    D3D11_BUFFER_DESC bd;
    ID3D11Buffer_GetDesc(b, &bd);
    ID3D11Buffer_Release(b);
    float m[48] = {0};
    BOOL sized = bd.ByteWidth == 192 || bd.ByteWidth == 64, valid = FALSE;
    if (data && sized) {
        memcpy(m, data, bd.ByteWidth);
        valid = bd.ByteWidth == 192 ? umi_camera(m) : umi_rope_camera(m);
    }
    if (data && sized)
        ID3D11Resource_SetPrivateData(r, &rawCameraTag, bd.ByteWidth, m);
    else
        ID3D11Resource_SetPrivateData(r, &rawCameraTag, 0, NULL);
    if (valid)
        ID3D11Resource_SetPrivateData(r, &cameraDataTag, bd.ByteWidth, m);
    else
        ID3D11Resource_SetPrivateData(r, &cameraDataTag, 0, NULL);
}

/* ---- Upload hooks (shared with texture tracking) ------------------------------------- */

static HRESULT STDMETHODCALLTYPE cameraCreateBuffer(ID3D11Device *d, const D3D11_BUFFER_DESC *bd,
                                                    const D3D11_SUBRESOURCE_DATA *data, ID3D11Buffer **out) {
    HRESULT hr = ((BufferFn)original(d, SLOT_DEV_CREATE_BUFFER))(d, bd, data, out);
    if (SUCCEEDED(hr) && out && *out && bd && (bd->ByteWidth == 192 || bd->ByteWidth == 64) &&
        bd->BindFlags == D3D11_BIND_CONSTANT_BUFFER)
        rememberCamera((ID3D11Resource *)*out, data ? data->pSysMem : NULL);
    return hr;
}

static HRESULT STDMETHODCALLTYPE cameraMap(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub,
                                           D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE *out) {
    typedef HRESULT(STDMETHODCALLTYPE * Fn)(ID3D11DeviceContext *, ID3D11Resource *, UINT, D3D11_MAP, UINT,
                                            D3D11_MAPPED_SUBRESOURCE *);
    HRESULT hr = ((Fn)original(c, SLOT_CTX_MAP))(c, r, sub, type, flags, out);
    if (SUCCEEDED(hr) && type != D3D11_MAP_READ)
        textureInvalidate(r);
    if (SUCCEEDED(hr) && sub == 0 && type != D3D11_MAP_READ && cameraBuffer(r)) {
        rememberCamera(r, NULL);
        if (type == D3D11_MAP_WRITE_DISCARD || type == D3D11_MAP_WRITE || type == D3D11_MAP_READ_WRITE) {
            EnterCriticalSection(&lock);
            for (unsigned i = 0; i < 64; i++)
                if (!mappedCamera[i].r) {
                    mappedCamera[i] = (struct Mapping){c, r, out->pData};
                    break;
                }
            LeaveCriticalSection(&lock);
        }
    }
    return hr;
}

static void STDMETHODCALLTYPE cameraUnmap(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub) {
    typedef void(STDMETHODCALLTYPE * Fn)(ID3D11DeviceContext *, ID3D11Resource *, UINT);
    if (sub == 0) {
        EnterCriticalSection(&lock);
        for (unsigned i = 0; i < 64; i++)
            if (mappedCamera[i].c == c && mappedCamera[i].r == r) {
                rememberCamera(r, mappedCamera[i].data); /* read before the real Unmap */
                mappedCamera[i] = (struct Mapping){0};
                break;
            }
        LeaveCriticalSection(&lock);
    }
    ((Fn)original(c, SLOT_CTX_UNMAP))(c, r, sub);
}

static void STDMETHODCALLTYPE cameraUpdate(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub,
                                           const D3D11_BOX *box, const void *data, UINT row, UINT depth) {
    if (cameraBuffer(r))
        rememberCamera(r, sub == 0 && !box ? data : NULL);
    ((UpdateFn)original(c, SLOT_CTX_UPDATE))(c, r, sub, box, data, row, depth);
    textureUpdated(c, r, sub, box, data, row);
}

static void STDMETHODCALLTYPE cameraCopy(ID3D11DeviceContext *c, ID3D11Resource *dst, ID3D11Resource *src) {
    textureInvalidate(dst);
    if (cameraBuffer(dst))
        rememberCamera(dst, NULL);
    ((CopyFn)original(c, SLOT_CTX_COPY_RESOURCE))(c, dst, src);
}

static void STDMETHODCALLTYPE cameraCopyRegion(ID3D11DeviceContext *c, ID3D11Resource *dst, UINT sub, UINT x,
                                               UINT y, UINT z, ID3D11Resource *src, UINT ss,
                                               const D3D11_BOX *box) {
    typedef void(STDMETHODCALLTYPE * Fn)(ID3D11DeviceContext *, ID3D11Resource *, UINT, UINT, UINT, UINT,
                                         ID3D11Resource *, UINT, const D3D11_BOX *);
    textureInvalidate(dst);
    if (cameraBuffer(dst))
        rememberCamera(dst, NULL);
    ((Fn)original(c, SLOT_CTX_COPY_REGION))(c, dst, sub, x, y, z, src, ss, box);
}

/* Tags the swap chain's back buffer so draws to the final output can be recognised. */
static void markOutput(IDXGISwapChain *s) {
    if (!s)
        return;
    ID3D11Texture2D *t = NULL;
    if (SUCCEEDED(IDXGISwapChain_GetBuffer(s, 0, &texture2DIID, (void **)&t))) {
        UINT yes = 1;
        ID3D11Texture2D_SetPrivateData(t, &outputTag, sizeof(yes), &yes);
        ID3D11Texture2D_Release(t);
    }
}

/* ---- Per-draw correction --------------------------------------------------------------- */

/* Mod-owned constant buffers holding the corrected matrices: [0] = 64-byte rope, [1] = 192-byte
   camera. Reused and refreshed with UpdateSubresource only when the corrected values change,
   rather than creating a new buffer each time the camera moves. */
static struct {
    ID3D11Device *device; /* weak: the buffers keep the device alive */
    ID3D11Buffer *buffer[2];
    float data[2][48];
} aspectBuffers;

static ID3D11Buffer *aspectBufferFor(ID3D11DeviceContext *c, const float *changed, UINT size) {
    unsigned k = size == 192;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext_GetDevice(c, &device);
    if (aspectBuffers.device != device) {
        for (unsigned i = 0; i < 2; i++)
            if (aspectBuffers.buffer[i]) {
                ID3D11Buffer_Release(aspectBuffers.buffer[i]);
                aspectBuffers.buffer[i] = NULL;
            }
        aspectBuffers.device = device;
    }
    if (!aspectBuffers.buffer[k]) {
        D3D11_BUFFER_DESC bd = {0};
        bd.ByteWidth = size;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA init = {0};
        init.pSysMem = changed;
        /* Original CreateBuffer: our own buffers need no camera tracking. */
        HRESULT hr = ((BufferFn)original(device, SLOT_DEV_CREATE_BUFFER))(device, &bd, &init,
                                                                          &aspectBuffers.buffer[k]);
        if (FAILED(hr)) {
            aspectBuffers.buffer[k] = NULL;
            logline(TRUE, "ASPECT replacement failed hr=0x%08lx", (unsigned long)hr);
        } else
            memcpy(aspectBuffers.data[k], changed, size);
    } else if (memcmp(aspectBuffers.data[k], changed, size)) {
        ((UpdateFn)original(c, SLOT_CTX_UPDATE))(c, (ID3D11Resource *)aspectBuffers.buffer[k], 0, NULL,
                                                 changed, 0, 0);
        memcpy(aspectBuffers.data[k], changed, size);
    }
    ID3D11Device_Release(device);
    return aspectBuffers.buffer[k];
}

/* Called before every draw. If this is a 3D camera draw into the gameplay target, binds the
   corrected matrices and returns the game's original b0 buffer (to restore after the draw). */
static ID3D11Buffer *correctDraw(ID3D11DeviceContext *c) {
    if (!aspectEnabled || ID3D11DeviceContext_GetType(c) != D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return NULL;
    D3D11_VIEWPORT vp[16];
    UINT nv = 16;
    ID3D11DeviceContext_RSGetViewports(c, &nv, vp);
    if (nv != 1 || vp[0].Height <= 0)
        return NULL;
    ID3D11RenderTargetView *rt = NULL;
    ID3D11DeviceContext_OMGetRenderTargets(c, 1, &rt, NULL);
    if (!rt)
        return NULL;
    ID3D11Resource *r = NULL;
    ID3D11RenderTargetView_GetResource(rt, &r);
    ID3D11RenderTargetView_Release(rt);
    if (!r)
        return NULL;

    /* A draw to the final output: its viewport reveals the displayed aspect ratio
       (this is where the game's own aspect slider takes effect). */
    UINT tag = 0, sz = sizeof(tag);
    if (SUCCEEDED(ID3D11Resource_GetPrivateData(r, &outputTag, &sz, &tag)) && tag == 1) {
        float a = vp[0].Width / vp[0].Height;
        if (a >= 0.2f && a <= 8 && a != displayAspect) {
            displayAspect = a;
            logline(TRUE, "ASPECT output viewport=%.3fx%.3f ratio=%.6f", vp[0].Width, vp[0].Height, a);
        }
        ID3D11Resource_Release(r);
        return NULL;
    }

    /* Otherwise only the enlarged gameplay colour target qualifies. */
    ID3D11Texture2D *t = NULL;
    ID3D11Resource_QueryInterface(r, &texture2DIID, (void **)&t);
    ID3D11Resource_Release(r);
    if (!t)
        return NULL;
    D3D11_TEXTURE2D_DESC td;
    ID3D11Texture2D_GetDesc(t, &td);
    ID3D11Texture2D_Release(t);
    if (vp[0].Width != render.viewport || vp[0].Height != render.height || vp[0].TopLeftX != 0 ||
        vp[0].TopLeftY != 0 || td.Width != render.width || td.Height != render.height || td.Format != 28 ||
        td.BindFlags != 0x28 || displayAspect == 0)
        return NULL;

    uint64_t hash = boundVertexShader(c);
    BOOL rope = hash == ROPE_SHADER;
    if (!rope && !isCameraShader(hash))
        return NULL;
    ID3D11Buffer *b = NULL;
    ID3D11DeviceContext_VSGetConstantBuffers(c, 0, 1, &b);
    if (!b)
        return NULL;
    float raw[48] = {0}, changed[48] = {0};
    sz = sizeof(raw);
    if (FAILED(ID3D11Buffer_GetPrivateData(b, &cameraDataTag, &sz, raw)) ||
        (rope ? (sz != 64 || !umi_correct_rope(raw, displayAspect, changed))
              : (sz != 192 || !umi_correct_camera(raw, displayAspect, changed)))) {
        if (!rope)
            VLOG("ASPECT skipped: no validated CPU camera data shader=%016llx", (unsigned long long)hash);
        ID3D11Buffer_Release(b);
        return NULL;
    }
    ID3D11Buffer *replacement = aspectBufferFor(c, changed, sz);
    if (!replacement) {
        ID3D11Buffer_Release(b);
        return NULL;
    }
    ID3D11DeviceContext_VSSetConstantBuffers(c, 0, 1, &replacement);
    /* The rope matrix changes as the rope moves, so log it without per-frame values. */
    if (rope)
        VLOG("ASPECT corrected rope target=%.6f", displayAspect);
    else
        VLOG("ASPECT corrected shader=%016llx native=%.6f target=%.6f", (unsigned long long)hash,
             raw[5] / raw[0], displayAspect);
    return b;
}

static void restoreCamera(ID3D11DeviceContext *c, ID3D11Buffer *b) {
    if (b) {
        ID3D11DeviceContext_VSSetConstantBuffers(c, 0, 1, &b);
        ID3D11Buffer_Release(b);
    }
}
