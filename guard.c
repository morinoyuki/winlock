/*
 * winlock-guard.exe  ——  Windows 侧守护程序
 *
 * 功能: 当前台窗口是配置中指定的游戏时, 拦截 Win 键(左/右), 防止误触
 *       弹出开始菜单 / 误切回桌面。切出游戏(焦点不在游戏)后 Win 键立即恢复。
 *
 * 运行平台: Windows
 * 编译(在 WSL2 内交叉编译):
 *   x86_64-w64-mingw32-gcc -O2 -Wall -mwindows -o winlock-guard.exe guard.c -lpsapi
 *
 * 使用:
 *   winlock-guard.exe                     # 默认读取 exe 同目录下的 winlock.conf
 *   winlock-guard.exe --config 路径 --status 路径
 */

#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- 配置 */

typedef struct {
    wchar_t games[16][64];  /* 目标游戏进程名列表, 如 eldenring.exe (不区分大小写) */
    int     n_games;       /* 已配置游戏数量                                          */
    wchar_t title[192];    /* 可选: 窗口标题子串(对全部游戏生效), 留空则只按进程名判断 */
    int     poll_ms;       /* 前台窗口检测间隔(毫秒)                                 */
    int     elevate;       /* run_as_admin: 1=以管理员权限运行(游戏管理员运行时需要)   */
} Config;

static Config   cfg;
static volatile LONG g_blocking = 0;   /* 1 = 正在拦截 Win 键 */
static const char *g_status_path = NULL;

/* ---------- 小工具函数 ---------- */

static void utf8_to_w(const char *s, wchar_t *out, int max) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, out, max);
    if (n <= 0) out[0] = 0;
}

static void w_to_utf8(const wchar_t *s, char *out, int max) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, out, max, NULL, NULL);
    if (n <= 0) out[0] = 0;
}

static int keyis(const char *k, size_t n, const char *s) {
    return strlen(s) == n && strncmp(k, s, n) == 0;
}

static wchar_t wl(wchar_t c);   /* 宽字符小写化, 定义见下(避免循环依赖先声明) */

static int has_exe_suffix(const wchar_t *s) {
    size_t n = wcslen(s);
    return n >= 4 && s[n-4] == L'.' &&
           wl(s[n-3]) == L'e' && wl(s[n-2]) == L'x' && wl(s[n-1]) == L'e';
}

/* 把一个游戏进程名加入列表; 未带 .exe 后缀则自动补上 */
static void add_game(const char *utf8_name) {
    if (cfg.n_games >= 16) return;
    wchar_t buf[64];
    utf8_to_w(utf8_name, buf, 63);
    if (!buf[0]) return;
    size_t n = wcslen(buf);
    if (!has_exe_suffix(buf) && n + 4 < 64) {
        buf[n] = L'.'; buf[n+1] = L'e'; buf[n+2] = L'x'; buf[n+3] = L'e';
        buf[n+4] = 0;
    }
    wcsncpy(cfg.games[cfg.n_games], buf, 63);
    cfg.games[cfg.n_games][63] = 0;
    cfg.n_games++;
}

static void parse_config(const char *path) {
    FILE *f = fopen(path, "rb");
    char line[512];
    if (!f) {
        fprintf(stderr, "winlock: 无法读取配置文件: %s\n", path);
        exit(2);
    }
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        char *key = p, *val = eq + 1;

        char *kend = eq;
        while (kend > key && (kend[-1] == ' ' || kend[-1] == '\t')) kend--;
        while (*val == ' ' || *val == '\t') val++;
        char *vend = val + strlen(val);
        while (vend > val && (vend[-1] == '\r' || vend[-1] == '\n' ||
                              vend[-1] == ' ' || vend[-1] == '\t')) vend--;

        size_t klen = (size_t)(kend - key);
        size_t vlen = (size_t)(vend - val);
        char buf[512];
        if (vlen > sizeof buf - 1) vlen = sizeof buf - 1;
        memcpy(buf, val, vlen);
        buf[vlen] = 0;

        if (keyis(key, klen, "game_exe")) {
            /* 支持多行 game_exe, 也支持一行内用逗号(,)或竖线(|)分隔多个进程名 */
            char *tok = buf;
            for (;;) {
                char *sep = tok;
                while (*sep && *sep != ',' && *sep != '|') sep++;
                char save = *sep;
                *sep = 0;
                char *t = tok;
                while (*t == ' ' || *t == '\t') t++;
                char *te = t + strlen(t);
                while (te > t && (te[-1] == ' ' || te[-1] == '\t')) te--;
                if (te > t) { *te = 0; add_game(t); }
                if (!save) break;
                tok = sep + 1;
            }
        } else if (keyis(key, klen, "window_title")) {
            utf8_to_w(buf, cfg.title, 191);
        } else if (keyis(key, klen, "poll_ms")) {
            cfg.poll_ms = atoi(buf);
        } else if (keyis(key, klen, "run_as_admin")) {
            cfg.elevate = (buf[0] == '1' || buf[0] == 'y' || buf[0] == 'Y');
        }
    }
    fclose(f);
}

