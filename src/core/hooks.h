/* Vtable hooking. D3D11 objects of one type share a vtable, so the original function
   is remembered per (vtable, slot) and looked up again inside each hook.

   original() runs on every hooked call, so it is lock-free: entries are only ever
   appended, and each entry is fully written before hookCount is published and before
   the vtable slot is swapped to point at the hook. */
#define MAX_HOOKS 128u

struct Hook {
    void **table;
    unsigned slot;
    void *original;
};
static struct Hook hooks[MAX_HOOKS];
static volatile LONG hookCount;

static void *original(void *obj, unsigned slot) {
    void **v = *(void ***)obj;
    LONG n = hookCount;
    for (LONG i = 0; i < n; i++)
        if (hooks[i].table == v && hooks[i].slot == slot)
            return hooks[i].original;
    return NULL;
}

static void patch(void *obj, unsigned slot, void *fn) {
    if (!obj)
        return;
    void **v = *(void ***)obj;
    DWORD old;
    EnterCriticalSection(&lock);
    for (LONG i = 0; i < hookCount; i++)
        if (hooks[i].table == v && hooks[i].slot == slot) {
            LeaveCriticalSection(&lock);
            return; /* already hooked through another object sharing this vtable */
        }
    if (hookCount >= (LONG)MAX_HOOKS || !VirtualProtect(&v[slot], sizeof(void *), PAGE_READWRITE, &old)) {
        LeaveCriticalSection(&lock);
        logline(FALSE, "HOOK FAILED slot=%u error=%lu", slot, GetLastError());
        return;
    }
    hooks[hookCount] = (struct Hook){v, slot, v[slot]};
    InterlockedIncrement(&hookCount); /* full barrier: entry visible before the slot swap */
    InterlockedExchangePointer((PVOID volatile *)&v[slot], fn);
    DWORD ignored;
    BOOL restored = VirtualProtect(&v[slot], sizeof(void *), old, &ignored);
    LeaveCriticalSection(&lock);
    logline(FALSE, "HOOK installed slot=%u protectionRestored=%d", slot, restored);
}
