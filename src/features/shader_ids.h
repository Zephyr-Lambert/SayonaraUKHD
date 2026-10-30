/* Vertex shader identification.
   Each vertex shader is tagged with the FNV-1a hash of its bytecode when created. The aspect
   correction and FXAA use the tag to recognise the game's 3D camera shaders (camera_shaders.h)
   and its line/rope shader. */
#include "camera_shaders.h"

static const GUID shaderTag = {0xc93d4bee, 0x508e, 0x4e77, {0x99, 0x64, 0x39, 0xa2, 0xc1, 0x01, 0xc3, 0x27}};

/* The rope/line shader: a separate 64-byte column-major matrix. Also used, with an identity
   matrix, for the orthographic overlay that marks the start of the HUD. */
#define ROPE_SHADER UINT64_C(0x1ed5e1af9df5ec9e)

static BOOL isCameraShader(uint64_t hash) {
    for (unsigned i = 0; i < sizeof(cameraShaders) / sizeof(*cameraShaders); i++)
        if (cameraShaders[i] == hash)
            return TRUE;
    return FALSE;
}

/* Hash of the currently bound vertex shader, or 0. */
static uint64_t boundVertexShader(ID3D11DeviceContext *c) {
    ID3D11VertexShader *vs = NULL;
    ID3D11DeviceContext_VSGetShader(c, &vs, NULL, NULL);
    uint64_t hash = 0;
    UINT size = sizeof(hash);
    if (vs) {
        ID3D11VertexShader_GetPrivateData(vs, &shaderTag, &size, &hash);
        ID3D11VertexShader_Release(vs);
    }
    return hash;
}

static HRESULT STDMETHODCALLTYPE hookedVertexShader(ID3D11Device *d, const void *code, SIZE_T len,
                                                    ID3D11ClassLinkage *link, ID3D11VertexShader **out) {
    typedef HRESULT(STDMETHODCALLTYPE * Fn)(ID3D11Device *, const void *, SIZE_T, ID3D11ClassLinkage *,
                                            ID3D11VertexShader **);
    static unsigned shaderFiles;
    HRESULT hr = ((Fn)original(d, SLOT_DEV_CREATE_VERTEX_SHADER))(d, code, len, link, out);
    if (SUCCEEDED(hr) && out && *out && code && len && len <= 1048576) {
        uint64_t hash = hashBytes(code, len);
        ID3D11VertexShader_SetPrivateData(*out, &shaderTag, sizeof(hash), &hash);
        if (diagnostics && shaderFiles++ < 128) {
            wchar_t name[100];
            swprintf(name, 100, L"vs-%016llx.dxbc", (unsigned long long)hash);
            BOOL ok = debugWrite(name, code, (DWORD)len);
            logline(TRUE, "DEBUG vertex shader=%016llx bytes=%lu saved=%d", (unsigned long long)hash,
                    (unsigned long)len, ok);
        }
    }
    return hr;
}
