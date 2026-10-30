/* Output folder for developer diagnostics ([Debug] Diagnostics=1 only):
   SayonaraUKHD-debug next to the game executable. Nothing is written here in normal use. */
static wchar_t debugDirectory[32768];
static BOOL debugReady;

static BOOL debugWrite(const wchar_t *name, const void *data, DWORD size) {
    if (!debugReady)
        return FALSE;
    wchar_t path[32768];
    if (wcslen(debugDirectory) + wcslen(name) + 2 >= 32768)
        return FALSE;
    wcscpy(path, debugDirectory);
    wcscat(path, L"\\");
    wcscat(path, name);
    HANDLE f =
        CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    DWORD written = 0;
    BOOL ok = WriteFile(f, data, size, &written, NULL) && written == size;
    CloseHandle(f);
    return ok;
}