/* ---------- 宽字符忽略大小写比较 ---------- */

static wchar_t wl(wchar_t c) { return (c >= L'A' && c <= L'Z') ? (wchar_t)(c + 32) : c; }

static int wieq(const wchar_t *a, const wchar_t *b) {
    if (!a || !b) return 0;
    while (*a && *b) { if (wl(*a) != wl(*b)) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

static int wicontains(const wchar_t *h, const wchar_t *n) {
    if (!h || !n || *n == 0) return 1;
    for (int i = 0; h[i]; i++) {
        int j = 0;
        while (h[i+j] && n[j] && wl(h[i+j]) == wl(n[j])) j++;
        if (!n[j]) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------ 状态文件 */

static int        g_elevated = 0;   /* 本进程是否以管理员身份运行 */
static wchar_t g_last_title[256];
static char    g_last_payload[1152];
static int     g_has_payload = 0;

static void write_status(const wchar_t *title, int focused, const wchar_t *exe_base) {
    if (!g_status_path) return;
    char tbuf[768], ebuf[128], buf[1152];
    w_to_utf8(title, tbuf, sizeof tbuf);
    w_to_utf8(exe_base, ebuf, sizeof ebuf);
    snprintf(buf, sizeof buf,
             "pid=%lu\nfocused=%d\nblocking=%d\nelevated=%d\nexe=%s\ntitle=%s\n",
             (unsigned long)GetCurrentProcessId(), focused, focused, g_elevated, ebuf, tbuf);
    if (g_has_payload && strcmp(buf, g_last_payload) == 0) return;

    strcpy(g_last_payload, buf);
    g_has_payload = 1;
    memcpy(g_last_title, title, sizeof g_last_title);

    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s.tmp", g_status_path);
    FILE *f = fopen(tmp, "wb");
    if (f) {
        fputs(buf, f);
        fclose(f);
        remove(g_status_path);
        rename(tmp, g_status_path);
    }
}

/* ------------------------------------------------------------ 检测逻辑 */

/* 部分 mingw 头文件未声明 IsUserAnAdmin, 手动声明(user32.dll 导出) */
extern int IsUserAnAdmin(void);

/* 当前进程是否具备管理员权限 */
static int is_admin(void) {
    return IsUserAnAdmin() != 0;
}

static void poll_once(void) {
    HWND h = GetForegroundWindow();
    wchar_t wtitle[256] = L"";
    wchar_t wexe[64] = L"";
    int focused = 0;

    if (h) {
        GetWindowTextW(h, wtitle, 255);
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid) {
            HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (hp) {
                wchar_t img[MAX_PATH];
                if (GetModuleFileNameExW(hp, NULL, img, MAX_PATH)) {
                    wchar_t *base = img;
                    for (int i = 0; img[i]; i++)
                        if (img[i] == L'\\' || img[i] == L'/') base = img + i + 1;
                    size_t bn = wcslen(base);
                    if (bn > 63) bn = 63;
                    wcsncpy(wexe, base, bn);
                    wexe[bn] = 0;
                }
                CloseHandle(hp);
            }
        }
        if (cfg.n_games) {
            for (int i = 0; i < cfg.n_games; i++)
                if (wieq(wexe, cfg.games[i])) { focused = 1; break; }
        }
        if (focused && cfg.title[0] && !wicontains(wtitle, cfg.title)) focused = 0;
    }

    InterlockedExchange(&g_blocking, focused);
    write_status(wtitle, focused, wexe);
}

static DWORD WINAPI poll_thread(void *ignored) {
    (void)ignored;
    poll_once();
    for (;;) {
        Sleep(cfg.poll_ms > 0 ? cfg.poll_ms : 300);
        poll_once();
    }
    return 0;
}

/* ------------------------------------------------------------ 键钩子 */

typedef struct { DWORD vk; DWORD scan; DWORD flags; DWORD time; DWORD extra; } KEV;

static LRESULT CALLBACK hook_proc(int code, WPARAM w, LPARAM l) {
    if (code >= 0 && g_blocking) {
        KEV *ke = (KEV *)l;
        if (ke->vk == VK_LWIN || ke->vk == VK_RWIN) {
            return 1;            /* 吞掉 Win 键(按下/抬起都吞) */
        }
    }
    return CallNextHookEx(NULL, code, w, l);
}

/* ---------------------------------------------------------------- main */

int main(int argc, char **argv) {
    cfg.poll_ms = 300;
    const char *conf_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) conf_path = argv[++i];
        else if (strcmp(argv[i], "--status") == 0 && i + 1 < argc) g_status_path = argv[++i];
    }

    if (!conf_path) {
        /* 无参数运行: 配置/状态文件放在 exe 同目录 */
        wchar_t dir[MAX_PATH];
        GetModuleFileNameW(NULL, dir, MAX_PATH);
        for (int i = 0; dir[i]; i++)
            if (dir[i] == L'/') dir[i] = L'\\';
        wchar_t *slash = NULL;
        for (int i = 0; dir[i]; i++) if (dir[i] == L'\\') slash = dir + i;
        if (slash) *(slash + 1) = 0;

        static wchar_t wdef[MAX_PATH], wstat[MAX_PATH];
        wcsncpy(wdef, dir, MAX_PATH - 32);
        wcsncpy(wstat, dir, MAX_PATH - 32);
        wcscat(wdef, L"winlock.conf");
        wcscat(wstat, L"winlock-status.txt");

        static char cb[MAX_PATH * 2], sb[MAX_PATH * 2];
        w_to_utf8(wdef, cb, sizeof cb);
        w_to_utf8(wstat, sb, sizeof sb);
        conf_path = cb;
        g_status_path = sb;
    }

    parse_config(conf_path);
    if (cfg.n_games == 0) {
        fprintf(stderr, "winlock: 配置错误, 请在 %s 中设置 game_exe\n", conf_path);
        return 3;
    }

    /* run_as_admin=1 且当前不是管理员: 用 PowerShell UAC 重新以管理员启动自己 */
    if (cfg.elevate && !is_admin()) {
        wchar_t wexe[MAX_PATH];
        GetModuleFileNameW(NULL, wexe, MAX_PATH);
        wchar_t wconf[640], wstat[640];
        utf8_to_w(conf_path, wconf, 639);
        utf8_to_w(g_status_path, wstat, 639);
        wchar_t wcmd[1400];
        swprintf(wcmd, 1400,
                 L"powershell -NoProfile -Command \"Start-Process -Verb RunAs -FilePath '%ls' -ArgumentList '--config','%ls','--status','%ls'\"",
                 wexe, wconf, wstat);
        _wsystem(wcmd);   /* 弹出 UAC, 确认后由提升的新实例接管 */
        return 0;
    }
    g_elevated = is_admin() ? 1 : 0;

    CreateThread(NULL, 0, poll_thread, NULL, 0, NULL);

    HHOOK hk = SetWindowsHookEx(WH_KEYBOARD_LL, hook_proc, NULL, 0);
    if (!hk) {
        fprintf(stderr, "winlock: 无法安装键盘钩子 (错误 %lu)\n", (unsigned long)GetLastError());
        return 4;
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}