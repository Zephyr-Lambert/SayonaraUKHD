/* Texture dumping and replacement.

   Dumping: supported textures uploaded from the CPU (initial data or a whole-texture
   UpdateSubresource) are written once to Textures/dump as DDS files named
   tex_<hash>_<width>x<height>_f<format>.dds.

   Replacement: a DDS (or, if no DDS exists, a PNG) with the same name in Textures/replace is loaded into a
   separate texture + view and attached to the original resource as private data. During
   Draw/DrawIndexed the original pixel-shader view is swapped for the replacement view and
   restored afterwards, so the game still sees its original resources.

   The content hash and file name format must stay unchanged so existing dumps and
   replacement packs keep matching. */
#include "texture_format.h"

#include "png_decode.h"

#define TEXTURE_MAX_PNG_FILE (128u * 1024u * 1024u)

static BOOL textureDump, textureReplace;
static wchar_t textureDumpDir[32768], textureReplaceDir[32768];
static UINT textureMaxDumps = 2048;

/* Guards dump/replacement bookkeeping and file I/O. Separate from the global lock so a
   loader thread writing a large file never blocks hooks on the render thread. */
static CRITICAL_SECTION texLock;
static UINT textureDumpCount, textureKnownCount;
static uint64_t textureDumpBytes, textureKnown[4096];

/* Hashes of files present in the replace folder at startup (sorted). Lets uploads with no
   replacement skip a file-open attempt, which is comparatively slow under Wine/Proton. */
static uint64_t *textureReplaceIndex;
static UINT textureReplaceIndexCount;

static volatile LONG textureCandidates, textureUploads, textureLoaded, textureBindings, textureInvalidated;
static volatile LONG textureHighSlot; /* highest PS SRV slot (exclusive) the game has used */

static const GUID textureTicketTag = {
    0x8b3f62e4, 0x353b, 0x44f2, {0xa7, 0xa9, 0x41, 0xc4, 0x36, 0x65, 0x10, 0x01}};
static const GUID textureReplacementTag = {
    0x8b3f62e4, 0x353b, 0x44f2, {0xa7, 0xa9, 0x41, 0xc4, 0x36, 0x65, 0x10, 0x02}};

/* Stored as private data on each eligible game texture. */
typedef struct {
    UmiTexture t;
    uint64_t hash;
    BOOL identified;
} TextureTicket;

static void textureName(wchar_t name[100], const UmiTexture *t, uint64_t hash) {
    swprintf(name, 100, L"tex_%016llx_%ux%u_f%u.dds", (unsigned long long)hash, t->width, t->height,
             t->format);
}

static BOOL texturePath(wchar_t path[32768], const wchar_t *dir, const wchar_t *name) {
    if (!*dir || wcslen(dir) + wcslen(name) + 2 >= 32768)
        return FALSE;
    wcscpy(path, dir);
    wcscat(path, L"\\");
    wcscat(path, name);
    return TRUE;
}

static BOOL textureWrite(HANDLE f, const void *p, DWORD bytes) {
    DWORD done = 0;
    return WriteFile(f, p, bytes, &done, NULL) && done == bytes;
}

/* ---- Replace-folder index ------------------------------------------------------------ */

/* Parses the 16 hex digits after "tex_" (any letter case) of a .dds or .png name. */
static BOOL textureParseHash(const wchar_t *name, uint64_t *out) {
    if (_wcsnicmp(name, L"tex_", 4) != 0)
        return FALSE;
    uint64_t h = 0;
    for (unsigned i = 4; i < 20; i++) {
        wchar_t c = name[i];
        unsigned v;
        if (c >= L'0' && c <= L'9')
            v = (unsigned)(c - L'0');
        else if (c >= L'a' && c <= L'f')
            v = (unsigned)(c - L'a' + 10);
        else if (c >= L'A' && c <= L'F')
            v = (unsigned)(c - L'A' + 10);
        else
            return FALSE;
        h = (h << 4) | v;
    }
    if (name[20] != L'_')
        return FALSE;
    size_t n = wcslen(name);
    if (n < 25 || (_wcsicmp(name + n - 4, L".dds") != 0 && _wcsicmp(name + n - 4, L".png") != 0))
        return FALSE;
    *out = h;
    return TRUE;
}

