/* Developer frame capture ([Debug] Diagnostics=1 only). Used while reverse-engineering the
   game's rendering; not needed to use the mod.

   - Pixel shaders and input layouts are tagged/saved so captured draws can be identified.
   - F8 arms a capture of the next frame (up to 8 per run, 512 draws each): every draw's
     state, constant/vertex/index buffer contents and calling game addresses are logged and
     saved to SayonaraUKHD-debug, together with a one-time copy of the game's read-only code
     sections (.text/.rdata) for offline analysis.
   Nothing here modifies rendering. */
static BOOL captureArmed;
static unsigned captureIndex, captureDraws, captureWait;
static unsigned pixelFiles;
static uint64_t bufferHashes[4096];
static unsigned bufferHashCount;
static unsigned captureBytes;
static const GUID layoutTag = {
    0x7106d459,
    0x5607,
    0x486f,
    {0x96, 0xb2, 0xb4, 0x4b, 0x25, 0xb3, 0xd9, 0x04}}; /* distinct from rawCameraTag */
static HRESULT STDMETHODCALLTYPE probePixelShader(ID3D11Device *d, const void *code, SIZE_T len,
                                                  ID3D11ClassLinkage *link, ID3D11PixelShader **out) {
    typedef HRESULT(STDMETHODCALLTYPE * Fn)(ID3D11Device *, const void *, SIZE_T, ID3D11ClassLinkage *,
                                            ID3D11PixelShader **);
    HRESULT hr = ((Fn)original(d, SLOT_DEV_CREATE_PIXEL_SHADER))(d, code, len, link, out);
    if (SUCCEEDED(hr) && out && *out && code && len && len <= 1048576) {
        uint64_t hash = hashBytes(code, len);
        ID3D11PixelShader_SetPrivateData(*out, &shaderTag, sizeof(hash), &hash);
        if (pixelFiles++ < 256) {
            wchar_t name[100];
            swprintf(name, 100, L"ps-%016llx.dxbc", (unsigned long long)hash);
            debugWrite(name, code, (DWORD)len);
        }
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE probeLayout(ID3D11Device *d, const D3D11_INPUT_ELEMENT_DESC *items,
                                             UINT count, const void *code, SIZE_T len,
                                             ID3D11InputLayout **out) {
    typedef HRESULT(STDMETHODCALLTYPE * Fn)(ID3D11Device *, const D3D11_INPUT_ELEMENT_DESC *, UINT,
                                            const void *, SIZE_T, ID3D11InputLayout **);
    HRESULT hr = ((Fn)original(d, SLOT_DEV_CREATE_INPUT_LAYOUT))(d, items, count, code, len, out);
    if (SUCCEEDED(hr) && out && *out && items && count <= 32) {
        char text[16384];
        size_t used = 0;
        for (UINT i = 0; i < count; i++) {
            int n =
                snprintf(text + used, sizeof(text) - used,
                         "%u semantic=%.128s index=%u format=%u slot=%u offset=%u class=%u step=%u\n", i,
                         items[i].SemanticName, items[i].SemanticIndex, items[i].Format, items[i].InputSlot,
                         items[i].AlignedByteOffset, items[i].InputSlotClass, items[i].InstanceDataStepRate);
            if (n < 0 || (size_t)n >= sizeof(text) - used)
                break;
            used += (size_t)n;
        }
        uint64_t hash = hashBytes(text, used);
        ID3D11InputLayout_SetPrivateData(*out, &layoutTag, sizeof(hash), &hash);
        wchar_t name[100];
        swprintf(name, 100, L"layout-%016llx.txt", (unsigned long long)hash);
        debugWrite(name, text, (DWORD)used);
    }
    return hr;
}
static void probeBuffer(ID3D11DeviceContext *c, ID3D11Buffer *b, const char *kind, UINT slot, UINT stride,
                        UINT offset) {
    if (!b)
        return;
    D3D11_BUFFER_DESC bd;
    ID3D11Buffer_GetDesc(b, &bd);
    if (!bd.ByteWidth || bd.ByteWidth > 4194304 || captureBytes > 134217728 - bd.ByteWidth) {
        logline(FALSE, "FRAME capture=%u draw=%u %s slot=%u bytes=%u skipped=budget_or_size", captureIndex,
                captureDraws, kind, slot, bd.ByteWidth);
        return;
    }
    captureBytes += bd.ByteWidth;
    ID3D11Device *d = NULL;
    ID3D11DeviceContext_GetDevice(c, &d);
    D3D11_BUFFER_DESC sd = {0};
    sd.ByteWidth = bd.ByteWidth;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Buffer *staging = NULL;
    HRESULT hr = ID3D11Device_CreateBuffer(d, &sd, NULL, &staging);
    ID3D11Device_Release(d);
    if (SUCCEEDED(hr)) {
        ID3D11DeviceContext_CopyResource(c, (ID3D11Resource *)staging, (ID3D11Resource *)b);
        D3D11_MAPPED_SUBRESOURCE map;
        hr = ID3D11DeviceContext_Map(c, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
        if (SUCCEEDED(hr)) {
            uint64_t hash = hashBytes(map.pData, bd.ByteWidth);
            BOOL seen = FALSE;
            for (unsigned i = 0; i < bufferHashCount; i++)
                if (bufferHashes[i] == hash)
                    seen = TRUE;
            BOOL saved = seen;
            if (!seen && bufferHashCount < 4096) {
                wchar_t name[120];
                swprintf(name, 120, L"capture-%02u-data-%016llx.bin", captureIndex, (unsigned long long)hash);
                saved = debugWrite(name, map.pData, bd.ByteWidth);
                if (saved)
                    bufferHashes[bufferHashCount++] = hash;
            }
            logline(FALSE,
                    "FRAME capture=%u draw=%u %s slot=%u hash=%016llx bytes=%u stride=%u offset=%u saved=%d",
                    captureIndex, captureDraws, kind, slot, (unsigned long long)hash, bd.ByteWidth, stride,
                    offset, saved);
            ID3D11DeviceContext_Unmap(c, (ID3D11Resource *)staging, 0);
        }
        ID3D11Buffer_Release(staging);
    }
    if (FAILED(hr))
        logline(FALSE, "FRAME readback failed kind=%s slot=%u hr=0x%08lx", kind, slot, (unsigned long)hr);
}
/* Read-only snapshot of the game's loaded code and read-only data.
   Deliberately excludes writable data, heap, stack and other modules. */
static BOOL codeSnapshotDone;
static uintptr_t gameBase;
static DWORD gameImageSize;
static void snapshotGameCode(void) {
    if (codeSnapshotDone)
        return;
    codeSnapshotDone = TRUE;
    HMODULE module = GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER dos;
    SIZE_T got = 0;
    if (!module || !ReadProcessMemory(GetCurrentProcess(), module, &dos, sizeof(dos), &got) ||
        got != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
        dos.e_lfanew > 1048576) {
        logline(FALSE, "CODE snapshot failed: DOS header");
        return;
    }
    IMAGE_NT_HEADERS32 nt;
    uintptr_t base = (uintptr_t)module;
    if (!ReadProcessMemory(GetCurrentProcess(), (const void *)(base + (DWORD)dos.e_lfanew), &nt, sizeof(nt),
                           &got) ||
        got != sizeof(nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || nt.FileHeader.NumberOfSections > 96 ||
        nt.OptionalHeader.SizeOfImage > 67108864) {
        logline(FALSE, "CODE snapshot failed: PE header");
        return;
    }
    gameBase = base;
    gameImageSize = nt.OptionalHeader.SizeOfImage;
    char manifest[16384];
    size_t used = (size_t)snprintf(manifest, sizeof(manifest),
                                   "UmiByeWin.exe loaded sections; not a runnable executable\nbase=0x%08lx "
                                   "size=0x%08lx timestamp=0x%08lx\n",
                                   (unsigned long)base, (unsigned long)gameImageSize,
                                   (unsigned long)nt.FileHeader.TimeDateStamp);
    uintptr_t sh =
        base + (DWORD)dos.e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
    for (WORD i = 0; i < nt.FileHeader.NumberOfSections; i++) {
        IMAGE_SECTION_HEADER sec;
        if (!ReadProcessMemory(GetCurrentProcess(), (const void *)(sh + i * sizeof(sec)), &sec, sizeof(sec),
                               &got) ||
            got != sizeof(sec))
            break;
        char name[9] = {0};
        memcpy(name, sec.Name, 8);
        DWORD size = sec.Misc.VirtualSize, rva = sec.VirtualAddress;
        if (strcmp(name, ".text") && strcmp(name, ".rdata"))
            continue;
        if ((sec.Characteristics & IMAGE_SCN_MEM_WRITE) || !size || size > 8388608 || rva > gameImageSize ||
            size > gameImageSize - rva)
            continue;
        void *bytes = HeapAlloc(GetProcessHeap(), 0, size);
        BOOL ok = FALSE;
        if (bytes) {
            if (ReadProcessMemory(GetCurrentProcess(), (const void *)(base + rva), bytes, size, &got) &&
                got == size) {
                wchar_t file[120];
                swprintf(file, 120, L"game-%hs-rva-%08lx.bin", name + 1, (unsigned long)rva);
                ok = debugWrite(file, bytes, size);
            }
            HeapFree(GetProcessHeap(), 0, bytes);
        }
        int n =
            snprintf(manifest + used, sizeof(manifest) - used, "section=%s rva=0x%08lx bytes=%lu saved=%d\n",
                     name, (unsigned long)rva, (unsigned long)size, ok);
        if (n > 0 && (size_t)n < sizeof(manifest) - used)
            used += (size_t)n;
        logline(FALSE, "CODE section=%s rva=0x%08lx bytes=%lu saved=%d", name, (unsigned long)rva,
                (unsigned long)size, ok);
    }
    debugWrite(L"game-sections.txt", manifest, (DWORD)used);
}
static void captureGameCallers(void) {
    if (!gameBase || !gameImageSize)
        return;
    void *stack[48];
    USHORT count = CaptureStackBackTrace(0, 48, stack, NULL);
    char text[1024];
    size_t used = 0;
    unsigned matches = 0;
    for (USHORT i = 0; i < count; i++) {
        uintptr_t address = (uintptr_t)stack[i];
        if (address >= gameBase && address - gameBase < gameImageSize) {
            int n = snprintf(text + used, sizeof(text) - used, "%s0x%08lx", matches ? "," : "",
                             (unsigned long)(address - gameBase));
            if (n <= 0 || (size_t)n >= sizeof(text) - used)
                break;
            used += (size_t)n;
            ++matches;
        }
    }
    if (!matches)
        strcpy(text, "unavailable");
    logline(FALSE, "FRAME capture=%u draw=%u gameCallerRVAs=%s", captureIndex, captureDraws, text);
}

static void captureDraw(ID3D11DeviceContext *c, const char *type, UINT count, UINT start, INT base) {
    if (!captureArmed || captureDraws >= 512 ||
        ID3D11DeviceContext_GetType(c) != D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return;
    if (!captureDraws)
        captureBytes = 0;
    ++captureDraws;
    captureGameCallers();
    D3D11_VIEWPORT vp[16] = {{0}};
    UINT nv = 16;
    ID3D11DeviceContext_RSGetViewports(c, &nv, vp);
    ID3D11VertexShader *vs = NULL;
    ID3D11DeviceContext_VSGetShader(c, &vs, NULL, NULL);
    uint64_t shader = 0, pixel = 0, layout = 0;
    UINT sz = 8;
    if (vs) {
        ID3D11VertexShader_GetPrivateData(vs, &shaderTag, &sz, &shader);
        ID3D11VertexShader_Release(vs);
    }
    ID3D11PixelShader *ps = NULL;
    ID3D11DeviceContext_PSGetShader(c, &ps, NULL, NULL);
    sz = 8;
    if (ps) {
        ID3D11PixelShader_GetPrivateData(ps, &shaderTag, &sz, &pixel);
        ID3D11PixelShader_Release(ps);
    }
    ID3D11InputLayout *il = NULL;
    ID3D11DeviceContext_IAGetInputLayout(c, &il);
    sz = 8;
    if (il) {
        ID3D11InputLayout_GetPrivateData(il, &layoutTag, &sz, &layout);
        ID3D11InputLayout_Release(il);
    }
    D3D11_PRIMITIVE_TOPOLOGY topology;
    ID3D11DeviceContext_IAGetPrimitiveTopology(c, &topology);
    D3D11_TEXTURE2D_DESC td = {0};
    ID3D11RenderTargetView *rt = NULL;
    ID3D11DeviceContext_OMGetRenderTargets(c, 1, &rt, NULL);
    if (rt) {
        ID3D11Resource *r = NULL;
        ID3D11RenderTargetView_GetResource(rt, &r);
        ID3D11RenderTargetView_Release(rt);
        if (r) {
            ID3D11Texture2D *t = NULL;
            ID3D11Resource_QueryInterface(r, &texture2DIID, (void **)&t);
            ID3D11Resource_Release(r);
            if (t) {
                ID3D11Texture2D_GetDesc(t, &td);
                ID3D11Texture2D_Release(t);
            }
        }
    }
    ID3D11Buffer *buffers[8] = {0};
    ID3D11DeviceContext_VSGetConstantBuffers(c, 0, 8, buffers);
    BOOL known = isCameraShader(shader), regular = FALSE;
    float raw[48];
    sz = sizeof(raw);
    if (known && buffers[0] && SUCCEEDED(ID3D11Buffer_GetPrivateData(buffers[0], &cameraDataTag, &sz, raw)) &&
        sz == sizeof(raw) && umi_camera(raw))
        regular = TRUE;
    logline(FALSE,
            "FRAME capture=%u draw=%u type=%s count=%u start=%u base=%d topology=%u vs=%016llx ps=%016llx "
            "layout=%016llx camera=%d RT=%ux%u bind=0x%x viewportCount=%u viewport=%.3f,%.3f,%.3f,%.3f "
            "aspect=%.6f",
            captureIndex, captureDraws, type, count, start, base, topology, (unsigned long long)shader,
            (unsigned long long)pixel, (unsigned long long)layout, regular, td.Width, td.Height, td.BindFlags,
            nv, vp[0].TopLeftX, vp[0].TopLeftY, vp[0].Width, vp[0].Height, displayAspect);
    for (UINT i = 0; i < 8; i++)
        if (buffers[i]) {
            probeBuffer(c, buffers[i], "VS-CB", i, 0, 0);
            ID3D11Buffer_Release(buffers[i]);
        }
    if (!regular) {
        ID3D11Buffer *vb[4] = {0};
        UINT strides[4] = {0}, offsets[4] = {0};
        ID3D11DeviceContext_IAGetVertexBuffers(c, 0, 4, vb, strides, offsets);
        for (UINT i = 0; i < 4; i++)
            if (vb[i]) {
                probeBuffer(c, vb[i], "VB", i, strides[i], offsets[i]);
                ID3D11Buffer_Release(vb[i]);
            }
        ID3D11Buffer *ib = NULL;
        DXGI_FORMAT fmt;
        UINT offset;
        ID3D11DeviceContext_IAGetIndexBuffer(c, &ib, &fmt, &offset);
        if (ib) {
            probeBuffer(c, ib, "IB", (UINT)fmt, 0, offset);
            ID3D11Buffer_Release(ib);
        }
    }
    if (captureDraws == 512) {
        captureArmed = FALSE;
        logline(FALSE, "FRAME capture=%u truncated at 512 draws", captureIndex);
    }
}

/* Called once per Present. */
static void capturePresent(void) {
    static BOOL held;
    if (!diagnostics)
        return;
    BOOL down = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (down && !held && debugReady && !captureArmed && captureIndex < 8) {
        snapshotGameCode();
        ++captureIndex;
        captureDraws = captureWait = bufferHashCount = 0;
        captureArmed = TRUE;
        logline(FALSE, "DEBUG capture=%u armed; next frame including non-camera draws", captureIndex);
    } else if (captureArmed) {
        if (captureDraws || ++captureWait >= 600) {
            captureArmed = FALSE;
            logline(FALSE, "DEBUG capture=%u ended draws=%u", captureIndex, captureDraws);
        }
    }
    held = down;
}
