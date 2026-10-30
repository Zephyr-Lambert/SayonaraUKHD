/* SayonaraUKHD, formerly UmiGraphics:
   graphics enhancements for Sayonara Umihara Kawase (UmiByeWin.exe, x86, Direct3D 11).

   Built as a d3d11.dll proxy. The game calls our D3D11CreateDevice(AndSwapChain); we forward to
   the system d3d11.dll and then hook selected device, context and swap-chain methods.
   Inside any other executable the proxy only forwards and changes nothing.

   This file is the single translation unit; the modules below are included in dependency order.
     core/      shared state, logging, vtable hooking
     policy/    pure, unit-tested math and format rules (no D3D calls)
     features/  resolution, aspect correction, culling, FXAA, textures
     debug/     developer capture tools, only active with [Debug] Diagnostics=1 */
#include "core/common.h"
#include "core/log.h"
#include "core/hooks.h"
#include "debug/debug_files.h"
#include "features/texture_system.h"
#include "features/resolution_hooks.h"
#include "features/shader_ids.h"
#include "features/aspect_hooks.h"
#include "debug/frame_capture.h"
#include "features/fxaa_pass.h"
#include "features/culling_hook.h"
#include "features/frame_hooks.h"

/* ---- Hook installation ------------------------------------------------------------------ */

static void attach(ID3D11Device *d, ID3D11DeviceContext *c, IDXGISwapChain *s) {
    if (!active)
        return;
    if (diagnostics) {
        patch(d, SLOT_DEV_CREATE_INPUT_LAYOUT, (void *)probeLayout);
        patch(d, SLOT_DEV_CREATE_PIXEL_SHADER, (void *)probePixelShader);
    }
    patch(d, SLOT_DEV_CREATE_BUFFER, (void *)cameraCreateBuffer);
    markOutput(s);
    patch(d, SLOT_DEV_CREATE_TEXTURE2D, (void *)hookedTexture);
    patch(d, SLOT_DEV_CREATE_VERTEX_SHADER, (void *)hookedVertexShader);
    ID3D11DeviceContext *own = NULL;
    if (d && !c) {
        ID3D11Device_GetImmediateContext(d, &own);
        c = own;
    }
    patch(c, SLOT_CTX_PS_SET_RESOURCES, (void *)texturePSResources);
    patch(c, SLOT_CTX_MAP, (void *)cameraMap);
    patch(c, SLOT_CTX_UNMAP, (void *)cameraUnmap);
    patch(c, SLOT_CTX_COPY_REGION, (void *)cameraCopyRegion);
    patch(c, SLOT_CTX_COPY_RESOURCE, (void *)cameraCopy);
    patch(c, SLOT_CTX_UPDATE, (void *)cameraUpdate);
    patch(c, SLOT_CTX_DRAW_INDEXED, (void *)hookedDrawIndexed);
    patch(c, SLOT_CTX_DRAW, (void *)hookedDraw);
    patch(c, SLOT_CTX_SET_TARGETS, (void *)hookedTargets);
    patch(c, SLOT_CTX_SET_VIEWPORTS, (void *)hookedViewport);
    patch(c, SLOT_CTX_SET_SCISSORS, (void *)hookedScissors);
    patch(s, SLOT_SWAP_PRESENT, (void *)hookedPresent);
    patch(s, SLOT_SWAP_RESIZE, (void *)hookedResize);
    if (own)
        ID3D11DeviceContext_Release(own);
}

/* ---- Configuration ---------------------------------------------------------------------- */

static BOOL iniFlag(const wchar_t *ini, const wchar_t *section, const wchar_t *key) {
    return GetPrivateProfileIntW(section, key, 0, ini) == 1; /* missing settings default OFF */
}