static int textureCompareHash(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static void textureBuildReplaceIndex(void) {
    if (!textureReplace || !*textureReplaceDir)
        return;
    wchar_t pattern[32768];
    if (!texturePath(pattern, textureReplaceDir, L"tex_*"))
        return;
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE)
        return;
    UINT capacity = 0;
    do {
        uint64_t h;
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !textureParseHash(found.cFileName, &h))
            continue;
        if (textureReplaceIndexCount == capacity) {
            UINT grown = capacity ? capacity * 2 : 256;
            void *p = textureReplaceIndex
                          ? HeapReAlloc(GetProcessHeap(), 0, textureReplaceIndex, grown * sizeof(uint64_t))
                          : HeapAlloc(GetProcessHeap(), 0, grown * sizeof(uint64_t));
            if (!p)
                break;
            textureReplaceIndex = p;
            capacity = grown;
        }
        textureReplaceIndex[textureReplaceIndexCount++] = h;
    } while (FindNextFileW(search, &found));
    FindClose(search);
    if (textureReplaceIndexCount)
        qsort(textureReplaceIndex, textureReplaceIndexCount, sizeof(uint64_t), textureCompareHash);
}

static BOOL textureMayHaveReplacement(uint64_t hash) {
    return textureReplaceIndexCount && bsearch(&hash, textureReplaceIndex, textureReplaceIndexCount,
                                               sizeof(uint64_t), textureCompareHash);
}

/* ---- Dump and load (called with texLock held) --------------------------------------- */

static void textureDumpData(const UmiTexture *t, uint64_t hash, const void *data, UINT pitch) {
    if (!textureDump || !*textureDumpDir)
        return;
    for (UINT i = 0; i < textureKnownCount; i++)
        if (textureKnown[i] == hash)
            return;
    if (textureKnownCount == 4096 || textureDumpCount >= textureMaxDumps ||
        textureDumpBytes + t->bytes > UINT64_C(536870912)) {
        logline(TRUE, "TEXTURE dump limit reached; replacement and rendering continue");
        return;
    }
    wchar_t name[100], path[32768];
    textureName(name, t, hash);
    if (!texturePath(path, textureDumpDir, name))
        return;
    HANDLE f =
        CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_EXISTS)
            textureKnown[textureKnownCount++] = hash; /* dumped on an earlier run */
        else
            logline(TRUE, "TEXTURE dump create failed error=%lu", GetLastError());
        return;
    }
    unsigned char header[148];
    UINT n = umi_dds_header(t, header);
    BOOL ok = textureWrite(f, header, n);
    for (UINT y = 0; y < t->rows && ok; y++)
        ok = textureWrite(f, (const unsigned char *)data + (size_t)y * pitch, t->row);
    CloseHandle(f);
    if (ok) {
        textureKnown[textureKnownCount++] = hash;
        ++textureDumpCount;
        textureDumpBytes += t->bytes + n;
        VLOG("TEXTURE dumped hash=%016llx size=%ux%u format=%u bytes=%u", (unsigned long long)hash, t->width,
             t->height, t->format, t->bytes);
    } else {
        DeleteFileW(path);
        logline(TRUE, "TEXTURE dump write failed; incomplete file removed");
    }
}

/* Returns FALSE if the file does not exist. Otherwise *bytes is a heap copy of the file,
   or NULL if it could not be read or is larger than maxBytes. */
static BOOL textureReadFile(const wchar_t *path, uint64_t maxBytes, unsigned char **bytes, DWORD *size) {
    *bytes = NULL;
    *size = 0;
    HANDLE f =
        CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    LARGE_INTEGER length;
    if (GetFileSizeEx(f, &length) && length.QuadPart > 0 && (uint64_t)length.QuadPart <= maxBytes) {
        unsigned char *p = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length.QuadPart);
        DWORD got = 0;
        if (p && ReadFile(f, p, (DWORD)length.QuadPart, &got, NULL) && got == (DWORD)length.QuadPart) {
            *bytes = p;
            *size = got;
        } else if (p)
            HeapFree(GetProcessHeap(), 0, p);
    }
    CloseHandle(f);
    return TRUE;
}

