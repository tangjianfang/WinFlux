#include <flux/flux.hpp>

namespace flux {

namespace {
using SetDpiAwarenessContextProc = BOOL(WINAPI*)(HANDLE);
} // namespace

bool enable_per_monitor_dpi_v2() noexcept {
    // Resolve dynamically so a hypothetical pre-1703 system degrades to the
    // default awareness instead of failing to launch.
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) return false;
    const auto proc = reinterpret_cast<SetDpiAwarenessContextProc>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (proc == nullptr) return false;
    // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == ((HANDLE)-4)
    return proc(reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-4))) != FALSE;
}

int run_message_loop() noexcept {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

} // namespace flux
