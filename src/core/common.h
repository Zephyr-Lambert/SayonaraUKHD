/* Shared includes, D3D11 vtable slot checks, function types and global state.
   Everything is compiled as one translation unit from main.c, so all state is static. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <wchar.h>
#include <stdlib.h>
#include "scale_policy.h"
#include "resolution.h"

#define UMI_NAME "SayonaraUKHD"
#define UMI_VERSION "1.0"

/* COM methods are hooked by patching shared vtable slots. These asserts pin the
   slot numbers used in attach() so a header mismatch fails at compile time. */
_Static_assert(sizeof(void *) == 4, "Build this DLL for x86");
_Static_assert(offsetof(ID3D11DeviceVtbl, CreateBuffer) / sizeof(void *) == 3, "CreateBuffer ABI");
_Static_assert(offsetof(ID3D11DeviceVtbl, CreateTexture2D) / sizeof(void *) == 5, "CreateTexture2D ABI");
_Static_assert(offsetof(ID3D11DeviceVtbl, CreateInputLayout) / sizeof(void *) == 11, "Layout ABI");
_Static_assert(offsetof(ID3D11DeviceVtbl, CreateVertexShader) / sizeof(void *) == 12, "VS ABI");
_Static_assert(offsetof(ID3D11DeviceVtbl, CreatePixelShader) / sizeof(void *) == 15, "PS ABI");
_Static_assert(offsetof(ID3D11DeviceContextVtbl, PSSetShaderResources) / sizeof(void *) == 8, "PS SRV ABI");
_Static_assert(offsetof(ID3D11DeviceContextVtbl, DrawIndexed) / sizeof(void *) == 12, "DrawIndexed ABI");
_Static_assert(offsetof(ID3D11DeviceContextVtbl, Draw) / sizeof(void *) == 13, "Draw ABI");
_Static_assert(offsetof(ID3D11DeviceContextVtbl, Map) / sizeof(void *) == 14, "Map ABI");
_Static_assert(offsetof(ID3D11DeviceContextVtbl, Unmap) / sizeof(void *) == 15, "Unmap ABI");
_Static_assert(offsetof(ID3D11DeviceContextVtbl, OMSetRenderTargets) / sizeof(void *) == 33, "RT ABI");
_Static_assert(offsetof(ID3D11DeviceContextVtbl, RSSetViewports) / sizeof(void *) == 44, "viewport ABI");
_Static_assert(offsetof(ID3D11DeviceContextVtbl, RSSetScissorRects) / sizeof(void *) == 45, "scissor ABI");
_Static_assert(offsetof(ID3D11DeviceContextVtbl, CopySubresourceRegion) / sizeof(void *) == 46,
               "CopyRegion ABI");
_Static_assert(offsetof(ID3D11DeviceContextVtbl, CopyResource) / sizeof(void *) == 47, "CopyResource ABI");
_Static_assert(offsetof(ID3D11DeviceContextVtbl, UpdateSubresource) / sizeof(void *) == 48, "Update ABI");
_Static_assert(offsetof(IDXGISwapChainVtbl, Present) / sizeof(void *) == 8, "Present ABI");
_Static_assert(offsetof(IDXGISwapChainVtbl, ResizeBuffers) / sizeof(void *) == 13, "Resize ABI");

/* Vtable slot numbers, named for readability. */
enum {
    SLOT_DEV_CREATE_BUFFER = 3,
    SLOT_DEV_CREATE_TEXTURE2D = 5,
    SLOT_DEV_CREATE_INPUT_LAYOUT = 11,
    SLOT_DEV_CREATE_VERTEX_SHADER = 12,
    SLOT_DEV_CREATE_PIXEL_SHADER = 15,
    SLOT_CTX_PS_SET_RESOURCES = 8,
    SLOT_CTX_DRAW_INDEXED = 12,
    SLOT_CTX_DRAW = 13,
    SLOT_CTX_MAP = 14,
    SLOT_CTX_UNMAP = 15,
    SLOT_CTX_SET_TARGETS = 33,
    SLOT_CTX_SET_VIEWPORTS = 44,
    SLOT_CTX_SET_SCISSORS = 45,
    SLOT_CTX_COPY_REGION = 46,
    SLOT_CTX_COPY_RESOURCE = 47,
    SLOT_CTX_UPDATE = 48,
    SLOT_SWAP_PRESENT = 8,
    SLOT_SWAP_RESIZE = 13,
};

typedef HRESULT(WINAPI *CreateFn)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                  UINT, UINT, const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **,
                                  ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT(WINAPI *DeviceFn)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                  UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT(STDMETHODCALLTYPE *BufferFn)(ID3D11Device *, const D3D11_BUFFER_DESC *,
                                             const D3D11_SUBRESOURCE_DATA *, ID3D11Buffer **);
typedef HRESULT(STDMETHODCALLTYPE *TexFn)(ID3D11Device *, const D3D11_TEXTURE2D_DESC *,
                                          const D3D11_SUBRESOURCE_DATA *, ID3D11Texture2D **);
typedef void(STDMETHODCALLTYPE *DrawFn)(ID3D11DeviceContext *, UINT, UINT);
typedef void(STDMETHODCALLTYPE *DrawIndexedFn)(ID3D11DeviceContext *, UINT, UINT, INT);
typedef void(STDMETHODCALLTYPE *VpFn)(ID3D11DeviceContext *, UINT, const D3D11_VIEWPORT *);
typedef void(STDMETHODCALLTYPE *ScissorFn)(ID3D11DeviceContext *, UINT, const D3D11_RECT *);
typedef void(STDMETHODCALLTYPE *RtFn)(ID3D11DeviceContext *, UINT, ID3D11RenderTargetView *const *,
                                      ID3D11DepthStencilView *);
typedef void(STDMETHODCALLTYPE *CopyFn)(ID3D11DeviceContext *, ID3D11Resource *, ID3D11Resource *);
typedef void(STDMETHODCALLTYPE *UpdateFn)(ID3D11DeviceContext *, ID3D11Resource *, UINT, const D3D11_BOX *,
                                          const void *, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE *PresentFn)(IDXGISwapChain *, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE *ResizeFn)(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);

/* Process-wide state. */
static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION lock; /* log file, hook table writes, camera map tracking */
static HMODULE backend, selfModule;
static CreateFn realCreate;
static DeviceFn realDevice;
static BOOL active; /* TRUE only inside UmiByeWin.exe */
static LONG frames;

/* [Rendering] */
static BOOL customResolution;
static BOOL invalidConfig;
static UmiSize render = {1024, 512, 896.0f};
static uint32_t requestedWidth = 896, requestedHeight = 512;

/* [Debug] */
static BOOL logEnabled;  /* write UmiGraphics.log at all ([Debug] Log, off by default) */
static BOOL verboseLog;  /* per-draw / per-call trace lines */
static BOOL diagnostics; /* developer capture tools (shader dumps, F8 frame capture) */

static const GUID texture2DIID = {
    0x6f15aaf2, 0xd208, 0x4e89, {0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c}};
static const GUID bufferIID = {0x48570b85, 0xd1ee, 0x4fcd, {0xa2, 0x50, 0xeb, 0x35, 0x07, 0x22, 0xb0, 0x37}};

/* 64-bit FNV-1a. Also used for shader identity, so it must not change. */
static uint64_t hashBytes(const void *data, size_t n) {
    uint64_t h = UINT64_C(14695981039346656037);
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * UINT64_C(1099511628211);
    return h;
}
