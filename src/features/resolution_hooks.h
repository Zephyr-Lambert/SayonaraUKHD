/* Internal resolution.
   The game renders its 3D scene into a 1024x512 buffer with an 896x512 viewport (7/8 of the
   width). These hooks enlarge exactly those buffers, viewports and scissor rectangles to the
   [Rendering] size; everything else keeps the game's original sizes. See resolution.h. */

static HRESULT STDMETHODCALLTYPE hookedTexture(ID3D11Device *d, const D3D11_TEXTURE2D_DESC *spec,
                                               const D3D11_SUBRESOURCE_DATA *init, ID3D11Texture2D **out) {
    TexFn f = (TexFn)original(d, SLOT_DEV_CREATE_TEXTURE2D);
    D3D11_TEXTURE2D_DESC adjusted;
    const D3D11_TEXTURE2D_DESC *request = spec;
    if (spec && customResolution &&
        umi_game_buffer(spec->Width, spec->Height, spec->Format, spec->BindFlags, spec->MipLevels,
                        spec->ArraySize, spec->SampleDesc.Count, init != NULL)) {
        adjusted = *spec;
        adjusted.Width = render.width;
        adjusted.Height = render.height;
        request = &adjusted;
        logline(TRUE, "SCALE buffer format=%u bind=0x%x %ux%u -> %ux%u", spec->Format, spec->BindFlags,
                spec->Width, spec->Height, adjusted.Width, adjusted.Height);
    }
    HRESULT hr = f(d, request, init, out);
    if (SUCCEEDED(hr) && out && *out)
        textureCreated(d, request, init, *out);
    if (spec) {
        /* Render targets / depth buffers are few and useful for troubleshooting; art textures are many. */
        if (spec->BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL))
            logTextureDesc("CreateTexture2D target", request);
        else if (verboseLog)
            logTextureDesc("CreateTexture2D texture", request);
        if (FAILED(hr))
            logline(TRUE, "CreateTexture2D FAILED hr=0x%08lx size=%ux%u", (unsigned long)hr, spec->Width,
                    spec->Height);
    }
    return hr;
}

static void STDMETHODCALLTYPE hookedViewport(ID3D11DeviceContext *c, UINT n, const D3D11_VIEWPORT *v) {
    VpFn f = (VpFn)original(c, SLOT_CTX_SET_VIEWPORTS);
    D3D11_VIEWPORT adjusted[1];
    const D3D11_VIEWPORT *request = v;
    if (v && n == 1 && customResolution && v[0].TopLeftX == 0 && v[0].TopLeftY == 0 && v[0].Width == 896 &&
        v[0].Height == 512) {
        adjusted[0] = v[0];
        adjusted[0].Width = render.viewport;
        adjusted[0].Height = (float)render.height;
        request = adjusted;
        static BOOL logged;
        if (!logged) {
            logged = TRUE;
            logline(FALSE, "SCALE viewport 896x512 -> %.3fx%.3f", adjusted[0].Width, adjusted[0].Height);
        }
    }
    f(c, n, request);
    if (verboseLog && v)
        for (UINT i = 0; i < n && i < D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; i++)
            logline(TRUE, "Viewport[%u/%u] xy=%.3f,%.3f size=%.3fx%.3f depth=%.3f..%.3f", i, n, v[i].TopLeftX,
                    v[i].TopLeftY, v[i].Width, v[i].Height, v[i].MinDepth, v[i].MaxDepth);
}

static void STDMETHODCALLTYPE hookedScissors(ID3D11DeviceContext *c, UINT n, const D3D11_RECT *r) {
    ScissorFn f = (ScissorFn)original(c, SLOT_CTX_SET_SCISSORS);
    D3D11_RECT changed[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    const D3D11_RECT *request = r;
    if (r && customResolution && n <= D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) {
        /* Only scale scissors used while the enlarged gameplay viewport is bound. */
        D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        UINT count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        ID3D11DeviceContext_RSGetViewports(c, &count, vp);
        if (count == 1 && vp[0].Width == render.viewport && vp[0].Height == (float)render.height) {
            BOOL fits = TRUE;
            for (UINT i = 0; i < n; i++)
                if (r[i].left < 0 || r[i].top < 0 || r[i].right > 1024 || r[i].bottom > 512)
                    fits = FALSE;
            if (fits) {
                for (UINT i = 0; i < n; i++) {
                    changed[i] = r[i];
                    changed[i].left = umi_floor_edge(r[i].left, render.width, 1024);
                    changed[i].top = umi_floor_edge(r[i].top, render.height, 512);
                    changed[i].right = umi_ceil_edge(r[i].right, render.width, 1024);
                    changed[i].bottom = umi_ceil_edge(r[i].bottom, render.height, 512);
                }
                request = changed;
                VLOG("SCALE scissor count=%u", n);
            } else
                VLOG("Scissor unchanged: rectangle exceeds original gameplay buffer bounds");
        }
    }
    f(c, n, request);
}
