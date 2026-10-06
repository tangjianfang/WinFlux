#include <flux/Accessibility.hpp>

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace flux::a11y {
namespace {

// One provider per window; the map also tracks which windows are still
// alive (shutdown_for erases + disconnects).
class WindowProvider final : public IRawElementProviderSimple {
public:
    explicit WindowProvider(HWND hwnd) : hwnd_(hwnd) {}

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs_;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (out == nullptr) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) {
            *out = static_cast<IRawElementProviderSimple*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override {
        if (out == nullptr) return E_POINTER;
        *out = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** out) override {
        if (out == nullptr) return E_POINTER;
        *out = nullptr;   // L0: no patterns; L1 adds Invoke/Value per element
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* out) override {
        if (out == nullptr) return E_POINTER;
        VariantInit(out);
        switch (id) {
        case UIA_NamePropertyId: {
            wchar_t title[256]{};
            const int len = GetWindowTextW(hwnd_, title, 256);
            out->vt = VT_BSTR;
            out->bstrVal = SysAllocStringLen(title, static_cast<UINT>(len));
            return out->bstrVal != nullptr ? S_OK : E_OUTOFMEMORY;
        }
        case UIA_ControlTypePropertyId:
            out->vt = VT_I4;
            out->lVal = UIA_WindowControlTypeId;
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
        case UIA_HasKeyboardFocusPropertyId:
            out->vt = VT_BOOL;
            out->boolVal = VARIANT_TRUE;   // the HWND is one focus surface (L0)
            return S_OK;
        case UIA_NativeWindowHandlePropertyId:
            out->vt = VT_I4;
            out->lVal = static_cast<LONG>(reinterpret_cast<LONG_PTR>(hwnd_));
            return S_OK;
        default:
            return S_OK;
        }
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
        IRawElementProviderSimple** out) override {
        if (out == nullptr) return E_POINTER;
        *out = nullptr;   // we ARE the window's provider
        return S_OK;
    }

private:
    std::atomic<ULONG> refs_{1};
    HWND hwnd_;
};

std::mutex g_map_mutex;
std::unordered_map<HWND, WindowProvider*> g_providers;

} // namespace

IRawElementProviderSimple* window_provider(HWND hwnd) noexcept {
    if (hwnd == nullptr) return nullptr;
    std::lock_guard lock{g_map_mutex};
    if (const auto it = g_providers.find(hwnd); it != g_providers.end()) {
        return it->second;
    }
    auto* provider = new (std::nothrow) WindowProvider(hwnd);
    if (provider == nullptr) return nullptr;
    g_providers.emplace(hwnd, provider);
    return provider;
}

void shutdown_for(HWND hwnd) noexcept {
    if (hwnd == nullptr) return;
    IRawElementProviderSimple* disconnect = nullptr;
    {
        std::lock_guard lock{g_map_mutex};
        if (const auto it = g_providers.find(hwnd); it != g_providers.end()) {
            disconnect = it->second;
            g_providers.erase(it);
        }
    }
    if (disconnect != nullptr) {
        UiaDisconnectProvider(disconnect);
        disconnect->Release();   // pair the constructor-time AddRef (refs_ = 1)
    }
}

void notify_focus(HWND hwnd) noexcept {
    if (hwnd != nullptr) {
        NotifyWinEvent(EVENT_OBJECT_FOCUS, hwnd, OBJID_CLIENT, CHILDID_SELF);
    }
}

} // namespace flux::a11y
