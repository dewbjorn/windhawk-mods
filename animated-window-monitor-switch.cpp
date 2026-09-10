// ==WindhawkMod==
// @id                animated-window-monitor-switch
// @name              Animated window monitor switch
// @description       Move the active window to the next monitor with a smooth animation using Alt+`
// @version           1.0
// @author            Tal Koren
// @include           explorer.exe
// @compilerOptions   -lole32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Animated window monitor switch

Press **Alt+`** (configurable) to move the currently active window to the
next monitor, with a smooth animation.

## Notes

- Runs in `explorer.exe`, so one global hotkey handles everything.
- Preserves the window's relative position between monitors.
- Preserves the window size, clamped to the destination monitor's work area.
- Maximized windows are moved to the next monitor and remain maximized.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- hotkey: "Alt+`"
  $name: Hotkey
  $description: >-
    Modifiers and key separated by "+", e.g. Ctrl+Alt+K, Win+F, Alt+`.
    Supported modifiers: Ctrl, Alt, Shift, Win. The key can be a single
    character, F1-F24, or a named key (Tab, Esc, Enter, Space, Backspace,
    Delete, Insert, Home, End, PageUp, PageDown, Up, Down, Left, Right).
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>

struct MonitorInfoEx {
    HMONITOR hMonitor;
    RECT rcMonitor;
    RECT rcWork;
};

static std::vector<MonitorInfoEx> g_monitors;
static std::thread g_hotkeyThread;
static std::atomic<bool> g_running = false;
static DWORD g_hotkeyThreadId = 0;
static HANDLE g_hotkeyThreadReady = nullptr;

static constexpr int kHotkeyId = 1;
static constexpr UINT kDefaultHotkeyModifiers = MOD_ALT;
static constexpr UINT kDefaultHotkeyVk = VK_OEM_3; // `
static UINT g_hotkeyModifiers = kDefaultHotkeyModifiers;
static UINT g_hotkeyVk = kDefaultHotkeyVk;
static constexpr int kAnimationDurationMs = 50;
static constexpr int kAnimationFrameMs = 10;

// Recognized non-character key names for the hotkey setting.
static bool ParseNamedKey(const std::wstring& tokenUpper, UINT& vkOut)
{
    static const std::pair<const wchar_t*, UINT> kNamedKeys[] = {
        {L"TAB", VK_TAB},       {L"ESC", VK_ESCAPE},   {L"ESCAPE", VK_ESCAPE},
        {L"ENTER", VK_RETURN},  {L"RETURN", VK_RETURN}, {L"SPACE", VK_SPACE},
        {L"BACKSPACE", VK_BACK}, {L"DELETE", VK_DELETE}, {L"DEL", VK_DELETE},
        {L"INSERT", VK_INSERT}, {L"INS", VK_INSERT},   {L"HOME", VK_HOME},
        {L"END", VK_END},       {L"PAGEUP", VK_PRIOR}, {L"PAGEDOWN", VK_NEXT},
        {L"UP", VK_UP},         {L"DOWN", VK_DOWN},    {L"LEFT", VK_LEFT},
        {L"RIGHT", VK_RIGHT},
    };

    for (const auto& [name, vk] : kNamedKeys) {
        if (tokenUpper == name) {
            vkOut = vk;
            return true;
        }
    }

    if (tokenUpper.size() >= 2 && tokenUpper.size() <= 3 && tokenUpper[0] == L'F') {
        wchar_t* end = nullptr;
        long n = wcstol(tokenUpper.c_str() + 1, &end, 10);
        if (end && *end == L'\0' && n >= 1 && n <= 24) {
            vkOut = VK_F1 + static_cast<UINT>(n - 1);
            return true;
        }
    }

    return false;
}

// Parses strings like "Ctrl+Alt+K" or "Alt+`" into MOD_* flags and a VK code.
static bool ParseHotkeySetting(const std::wstring& value, UINT& modifiersOut, UINT& vkOut)
{
    UINT modifiers = 0;
    bool haveKey = false;
    UINT vk = 0;

    size_t start = 0;
    while (start <= value.size()) {
        size_t plus = value.find(L'+', start);
        std::wstring token = (plus == std::wstring::npos) ? value.substr(start)
                                                            : value.substr(start, plus - start);

        size_t first = token.find_first_not_of(L" \t");
        if (first == std::wstring::npos) {
            token.clear();
        } else {
            size_t last = token.find_last_not_of(L" \t");
            token = token.substr(first, last - first + 1);
        }

        if (!token.empty()) {
            std::wstring upper = token;
            for (auto& c : upper) {
                c = static_cast<wchar_t>(towupper(c));
            }

            if (upper == L"CTRL" || upper == L"CONTROL") {
                modifiers |= MOD_CONTROL;
            } else if (upper == L"ALT") {
                modifiers |= MOD_ALT;
            } else if (upper == L"SHIFT") {
                modifiers |= MOD_SHIFT;
            } else if (upper == L"WIN" || upper == L"WINDOWS" || upper == L"SUPER") {
                modifiers |= MOD_WIN;
            } else if (haveKey) {
                return false; // more than one non-modifier token
            } else {
                UINT namedVk;
                if (ParseNamedKey(upper, namedVk)) {
                    vk = namedVk;
                    haveKey = true;
                } else if (token.size() == 1) {
                    SHORT scan = VkKeyScanW(token[0]);
                    if (scan == -1) {
                        return false;
                    }
                    vk = static_cast<UINT>(scan & 0xFF);
                    haveKey = true;
                } else {
                    return false;
                }
            }
        }

        if (plus == std::wstring::npos) {
            break;
        }
        start = plus + 1;
    }

    if (!haveKey) {
        return false;
    }

    modifiersOut = modifiers;
    vkOut = vk;
    return true;
}

