/* Per-draw and per-frame hooks that tie the features together. */

static void logBoundTarget(const char *kind, ID3D11Resource *r) {
    ID3D11Texture2D *t = NULL;
    if (SUCCEEDED(ID3D11Resource_QueryInterface(r, &texture2DIID, (void **)&t))) {
        D3D11_TEXTURE2D_DESC d;
        ID3D11Texture2D_GetDesc(t, &d);
        logTextureDesc(kind, &d);
        ID3D11Texture2D_Release(t);
    }
    ID3D11Resource_Release(r);
}

static void STDMETHODCALLTYPE hookedTargets(ID3D11DeviceContext *c, UINT n,
                                            ID3D11RenderTargetView *const *views,
                                            ID3D11DepthStencilView *depth) {
    RtFn f = (RtFn)original(c, SLOT_CTX_SET_TARGETS);
    fxaaBeforeTargets(c, n, views); /* FXAA may need to run before the game leaves the scene target */
    f(c, n, views, depth);
    if (!verboseLog)
        return;
    /* Verbose: sample bound targets during the first 120 frames and every 120th frame after. */
    LONG frame = frames;
    if (frame > 120 && frame % 120 != 0)
        return;
    if (views)
        for (UINT i = 0; i < n && i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
            if (views[i]) {
                ID3D11Resource *r = NULL;
                ID3D11RenderTargetView_GetResource(views[i], &r);
                if (r) {
                    char label[40];
                    snprintf(label, sizeof(label), "BoundRT[%u/%u]", i, n);
                    logBoundTarget(label, r);
                }
            }
    if (depth) {
        ID3D11Resource *r = NULL;
        ID3D11DepthStencilView_GetResource(depth, &r);
        if (r)
            logBoundTarget("BoundDepth", r);
    }
}

/* Order matters: capture sees the game's state, FXAA may run first (before the HUD draw),
   then textures and camera are substituted for this one draw and restored afterwards. */
static void STDMETHODCALLTYPE hookedDraw(ID3D11DeviceContext *c, UINT count, UINT start) {
    DrawFn f = (DrawFn)original(c, SLOT_CTX_DRAW);
    captureDraw(c, "Draw", count, start, 0);
    fxaaBeforeDraw(c);
    TextureBindings textures;
    textureBeforeDraw(c, &textures);
    ID3D11Buffer *saved = correctDraw(c);
    f(c, count, start);
    restoreCamera(c, saved);
    textureAfterDraw(c, &textures);
}

static void STDMETHODCALLTYPE hookedDrawIndexed(ID3D11DeviceContext *c, UINT count, UINT start, INT base) {
    DrawIndexedFn f = (DrawIndexedFn)original(c, SLOT_CTX_DRAW_INDEXED);
    captureDraw(c, "DrawIndexed", count, start, base);
    fxaaBeforeDraw(c);
    TextureBindings textures;
    textureBeforeDraw(c, &textures);
    ID3D11Buffer *saved = correctDraw(c);
    f(c, count, start, base);
    restoreCamera(c, saved);
    textureAfterDraw(c, &textures);
}

static HRESULT STDMETHODCALLTYPE hookedPresent(IDXGISwapChain *s, UINT sync, UINT flags) {
    PresentFn f = (PresentFn)original(s, SLOT_SWAP_PRESENT);
    if (!(flags & DXGI_PRESENT_TEST)) {
        installCulling();
        capturePresent();
        fxaaPresent();
    }
    HRESULT hr = f(s, sync, flags);
    if (!(flags & DXGI_PRESENT_TEST)) {
        LONG n = InterlockedIncrement(&frames);
        if (n == 1 || n == 60 || n == 300 || n % 600 == 0) {
            cullingStats();
            fxaaStats();
            textureStats();
            DXGI_SWAP_CHAIN_DESC d;
            if (SUCCEEDED(IDXGISwapChain_GetDesc(s, &d)))
                logline(FALSE, "Present frame=%ld output=%ux%u format=%u windowed=%d sync=%u hr=0x%08lx", n,
                        d.BufferDesc.Width, d.BufferDesc.Height, d.BufferDesc.Format, d.Windowed, sync,
                        (unsigned long)hr);
        }
    }
    if (FAILED(hr))
        logline(TRUE, "Present FAILED hr=0x%08lx", (unsigned long)hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE hookedResize(IDXGISwapChain *s, UINT count, UINT w, UINT h,
                                              DXGI_FORMAT format, UINT flags) {
    ResizeFn f = (ResizeFn)original(s, SLOT_SWAP_RESIZE);
    HRESULT hr = f(s, count, w, h, format, flags);
    if (SUCCEEDED(hr)) {
        displayAspect = 0; /* re-detected from the next output draw */
        markOutput(s);
    }
    logline(FALSE, "ResizeBuffers count=%u requested=%ux%u format=%u flags=0x%x hr=0x%08lx", count, w, h,
            format, flags, (unsigned long)hr);
    return hr;
}
