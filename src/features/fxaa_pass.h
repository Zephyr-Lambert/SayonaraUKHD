/* FXAA on the 3D scene only.
   The scene target is recognised from the first camera draw of each frame. FXAA is applied
   once per frame, just before the HUD starts (the identity-matrix orthographic overlay) or,
   failing that, when the game switches away from the scene target. The scene is copied to a
   mod-owned texture and the FXAA shader draws a full-screen triangle back over it, after which
   all touched pipeline state is restored. F7 toggles it at runtime. Shader: shaders/fxaa.hlsl. */
#include "fxaa_bytecode.h"
static BOOL fxaaConfigured, fxaaEnabled, fxaaDone;
static ID3D11Texture2D *fxaaScene;
static unsigned fxaaApplied, fxaaBeforeUI, fxaaAtSwitch;
static struct {
    ID3D11Device *device;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11SamplerState *sampler;
    ID3D11BlendState *blend;
    ID3D11DepthStencilState *depth;
    ID3D11RasterizerState *raster;
    ID3D11Buffer *constants;
    ID3D11Texture2D *copy;
    ID3D11ShaderResourceView *srv;
    UINT width, height;
    BOOL constantsReady; /* settings buffer filled for the current size */
    BOOL failed;
} fx;
#define FX_RELEASE(p)                                                                                        \
    do {                                                                                                     \
        if (p) {                                                                                             \
            IUnknown_Release((IUnknown *)(p));                                                               \
            (p) = NULL;                                                                                      \
        }                                                                                                    \
    } while (0)