/* Creates parent\name (if needed) into out; clears out on failure. */
static void makeFolder(wchar_t *out, const wchar_t *parent, const wchar_t *name) {
    if (wcslen(parent) + wcslen(name) + 2 >= 32768) {
        *out = 0;
        return;
    }
    wcscpy(out, parent);
    wcscat(out, name);
    if (!CreateDirectoryW(out, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        *out = 0;
}

static BOOL usingLegacyIni;

/* gameDir ends with a backslash. */
static void loadConfig(const wchar_t *gameDir) {
    /* SayonaraUKHD.ini; an UmiGraphics.ini from versions before 0.16 is used if that is all there is. */
    wchar_t ini[32768];
    wcscpy(ini, gameDir);
    wcscat(ini, L"SayonaraUKHD.ini");
    if (GetFileAttributesW(ini) == INVALID_FILE_ATTRIBUTES) {
        wchar_t legacy[32768];
        wcscpy(legacy, gameDir);
        wcscat(legacy, L"UmiGraphics.ini");
        if (GetFileAttributesW(legacy) != INVALID_FILE_ATTRIBUTES) {
            wcscpy(ini, legacy);
            usingLegacyIni = TRUE;
        }
    }

    wchar_t width[64], height[64];
    GetPrivateProfileStringW(L"Rendering", L"Width", L"", width, 64, ini);
    GetPrivateProfileStringW(L"Rendering", L"Height", L"", height, 64, ini);
    if (iniFlag(ini, L"Rendering", L"Enabled")) {
        if (umi_uint(width, &requestedWidth) && umi_uint(height, &requestedHeight) &&
            umi_size(requestedWidth, requestedHeight, &render))
            customResolution = TRUE;
        else {
            invalidConfig = TRUE;
            render = (UmiSize){1024, 512, 896};
        }
    }
    aspectEnabled = iniFlag(ini, L"AspectRatio", L"Enabled");
    cullingEnabled = iniFlag(ini, L"AspectRatio", L"FixCulling");
    fxaaConfigured = fxaaEnabled = iniFlag(ini, L"Antialiasing", L"Enabled");

    textureDump = iniFlag(ini, L"Textures", L"Dump");
    textureReplace = iniFlag(ini, L"Textures", L"Replacement");
    textureMaxDumps = GetPrivateProfileIntW(L"Textures", L"MaxDumpFiles", 2048, ini);
    if (textureMaxDumps < 1 || textureMaxDumps > 4096)
        textureMaxDumps = 2048;

    logEnabled = iniFlag(ini, L"Debug", L"Log");
    verboseLog = iniFlag(ini, L"Debug", L"VerboseLog");
    diagnostics = iniFlag(ini, L"Debug", L"Diagnostics");

    if (textureDump || textureReplace) {
        wchar_t root[32768];
        makeFolder(root, gameDir, L"Textures");
        if (*root) {
            if (textureDump)
                makeFolder(textureDumpDir, root, L"\\dump");
            if (textureReplace)
                makeFolder(textureReplaceDir, root, L"\\replace");
        }
        textureBuildReplaceIndex();
    }
    if (diagnostics) {
        makeFolder(debugDirectory, gameDir, L"SayonaraUKHD-debug");
        debugReady = *debugDirectory != 0;
    }
}

static void logConfig(void) {
    logline(FALSE, "%s %s / x86", UMI_NAME, UMI_VERSION);
    if (usingLegacyIni)
        logline(FALSE, "Settings read from UmiGraphics.ini (old name); rename it to SayonaraUKHD.ini.");
    logline(FALSE, "CustomResolution=%d requested=%ux%u effectiveViewport=%.3fx%u allocation=%ux%u",
            customResolution, requestedWidth, requestedHeight, render.viewport, render.height, render.width,
            render.height);
    if (invalidConfig)
        logline(FALSE, "Invalid dimensions: original resolution retained. Width range 320..7168, height "
                       "240..8192; both must be integers.");
    logline(FALSE, "AspectCorrection=%d FixCulling=%d; FXAA configured=%d (F7 toggle)", aspectEnabled,
            cullingEnabled, fxaaConfigured);
    logline(FALSE,
            "TEXTURE config dump=%d replacement=%d dumpDirectoryReady=%d replaceDirectoryReady=%d "
            "maxDumpFiles=%u replacementFilesFound=%u",
            textureDump, textureReplace, *textureDumpDir != 0, *textureReplaceDir != 0, textureMaxDumps,
            textureReplaceIndexCount);
    logline(FALSE, "Debug VerboseLog=%d Diagnostics=%d debugDirectoryReady=%d; log limit %u lines",
            verboseLog, diagnostics, debugReady, LOG_MAX_LINES);
}

/* ---- Startup ---------------------------------------------------------------------------- */

static BOOL CALLBACK initialize(PINIT_ONCE unused, PVOID param, PVOID *ctx) {
    (void)unused;
    (void)param;
    (void)ctx;
    InitializeCriticalSection(&lock);
    InitializeCriticalSection(&texLock);
    wchar_t path[32768];
    DWORD len = GetModuleFileNameW(NULL, path, 32768);
    if (len && len < 32768) {
        wchar_t *base = wcsrchr(path, L'\\');
        base = base ? base + 1 : path;
        active = (_wcsicmp(base, L"UmiByeWin.exe") == 0);
        if (active && (size_t)(base - path) + 64 < 32768) {
            *base = 0; /* path is now the game folder, with trailing backslash */
            loadConfig(path);
            /* With [Debug] Log=0 no log file is created or written (an existing one is left alone),
               and every logline() call returns immediately. */
            if (logEnabled) {
                wcscpy(base, L"UmiGraphics.log");
                logfile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, NULL);
            }
        }
    }
    logConfig();
    len = GetSystemDirectoryW(path, 32768);
    if (len && len < 32750) {
        wcscat(path, L"\\d3d11.dll");
        backend = LoadLibraryW(path);
    }
    if (backend == selfModule) {
        logline(FALSE, "Backend resolved to proxy itself; refusing recursion.");
        backend = NULL;
    }
    if (backend) {
        realCreate = (CreateFn)GetProcAddress(backend, "D3D11CreateDeviceAndSwapChain");
        realDevice = (DeviceFn)GetProcAddress(backend, "D3D11CreateDevice");
    }
    logline(FALSE, "Backend loaded=%d createSwapChain=%d createDevice=%d error=%lu", backend != NULL,
            realCreate != NULL, realDevice != NULL, backend ? 0 : GetLastError());
    if (!backend)
        OutputDebugStringA("SayonaraUKHD: could not load system d3d11.dll");
    return TRUE;
}

/* ---- Exported entry points (see exports.def) ------------------------------------------- */

HRESULT WINAPI UmiCreateSwap(IDXGIAdapter *a, D3D_DRIVER_TYPE b, HMODULE c, UINT d,
                             const D3D_FEATURE_LEVEL *e, UINT f, UINT g, const DXGI_SWAP_CHAIN_DESC *h,
                             IDXGISwapChain **i, ID3D11Device **j, D3D_FEATURE_LEVEL *k,
                             ID3D11DeviceContext **l) {
    InitOnceExecuteOnce(&once, initialize, NULL, NULL);
    if (!realCreate)
        return E_FAIL;
    if (active && h)
        logline(FALSE, "CreateDeviceAndSwapChain requested=%ux%u format=%u windowed=%d samples=%u flags=0x%x",
                h->BufferDesc.Width, h->BufferDesc.Height, h->BufferDesc.Format, h->Windowed,
                h->SampleDesc.Count, d);
    HRESULT hr = realCreate(a, b, c, d, e, f, g, h, i, j, k, l);
    if (active)
        logline(FALSE, "CreateDeviceAndSwapChain hr=0x%08lx", (unsigned long)hr);
    if (SUCCEEDED(hr))
        attach(j ? *j : NULL, l ? *l : NULL, i ? *i : NULL);
    return hr;
}

HRESULT WINAPI UmiCreateDevice(IDXGIAdapter *a, D3D_DRIVER_TYPE b, HMODULE c, UINT d,
                               const D3D_FEATURE_LEVEL *e, UINT f, UINT g, ID3D11Device **h,
                               D3D_FEATURE_LEVEL *i, ID3D11DeviceContext **j) {
    InitOnceExecuteOnce(&once, initialize, NULL, NULL);
    if (!realDevice)
        return E_FAIL;
    HRESULT hr = realDevice(a, b, c, d, e, f, g, h, i, j);
    if (active)
        logline(FALSE, "CreateDevice hr=0x%08lx", (unsigned long)hr);
    if (SUCCEEDED(hr))
        attach(h ? *h : NULL, j ? *j : NULL, NULL);
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        selfModule = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