static void LoadHotkeySetting()
{
    g_hotkeyModifiers = kDefaultHotkeyModifiers;
    g_hotkeyVk = kDefaultHotkeyVk;

    PCWSTR hotkeyStr = Wh_GetStringSetting(L"hotkey");
    std::wstring value = hotkeyStr;
    Wh_FreeStringSetting(hotkeyStr);

    UINT modifiers, vk;
    if (ParseHotkeySetting(value, modifiers, vk)) {
        g_hotkeyModifiers = modifiers;
        g_hotkeyVk = vk;
    } else {
        Wh_Log(L"Invalid hotkey setting %s, using default Alt+`", value.c_str());
    }
}

static BOOL CALLBACK EnumMonitorsProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam)
{
    auto monitors = reinterpret_cast<std::vector<MonitorInfoEx>*>(lParam);

    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);

    if (!GetMonitorInfoW(hMonitor, &mi)) {
        return TRUE;
    }

    monitors->push_back({
        hMonitor,
        mi.rcMonitor,
        mi.rcWork
    });

    return TRUE;
}

static void RefreshMonitors()
{
    g_monitors.clear();
    EnumDisplayMonitors(nullptr, nullptr, EnumMonitorsProc, reinterpret_cast<LPARAM>(&g_monitors));

    std::sort(g_monitors.begin(), g_monitors.end(), [](const auto& a, const auto& b) {
        if (a.rcMonitor.left != b.rcMonitor.left) {
            return a.rcMonitor.left < b.rcMonitor.left;
        }
        return a.rcMonitor.top < b.rcMonitor.top;
    });
}

static bool IsAltTabLikeWindow(HWND hwnd)
{
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
        return false;
    }

    hwnd = GetAncestor(hwnd, GA_ROOT);
    if (!hwnd) {
        return false;
    }

    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) {
        return false;
    }

    if (GetWindow(hwnd, GW_OWNER) != nullptr) {
        return false;
    }

    return true;
}

static RECT ClampRectToWorkArea(const RECT& rect, const RECT& work)
{
    RECT out = rect;

    int width = out.right - out.left;
    int height = out.bottom - out.top;

    int workWidth = work.right - work.left;
    int workHeight = work.bottom - work.top;

    width = std::min(width, workWidth);
    height = std::min(height, workHeight);

    out.right = out.left + width;
    out.bottom = out.top + height;

    if (out.left < work.left) {
        out.left = work.left;
        out.right = out.left + width;
    }
    if (out.top < work.top) {
        out.top = work.top;
        out.bottom = out.top + height;
    }
    if (out.right > work.right) {
        out.right = work.right;
        out.left = out.right - width;
    }
    if (out.bottom > work.bottom) {
        out.bottom = work.bottom;
        out.top = out.bottom - height;
    }

    return out;
}

static double EaseInOut(double t)
{
    // Smoothstep
    return t * t * (3.0 - 2.0 * t);
}

static void AnimateWindowToRect(HWND hwnd, const RECT& from, const RECT& to, int durationMs)
{
    if (durationMs <= 0) {
        SetWindowPos(
            hwnd,
            nullptr,
            to.left,
            to.top,
            to.right - to.left,
            to.bottom - to.top,
            SWP_NOZORDER | SWP_NOACTIVATE
        );
        return;
    }

    int steps = std::max(1, durationMs / kAnimationFrameMs);

    for (int i = 1; i <= steps; i++) {
        double t = static_cast<double>(i) / static_cast<double>(steps);
        double e = EaseInOut(t);

        int left = static_cast<int>(std::lround(from.left + (to.left - from.left) * e));
        int top = static_cast<int>(std::lround(from.top + (to.top - from.top) * e));
        int right = static_cast<int>(std::lround(from.right + (to.right - from.right) * e));
        int bottom = static_cast<int>(std::lround(from.bottom + (to.bottom - from.bottom) * e));

        SetWindowPos(
            hwnd,
            nullptr,
            left,
            top,
            right - left,
            bottom - top,
            SWP_NOZORDER | SWP_NOACTIVATE
        );

        std::this_thread::sleep_for(std::chrono::milliseconds(kAnimationFrameMs));
    }
}

