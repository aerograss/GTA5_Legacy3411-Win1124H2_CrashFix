// GTA V Legacy build 1.0.3411.0 - Windows 11 24H2 periodic crash workaround (single-player only)
//
// Crash site (identical in 3 dumps):  GTA5.exe + 0x038895F6
//     41 0F B6 04 10    movzx eax, byte ptr [r8+rdx]      ; r8 = 0x620026 (invalid), rdx = 0, r9 = 4
// This is the byte loop of an FNV-1a hash (basis 0x811C9DC5, prime 0x1000193).
//
// Approach (same idea as the published 3095 fix): the unpacked code has no RUNTIME_FUNCTION entry,
// so we rewrite the LAST .pdata entry to cover the crash instruction and attach a tiny exception
// handler. No game code is patched. The handler gives r8 a valid zeroed buffer and re-executes the
// instruction.
//
// Build:  g++ -shared -O2 -static -s GtaCrashFix3411.cpp -o GtaCrashFix3411.asi
// Install: put the .asi next to GTA5.exe (needs an ASI loader). Log: GtaCrashFix3411.log

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

namespace {

constexpr uint32_t kExpectedTimeDateStamp = 0x675ABFCB;  // from lmvm GTA5
constexpr uint32_t kExpectedSizeOfImage   = 0x044BC400;  // from lmvm GTA5
constexpr uint32_t kCrashRva              = 0x038895F6;
constexpr uint32_t kCrashLen              = 5;

alignas(16) char g_ZeroBuf[64] = {0};
volatile LONG g_FixCount = 0;
uintptr_t g_Base = 0;

void Log(const char* fmt, ...) {
    char path[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) strcpy_s(slash + 1, MAX_PATH - (slash - path + 1), "GtaCrashFix3411.log");
    else strcpy_s(path, MAX_PATH, "GtaCrashFix3411.log");
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") != 0 || !f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ", st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list args; va_start(args, fmt); vfprintf(f, fmt, args); va_end(args);
    fprintf(f, "\n");
    fclose(f);
}

uint8_t* AllocateNearModule(uint8_t* base, size_t imageSize, size_t size) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    uintptr_t gran = si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000;
    uintptr_t start = ((uintptr_t)base + imageSize + gran - 1) & ~(gran - 1);
    uintptr_t limit = (uintptr_t)base + 0x7FFF0000ULL;
    for (uintptr_t a = start; a < limit; a += gran) {
        void* p = VirtualAlloc((void*)a, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (p) return (uint8_t*)p;
    }
    return nullptr;
}

} // namespace

extern "C" EXCEPTION_DISPOSITION NTAPI GtaCrashSehHandler(
    PEXCEPTION_RECORD rec, PVOID /*establisher*/, PCONTEXT ctx, PVOID /*dispatcher*/) {
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && ctx->Rip == g_Base + kCrashRva) {
        DWORD64 oldR8 = ctx->R8;
        ctx->R8 = (DWORD64)&g_ZeroBuf[0];   // valid readable bytes; instruction is simply re-executed
        LONG n = InterlockedIncrement(&g_FixCount);
        if (n <= 10 || (n % 100) == 0)
            Log("Handled fault at GTA5+0x%X (r8 was 0x%llX, rdx=0x%llX, r9=0x%llX) count=%ld",
                kCrashRva, (unsigned long long)oldR8, (unsigned long long)ctx->Rdx,
                (unsigned long long)ctx->R9, n);
        return ExceptionContinueExecution;
    }
    return ExceptionContinueSearch;
}

static void Install() {
    uint8_t* base = (uint8_t*)GetModuleHandleW(NULL);
    g_Base = (uintptr_t)base;
    auto* dos = (IMAGE_DOS_HEADER*)base;
    auto* nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);

    if (nt->FileHeader.TimeDateStamp != kExpectedTimeDateStamp ||
        nt->OptionalHeader.SizeOfImage != kExpectedSizeOfImage) {
        Log("Wrong GTA5.exe build (timestamp 0x%X, size 0x%X) - not installing.",
            nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
        return;
    }

    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!dir.VirtualAddress || dir.Size < sizeof(RUNTIME_FUNCTION)) {
        Log("ERROR: no exception directory in GTA5.exe");
        return;
    }

    uint8_t* block = AllocateNearModule(base, nt->OptionalHeader.SizeOfImage, 4096);
    if (!block) { Log("ERROR: could not allocate memory near GTA5.exe"); return; }
    memset(block, 0, 4096);

    // block+0x00: jmp [rip+0] ; 8-byte handler address
    uint8_t* thunk = block;
    thunk[0] = 0xFF; thunk[1] = 0x25;
    *(uint32_t*)(thunk + 2) = 0;
    *(uint64_t*)(thunk + 6) = (uint64_t)&GtaCrashSehHandler;

    // block+0x20: UNWIND_INFO (version 1, UNW_FLAG_EHANDLER, no codes) + handler RVA
    uint8_t* unwind = block + 0x20;
    unwind[0] = 0x01 | (0x01 << 3);
    unwind[1] = 0; unwind[2] = 0; unwind[3] = 0;
    *(uint32_t*)(unwind + 4) = (uint32_t)(thunk - base);
    FlushInstructionCache(GetCurrentProcess(), block, 64);

    auto* pdata = (RUNTIME_FUNCTION*)(base + dir.VirtualAddress);
    size_t count = dir.Size / sizeof(RUNTIME_FUNCTION);
    RUNTIME_FUNCTION* last = &pdata[count - 1];

    Log("pdata: rva=0x%X size=0x%X entries=%zu; original last entry: begin=0x%X end=0x%X unwind=0x%X",
        dir.VirtualAddress, dir.Size, count, last->BeginAddress, last->EndAddress, last->UnwindData);
    if (count >= 2)
        Log("pdata: previous entry: begin=0x%X end=0x%X", pdata[count-2].BeginAddress, pdata[count-2].EndAddress);

    DWORD old = 0;
    if (!VirtualProtect(last, sizeof(*last), PAGE_READWRITE, &old)) {
        Log("ERROR: VirtualProtect failed (%lu)", GetLastError());
        return;
    }
    last->BeginAddress = kCrashRva;
    last->EndAddress   = kCrashRva + kCrashLen;
    last->UnwindData   = (uint32_t)(unwind - base);
    VirtualProtect(last, sizeof(*last), old, &old);
    Log("Installed .pdata handler for GTA5+0x%X", kCrashRva);
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(mod);
        Log("=== GtaCrashFix3411 loaded ===");
        Install();
    }
    return TRUE;
}
