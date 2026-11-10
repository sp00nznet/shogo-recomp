/*
 * Shogo: Mobile Armor Division (2.2) - static recompilation host.
 *
 * A 32-bit host on pcrecomp's runtime/native32 (the native bridge, callbacks
 * and machine lock; see its header), with the loader from the UT99 recomp.
 * LithTech is an engine exe (Client.exe) and a set of DLLs, every one lifted:
 * the renderer (d3d.ren), the server (server.dll), and the game code that
 * ships inside SHOGOP3.REZ (CSHELL.DLL, CRES.DLL, SRES.DLL, OBJECT.LTO).
 * Each is mapped at the base tools/rebase.py gave it, bound, and attached
 * (its DllMain run) the first time the engine loads it, the way Windows would.
 *
 * The engine copies the rez-resident DLLs to GetTempFileName files and
 * LoadLibrary's those, so a load is matched by name first, then by the PE
 * timestamp in the file it names.
 *
 * Linked at /BASE:0x60000000 (CMakeLists.txt) so 0x00400000 (Client.exe)
 * and 0x10000000..0x1076B000 (everything else) are free.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "native32.h"
#include "recomp_trace.h"

/* ---- the guest modules: same bases as tools/rebase.py ------------------ */
typedef struct {
    const char* name;
    uint32_t    base;
    int         attached;
    uint32_t    stamp;      /* PE TimeDateStamp, read once mapped */
} module_t;

static module_t g_modules[] = {
    { "Client.exe", 0x00400000u },
    { "server.dll", 0x10000000u },
    { "d3d.ren",    0x10100000u },
    { "CSHELL.DLL", 0x10200000u },
    { "CRES.DLL",   0x10300000u },
    { "OBJECT.LTO", 0x10400000u },
    { "SRES.DLL",   0x10600000u },
    { "soft.ren",   0x10700000u },
};
#define NMODULES ((int)(sizeof g_modules / sizeof g_modules[0]))

static module_t* g_exe = &g_modules[0];
static char g_game_dir[MAX_PATH];
static char g_guest_cmdline[2048];
static DWORD g_watchdog_s;

#define ARG(n) MEM32(g_esp + 4 + 4 * (n))

static IMAGE_NT_HEADERS32* nt_of(uint32_t base) {
    uint8_t* b = (uint8_t*)(uintptr_t)base;
    return (IMAGE_NT_HEADERS32*)(b + ((IMAGE_DOS_HEADER*)b)->e_lfanew);
}

static module_t* module_by_base(uint32_t h) {
    for (int i = 0; i < NMODULES; i++)
        if (g_modules[i].base == h) return &g_modules[i];
    return NULL;
}

static module_t* module_by_name(const char* path) {
    const char* p = path;
    for (const char* s = path; *s; s++)
        if (*s == '\\' || *s == '/' || *s == ':') p = s + 1;
    for (int i = 0; i < NMODULES; i++)
        if (!_stricmp(p, g_modules[i].name)) return &g_modules[i];
    return NULL;
}

/* A temp copy of a rez DLL: the file's PE timestamp names the module. A
 * different build of the same DLL (another rez won) is reported and runs
 * nothing: its code is not what was lifted. */
static module_t* module_by_file(const char* path) {
    uint8_t hdr[4096];
    DWORD got = 0;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    ReadFile(h, hdr, sizeof hdr, &got, NULL);
    CloseHandle(h);
    if (got < 0x200 || hdr[0] != 'M' || hdr[1] != 'Z') return NULL;
    uint32_t lfa = *(uint32_t*)(hdr + 0x3C);
    if (lfa + 0x100 > got) return NULL;
    uint32_t stamp = *(uint32_t*)(hdr + lfa + 8);
    for (int i = 0; i < NMODULES; i++)
        if (g_modules[i].stamp == stamp) return &g_modules[i];
    fprintf(stderr, "[load] %s: a PE we did not lift (stamp 0x%08X)\n", path, stamp);
    return NULL;
}