static int FindMonitorIndex(HMONITOR hMonitor)
{
    for (size_t i = 0; i < g_monitors.size(); i++) {
        if (g_monitors[i].hMonitor == hMonitor) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

static RECT GetTargetRectForMonitorSwitch(const RECT& sourceRect, const RECT& srcWork, const RECT& dstWork)
{
    double srcWidth = static_cast<double>(srcWork.right - srcWork.left);
    double srcHeight = static_cast<double>(srcWork.bottom - srcWork.top);

    double dstWidth = static_cast<double>(dstWork.right - dstWork.left);
    double dstHeight = static_cast<double>(dstWork.bottom - dstWork.top);

    int width = sourceRect.right - sourceRect.left;
    int height = sourceRect.bottom - sourceRect.top;

    double relX = 0.0;
    double relY = 0.0;

    if (srcWidth > 1.0) {
        relX = static_cast<double>(sourceRect.left - srcWork.left) / srcWidth;
    }
    if (srcHeight > 1.0) {
        relY = static_cast<double>(sourceRect.top - srcWork.top) / srcHeight;
    }

    RECT target = {};
    target.left = dstWork.left + static_cast<int>(std::lround(relX * dstWidth));
    target.top = dstWork.top + static_cast<int>(std::lround(relY * dstHeight));
    target.right = target.left + width;
    target.bottom = target.top + height;

    return ClampRectToWorkArea(target, dstWork);
}

static void MoveForegroundWindowToNextMonitor()
{
    RefreshMonitors();

    if (g_monitors.size() < 2) {
        return;
    }

    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        return;
    }

    hwnd = GetAncestor(hwnd, GA_ROOT);
    if (!hwnd || !IsAltTabLikeWindow(hwnd)) {
        return;
    }

    if (hwnd == GetShellWindow()) {
        return;
    }

    HMONITOR currentMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    int currentIndex = FindMonitorIndex(currentMonitor);
    if (currentIndex < 0) {
        return;
    }

    int nextIndex = (currentIndex + 1) % static_cast<int>(g_monitors.size());
    const auto& srcMon = g_monitors[currentIndex];
    const auto& dstMon = g_monitors[nextIndex];

    BOOL maximized = IsZoomed(hwnd);

    if (maximized) {
        WINDOWPLACEMENT wp = {};
        wp.length = sizeof(wp);
        if (!GetWindowPlacement(hwnd, &wp)) {
            return;
        }

        RECT normalRect = wp.rcNormalPosition;
        RECT targetNormalRect = GetTargetRectForMonitorSwitch(normalRect, srcMon.rcWork, dstMon.rcWork);

        ShowWindow(hwnd, SW_RESTORE);
        AnimateWindowToRect(hwnd, normalRect, targetNormalRect, kAnimationDurationMs);

        wp.rcNormalPosition = targetNormalRect;
        SetWindowPlacement(hwnd, &wp);
        ShowWindow(hwnd, SW_MAXIMIZE);
        return;
    }

    RECT windowRect = {};
    if (!GetWindowRect(hwnd, &windowRect)) {
        return;
    }

    RECT targetRect = GetTargetRectForMonitorSwitch(windowRect, srcMon.rcWork, dstMon.rcWork);
    AnimateWindowToRect(hwnd, windowRect, targetRect, kAnimationDurationMs);
}

static void HotkeyThreadProc()
{
    g_hotkeyThreadId = GetCurrentThreadId();

    bool registered = RegisterHotKey(nullptr, kHotkeyId, g_hotkeyModifiers, g_hotkeyVk);
    if (!registered) {
        Wh_Log(L"RegisterHotKey failed: %u", GetLastError());
    } else {
        Wh_Log(L"Hotkey registered: modifiers=0x%x vk=0x%x", g_hotkeyModifiers, g_hotkeyVk);
    }

    // Signal only after g_hotkeyThreadId is set, so Wh_ModUninit can never
    // read it before it's valid and skip posting WM_QUIT.
    SetEvent(g_hotkeyThreadReady);

    if (!registered) {
        return;
    }

    MSG msg;
    while (g_running.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_HOTKEY && msg.wParam == kHotkeyId) {
            MoveForegroundWindowToNextMonitor();
        }
    }

    UnregisterHotKey(nullptr, kHotkeyId);
    Wh_Log(L"Hotkey unregistered");
}

BOOL Wh_ModInit()
{
    Wh_Log(L"Init");

    LoadHotkeySetting();
    RefreshMonitors();

    g_hotkeyThreadReady = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    g_running = true;
    g_hotkeyThread = std::thread(HotkeyThreadProc);

    WaitForSingleObject(g_hotkeyThreadReady, INFINITE);
    CloseHandle(g_hotkeyThreadReady);
    g_hotkeyThreadReady = nullptr;

    return TRUE;
}

void Wh_ModUninit()
{
    Wh_Log(L"Uninit");

    g_running = false;

    if (g_hotkeyThreadId != 0) {
        PostThreadMessageW(g_hotkeyThreadId, WM_QUIT, 0, 0);
    }

    if (g_hotkeyThread.joinable()) {
        g_hotkeyThread.join();
    }
}

BOOL Wh_ModSettingsChanged(BOOL* bReload)
{
    Wh_Log(L"SettingsChanged");

    *bReload = TRUE;
    return TRUE;
}
