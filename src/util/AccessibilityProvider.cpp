#include "util/AccessibilityProvider.h"

#include <atomic>
#include <mutex>

namespace liney {
namespace {

class RootProvider;

class ChildProvider final : public IRawElementProviderSimple,
                            public IRawElementProviderFragment,
                            public IInvokeProvider {
public:
    ChildProvider(RootProvider* root, AccessibleElementId id);
    ~ChildProvider();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* options) override;
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern,
                                                  IUnknown** provider) override;
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property,
                                                VARIANT* value) override;
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
        IRawElementProviderSimple** provider) override;

    HRESULT STDMETHODCALLTYPE Navigate(
        NavigateDirection direction,
        IRawElementProviderFragment** provider) override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** runtimeId) override;
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* rect) override;
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** roots) override;
    HRESULT STDMETHODCALLTYPE SetFocus() override;
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(
        IRawElementProviderFragmentRoot** root) override;

    HRESULT STDMETHODCALLTYPE Invoke() override;

private:
    std::atomic<ULONG> refs_{1};
    RootProvider* root_ = nullptr;
    AccessibleElementId id_{};
};

class RootProvider final : public IRawElementProviderSimple,
                           public IRawElementProviderFragment,
                           public IRawElementProviderFragmentRoot {
public:
    explicit RootProvider(HWND hwnd) : hwnd_(hwnd) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (id == __uuidof(IUnknown) ||
            id == __uuidof(IRawElementProviderSimple)) {
            *object = static_cast<IRawElementProviderSimple*>(this);
        } else if (id == __uuidof(IRawElementProviderFragment)) {
            *object = static_cast<IRawElementProviderFragment*>(this);
        } else if (id == __uuidof(IRawElementProviderFragmentRoot)) {
            *object = static_cast<IRawElementProviderFragmentRoot*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG refs = --refs_;
        if (refs == 0) delete this;
        return refs;
    }
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(
        ProviderOptions* options) override {
        if (!options) return E_POINTER;
        *options = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID,
                                                  IUnknown** provider) override {
        if (!provider) return E_POINTER;
        *provider = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property,
                                                VARIANT* value) override {
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
        case UIA_HelpTextPropertyId:
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(
                L"Terminal workspace. Press Ctrl+Shift+P to search commands, "
                L"profiles, workspaces, SSH hosts and agents.");
            break;
        case UIA_AcceleratorKeyPropertyId:
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(L"Ctrl+Shift+P");
            break;
        case UIA_LocalizedControlTypePropertyId:
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(L"terminal workspace");
            break;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
        case UIA_IsKeyboardFocusablePropertyId:
            value->vt = VT_BOOL;
            value->boolVal = VARIANT_TRUE;
            break;
        case UIA_HasKeyboardFocusPropertyId:
            value->vt = VT_BOOL;
            value->boolVal = ::GetFocus() == hwnd_ ? VARIANT_TRUE : VARIANT_FALSE;
            break;
        case UIA_NativeWindowHandlePropertyId:
            value->vt = VT_I4;
            value->lVal =
                static_cast<LONG>(reinterpret_cast<LONG_PTR>(hwnd_));
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

    HRESULT STDMETHODCALLTYPE Navigate(
        NavigateDirection direction,
        IRawElementProviderFragment** provider) override {
        if (!provider) return E_POINTER;
        *provider = nullptr;
        std::lock_guard lock(mutex_);
        if (elements_.empty()) return S_OK;
        if (direction == NavigateDirection_FirstChild)
            *provider = makeChild(elements_.front().id);
        else if (direction == NavigateDirection_LastChild)
            *provider = makeChild(elements_.back().id);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** runtimeId) override {
        if (!runtimeId) return E_POINTER;
        *runtimeId = nullptr; // HWND-backed fragment roots use the host runtime ID.
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* rect) override {
        if (!rect) return E_POINTER;
        RECT bounds{};
        GetWindowRect(hwnd_, &bounds);
        *rect = {static_cast<double>(bounds.left),
                 static_cast<double>(bounds.top),
                 static_cast<double>(bounds.right - bounds.left),
                 static_cast<double>(bounds.bottom - bounds.top)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(
        SAFEARRAY** roots) override {
        if (!roots) return E_POINTER;
        *roots = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override {
        ::SetFocus(hwnd_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(
        IRawElementProviderFragmentRoot** root) override {
        if (!root) return E_POINTER;
        *root = static_cast<IRawElementProviderFragmentRoot*>(this);
        AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(
        double x, double y, IRawElementProviderFragment** provider) override {
        if (!provider) return E_POINTER;
        *provider = nullptr;
        std::lock_guard lock(mutex_);
        for (const AccessibleElementInfo& element : elements_) {
            RECT bounds = element.clientRect;
            MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&bounds), 2);
            if (x >= bounds.left && x < bounds.right &&
                y >= bounds.top && y < bounds.bottom) {
                *provider = makeChild(element.id);
                break;
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFocus(
        IRawElementProviderFragment** provider) override {
        if (!provider) return E_POINTER;
        *provider = nullptr;
        return S_OK;
    }

    void update(const std::vector<AccessibleElementInfo>& elements) {
        std::lock_guard lock(mutex_);
        elements_ = elements;
    }
    bool info(AccessibleElementId id, AccessibleElementInfo& out) const {
        std::lock_guard lock(mutex_);
        for (const AccessibleElementInfo& element : elements_) {
            if (element.id == id) {
                out = element;
                return true;
            }
        }
        return false;
    }
    bool neighbor(AccessibleElementId id, int delta,
                  AccessibleElementId& neighborId) const {
        std::lock_guard lock(mutex_);
        for (size_t i = 0; i < elements_.size(); ++i) {
            if (elements_[i].id != id) continue;
            const long long next =
                static_cast<long long>(i) + static_cast<long long>(delta);
            if (next < 0 || next >= static_cast<long long>(elements_.size()))
                return false;
            neighborId = elements_[static_cast<size_t>(next)].id;
            return true;
        }
        return false;
    }
    HWND hwnd() const { return hwnd_; }
    IRawElementProviderFragment* child(AccessibleElementId id) {
        return makeChild(id);
    }

private:
    IRawElementProviderFragment* makeChild(AccessibleElementId id) {
        return static_cast<IRawElementProviderFragment*>(
            new ChildProvider(this, id));
    }

    std::atomic<ULONG> refs_{1};
    HWND hwnd_ = nullptr;
    mutable std::mutex mutex_;
    std::vector<AccessibleElementInfo> elements_;
};

ChildProvider::ChildProvider(RootProvider* root, AccessibleElementId id)
    : root_(root), id_(id) {
    if (root_) root_->AddRef();
}

ChildProvider::~ChildProvider() {
    if (root_) root_->Release();
}

HRESULT STDMETHODCALLTYPE ChildProvider::QueryInterface(REFIID id,
                                                        void** object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (id == __uuidof(IUnknown) ||
        id == __uuidof(IRawElementProviderSimple)) {
        *object = static_cast<IRawElementProviderSimple*>(this);
    } else if (id == __uuidof(IRawElementProviderFragment)) {
        *object = static_cast<IRawElementProviderFragment*>(this);
    } else if (id == __uuidof(IInvokeProvider)) {
        *object = static_cast<IInvokeProvider*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE ChildProvider::Release() {
    const ULONG refs = --refs_;
    if (refs == 0) delete this;
    return refs;
}

HRESULT STDMETHODCALLTYPE ChildProvider::get_ProviderOptions(
    ProviderOptions* options) {
    if (!options) return E_POINTER;
    *options = ProviderOptions_ServerSideProvider;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ChildProvider::GetPatternProvider(
    PATTERNID pattern, IUnknown** provider) {
    if (!provider) return E_POINTER;
    *provider = nullptr;
    if (pattern == UIA_InvokePatternId) {
        *provider = static_cast<IInvokeProvider*>(this);
        AddRef();
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ChildProvider::GetPropertyValue(PROPERTYID property,
                                                          VARIANT* value) {
    if (!value) return E_POINTER;
    VariantInit(value);
    AccessibleElementInfo info;
    if (!root_ || !root_->info(id_, info)) return UIA_E_ELEMENTNOTAVAILABLE;
    switch (property) {
    case UIA_ControlTypePropertyId:
        value->vt = VT_I4;
        value->lVal = UIA_ButtonControlTypeId;
        break;
    case UIA_NamePropertyId:
        value->vt = VT_BSTR;
        value->bstrVal = SysAllocString(info.name.c_str());
        break;
    case UIA_AutomationIdPropertyId:
        value->vt = VT_BSTR;
        value->bstrVal = SysAllocString(info.automationId.c_str());
        break;
    case UIA_HelpTextPropertyId:
        value->vt = VT_BSTR;
        value->bstrVal = SysAllocString(info.helpText.c_str());
        break;
    case UIA_AcceleratorKeyPropertyId:
        value->vt = VT_BSTR;
        value->bstrVal = SysAllocString(info.accelerator.c_str());
        break;
    case UIA_AccessKeyPropertyId:
        value->vt = VT_BSTR;
        value->bstrVal = SysAllocString(info.accessKey.c_str());
        break;
    case UIA_IsControlElementPropertyId:
    case UIA_IsContentElementPropertyId:
    case UIA_IsKeyboardFocusablePropertyId:
        value->vt = VT_BOOL;
        value->boolVal = VARIANT_TRUE;
        break;
    case UIA_IsEnabledPropertyId:
        value->vt = VT_BOOL;
        value->boolVal = info.enabled ? VARIANT_TRUE : VARIANT_FALSE;
        break;
    case UIA_HasKeyboardFocusPropertyId:
        value->vt = VT_BOOL;
        value->boolVal = VARIANT_FALSE;
        break;
    default:
        break;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ChildProvider::get_HostRawElementProvider(
    IRawElementProviderSimple** provider) {
    if (!provider) return E_POINTER;
    *provider = nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ChildProvider::Navigate(
    NavigateDirection direction, IRawElementProviderFragment** provider) {
    if (!provider) return E_POINTER;
    *provider = nullptr;
    if (!root_) return UIA_E_ELEMENTNOTAVAILABLE;
    if (direction == NavigateDirection_Parent) {
        *provider = static_cast<IRawElementProviderFragment*>(root_);
        root_->AddRef();
    } else if (direction == NavigateDirection_NextSibling ||
               direction == NavigateDirection_PreviousSibling) {
        AccessibleElementId neighbor{};
        const int delta =
            direction == NavigateDirection_NextSibling ? 1 : -1;
        if (root_->neighbor(id_, delta, neighbor))
            *provider = root_->child(neighbor);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ChildProvider::GetRuntimeId(SAFEARRAY** runtimeId) {
    if (!runtimeId) return E_POINTER;
    *runtimeId = SafeArrayCreateVector(VT_I4, 0, 2);
    if (!*runtimeId) return E_OUTOFMEMORY;
    LONG index = 0;
    int value = UiaAppendRuntimeId;
    SafeArrayPutElement(*runtimeId, &index, &value);
    index = 1;
    value = static_cast<int>(id_);
    SafeArrayPutElement(*runtimeId, &index, &value);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ChildProvider::get_BoundingRectangle(UiaRect* rect) {
    if (!rect) return E_POINTER;
    AccessibleElementInfo info;
    if (!root_ || !root_->info(id_, info)) return UIA_E_ELEMENTNOTAVAILABLE;
    RECT bounds = info.clientRect;
    MapWindowPoints(root_->hwnd(), nullptr,
                    reinterpret_cast<POINT*>(&bounds), 2);
    *rect = {static_cast<double>(bounds.left),
             static_cast<double>(bounds.top),
             static_cast<double>(bounds.right - bounds.left),
             static_cast<double>(bounds.bottom - bounds.top)};
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ChildProvider::GetEmbeddedFragmentRoots(
    SAFEARRAY** roots) {
    if (!roots) return E_POINTER;
    *roots = nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ChildProvider::SetFocus() {
    if (!root_) return UIA_E_ELEMENTNOTAVAILABLE;
    ::SetFocus(root_->hwnd());
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ChildProvider::get_FragmentRoot(
    IRawElementProviderFragmentRoot** root) {
    if (!root) return E_POINTER;
    *root = nullptr;
    if (!root_) return UIA_E_ELEMENTNOTAVAILABLE;
    *root = static_cast<IRawElementProviderFragmentRoot*>(root_);
    root_->AddRef();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ChildProvider::Invoke() {
    AccessibleElementInfo info;
    if (!root_ || !root_->info(id_, info)) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!info.enabled) return UIA_E_ELEMENTNOTENABLED;
    return PostMessageW(root_->hwnd(), kAccessibilityInvokeMessage,
                        static_cast<WPARAM>(id_), 0)
        ? S_OK
        : HRESULT_FROM_WIN32(GetLastError());
}

} // namespace

IRawElementProviderSimple* createAccessibilityProvider(HWND hwnd) {
    return hwnd ? static_cast<IRawElementProviderSimple*>(
                      new RootProvider(hwnd))
                : nullptr;
}

void updateAccessibilityProvider(
    IRawElementProviderSimple* provider,
    const std::vector<AccessibleElementInfo>& elements) {
    if (!provider) return;
    static_cast<RootProvider*>(provider)->update(elements);
}

} // namespace liney