static int attach(module_t* m) {
    if (m->attached) return 1;
    m->attached = 1;
    uint8_t* b = (uint8_t*)(uintptr_t)m->base;
    IMAGE_NT_HEADERS32* nt = nt_of(m->base);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR* d = (IMAGE_IMPORT_DESCRIPTOR*)(b + dd.VirtualAddress);
         dd.VirtualAddress && d->Name; d++) {
        module_t* dep = module_by_name((const char*)(b + d->Name));
        if (dep && !attach(dep)) return 0;
    }
    if (nt->FileHeader.Characteristics & IMAGE_FILE_DLL && nt->OptionalHeader.AddressOfEntryPoint) {
        uint32_t args[3] = { m->base, DLL_PROCESS_ATTACH, 0 };
        fprintf(stderr, "[load] %s: DllMain\n", m->name);
        native32_call_guest(m->base + nt->OptionalHeader.AddressOfEntryPoint, 3, args);
        if (!g_eax) { fprintf(stderr, "[load] %s: DllMain failed\n", m->name); return 0; }
    }
    return 1;
}

/* ---- the loader's shims (stdcall, arguments from g_esp) ----------------- */

static void shim_LoadLibraryA(void) {
    const char* name = (const char*)(uintptr_t)ARG(0);
    module_t* m = module_by_name(name);
    if (!m) m = module_by_file(name);
    if (m) {
        fprintf(stderr, "[load] %s -> %s\n", name, m->name);
        g_eax = attach(m) ? m->base : 0;
    } else {
        g_eax = (uint32_t)(uintptr_t)LoadLibraryA(name);
        fprintf(stderr, "[load] %s native -> %08X\n", name, g_eax);
    }
    g_esp += 4 + 1 * 4;
}

static void shim_FreeLibrary(void) {
    g_eax = module_by_base(ARG(0)) ? TRUE : FreeLibrary((HMODULE)(uintptr_t)ARG(0));
    g_esp += 4 + 1 * 4;
}

static void shim_GetProcAddress(void) {
    uint32_t h = ARG(0);
    const char* name = (const char*)(uintptr_t)ARG(1);
    g_eax = module_by_base(h) ? native32_export(h, name)
                              : (uint32_t)(uintptr_t)GetProcAddress((HMODULE)(uintptr_t)h, name);
    if (module_by_base(h) && !g_eax && (uintptr_t)name > 0xFFFF)
        fprintf(stderr, "[load] GetProcAddress(%s, %s): no such export\n", module_by_base(h)->name, name);
    g_esp += 4 + 2 * 4;
}

static void shim_GetModuleHandleA(void) {
    const char* name = (const char*)(uintptr_t)ARG(0);
    module_t* m = name ? module_by_name(name) : g_exe;
    g_eax = m ? m->base : (uint32_t)(uintptr_t)GetModuleHandleA(name);
    g_esp += 4 + 1 * 4;
}

static void shim_GetModuleFileNameA(void) {
    char* out = (char*)(uintptr_t)ARG(1);
    uint32_t size = ARG(2);
    module_t* m = ARG(0) ? module_by_base(ARG(0)) : g_exe;
    if (m) {
        char path[MAX_PATH];
        _snprintf(path, sizeof path - 1, "%s\\%s", g_game_dir, m->name);
        path[MAX_PATH - 1] = 0;
        uint32_t len = (uint32_t)strlen(path);
        if (size) {
            uint32_t k = len < size ? len : size - 1;
            memcpy(out, path, k);
            out[k] = 0;
            len = k;
        }
        g_eax = len;
    } else {
        g_eax = GetModuleFileNameA((HMODULE)(uintptr_t)ARG(0), out, size);
    }
    g_esp += 4 + 3 * 4;
}

static void shim_GetCommandLineA(void) { g_eax = (uint32_t)(uintptr_t)g_guest_cmdline; g_esp += 4; }

static void shim_MessageBoxA(void) {
    fprintf(stderr, "[messagebox] %s: %s\n", (char*)(uintptr_t)ARG(2), (char*)(uintptr_t)ARG(1));
    g_eax = IDOK;
    g_esp += 4 + 4 * 4;
}

