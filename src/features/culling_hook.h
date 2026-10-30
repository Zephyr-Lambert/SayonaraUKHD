/* Visibility culling fix.
   With a wider (or taller) view, the game's own visibility test would still reject objects
   outside the original 16:9 frustum, so they would pop in at the screen edges. The game's
   shared sphere-visibility routine is patched (only for the exact known executable build,
   verified by PE timestamp, image size and a hash of the routine's bytes). When the original
   test rejects an object for one of the known render callers, it is re-tested against side
   planes widened to the corrected view; accepted objects are never changed.
   See culling_policy.h for the plane math. */
#include "culling_policy.h"
static BOOL cullingEnabled, cullingAttempted, cullingInstalled;
static uintptr_t cullModule;
static LONG cullCalls, cullExpanded, cullRecovered, cullDoorRecovered;
static void *cullOriginal __attribute__((used));
/* Game ABI: ECX=camera, [ESP+4]=aligned sphere center, XMM2=radius, RET 4. */
static int __attribute__((naked, noinline)) invokeCulling(const float *camera __attribute__((unused)),
                                                          const float *point __attribute__((unused)),
                                                          float radius __attribute__((unused))) {
    __asm__("mov 4(%esp),%ecx\nmovss 12(%esp),%xmm2\npush 8(%esp)\ncall *_cullOriginal\nret\n");
}
/* Explicit cdecl: cullingEntry's assembly passes all four arguments on the stack. */
static int __attribute__((used, noinline, cdecl)) cullingDispatch(const float *camera, const float *point,
                                                                  float radius, uintptr_t caller) {
    static const DWORD callers[] = {0x35db,  0x3dd9,  0xfb7c,  0x168fe, 0x172be,
                                    0x18e27, 0x18fbe, 0x1a926, 0x1c546, 0x3556c};
    BOOL known = FALSE;
    for (unsigned i = 0; i < sizeof(callers) / sizeof(*callers); i++)
        if (caller == cullModule + callers[i])
            known = TRUE;
    InterlockedIncrement(&cullCalls);
    int originalResult = invokeCulling(camera, point, radius);
    /* Preserve accepted objects exactly; broaden only rejections from render callers. */
    if (originalResult || !known || !aspectEnabled || !cullingEnabled || !isfinite(radius) || radius < 0)
        return originalResult;
    float expanded[64] __attribute__((aligned(16)));
    if (!umi_expand_planes(camera, displayAspect, expanded))
        return originalResult;
    InterlockedIncrement(&cullExpanded);
    int result = invokeCulling(expanded, point, radius);
    if (result) {
        InterlockedIncrement(&cullRecovered);
        if (caller == cullModule + 0x168fe)
            InterlockedIncrement(&cullDoorRecovered);
    }
    return result;
}
/* Save all register/FPU/SSE state because this optimized internal ABI is narrower
   than a public C ABI. Only EAX's visibility result is replaced. */
static void __attribute__((naked, used)) cullingEntry(void) {
    __asm__("push %ebp\nmov %esp,%ebp\npushfl\npushal\nsub $544,%esp\nand $-16,%esp\n"
            "fxsave (%esp)\nmovss %xmm2,512(%esp)\nmov %esp,%eax\n"
            "push 4(%ebp)\npush 512(%eax)\npush 8(%ebp)\npush -12(%ebp)\n"
            "call _cullingDispatch\nadd $16,%esp\nmov %eax,-8(%ebp)\n"
            "fxrstor (%esp)\nlea -36(%ebp),%esp\npopal\npopfl\nleave\nret $4\n");
}
static void installCulling(void) {
    if (cullingAttempted || !aspectEnabled || !cullingEnabled)
        return;
    cullingAttempted = TRUE;
    unsigned char *base = (unsigned char *)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS32 nt;
    SIZE_T got;
    if (!base || !ReadProcessMemory(GetCurrentProcess(), base, &dos, sizeof(dos), &got) ||
        got != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
        dos.e_lfanew > 1048576 ||
        !ReadProcessMemory(GetCurrentProcess(), base + dos.e_lfanew, &nt, sizeof(nt), &got) ||
        got != sizeof(nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt.FileHeader.TimeDateStamp != 0x56c2ed52 || nt.OptionalHeader.SizeOfImage != 0x1bd000) {
        logline(FALSE, "CULLING not installed: executable build mismatch");
        return;
    }
    unsigned char bytes[178], *target = base + 0x4d220;
    if (!ReadProcessMemory(GetCurrentProcess(), target, bytes, sizeof(bytes), &got) || got != sizeof(bytes) ||
        hashBytes(bytes, sizeof(bytes)) != UINT64_C(0x8614a41bf3b2bc73)) {
        logline(FALSE, "CULLING not installed: visibility routine signature mismatch");
        return;
    }
    unsigned char *tramp = VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!tramp) {
        logline(FALSE, "CULLING trampoline allocation failed");
        return;
    }
    memcpy(tramp, bytes, 6);
    tramp[6] = 0xe9;
    DWORD back = (DWORD)(uintptr_t)(target + 6) - (DWORD)(uintptr_t)(tramp + 11);
    memcpy(tramp + 7, &back, 4);
    DWORD old;
    if (!VirtualProtect(tramp, 32, PAGE_EXECUTE_READ, &old)) {
        VirtualFree(tramp, 0, MEM_RELEASE);
        logline(FALSE, "CULLING trampoline protection failed");
        return;
    }
    FlushInstructionCache(GetCurrentProcess(), tramp, 11);
    if (!VirtualProtect(target, 6, PAGE_EXECUTE_READWRITE, &old)) {
        VirtualFree(tramp, 0, MEM_RELEASE);
        logline(FALSE, "CULLING target protection failed");
        return;
    }
    cullOriginal = tramp;
    cullModule = (uintptr_t)base;
    unsigned char patchBytes[6] = {0xe9, 0, 0, 0, 0, 0x90};
    DWORD jump = (DWORD)(uintptr_t)cullingEntry - (DWORD)(uintptr_t)(target + 5);
    memcpy(patchBytes + 1, &jump, 4);
    memcpy(target, patchBytes, 6);
    FlushInstructionCache(GetCurrentProcess(), target, 6);
    DWORD ignored;
    BOOL restored = VirtualProtect(target, 6, old, &ignored);
    cullingInstalled = TRUE;
    logline(FALSE, "CULLING installed: validated shared visibility routine; protectionRestored=%d", restored);
}
static void cullingStats(void) {
    if (cullingInstalled)
        logline(FALSE, "CULLING calls=%ld expandedTests=%ld recovered=%ld doorRecovered=%ld", cullCalls,
                cullExpanded, cullRecovered, cullDoorRecovered);
}