static BOOL textureIsSrgb(uint32_t format) {
    return format == 29 || format == 72 || format == 75 || format == 78 || format == 91;
}

/* Looks for <name>.dds, then <name>.png, in the replace folder. A valid file is turned into a
   mod-owned texture + view and attached to the game's texture as private data. */
static void textureLoad(ID3D11Device *d, ID3D11Resource *resource, const UmiTexture *originalTexture,
                        uint64_t hash) {
    if (!textureReplace || !*textureReplaceDir || !textureMayHaveReplacement(hash))
        return;
    wchar_t name[100], path[32768];
    textureName(name, originalTexture, hash);
    unsigned char *bytes = NULL, *pixels = NULL;
    DWORD size = 0;
    BOOL valid = FALSE, png = FALSE;
    UmiTexture t = {0};
    D3D11_TEXTURE2D_DESC td = {0};
    D3D11_SUBRESOURCE_DATA init = {0};

    if (texturePath(path, textureReplaceDir, name) &&
        textureReadFile(path, UMI_TEXTURE_MAX_BYTES + 148u, &bytes, &size)) {
        valid = bytes && umi_dds_parse(bytes, size, &t) && umi_tex_scale(originalTexture, &t);
        if (valid) {
            td.Format = (DXGI_FORMAT)t.format;
            init.pSysMem = bytes + t.offset;
            init.SysMemPitch = t.row;
        }
    } else {
        png = TRUE;
        wcscpy(name + wcslen(name) - 3, L"png");
        if (!texturePath(path, textureReplaceDir, name) ||
            !textureReadFile(path, TEXTURE_MAX_PNG_FILE, &bytes, &size))
            return; /* no replacement under either name */
        int w = 0, h = 0, channels = 0;
        /* Check the size from the header before decoding anything. */
        valid = bytes && stbi_info_from_memory(bytes, (int)size, &w, &h, &channels) &&
                umi_tex_layout((uint32_t)w, (uint32_t)h, 28, &t) && umi_tex_scale(originalTexture, &t);
        if (valid) {
            pixels = stbi_load_from_memory(bytes, (int)size, &w, &h, &channels, 4);
            valid = pixels && (uint32_t)w == t.width && (uint32_t)h == t.height;
        }
        if (valid) {
            /* PNG pixels are plain RGBA8; match the original's sRGB-ness so colours come out the same. */
            td.Format = textureIsSrgb(originalTexture->format) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                                                               : DXGI_FORMAT_R8G8B8A8_UNORM;
            t.format = td.Format;
            init.pSysMem = pixels;
            init.SysMemPitch = t.width * 4;
        }
    }
    if (!valid) {
        if (png)
            logline(TRUE,
                    "TEXTURE replacement rejected hash=%016llx (PNG): require a readable PNG, same shape, "
                    "integer scale 1..8, <=8192 and <=64 MiB as RGBA",
                    (unsigned long long)hash);
        else
            logline(TRUE,
                    "TEXTURE replacement rejected hash=%016llx (DDS): require supported single-mip DDS, same "
                    "shape, "
                    "integer scale 1..8, <=8192 and <=64 MiB",
                    (unsigned long long)hash);
        if (pixels)
            stbi_image_free(pixels);
        if (bytes)
            HeapFree(GetProcessHeap(), 0, bytes);
        return;
    }
    td.Width = t.width;
    td.Height = t.height;
    td.MipLevels = td.ArraySize = td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D *replacement = NULL;
    ID3D11ShaderResourceView *view = NULL;
    /* Bypass the upload hook: mod-owned textures must not dump or replace themselves. */
    HRESULT hr = ((TexFn)original(d, SLOT_DEV_CREATE_TEXTURE2D))(d, &td, &init, &replacement);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateShaderResourceView(d, (ID3D11Resource *)replacement, NULL, &view);
    if (SUCCEEDED(hr))
        hr = ID3D11Resource_SetPrivateDataInterface(resource, &textureReplacementTag, (IUnknown *)view);
    if (SUCCEEDED(hr)) {
        InterlockedIncrement(&textureLoaded);
        logline(FALSE, "TEXTURE loaded hash=%016llx original=%ux%u replacement=%ux%u format=%u source=%s",
                (unsigned long long)hash, originalTexture->width, originalTexture->height, t.width, t.height,
                t.format, png ? "png" : "dds");
    } else
        logline(TRUE, "TEXTURE replacement creation failed hash=%016llx hr=0x%08lx; original retained",
                (unsigned long long)hash, (unsigned long)hr);
    /* The resource's private data now holds the only reference to the view (and the view to the texture). */
    if (view)
        ID3D11ShaderResourceView_Release(view);
    if (replacement)
        ID3D11Texture2D_Release(replacement);
    if (pixels)
        stbi_image_free(pixels);
    HeapFree(GetProcessHeap(), 0, bytes);
}