/* Miles' AIL_startup, called from the lifted engine, faults inside Windows
 * on a wild read two runs in three; called once from the host's own stack
 * before the guest starts it never has. So it runs here, early, and the
 * engine's call only reports that Miles is up. */
static void shim_AIL_startup(void) { g_esp += 4; }

static native32_shim_t g_shims[] = {
    { "_AIL_startup@0", shim_AIL_startup },
    { "LoadLibraryA", shim_LoadLibraryA },
    { "FreeLibrary", shim_FreeLibrary },
    { "GetProcAddress", shim_GetProcAddress },
    { "GetModuleHandleA", shim_GetModuleHandleA },
    { "GetModuleFileNameA", shim_GetModuleFileNameA },
    { "GetCommandLineA", shim_GetCommandLineA },
    { "MessageBoxA", shim_MessageBoxA },
};

/* ---- diagnostics ---------------------------------------------------------- */


void recomp_not_lifted(uint32_t va) {
    fprintf(stderr, "\n[not-lifted] sub_%08X  (called from 0x%08X)\n", va, g_cur_func);
    recomp_dump_trace("not-lifted");
    native32_dump_icalls(8);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 2);
}

static const char* module_of(uint32_t va) {
    for (int i = 0; i < NMODULES; i++) {
        if (va < g_modules[i].base || !native32_in_guest(g_modules[i].base)) continue;
        if (va < g_modules[i].base + nt_of(g_modules[i].base)->OptionalHeader.SizeOfImage)
            return g_modules[i].name;
    }
    return "";
}

static char g_crash_buf[4096];
static int g_crash_len;
static void crash_emit(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(g_crash_buf + g_crash_len, sizeof g_crash_buf - 1 - g_crash_len, fmt, ap);
    va_end(ap);
    if (n > 0) g_crash_len += n;
}

static LONG CALLBACK crash(EXCEPTION_POINTERS* ep) {
    static volatile LONG once;
    EXCEPTION_RECORD* r = ep->ExceptionRecord;
    if ((r->ExceptionCode & 0xF0000000u) != 0xC0000000u) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedExchange(&once, 1)) TerminateProcess(GetCurrentProcess(), 3);
    crash_emit("\n=== fault 0x%08lX at 0x%p, thread %lu ===\n", r->ExceptionCode, r->ExceptionAddress,
               GetCurrentThreadId());
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
        ULONG_PTR op = r->ExceptionInformation[0];
        uint32_t at = (uint32_t)r->ExceptionInformation[1];
        crash_emit("  %s of 0x%08X %s\n", op == 0 ? "read" : op == 1 ? "write" : "execute", at, module_of(at));
    }
    crash_emit("  in lifted sub_%08X (%s), last native call %s\n", g_cur_func, module_of(g_cur_func),
               g_cur_import);
    crash_emit("  eax=%08X ecx=%08X edx=%08X ebx=%08X esp=%08X ebp=%08X esi=%08X edi=%08X\n",
               g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi);
    crash_emit("last indirect calls (newest first):\n");
    for (int i = 1; i <= 12 && i <= (int)g_icall_trace_idx; i++) {
        uint32_t k = (g_icall_trace_idx - i) & (ICALL_TRACE_SIZE - 1);
        const char* nm = native32_name(g_icall_trace[k]);
        crash_emit("  0x%08X  from 0x%08X  %s%s\n", g_icall_trace[k], g_icall_from[k], nm ? nm : "",
                   nm ? "" : module_of(g_icall_trace[k]));
    }
    DWORD w;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), g_crash_buf, (DWORD)g_crash_len, &w, NULL);
    recomp_trace_flush();
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ---- --shot S FILE: the game window, from the screen, as a .bmp ----------- */
static DWORD g_shot_s[8];
static char g_shot_file[8][MAX_PATH];
static int g_shots;

static BOOL CALLBACK find_window(HWND w, LPARAM out) {
    DWORD pid;
    GetWindowThreadProcessId(w, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(w)) { *(HWND*)out = w; return FALSE; }
    return TRUE;
}

