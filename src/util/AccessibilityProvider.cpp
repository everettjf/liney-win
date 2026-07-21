#include "util/AccessibilityProvider.h"

#include <atomic>

namespace liney {
namespace {

class RootProvider final : public IRawElementProviderSimple {
public:
    explicit RootProvider(HWND hwnd) : hwnd_(hwnd) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IRawElementProviderSimple)) {
            *object = static_cast<IRawElementProviderSimple*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG refs = --refs_;
        if (refs == 0) delete this;
        return refs;
    }
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* options) override {
        if (!options) return E_POINTER;
        *options = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** provider) override {
        if (!provider) return E_POINTER;
        *provider = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* value) override {
        if (!value) return E_POINTER;
        VariantInit(value);
        switch (property) {
        case UIA_ControlTypePropertyId:
            value->vt = VT_I4;
            value->lVal = UIA_WindowControlTypeId;
            break;
        case UIA_NamePropertyId:
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(L"Liney terminal workspace");
            break;
        case UIA_AutomationIdPropertyId:
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(L"Liney.MainWindow");
            break;
        case UIA_IsKeyboardFocusablePropertyId:
            value->vt = VT_BOOL;
            value->boolVal = VARIANT_TRUE;
            break;
        case UIA_HasKeyboardFocusPropertyId:
            value->vt = VT_BOOL;
            value->boolVal = GetFocus() == hwnd_ ? VARIANT_TRUE : VARIANT_FALSE;
            break;
        case UIA_NativeWindowHandlePropertyId:
            value->vt = VT_I4;
            value->lVal = static_cast<LONG>(reinterpret_cast<LONG_PTR>(hwnd_));
            break;
        default:
            break;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
        IRawElementProviderSimple** provider) override {
        if (!provider) return E_POINTER;
        return UiaHostProviderFromHwnd(hwnd_, provider);
    }

private:
    std::atomic<ULONG> refs_{1};
    HWND hwnd_ = nullptr;
};

} // namespace

IRawElementProviderSimple* createAccessibilityProvider(HWND hwnd) {
    return hwnd ? new RootProvider(hwnd) : nullptr;
}

} // namespace liney