/* ---- Upload tracking ------------------------------------------------------------------ */

/* Content changed in a way we cannot hash (GPU copy, partial update, Map): drop any replacement. */
static void textureInvalidate(ID3D11Resource *r) {
    if (!r || (!textureDump && !textureReplace))
        return;
    TextureTicket ticket;
    UINT n = sizeof(ticket);
    if (SUCCEEDED(ID3D11Resource_GetPrivateData(r, &textureTicketTag, &n, &ticket)) && n == sizeof(ticket)) {
        ticket.identified = FALSE;
        ID3D11Resource_SetPrivateData(r, &textureTicketTag, sizeof(ticket), &ticket);
        ID3D11Resource_SetPrivateData(r, &textureReplacementTag, 0, NULL);
        InterlockedIncrement(&textureInvalidated);
    }
}

static void textureUpload(ID3D11Device *d, ID3D11Resource *r, TextureTicket *ticket, const void *data,
                          UINT pitch) {
    if (!data || pitch < ticket->t.row || (uint64_t)pitch * ticket->t.rows > UINT64_C(268435456)) {
        textureInvalidate(r);
        return;
    }
    /* Hash outside any lock: for large textures this is the slowest step. */
    uint64_t hash = umi_tex_hash(&ticket->t, data, pitch);
    InterlockedIncrement(&textureUploads);
    if (ticket->identified && ticket->hash == hash)
        return; /* same content re-uploaded */
    EnterCriticalSection(&texLock);
    ID3D11Resource_SetPrivateData(r, &textureReplacementTag, 0, NULL);
    textureDumpData(&ticket->t, hash, data, pitch);
    textureLoad(d, r, &ticket->t, hash);
    ticket->hash = hash;
    ticket->identified = TRUE;
    ID3D11Resource_SetPrivateData(r, &textureTicketTag, sizeof(*ticket), ticket);
    LeaveCriticalSection(&texLock);
}

/* Called from the CreateTexture2D hook after the real texture exists. */
static void textureCreated(ID3D11Device *d, const D3D11_TEXTURE2D_DESC *spec,
                           const D3D11_SUBRESOURCE_DATA *data, ID3D11Texture2D *texture) {
    if ((!textureDump && !textureReplace) || !spec || !texture || spec->MipLevels != 1 ||
        spec->ArraySize != 1 || spec->SampleDesc.Count != 1 ||
        spec->BindFlags != D3D11_BIND_SHADER_RESOURCE || spec->CPUAccessFlags || spec->MiscFlags ||
        (spec->Usage != D3D11_USAGE_DEFAULT && spec->Usage != D3D11_USAGE_IMMUTABLE))
        return;
    TextureTicket ticket = {0};
    if (!umi_tex_layout(spec->Width, spec->Height, spec->Format, &ticket.t)) {
        logline(TRUE, "TEXTURE unsupported upload format=%u size=%ux%u", spec->Format, spec->Width,
                spec->Height);
        return;
    }
    InterlockedIncrement(&textureCandidates);
    ID3D11Resource *r = (ID3D11Resource *)texture;
    ID3D11Resource_SetPrivateData(r, &textureTicketTag, sizeof(ticket), &ticket);
    if (data)
        textureUpload(d, r, &ticket, data->pSysMem, data->SysMemPitch);
}