/* d3d.ren's surfaces (IDirectDrawSurface4s it keeps at these VAs), locked
 * and read as 16-bit RGB: a GDI copy of the screen is black. The primary
 * holds the finished frame (windowed, its part under the client area); the
 * back buffer is the fallback, and may be caught mid-frame. */
#define D3D_BACK_SURFACE    0x10145AC0u
#define D3D_PRIMARY_SURFACE 0x10145AC4u
static int shot_surface(const char* file, uint32_t slot, RECT* rc) {
    void** surf = (void**)(uintptr_t)MEM32(slot);
    if (!native32_in_guest(slot) || !surf) return 0;
    typedef HRESULT (WINAPI *lock_t)(void*, RECT*, void*, DWORD, HANDLE);
    typedef HRESULT (WINAPI *unlock_t)(void*, RECT*);
    void** vt = *(void***)surf;
    uint8_t d[0x7C] = { 0 };
    *(DWORD*)d = sizeof d;
    if (((lock_t)vt[25])(surf, rc, d, 1 /* DDLOCK_WAIT */, NULL)) return 0;
    int w = rc ? rc->right - rc->left : *(int*)(d + 0x0C), h = rc ? rc->bottom - rc->top : *(int*)(d + 0x08);
    int pitch = *(int*)(d + 0x10);
    uint32_t bits = *(uint32_t*)(d + 0x54), rmask = *(uint32_t*)(d + 0x58);
    const uint8_t* src = *(const uint8_t**)(d + 0x24);
    DWORD stride = (w * 3 + 3) & ~3u;
    uint8_t* out = (uint8_t*)calloc(stride, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t* o = out + (h - 1 - y) * stride + x * 3;
            if (bits == 16) {
                uint16_t p = ((const uint16_t*)(src + y * pitch))[x];
                int r5 = rmask == 0x7C00 ? (p >> 10) & 31 : p >> 11;
                int g = rmask == 0x7C00 ? ((p >> 5) & 31) << 3 : ((p >> 5) & 63) << 2;
                o[0] = (uint8_t)((p & 31) << 3); o[1] = (uint8_t)g; o[2] = (uint8_t)(r5 << 3);
            } else {
                memcpy(o, src + y * pitch + x * 4, 3);
            }
        }
    ((unlock_t)vt[32])(surf, rc);
    BITMAPINFOHEADER bh = { sizeof bh, w, h, 1, 24, BI_RGB };
    BITMAPFILEHEADER fh = { 0x4D42, sizeof fh + sizeof bh + stride * h, 0, 0, sizeof fh + sizeof bh };
    FILE* f = fopen(file, "wb");
    if (f) {
        fwrite(&fh, sizeof fh, 1, f);
        fwrite(&bh, sizeof bh, 1, f);
        fwrite(out, stride * h, 1, f);
        fclose(f);
        printf("[shot] %s %dx%d %ubpp -> %s\n", rc ? "screen" : "back buffer", w, h, bits, file);
    }
    free(out);
    return 1;
}

static void shot(const char* file) {
    HWND w = NULL;
    RECT rc;
    EnumWindows(find_window, (LPARAM)&w);
    if (!w || !GetClientRect(w, &rc)) { printf("[shot] no window\n"); return; }
    POINT o = { 0, 0 };
    ClientToScreen(w, &o);
    RECT on_screen = { o.x, o.y, o.x + rc.right, o.y + rc.bottom };
    if (shot_surface(file, D3D_PRIMARY_SURFACE, &on_screen) || shot_surface(file, D3D_BACK_SURFACE, NULL))
        return;
    int cw = rc.right, ch = rc.bottom;
    HDC scr = GetDC(NULL), mem = CreateCompatibleDC(scr);
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), cw, ch, 1, 24, BI_RGB } };
    void* bits;
    HBITMAP bmp = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    SelectObject(mem, bmp);
    /* From the screen, the window brought to the front first: DirectDraw's
     * primary-surface blits are on the screen, not in the window's DC. */
    SetForegroundWindow(w);
    BringWindowToTop(w);
    Sleep(300);
    BitBlt(mem, 0, 0, cw, ch, scr, o.x, o.y, SRCCOPY);
    GdiFlush();
    DWORD stride = (cw * 3 + 3) & ~3u, size = stride * ch;
    BITMAPFILEHEADER fh = { 0x4D42, sizeof fh + sizeof bi.bmiHeader + size, 0, 0, sizeof fh + sizeof bi.bmiHeader };
    FILE* f = fopen(file, "wb");
    if (f) {
        fwrite(&fh, sizeof fh, 1, f);
        fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f);
        fwrite(bits, size, 1, f);
        fclose(f);
        printf("[shot] %dx%d -> %s\n", cw, ch, file);
    }
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, scr);
}