static void fxaaReleaseObjects(void) {
    FX_RELEASE(fx.srv);
    FX_RELEASE(fx.copy);
    FX_RELEASE(fx.constants);
    FX_RELEASE(fx.vs);
    FX_RELEASE(fx.ps);
    FX_RELEASE(fx.sampler);
    FX_RELEASE(fx.blend);
    FX_RELEASE(fx.depth);
    FX_RELEASE(fx.raster);
    FX_RELEASE(fx.device);
    memset(&fx, 0, sizeof(fx));
}
static BOOL fxaaPrepare(ID3D11Device *d, UINT w, UINT h) {
    if (fx.device != d) {
        fxaaReleaseObjects();
        fx.device = d;
        ID3D11Device_AddRef(d);
    }
    if (fx.failed)
        return FALSE;
    HRESULT hr = S_OK;
    if (!fx.vs) {
        hr = ID3D11Device_CreateVertexShader(d, fxaaVS, sizeof(fxaaVS), NULL, &fx.vs);
        if (FAILED(hr))
            goto fail;
        hr = ID3D11Device_CreatePixelShader(d, fxaaPS, sizeof(fxaaPS), NULL, &fx.ps);
        if (FAILED(hr))
            goto fail;
        D3D11_SAMPLER_DESC s = {0};
        s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        s.ComparisonFunc = D3D11_COMPARISON_NEVER;
        s.MaxAnisotropy = 1;
        s.MaxLOD = D3D11_FLOAT32_MAX;
        hr = ID3D11Device_CreateSamplerState(d, &s, &fx.sampler);
        if (FAILED(hr))
            goto fail;
        D3D11_BLEND_DESC b = {0};
        for (unsigned i = 0; i < 8; i++) {
            b.RenderTarget[i].SrcBlend = b.RenderTarget[i].SrcBlendAlpha = D3D11_BLEND_ONE;
            b.RenderTarget[i].DestBlend = b.RenderTarget[i].DestBlendAlpha = D3D11_BLEND_ZERO;
            b.RenderTarget[i].BlendOp = b.RenderTarget[i].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            b.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        }
        hr = ID3D11Device_CreateBlendState(d, &b, &fx.blend);
        if (FAILED(hr))
            goto fail;
        D3D11_DEPTH_STENCIL_DESC depth = {0};
        depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
        depth.FrontFace.StencilFailOp = depth.FrontFace.StencilDepthFailOp = depth.FrontFace.StencilPassOp =
            D3D11_STENCIL_OP_KEEP;
        depth.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
        depth.BackFace = depth.FrontFace;
        hr = ID3D11Device_CreateDepthStencilState(d, &depth, &fx.depth);
        if (FAILED(hr))
            goto fail;
        D3D11_RASTERIZER_DESC r = {0};
        r.FillMode = D3D11_FILL_SOLID;
        r.CullMode = D3D11_CULL_NONE;
        r.DepthClipEnable = TRUE;
        hr = ID3D11Device_CreateRasterizerState(d, &r, &fx.raster);
        if (FAILED(hr))
            goto fail;
        D3D11_BUFFER_DESC cb = {0};
        cb.ByteWidth = 16;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = ID3D11Device_CreateBuffer(d, &cb, NULL, &fx.constants);
        if (FAILED(hr))
            goto fail;
    }
    if (!fx.copy || fx.width != w || fx.height != h) {
        FX_RELEASE(fx.srv);
        FX_RELEASE(fx.copy);
        D3D11_TEXTURE2D_DESC t = {0};
        t.Width = w;
        t.Height = h;
        t.MipLevels = t.ArraySize = t.SampleDesc.Count = 1;
        t.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        t.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        hr = ((TexFn)original(d, SLOT_DEV_CREATE_TEXTURE2D))(d, &t, NULL, &fx.copy);
        if (FAILED(hr))
            goto fail;
        hr = ID3D11Device_CreateShaderResourceView(d, (ID3D11Resource *)fx.copy, NULL, &fx.srv);
        if (FAILED(hr))
            goto fail;
        fx.width = w;
        fx.height = h;
        fx.constantsReady = FALSE;
        logline(FALSE, "FXAA resources ready: scene=%ux%u extraColorBytes=%llu", w, h,
                (unsigned long long)w * h * 4);
    }
    return TRUE;
fail:
    logline(FALSE, "FXAA disabled for this device: resource creation failed hr=0x%08lx", (unsigned long)hr);
    fx.failed = TRUE;
    return FALSE;
}
/* Owns the returned reference. Exact observed scene target and viewport only. */
static ID3D11Texture2D *fxaaCurrentScene(ID3D11DeviceContext *c) {
    if (ID3D11DeviceContext_GetType(c) != D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return NULL;
    D3D11_VIEWPORT vp[16];
    UINT count = 16;
    ID3D11DeviceContext_RSGetViewports(c, &count, vp);
    if (count != 1 || vp[0].TopLeftX != 0 || vp[0].TopLeftY != 0 || vp[0].Width != render.viewport ||
        vp[0].Height != render.height || vp[0].MinDepth != 0 || vp[0].MaxDepth != 1)
        return NULL;
    ID3D11RenderTargetView *rt[8] = {0};
    ID3D11DeviceContext_OMGetRenderTargets(c, 8, rt, NULL);
    BOOL single = rt[0] != NULL;
    for (unsigned i = 1; i < 8; i++)
        if (rt[i])
            single = FALSE;
    ID3D11Resource *r = NULL;
    if (single)
        ID3D11RenderTargetView_GetResource(rt[0], &r);
    for (unsigned i = 0; i < 8; i++)
        FX_RELEASE(rt[i]);
    if (!r)
        return NULL;
    ID3D11Texture2D *t = NULL;
    ID3D11Resource_QueryInterface(r, &texture2DIID, (void **)&t);
    FX_RELEASE(r);
    if (t) {
        D3D11_TEXTURE2D_DESC d;
        ID3D11Texture2D_GetDesc(t, &d);
        if (d.Width != render.width || d.Height != render.height || d.Format != 28 || d.BindFlags != 0x28 ||
            d.MipLevels != 1 || d.ArraySize != 1 || d.SampleDesc.Count != 1)
            FX_RELEASE(t);
    }
    return t;
}
static void fxaaApply(ID3D11DeviceContext *c, BOOL beforeUI) {
    if (!fxaaEnabled || !fxaaScene || fxaaDone)
        return;
    ID3D11Texture2D *current = fxaaCurrentScene(c);
    BOOL match = current == fxaaScene;
    FX_RELEASE(current);
    if (!match)
        return;
    /* Unknown geometry/tessellation/stream-output pipelines are deliberately skipped. */
    ID3D11GeometryShader *gs = NULL;
    ID3D11HullShader *hs = NULL;
    ID3D11DomainShader *ds = NULL;
    ID3D11DeviceContext_GSGetShader(c, &gs, NULL, NULL);
    ID3D11DeviceContext_HSGetShader(c, &hs, NULL, NULL);
    ID3D11DeviceContext_DSGetShader(c, &ds, NULL, NULL);
    ID3D11Buffer *so[4] = {0};
    ID3D11DeviceContext_SOGetTargets(c, 4, so);
    BOOL unsupported = gs || hs || ds;
    FX_RELEASE(gs);
    FX_RELEASE(hs);
    FX_RELEASE(ds);
    for (unsigned i = 0; i < 4; i++) {
        if (so[i])
            unsupported = TRUE;
        FX_RELEASE(so[i]);
    }
    if (unsupported) {
        logline(TRUE, "FXAA skipped unsupported geometry/tessellation/stream-output state");
        return;
    }
    ID3D11Device *device = NULL;
    ID3D11DeviceContext_GetDevice(c, &device);
    BOOL ready = fxaaPrepare(device, render.width, render.height);
    FX_RELEASE(device);
    if (!ready)
        return;

    ID3D11VertexShader *vs = NULL;
    ID3D11PixelShader *ps = NULL;
    ID3D11ClassInstance *vc[256] = {0}, *pc[256] = {0};
    UINT vn = 256, pn = 256;
    ID3D11DeviceContext_VSGetShader(c, &vs, vc, &vn);
    ID3D11DeviceContext_PSGetShader(c, &ps, pc, &pn);
    ID3D11Buffer *cb = NULL;
    ID3D11ShaderResourceView *srv = NULL;
    ID3D11SamplerState *sampler = NULL;
    ID3D11DeviceContext_PSGetConstantBuffers(c, 0, 1, &cb);
    ID3D11DeviceContext_PSGetShaderResources(c, 0, 1, &srv);
    ID3D11DeviceContext_PSGetSamplers(c, 0, 1, &sampler);
    ID3D11InputLayout *layout = NULL;
    D3D11_PRIMITIVE_TOPOLOGY topology;
    ID3D11DeviceContext_IAGetInputLayout(c, &layout);
    ID3D11DeviceContext_IAGetPrimitiveTopology(c, &topology);
    ID3D11BlendState *blend = NULL;
    FLOAT blendFactor[4];
    UINT sampleMask;
    ID3D11DepthStencilState *depth = NULL;
    UINT stencil;
    ID3D11RasterizerState *raster = NULL;
    ID3D11Predicate *predicate = NULL;
    BOOL predicateValue;
    ID3D11DeviceContext_OMGetBlendState(c, &blend, blendFactor, &sampleMask);
    ID3D11DeviceContext_OMGetDepthStencilState(c, &depth, &stencil);
    ID3D11DeviceContext_RSGetState(c, &raster);
    ID3D11DeviceContext_GetPredication(c, &predicate, &predicateValue);
    ID3D11DeviceContext_SetPredication(c, NULL, FALSE);
    /* Original Copy/Update functions: mod-owned resources need no texture or camera tracking. */
    ((CopyFn)original(c, SLOT_CTX_COPY_RESOURCE))(c, (ID3D11Resource *)fx.copy, (ID3D11Resource *)fxaaScene);
    if (!fx.constantsReady) {
        float values[4] = {1.0f / render.width, 1.0f / render.height,
                           (floorf(render.viewport + 0.5f) - 0.5f) / render.width,
                           (render.height - 0.5f) / render.height};
        ((UpdateFn)original(c, SLOT_CTX_UPDATE))(c, (ID3D11Resource *)fx.constants, 0, NULL, values, 0, 0);
        fx.constantsReady = TRUE;
    }
    ID3D11DeviceContext_VSSetShader(c, fx.vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(c, fx.ps, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(c, 0, 1, &fx.constants);
    ID3D11DeviceContext_PSSetShaderResources(c, 0, 1, &fx.srv);
    ID3D11DeviceContext_PSSetSamplers(c, 0, 1, &fx.sampler);
    ID3D11DeviceContext_IASetInputLayout(c, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(c, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_OMSetBlendState(c, fx.blend, NULL, 0xffffffff);
    ID3D11DeviceContext_OMSetDepthStencilState(c, fx.depth, 0);
    ID3D11DeviceContext_RSSetState(c, fx.raster);
    ((DrawFn)original(c, SLOT_CTX_DRAW))(c, 3, 0);
    ID3D11DeviceContext_VSSetShader(c, vs, vc, vn);
    ID3D11DeviceContext_PSSetShader(c, ps, pc, pn);
    ID3D11DeviceContext_PSSetConstantBuffers(c, 0, 1, &cb);
    ID3D11DeviceContext_PSSetShaderResources(c, 0, 1, &srv);
    ID3D11DeviceContext_PSSetSamplers(c, 0, 1, &sampler);
    ID3D11DeviceContext_IASetInputLayout(c, layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(c, topology);
    ID3D11DeviceContext_OMSetBlendState(c, blend, blendFactor, sampleMask);
    ID3D11DeviceContext_OMSetDepthStencilState(c, depth, stencil);
    ID3D11DeviceContext_RSSetState(c, raster);
    ID3D11DeviceContext_SetPredication(c, predicate, predicateValue);
    FX_RELEASE(vs);
    FX_RELEASE(ps);
    FX_RELEASE(cb);
    FX_RELEASE(srv);
    FX_RELEASE(sampler);
    FX_RELEASE(layout);
    FX_RELEASE(blend);
    FX_RELEASE(depth);
    FX_RELEASE(raster);
    FX_RELEASE(predicate);
    for (UINT i = 0; i < vn; i++)
        FX_RELEASE(vc[i]);
    for (UINT i = 0; i < pn; i++)
        FX_RELEASE(pc[i]);
    fxaaDone = TRUE;
    ++fxaaApplied;
    if (beforeUI)
        ++fxaaBeforeUI;
    else
        ++fxaaAtSwitch;
    VLOG("FXAA applied before %s at %.3fx%u", beforeUI ? "orthographic overlay" : "scene target switch",
         render.viewport, render.height);
}
static void fxaaBeforeDraw(ID3D11DeviceContext *c) {
    if (!fxaaEnabled || fxaaDone)
        return;
    ID3D11Texture2D *t = fxaaCurrentScene(c);
    if (!t)
        return;
    uint64_t hash = boundVertexShader(c);
    BOOL known = isCameraShader(hash);
    BOOL flat = hash == ROPE_SHADER;
    ID3D11Buffer *cb = NULL;
    float m[48] = {0};
    UINT size = sizeof(m);
    if (known || flat)
        ID3D11DeviceContext_VSGetConstantBuffers(c, 0, 1, &cb);
    BOOL hasData = cb && SUCCEEDED(ID3D11Buffer_GetPrivateData(cb, &rawCameraTag, &size, m));
    FX_RELEASE(cb);
    BOOL scene =
        hasData && ((known && size == 192 && umi_camera(m)) || (flat && size == 64 && umi_rope_camera(m)));
    if (scene && fxaaScene != t) {
        FX_RELEASE(fxaaScene);
        fxaaScene = t;
        ID3D11Texture2D_AddRef(t);
    }
    if (hasData && flat && size == 64 && fxaaScene == t) {
        BOOL identity = TRUE;
        for (unsigned i = 0; i < 16; i++)
            if (!isfinite(m[i]) || fabsf(m[i] - ((i % 5 == 0) ? 1.0f : 0.0f)) > 0.00001f)
                identity = FALSE;
        if (identity)
            fxaaApply(c, TRUE);
    }
    FX_RELEASE(t);
}
static void fxaaBeforeTargets(ID3D11DeviceContext *c, UINT n, ID3D11RenderTargetView *const *views) {
    if (!fxaaEnabled || !fxaaScene || fxaaDone)
        return;
    ID3D11Resource *next = NULL;
    if (n && views && views[0])
        ID3D11RenderTargetView_GetResource(views[0], &next);
    BOOL same = next == (ID3D11Resource *)fxaaScene;
    FX_RELEASE(next);
    if (!same)
        fxaaApply(c, FALSE);
}
static void fxaaPresent(void) {
    FX_RELEASE(fxaaScene);
    fxaaDone = FALSE;
    static BOOL held;
    BOOL down = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
    DWORD foregroundPID = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPID);
    if (fxaaConfigured && foregroundPID == GetCurrentProcessId() && down && !held) {
        fxaaEnabled = !fxaaEnabled;
        logline(FALSE, "FXAA F7 toggle: enabled=%d", fxaaEnabled);
    }
    held = down;
}
static void fxaaStats(void) {
    if (fxaaConfigured)
        logline(FALSE, "FXAA enabled=%d appliedFrames=%u beforeUI=%u targetSwitch=%u failed=%d", fxaaEnabled,
                fxaaApplied, fxaaBeforeUI, fxaaAtSwitch, fx.failed);
}
#undef FX_RELEASE
