#include <flux/Clipboard.hpp>

#include <windows.h>

namespace flux {

std::wstring clipboard_get_text() {
    if (!OpenClipboard(nullptr)) return {};
    std::wstring out;
    if (const HANDLE h = GetClipboardData(CF_UNICODETEXT); h != nullptr) {
        if (const auto* raw = static_cast<const wchar_t*>(GlobalLock(h)); raw != nullptr) {
            const SIZE_T bytes = GlobalSize(h);
            const size_t max_chars = bytes / sizeof(wchar_t);
            size_t len = 0;
            while (len < max_chars && raw[len] != L'\0') ++len;
            out.assign(raw, len);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

bool clipboard_set_text(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return false;
    bool ok = false;
    if (EmptyClipboard()) {
        const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        if (const HANDLE h = GlobalAlloc(GMEM_MOVEABLE, bytes); h != nullptr) {
            if (auto* dst = static_cast<wchar_t*>(GlobalLock(h)); dst != nullptr) {
                std::memcpy(dst, text.c_str(), bytes);
                GlobalUnlock(h);
                ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
            }
            if (!ok) GlobalFree(h);   // SetClipboardData failed — don't leak
        }
    }
    CloseClipboard();
    return ok;
}

} // namespace flux