/* A due shot is taken in d3d.ren's present (the engine calls it through
 * the renderer's function table, so it is an indirect call the host can
 * wrap): right after it the back buffer holds the whole frame, where a
 * shot taken at any other moment catches it half drawn (no menu text). */
#define D3D_PRESENT 0x10116A50u
void sub_10116A50(void);
static volatile LONG g_shot_due = -1;
/* D3DDP_DONOTCLIP off. d3d.ren asks Direct3D not to clip most of what it
 * draws (it clips itself, and 1998's hardware had a guard band). The RGB
 * rasteriser's span code then reads and writes past the surface for a
 * triangle that reaches outside it (wild reads and heap corruption inside
 * DrawIndexedPrimitive). So the device's DrawPrimitive and
 * DrawIndexedPrimitive go through these, which clear the flag, by giving
 * the device object a copy of its vtable. */
#define D3D_DEVICE3 0x101461F8u
#define D3DDP_DONOTCLIP 4u
typedef HRESULT (WINAPI *dp_t)(void*, DWORD, DWORD, void*, DWORD, DWORD);
typedef HRESULT (WINAPI *dip_t)(void*, DWORD, DWORD, void*, DWORD, WORD*, DWORD, DWORD);
static void* g_dev_vt[64];
static dp_t g_real_dp;
static dip_t g_real_dip;
static HRESULT WINAPI clip_dp(void* d, DWORD t, DWORD fvf, void* v, DWORD n, DWORD f) {
    return g_real_dp(d, t, fvf, v, n, f & ~D3DDP_DONOTCLIP);
}
static HRESULT WINAPI clip_dip(void* d, DWORD t, DWORD fvf, void* v, DWORD n, WORD* ix, DWORD ni, DWORD f) {
    return g_real_dip(d, t, fvf, v, n, ix, ni, f & ~D3DDP_DONOTCLIP);
}
static void clip_device(void) {
    void*** dev = (void***)(uintptr_t)MEM32(D3D_DEVICE3);
    if (!dev || *dev == g_dev_vt) return;
    memcpy(g_dev_vt, *dev, sizeof g_dev_vt);   /* IDirect3DDevice3: 42 methods */
    g_real_dp = (dp_t)g_dev_vt[28];             /* 0x70 */
    g_real_dip = (dip_t)g_dev_vt[29];           /* 0x74 */
    g_dev_vt[28] = (void*)clip_dp;
    g_dev_vt[29] = (void*)clip_dip;
    *dev = g_dev_vt;
}

static void hook_present(void) {
    sub_10116A50();
    clip_device();
    LONG i = g_shot_due;
    if (i >= 0 && shot_surface(g_shot_file[i], D3D_BACK_SURFACE, NULL)) InterlockedExchange(&g_shot_due, -1);
}

recomp_func_t recomp_lookup_manual(uint32_t va) {
    return va == D3D_PRESENT ? hook_present : NULL;
}

static DWORD WINAPI shot_thread(LPVOID unused) {
    (void)unused;
    DWORD t0 = GetTickCount();
    for (int i = 0; i < g_shots; i++) {
        DWORD at = t0 + g_shot_s[i] * 1000, now = GetTickCount();
        if (at > now) Sleep(at - now);
        InterlockedExchange(&g_shot_due, i);
        for (int k = 0; k < 100 && g_shot_due >= 0; k++) Sleep(20);
        if (InterlockedExchange(&g_shot_due, -1) >= 0) shot(g_shot_file[i]);   /* no frame in 2 s */
    }
    return 0;
}

