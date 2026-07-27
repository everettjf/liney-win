#include "util/AccessibilityProvider.h"

#include <atomic>
#include <algorithm>
#include <cwctype>
#include <mutex>

namespace liney {
namespace {

class RootProvider;

class TextRangeProvider final : public ITextRangeProvider {
public:
    TextRangeProvider(RootProvider* root, std::wstring text, int start, int end);
    ~TextRangeProvider();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE Clone(ITextRangeProvider** result) override;
    HRESULT STDMETHODCALLTYPE Compare(ITextRangeProvider* range,
                                      BOOL* result) override;
    HRESULT STDMETHODCALLTYPE CompareEndpoints(
        TextPatternRangeEndpoint endpoint, ITextRangeProvider* target,
        TextPatternRangeEndpoint targetEndpoint, int* result) override;
    HRESULT STDMETHODCALLTYPE ExpandToEnclosingUnit(TextUnit unit) override;
    HRESULT STDMETHODCALLTYPE FindAttribute(TEXTATTRIBUTEID, VARIANT, BOOL,
                                            ITextRangeProvider** result) override;
    HRESULT STDMETHODCALLTYPE FindText(BSTR text, BOOL backward, BOOL ignoreCase,
                                       ITextRangeProvider** result) override;
    HRESULT STDMETHODCALLTYPE GetAttributeValue(TEXTATTRIBUTEID,
                                                VARIANT* result) override;
    HRESULT STDMETHODCALLTYPE GetBoundingRectangles(SAFEARRAY** result) override;
    HRESULT STDMETHODCALLTYPE GetEnclosingElement(
        IRawElementProviderSimple** result) override;
    HRESULT STDMETHODCALLTYPE GetText(int maxLength, BSTR* result) override;
    HRESULT STDMETHODCALLTYPE Move(TextUnit unit, int count, int* moved) override;
    HRESULT STDMETHODCALLTYPE MoveEndpointByUnit(
        TextPatternRangeEndpoint endpoint, TextUnit unit, int count,
        int* moved) override;
    HRESULT STDMETHODCALLTYPE MoveEndpointByRange(
        TextPatternRangeEndpoint endpoint, ITextRangeProvider* target,
        TextPatternRangeEndpoint targetEndpoint) override;
    HRESULT STDMETHODCALLTYPE Select() override { return UIA_E_NOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE AddToSelection() override {
        return UIA_E_NOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override {
        return UIA_E_NOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE ScrollIntoView(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetChildren(SAFEARRAY** result) override;

    int endpoint(TextPatternRangeEndpoint which) const {
        return which == TextPatternRangeEndpoint_Start ? start_ : end_;
    }

private:
    int nextBoundary(int position, TextUnit unit, int direction) const;
    void normalize();

    std::atomic<ULONG> refs_{1};
    RootProvider* root_ = nullptr;
    std::wstring text_;
    int start_ = 0;
    int end_ = 0;
};

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
                           public IRawElementProviderFragmentRoot,
                           public ITextProvider {
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
        } else if (id == __uuidof(ITextProvider)) {
            *object = static_cast<ITextProvider*>(this);
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
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern,
                                                  IUnknown** provider) override {
        if (!provider) return E_POINTER;
        *provider = nullptr;
        if (pattern == UIA_TextPatternId) {
            *provider = static_cast<ITextProvider*>(this);
            AddRef();
        }
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

    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** result) override {
        if (!result) return E_POINTER;
        *result = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
        return *result ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetVisibleRanges(SAFEARRAY** result) override {
        if (!result) return E_POINTER;
        *result = SafeArrayCreateVector(VT_UNKNOWN, 0, 1);
        if (!*result) return E_OUTOFMEMORY;
        std::wstring text = textSnapshot();
        ITextRangeProvider* range =
            new TextRangeProvider(this, text, 0, static_cast<int>(text.size()));
        LONG index = 0;
        HRESULT hr = SafeArrayPutElement(*result, &index, range);
        range->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE RangeFromChild(
        IRawElementProviderSimple*, ITextRangeProvider** result) override {
        return documentRange(result);
    }
    HRESULT STDMETHODCALLTYPE RangeFromPoint(
        UiaPoint, ITextRangeProvider** result) override {
        return documentRange(result);
    }
    HRESULT STDMETHODCALLTYPE get_DocumentRange(
        ITextRangeProvider** result) override {
        return documentRange(result);
    }
    HRESULT STDMETHODCALLTYPE get_SupportedTextSelection(
        SupportedTextSelection* result) override {
        if (!result) return E_POINTER;
        *result = SupportedTextSelection_None;
        return S_OK;
    }

    void update(const std::vector<AccessibleElementInfo>& elements,
                const AccessibleTextInfo& text) {
        std::lock_guard lock(mutex_);
        elements_ = elements;
        terminalText_ = text;
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
    std::wstring textSnapshot() const {
        std::lock_guard lock(mutex_);
        return terminalText_.text;
    }
    RECT textBounds() const {
        std::lock_guard lock(mutex_);
        RECT bounds = terminalText_.clientRect;
        MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&bounds), 2);
        return bounds;
    }
    IRawElementProviderSimple* simpleProvider() {
        auto* result = static_cast<IRawElementProviderSimple*>(this);
        AddRef();
        return result;
    }
    IRawElementProviderFragment* child(AccessibleElementId id) {
        return makeChild(id);
    }

private:
    HRESULT documentRange(ITextRangeProvider** result) {
        if (!result) return E_POINTER;
        const std::wstring text = textSnapshot();
        *result =
            new TextRangeProvider(this, text, 0, static_cast<int>(text.size()));
        return S_OK;
    }
    IRawElementProviderFragment* makeChild(AccessibleElementId id) {
        return static_cast<IRawElementProviderFragment*>(
            new ChildProvider(this, id));
    }

    std::atomic<ULONG> refs_{1};
    HWND hwnd_ = nullptr;
    mutable std::mutex mutex_;
    std::vector<AccessibleElementInfo> elements_;
    AccessibleTextInfo terminalText_;
};

TextRangeProvider::TextRangeProvider(RootProvider* root, std::wstring text,
                                     int start, int end)
    : root_(root), text_(std::move(text)), start_(start), end_(end) {
    if (root_) root_->AddRef();
    normalize();
}

TextRangeProvider::~TextRangeProvider() {
    if (root_) root_->Release();
}

HRESULT STDMETHODCALLTYPE TextRangeProvider::QueryInterface(REFIID id,
                                                            void** object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (id == __uuidof(IUnknown) || id == __uuidof(ITextRangeProvider))
        *object = static_cast<ITextRangeProvider*>(this);
    else
        return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE TextRangeProvider::Release() {
    const ULONG refs = --refs_;
    if (refs == 0) delete this;
    return refs;
}

void TextRangeProvider::normalize() {
    const int size = static_cast<int>(text_.size());
    start_ = std::clamp(start_, 0, size);
    end_ = std::clamp(end_, start_, size);
}

HRESULT STDMETHODCALLTYPE TextRangeProvider::Clone(ITextRangeProvider** result) {
    if (!result) return E_POINTER;
    *result = new TextRangeProvider(root_, text_, start_, end_);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE TextRangeProvider::Compare(ITextRangeProvider* range,
                                                     BOOL* result) {
    if (!result) return E_POINTER;
    auto* other = dynamic_cast<TextRangeProvider*>(range);
    *result = other && start_ == other->start_ && end_ == other->end_ &&
                      text_ == other->text_;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE TextRangeProvider::CompareEndpoints(
    TextPatternRangeEndpoint endpointValue, ITextRangeProvider* target,
    TextPatternRangeEndpoint targetEndpoint, int* result) {
    if (!target || !result) return E_POINTER;
    auto* other = dynamic_cast<TextRangeProvider*>(target);
    if (!other) return E_INVALIDARG;
    *result = endpoint(endpointValue) - other->endpoint(targetEndpoint);
    return S_OK;
}

int TextRangeProvider::nextBoundary(int position, TextUnit unit,
                                    int direction) const {
    const int size = static_cast<int>(text_.size());
    position = std::clamp(position, 0, size);
    if (unit == TextUnit_Document) return direction < 0 ? 0 : size;
    if (unit == TextUnit_Character || unit == TextUnit_Format)
        return std::clamp(position + direction, 0, size);
    const wchar_t delimiter = unit == TextUnit_Line ||
                                      unit == TextUnit_Paragraph
                                  ? L'\n'
                                  : L' ';
    if (direction > 0) {
        int next = position;
        while (next < size && text_[next] != delimiter) ++next;
        return next < size ? next + 1 : size;
    }
    int previous = std::max(0, position - 1);
    while (previous > 0 && text_[previous - 1] != delimiter) --previous;
    return previous;
}

HRESULT STDMETHODCALLTYPE TextRangeProvider::ExpandToEnclosingUnit(TextUnit unit) {
    if (unit == TextUnit_Document) {
        start_ = 0;
        end_ = static_cast<int>(text_.size());
    } else {
        start_ = nextBoundary(start_, unit, -1);
        end_ = nextBoundary(start_, unit, 1);
    }
    normalize();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE TextRangeProvider::FindAttribute(
    TEXTATTRIBUTEID, VARIANT, BOOL, ITextRangeProvider** result) {
    if (!result) return E_POINTER;
    *result = nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE TextRangeProvider::FindText(
    BSTR needle, BOOL backward, BOOL ignoreCase, ITextRangeProvider** result) {
    if (!result) return E_POINTER;
    *result = nullptr;
    if (!needle) return E_INVALIDARG;
    std::wstring hay = text_.substr(start_, end_ - start_);
    std::wstring find(needle, SysStringLen(needle));
    if (ignoreCase) {
        std::transform(hay.begin(), hay.end(), hay.begin(), towlower);
        std::transform(find.begin(), find.end(), find.begin(), towlower);
    }
    const size_t offset =
        backward ? hay.rfind(find) : hay.find(find);
    if (offset != std::wstring::npos)
        *result = new TextRangeProvider(
            root_, text_, start_ + static_cast<int>(offset),
            start_ + static_cast<int>(offset + find.size()));
    return S_OK;
}

HRESULT STDMETHODCALLTYPE TextRangeProvider::GetAttributeValue(
    TEXTATTRIBUTEID, VARIANT* result) {
    if (!result) return E_POINTER;
    VariantInit(result);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE TextRangeProvider::GetBoundingRectangles(
    SAFEARRAY** result) {
    if (!result) return E_POINTER;
    *result = SafeArrayCreateVector(VT_R8, 0, 4);
    if (!*result) return E_OUTOFMEMORY;
    const RECT r = root_ ? root_->textBounds() : RECT{};
    const double values[] = {static_cast<double>(r.left),
                             static_cast<double>(r.top),
                             static_cast<double>(r.right - r.left),
                             static_cast<double>(r.bottom - r.top)};
    for (LONG i = 0; i < 4; ++i)
        SafeArrayPutElement(*result, &i, const_cast<double*>(&values[i]));
    return S_OK;
}

HRESULT STDMETHODCALLTYPE TextRangeProvider::GetEnclosingElement(
    IRawElementProviderSimple** result) {
    if (!result) return E_POINTER;
    *result = root_ ? root_->simpleProvider() : nullptr;
    return *result ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}

HRESULT STDMETHODCALLTYPE TextRangeProvider::GetText(int maxLength,
                                                     BSTR* result) {
    if (!result) return E_POINTER;
    int length = end_ - start_;
    if (maxLength >= 0) length = std::min(length, maxLength);
    *result = SysAllocStringLen(text_.data() + start_, length);
    return *result || length == 0 ? S_OK : E_OUTOFMEMORY;
}

HRESULT STDMETHODCALLTYPE TextRangeProvider::Move(TextUnit unit, int count,
                                                  int* moved) {
    if (!moved) return E_POINTER;
    *moved = 0;
    if (count == 0) return S_OK;
    int position = start_;
    const int direction = count < 0 ? -1 : 1;
    for (int i = 0; i < std::abs(count); ++i) {
        const int next = nextBoundary(position, unit, direction);
        if (next == position) break;
        position = next;
        *moved += direction;
    }
    start_ = end_ = position;
    ExpandToEnclosingUnit(unit);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE TextRangeProvider::MoveEndpointByUnit(
    TextPatternRangeEndpoint which, TextUnit unit, int count, int* moved) {
    if (!moved) return E_POINTER;
    *moved = 0;
    int& position =
        which == TextPatternRangeEndpoint_Start ? start_ : end_;
    const int direction = count < 0 ? -1 : 1;
    for (int i = 0; i < std::abs(count); ++i) {
        const int next = nextBoundary(position, unit, direction);
        if (next == position) break;
        position = next;
        *moved += direction;
    }
    if (start_ > end_) {
        if (which == TextPatternRangeEndpoint_Start) end_ = start_;
        else start_ = end_;
    }
    normalize();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE TextRangeProvider::MoveEndpointByRange(
    TextPatternRangeEndpoint which, ITextRangeProvider* target,
    TextPatternRangeEndpoint targetEndpoint) {
    auto* other = dynamic_cast<TextRangeProvider*>(target);
    if (!other) return E_INVALIDARG;
    if (which == TextPatternRangeEndpoint_Start)
        start_ = other->endpoint(targetEndpoint);
    else
        end_ = other->endpoint(targetEndpoint);
    if (start_ > end_) {
        if (which == TextPatternRangeEndpoint_Start) end_ = start_;
        else start_ = end_;
    }
    normalize();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE TextRangeProvider::GetChildren(SAFEARRAY** result) {
    if (!result) return E_POINTER;
    *result = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
    return *result ? S_OK : E_OUTOFMEMORY;
}

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
    const std::vector<AccessibleElementInfo>& elements,
    const AccessibleTextInfo& terminalText) {
    if (!provider) return;
    static_cast<RootProvider*>(provider)->update(elements, terminalText);
}

} // namespace liney
