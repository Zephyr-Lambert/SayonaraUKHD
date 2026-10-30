/* UmiGraphics.log writer (the log keeps the mod's original name).
   logline(FALSE, ...) always writes; logline(TRUE, ...) writes each distinct line only once.
   VLOG(...) is a unique line written only when [Debug] VerboseLog=1; when verbose logging is off
   it costs a single branch, so it is safe to use on per-draw paths. */
#define LOG_MAX_LINES 24000u
#define LOG_SEEN_SLOTS 8192u /* power of two */
#define LOG_SEEN_LIMIT 6144u /* keep the table at most 75% full */

static HANDLE logfile = INVALID_HANDLE_VALUE;
static volatile LONG logLines;
static uint64_t logSeen[LOG_SEEN_SLOTS];
static unsigned logSeenCount;

/* Returns TRUE if the line hash was not seen before (and records it). Caller holds lock. */
static BOOL logFirstSighting(uint64_t h) {
    if (!h)
        h = 1; /* 0 marks an empty slot */
    unsigned i = (unsigned)(h ^ (h >> 32)) & (LOG_SEEN_SLOTS - 1);
    while (logSeen[i]) {
        if (logSeen[i] == h)
            return FALSE;
        i = (i + 1) & (LOG_SEEN_SLOTS - 1);
    }
    if (logSeenCount >= LOG_SEEN_LIMIT)
        return FALSE; /* table full: drop further unique lines rather than slow down */
    logSeen[i] = h;
    ++logSeenCount;
    return TRUE;
}

static void logline(BOOL unique, const char *fmt, ...) {
    /* Cheap unlocked early-out: avoid formatting once the log is closed or full. */
    if (logfile == INVALID_HANDLE_VALUE || logLines >= (LONG)LOG_MAX_LINES)
        return;
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 3, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 3] = 0;
    size_t n = strlen(buf);
    uint64_t h = unique ? hashBytes(buf, n) : 0;
    EnterCriticalSection(&lock);
    if (logLines >= (LONG)LOG_MAX_LINES || (unique && !logFirstSighting(h))) {
        LeaveCriticalSection(&lock);
        return;
    }
    buf[n++] = '\r';
    buf[n++] = '\n';
    DWORD done;
    WriteFile(logfile, buf, (DWORD)n, &done, NULL);
    if (++logLines == (LONG)LOG_MAX_LINES) {
        const char end[] = "LOG LIMIT REACHED; rendering continues normally.\r\n";
        WriteFile(logfile, end, sizeof(end) - 1, &done, NULL);
    }
    LeaveCriticalSection(&lock);
}

#define VLOG(...)                                                                                            \
    do {                                                                                                     \
        if (verboseLog)                                                                                      \
            logline(TRUE, __VA_ARGS__);                                                                      \
    } while (0)

static void logTextureDesc(const char *kind, const D3D11_TEXTURE2D_DESC *d) {
    logline(TRUE,
            "%s %ux%u format=%u mip=%u array=%u samples=%u quality=%u usage=%u bind=0x%x cpu=0x%x misc=0x%x",
            kind, d->Width, d->Height, d->Format, d->MipLevels, d->ArraySize, d->SampleDesc.Count,
            d->SampleDesc.Quality, d->Usage, d->BindFlags, d->CPUAccessFlags, d->MiscFlags);
}