/* --key VK@S: a key press sent to the game window at S seconds (DirectInput
 * reads the real keyboard, so it goes in through SendInput). */
static struct { WORD vk; DWORD at; } g_keys[64];
static int g_nkeys;

static DWORD WINAPI key_thread(LPVOID unused) {
    (void)unused;
    DWORD t0 = GetTickCount();
    for (int i = 0; i < g_nkeys; i++) {
        DWORD at = t0 + g_keys[i].at, now = GetTickCount();
        if (at > now) Sleep(at - now);
        HWND w = NULL;
        EnumWindows(find_window, (LPARAM)&w);
        if (w && GetForegroundWindow() != w) {
            /* Windows lets a background process take the foreground only
             * from inside the foreground thread's input queue. */
            DWORD fg = GetWindowThreadProcessId(GetForegroundWindow(), NULL), me = GetCurrentThreadId();
            AttachThreadInput(me, fg, TRUE);
            keybd_event(VK_MENU, 0, 0, 0);
            keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
            SetForegroundWindow(w);
            BringWindowToTop(w);
            AttachThreadInput(me, fg, FALSE);
            Sleep(200);
        }
        INPUT in[2] = { { INPUT_KEYBOARD }, { INPUT_KEYBOARD } };
        in[0].ki.wVk = in[1].ki.wVk = g_keys[i].vk;
        in[0].ki.wScan = in[1].ki.wScan = (WORD)MapVirtualKeyA(g_keys[i].vk, 0);
        /* the arrows, Insert..PageDown: their scancodes are extended ones */
        DWORD ext = (g_keys[i].vk >= VK_PRIOR && g_keys[i].vk <= VK_DOWN) ||
                    g_keys[i].vk == VK_INSERT || g_keys[i].vk == VK_DELETE ? KEYEVENTF_EXTENDEDKEY : 0;
        in[0].ki.dwFlags = KEYEVENTF_SCANCODE | ext;
        in[1].ki.dwFlags = KEYEVENTF_SCANCODE | ext | KEYEVENTF_KEYUP;
        /* Both ways in: the game's actions read DirectInput, its menus read
         * WM_KEYDOWN, which a window that is not in front never gets. */
        LPARAM lp = 1 | ((LPARAM)in[0].ki.wScan << 16) | (ext ? 1 << 24 : 0);
        SendInput(1, &in[0], sizeof(INPUT));
        if (w) PostMessageA(w, WM_KEYDOWN, g_keys[i].vk, lp);
        Sleep(250);   /* held across a few frames: DirectInput is polled once a frame */
        SendInput(1, &in[1], sizeof(INPUT));
        if (w) PostMessageA(w, WM_KEYUP, g_keys[i].vk, lp | 0xC0000000);
        printf("[key] 0x%02X at %lu ms%s\n", g_keys[i].vk, GetTickCount() - t0,
               GetForegroundWindow() == w ? "" : " (the window is not in front)");
    }
    return 0;
}

/* --trace-at S: the native-call trace for one second from S seconds, so a
 * steady state can be read without tracing the whole start-up. */
static DWORD g_trace_at;
static DWORD WINAPI trace_window(LPVOID unused) {
    (void)unused;
    Sleep(g_trace_at * 1000);
    native32_trace_native = 1;
    Sleep(1000);
    native32_trace_native = 0;
    return 0;
}

/* Code Windows' own DLLs build in heap memory and run: Direct3D's RGB
 * rasteriser (d3dim700) jumps into HeapAlloc'd span code, which a 1998
 * process could execute and this /NXCOMPAT host cannot. An execute fault in
 * read-write private memory outside the guest makes that region executable
 * (the UT99 recomp does the same for Galaxy's mixers).
 * ponytail: per title; a native32 option once a third title needs it. */