/* Called from the UpdateSubresource hook after the real update. */
static void textureUpdated(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub, const D3D11_BOX *box,
                           const void *data, UINT pitch) {
    if ((!textureDump && !textureReplace) || !r)
        return;
    TextureTicket ticket;
    UINT n = sizeof(ticket);
    if (FAILED(ID3D11Resource_GetPrivateData(r, &textureTicketTag, &n, &ticket)) || n != sizeof(ticket))
        return;
    if (sub || (box && (box->left || box->top || box->front || box->right != ticket.t.width ||
                        box->bottom != ticket.t.height || box->back != 1))) {
        textureInvalidate(r);
        logline(TRUE, "TEXTURE partial/GPU upload excluded; stale replacement detached");
        return;
    }
    ID3D11Device *d = NULL;
    ID3D11DeviceContext_GetDevice(c, &d);
    textureUpload(d, r, &ticket, data, pitch);
    ID3D11Device_Release(d);
}

/* ---- Draw-time substitution ----------------------------------------------------------- */

#define TEXTURE_SLOTS 128u
typedef struct {
    ID3D11ShaderResourceView *saved[TEXTURE_SLOTS];
    BOOL changed[TEXTURE_SLOTS];
    UINT count;
} TextureBindings;

/* PSSetShaderResources hook: only records the highest slot in use so draws scan no further. */
static void STDMETHODCALLTYPE texturePSResources(ID3D11DeviceContext *c, UINT start, UINT count,
                                                 ID3D11ShaderResourceView *const *views) {
    typedef void(STDMETHODCALLTYPE * Fn)(ID3D11DeviceContext *, UINT, UINT,
                                         ID3D11ShaderResourceView *const *);
    if (textureReplace && start < TEXTURE_SLOTS && count <= TEXTURE_SLOTS - start) {
        LONG end = (LONG)(start + count), seen;
        while ((seen = textureHighSlot) < end &&
               InterlockedCompareExchange(&textureHighSlot, end, seen) != seen)
            ;
    }
    ((Fn)original(c, SLOT_CTX_PS_SET_RESOURCES))(c, start, count, views);
}

static void textureBeforeDraw(ID3D11DeviceContext *c, TextureBindings *state) {
    state->count = 0;
    if (!textureReplace || !textureLoaded || ID3D11DeviceContext_GetType(c) != D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return;
    state->count = (UINT)textureHighSlot;
    if (!state->count)
        return;
    memset(state->changed, 0, sizeof(state->changed));
    ID3D11DeviceContext_PSGetShaderResources(c, 0, state->count, state->saved);
    for (UINT i = 0; i < state->count; i++)
        if (state->saved[i]) {
            D3D11_SHADER_RESOURCE_VIEW_DESC desc;
            ID3D11ShaderResourceView_GetDesc(state->saved[i], &desc);
            if (desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || desc.Texture2D.MostDetailedMip != 0)
                continue;
            ID3D11Resource *resource = NULL;
            ID3D11ShaderResourceView_GetResource(state->saved[i], &resource);
            ID3D11ShaderResourceView *replacement = NULL;
            UINT n = sizeof(replacement);
            if (resource &&
                SUCCEEDED(
                    ID3D11Resource_GetPrivateData(resource, &textureReplacementTag, &n, &replacement)) &&
                replacement) {
                ID3D11DeviceContext_PSSetShaderResources(c, i, 1, &replacement);
                state->changed[i] = TRUE;
                ID3D11ShaderResourceView_Release(replacement);
                InterlockedIncrement(&textureBindings);
            }
            if (resource)
                ID3D11Resource_Release(resource);
        }
}

static void textureAfterDraw(ID3D11DeviceContext *c, TextureBindings *state) {
    for (UINT i = 0; i < state->count; i++) {
        if (state->changed[i])
            ID3D11DeviceContext_PSSetShaderResources(c, i, 1, &state->saved[i]);
        if (state->saved[i])
            ID3D11ShaderResourceView_Release(state->saved[i]);
    }
}

static void textureStats(void) {
    if (textureDump || textureReplace)
        logline(
            FALSE,
            "TEXTURE stats candidates=%ld uploads=%ld dumped=%u bytes=%llu loaded=%ld replacedBindings=%ld "
            "invalidated=%ld",
            textureCandidates, textureUploads, textureDumpCount, (unsigned long long)textureDumpBytes,
            textureLoaded, textureBindings, textureInvalidated);
}