static LONG CALLBACK heap_code(EXCEPTION_POINTERS* ep) {
    EXCEPTION_RECORD* r = ep->ExceptionRecord;
    MEMORY_BASIC_INFORMATION mbi;
    DWORD old;
    if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2 ||
        r->ExceptionInformation[0] != 8)
        return EXCEPTION_CONTINUE_SEARCH;
    void* at = (void*)r->ExceptionInformation[1];
    if (native32_in_guest((uint32_t)(uintptr_t)at) || !VirtualQuery(at, &mbi, sizeof mbi) ||
        mbi.Type != MEM_PRIVATE || mbi.State != MEM_COMMIT || mbi.Protect != PAGE_READWRITE ||
        !VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_EXECUTE_READWRITE, &old))
        return EXCEPTION_CONTINUE_SEARCH;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static DWORD WINAPI watchdog(LPVOID unused) {
    (void)unused;
    Sleep(g_watchdog_s * 1000);
    fprintf(stderr, "\n[watchdog] %lu s: in sub_%08X (%s), last native call %s, %u indirect calls\n",
            g_watchdog_s, g_cur_func, module_of(g_cur_func), g_cur_import, g_icall_count);
    native32_dump_icalls(8);
    recomp_trace_flush();
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 4);
    return 0;
}

/* The renderers draw 16-bit surfaces only ("Unknown DirectDraw surface
 * format" on a 32-bit desktop), and windowed their surfaces take the
 * desktop's format. Windows' own "reduced color mode" compatibility layer
 * gives this process a 16-bit DirectDraw, but it is read at process start:
 * so the host runs itself again with it set. --no-16bit skips that. */
static int relaunch_16bit(void) {
    char layer[256] = "";
    /* A marker of our own: Windows takes __COMPAT_LAYER out of the
     * environment it hands the child, so it cannot say "already done". */
    if (GetEnvironmentVariableA("SHOGO_RECOMP_CHILD", NULL, 0)) return -1;
    SetEnvironmentVariableA("SHOGO_RECOMP_CHILD", "1");
    GetEnvironmentVariableA("__COMPAT_LAYER", layer, sizeof layer);
    strcat_s(layer, sizeof layer, layer[0] ? " 16BITCOLOR" : "16BITCOLOR");
    SetEnvironmentVariableA("__COMPAT_LAYER", layer);
    STARTUPINFOA si = { sizeof si };
    PROCESS_INFORMATION pi;
    DWORD rc = 1;
    if (!CreateProcessA(NULL, GetCommandLineA(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &rc);
    return (int)rc;
}

int main(int argc, char** argv) {
    const char* game = "game";
    char rebased[MAX_PATH] = "work\\rebased", tail[1024] = "";
    int run = 0;
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    {
        int no16 = 0, rc;
        for (int i = 1; i < argc; i++) no16 |= !strcmp(argv[i], "--no-16bit");
        for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--run") && !no16 && (rc = relaunch_16bit()) >= 0) return rc;
    }
    for (int i = 1; i < argc; i++) {
        int n = recomp_trace_arg(argc, argv, i);
        if (n) { i += n - 1; continue; }
        if (!strcmp(argv[i], "--run")) run = 1;
        else if (!strcmp(argv[i], "--no-16bit")) ;
        else if (!strcmp(argv[i], "--game") && i + 1 < argc) game = argv[++i];
        else if (!strcmp(argv[i], "--rebased") && i + 1 < argc) strcpy_s(rebased, sizeof rebased, argv[++i]);
        else if (!strcmp(argv[i], "--watchdog") && i + 1 < argc) g_watchdog_s = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc && g_shots < 8) {
            g_shot_s[g_shots] = strtoul(argv[++i], NULL, 0);
            GetFullPathNameA(argv[++i], MAX_PATH, g_shot_file[g_shots++], NULL);
        }
        else if (!strcmp(argv[i], "--key") && i + 1 < argc && g_nkeys < 64) {
            char* at = strchr(argv[++i], '@');
            if (!at) { fprintf(stderr, "--key wants VK@seconds\n"); return 1; }
            g_keys[g_nkeys].vk = (WORD)strtoul(argv[i], NULL, 0);
            g_keys[g_nkeys++].at = (DWORD)(atof(at + 1) * 1000);
        }
        else if (!strcmp(argv[i], "--native-trace")) native32_trace_native = 1;
        else if (!strcmp(argv[i], "--trace-at") && i + 1 < argc) g_trace_at = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--callbacks")) native32_trace_callbacks = 1;
        else if (!strcmp(argv[i], "--")) {
            for (i++; i < argc; i++) {
                strcat_s(tail, sizeof tail, " ");
                strcat_s(tail, sizeof tail, argv[i]);
            }
        } else {
            printf("usage: shogo [--run] [--game game] [--rebased work\\rebased] [--watchdog S]\n"
                   "             [--shot S FILE.bmp] [--key VK@S] [--native-trace] [--callbacks]\n"
                   "             [-- engine arguments, e.g. +windowed 1]\n");
            recomp_trace_help();
            return argv[i][1] == 'h' || argv[i][2] == 'h' ? 0 : 1;
        }
    }
    GetFullPathNameA(game, MAX_PATH, g_game_dir, NULL);
    /* What Shogo.exe (the launcher) passes, the 2.2 rezzes last so they win. */
    _snprintf(g_guest_cmdline, sizeof g_guest_cmdline - 1,
              "\"%s\\Client.exe\" -rez SHOGO.REZ -rez SOUND.REZ -rez ShogoP.rez -rez ShogoP2.rez"
              " -rez ShogoP3.rez -rez ShogoP4.rez%s", g_game_dir, tail);
    SetDllDirectoryA(g_game_dir);

    native32_init();
    {
        ULONG guarantee = 256 * 1024;
        SetThreadStackGuarantee(&guarantee);
    }
    {
        HMODULE mss = LoadLibraryA("mss32.dll");
        FARPROC up = mss ? GetProcAddress(mss, "_AIL_startup@0") : NULL;
        if (up) ((void (WINAPI*)(void))up)();
    }
    AddVectoredExceptionHandler(1, heap_code);
    AddVectoredExceptionHandler(0, crash);
    printf("Shogo recomp host\n  lifted functions in dispatch: %u\n", recomp_dispatch_count);

    for (int i = 0; i < NMODULES; i++) {
        char path[MAX_PATH];
        module_t* m = &g_modules[i];
        _snprintf(path, sizeof path - 1, "%s\\%s", rebased, m->name);
        uint32_t span = native32_map(path, m->base);
        if (!span) { fprintf(stderr, "cannot map %s at 0x%08X\n", path, m->base); return 1; }
        m->stamp = nt_of(m->base)->FileHeader.TimeDateStamp;
        printf("  mapped %-12s 0x%08X-0x%08X\n", m->name, m->base, m->base + span);
    }
    int missing = 0;
    for (int i = 0; i < NMODULES; i++)
        missing += native32_bind(g_modules[i].base, g_shims, (int)(sizeof g_shims / sizeof g_shims[0]));
    if (missing) fprintf(stderr, "warning: %d imports unresolved\n", missing);

    uint32_t entry = g_exe->base + nt_of(g_exe->base)->OptionalHeader.AddressOfEntryPoint;
    printf("  guest %s\n", g_guest_cmdline);
    if (!run) {
        printf("\n(dry run: mapped and bound; --run enters 0x%08X)\n", entry);
        return 0;
    }
    if (!SetCurrentDirectoryA(g_game_dir)) { fprintf(stderr, "cannot enter %s\n", g_game_dir); return 1; }
    if (g_watchdog_s) CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    if (g_shots) CloseHandle(CreateThread(NULL, 0, shot_thread, NULL, 0, NULL));
    if (g_trace_at) CloseHandle(CreateThread(NULL, 0, trace_window, NULL, 0, NULL));
    if (g_nkeys) CloseHandle(CreateThread(NULL, 0, key_thread, NULL, 0, NULL));
    if (!attach(g_exe)) return 1;
    printf("  entering 0x%08X\n\n", entry);
    native32_call_guest(entry, 0, NULL);
    printf("\nentry returned eax=%08X\n", g_eax);
    return (int)g_eax;
}
