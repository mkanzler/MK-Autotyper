#pragma once
#include "AppDialogs.h"
#include "ControlLayout.h"
#include "SnippetCodec.h"
#include <windowsx.h>

namespace stepflow {
constexpr int HeaderBase = 2000;
constexpr UINT Relayout = WM_APP + 61;

// The viewport owns scrolling, card headers and drag gestures. Editing and the
// action document stay with the delegate; a drag changes data only on drop.
struct Delegate {
    virtual ~Delegate() = default;
    virtual size_t count() const = 0;
    virtual size_t selected() const = 0;
    virtual bool expanded(size_t index) const = 0;
    virtual int bodyHeight(size_t index, int width) = 0;
    virtual secure::Text title(size_t index) const = 0;
    virtual void paintBody(size_t index, HDC dc, RECT rect) const = 0;
    virtual void placeBody(controls::Layout& batch, RECT rect) = 0;
    virtual void tabOrder(HWND& previous) = 0;
    virtual bool activate(size_t index, bool toggle) = 0;
    virtual bool reorder(size_t from, size_t to) = 0;
    virtual void menu(size_t index, HWND anchor) = 0;
    virtual void capture(bool active) = 0;

    virtual void afterLayout() {}
};

class View {
    enum class HeaderPart {
        none,
        grip,
        menu,
        toggle
    };

    struct Item {
        RECT rect{};
        HWND header = nullptr;
        int regionWidth = 0, regionHeight = 0;
        bool regionExpanded = false;
    };

    HWND owner_ = nullptr, window_ = nullptr, footer_ = nullptr;
    HINSTANCE instance_ = nullptr;
    Delegate* delegate_ = nullptr;
    dialogs::Style style_{};
    std::vector<Item> items_;
    int scroll_ = 0, total_ = 0, wheel_ = 0;
    bool layingOut_ = false, queued_ = false, enabled_ = true;
    bool pendingDrag_ = false, dragging_ = false;
    size_t source_ = 0, slot_ = 0;
    POINT start_{}, pointer_{};
    HWND hotHeader_ = nullptr;
    HeaderPart hotPart_ = HeaderPart::none;

    HeaderPart part(HWND field, int x) const {
        RECT r{};
        GetClientRect(field, &r);
        if (x < px(30))
            return HeaderPart::grip;
        return x >= r.right - px(66) && x < r.right - px(30) ? HeaderPart::menu : HeaderPart::toggle;
    }

    int px(int value) const {
        return MulDiv(value, style_.dpi ? style_.dpi : 96, 96);
    }

    RECT client() const {
        RECT r{};
        GetClientRect(window_, &r);
        return r;
    }

    RECT shown(RECT r) const {
        OffsetRect(&r, 0, -scroll_);
        return r;
    }

    RECT body(size_t index) const {
        auto r = shown(items_[index].rect);
        r.left += px(14);
        r.right -= px(14);
        r.top += px(48);
        r.bottom -= px(12);
        return r;
    }

    static void box(HDC dc, RECT r, COLORREF fill, COLORREF border, int radius) {
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, border);
        const auto oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
    }

    void destroyHeader(Item& item) {
        if (item.header) {
            if (hotHeader_ == item.header) {
                hotHeader_ = nullptr;
                hotPart_ = HeaderPart::none;
            }
            if (GetFocus() == item.header)
                SetFocus(window_);
            SetWindowTextW(item.header, L"");
            DestroyWindow(item.header);
            item.header = nullptr;
            item.regionWidth = item.regionHeight = 0;
        }
    }

    void updateHeaderRegion(Item& item, bool expanded) const {
        RECT rect{};
        GetClientRect(item.header, &rect);
        if (item.regionWidth == rect.right && item.regionHeight == rect.bottom &&
            item.regionExpanded == expanded)
            return;
        // The window owns a successful region. Extending its bottom keeps an
        // expanded header square along the join to the card body.
        const int radius = px(10);
        HRGN region = CreateRoundRectRgn(
            0, 0, rect.right + 1, rect.bottom + 1 + (expanded ? radius : 0), radius, radius);
        if (!region)
            return;
        if (!SetWindowRgn(item.header, region, TRUE)) {
            DeleteObject(region);
            return;
        }
        item.regionWidth = rect.right;
        item.regionHeight = rect.bottom;
        item.regionExpanded = expanded;
    }

    static LRESULT CALLBACK headerProc(
        HWND field, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data) {
        auto& self = *reinterpret_cast<View*>(data);
        const size_t index = static_cast<size_t>(GetDlgCtrlID(field) - HeaderBase);
        if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT && self.enabled_) {
            SetCursor(LoadCursorW(nullptr,
                self.hotHeader_ == field && self.hotPart_ == HeaderPart::grip ? IDC_SIZEALL : IDC_HAND));
            return TRUE;
        }
        if (message == WM_MOUSEMOVE && !self.pendingDrag_) {
            const auto part = self.part(field, GET_X_LPARAM(lParam));
            if (self.hotHeader_ != field || self.hotPart_ != part) {
                if (self.hotHeader_)
                    InvalidateRect(self.hotHeader_, nullptr, FALSE);
                self.hotHeader_ = field;
                self.hotPart_ = part;
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, field, 0};
                TrackMouseEvent(&track);
                InvalidateRect(field, nullptr, FALSE);
            }
            SetCursor(LoadCursorW(nullptr, part == HeaderPart::grip ? IDC_SIZEALL : IDC_HAND));
        }
        if (message == WM_MOUSELEAVE && self.hotHeader_ == field) {
            self.hotHeader_ = nullptr;
            self.hotPart_ = HeaderPart::none;
            InvalidateRect(field, nullptr, FALSE);
        }
        if (self.pendingDrag_ &&
            (message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == WM_CONTEXTMENU)) {
            if (message != WM_CONTEXTMENU && wParam == VK_ESCAPE)
                self.cancelDrag();
            return 0;
        }
        if (message == WM_LBUTTONDOWN && self.enabled_) {
            const auto hit = self.part(field, GET_X_LPARAM(lParam));
            if (hit == HeaderPart::grip) {
                POINT origin{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                MapWindowPoints(field, self.window_, &origin, 1);
                if (self.delegate_->activate(index, false)) {
                    SetFocus(field);
                    self.pendingDrag_ = true;
                    self.source_ = index;
                    self.start_ = origin;
                    self.pointer_ = self.start_;
                    SetCapture(self.window_);
                }
                return 0;
            }
            if (hit == HeaderPart::menu) {
                self.delegate_->menu(index, field);
                return 0;
            }
        }
        if (message == WM_CONTEXTMENU) {
            self.delegate_->menu(index, field);
            return 0;
        }
        if (message == WM_KEYDOWN) {
            if (wParam == VK_ESCAPE && self.pendingDrag_) {
                self.cancelDrag();
                return 0;
            }
            if (wParam == VK_UP || wParam == VK_DOWN) {
                const size_t target = wParam == VK_UP ? (index ? index - 1 : index)
                                                      : std::min(index + 1, self.delegate_->count() - 1);
                if (GetKeyState(VK_MENU) & 0x8000)
                    self.delegate_->reorder(index, target);
                else if (self.delegate_->activate(target, false))
                    self.focusHeader(target);
                return 0;
            }
        }
        if (message == WM_SYSKEYDOWN && (wParam == VK_UP || wParam == VK_DOWN)) {
            const size_t target = wParam == VK_UP ? (index ? index - 1 : index)
                                                  : std::min(index + 1, self.delegate_->count() - 1);
            self.delegate_->reorder(index, target);
            return 0;
        }
        if (message == WM_MOUSEWHEEL)
            return SendMessageW(self.window_, message, wParam, lParam);
        if (message == WM_SETFOCUS)
            self.ensure(index);
        if (message == WM_SETFOCUS || message == WM_KILLFOCUS)
            InvalidateRect(field, nullptr, FALSE);
        if (message == WM_NCDESTROY)
            RemoveWindowSubclass(field, headerProc, id);
        return DefSubclassProc(field, message, wParam, lParam);
    }

    static LRESULT CALLBACK footerProc(
        HWND field, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data) {
        auto& self = *reinterpret_cast<View*>(data);
        if (message == WM_SETFOCUS)
            self.scrollTo(self.total_);
        if (message == WM_MOUSEWHEEL)
            return SendMessageW(self.window_, message, wParam, lParam);
        if (message == WM_NCDESTROY)
            RemoveWindowSubclass(field, footerProc, id);
        return DefSubclassProc(field, message, wParam, lParam);
    }

    void header(const DRAWITEMSTRUCT& item, size_t index) const {
        const bool selected = delegate_->selected() == index;
        const bool disabled = !enabled_;
        RECT rect = item.rcItem;
        HBRUSH background = CreateSolidBrush(style_.background);
        FillRect(item.hDC, &rect, background);
        DeleteObject(background);
        const bool hot = hotHeader_ == item.hwndItem;
        const COLORREF fill = selected || hot ? style_.field : style_.background;
        box(item.hDC, rect, fill, fill, px(10));
        if (hot && (hotPart_ == HeaderPart::grip || hotPart_ == HeaderPart::menu)) {
            RECT target = hotPart_ == HeaderPart::grip
                              ? RECT{px(4), px(7), px(28), rect.bottom - px(7)}
                              : RECT{rect.right - px(66), px(7), rect.right - px(30), rect.bottom - px(7)};
            box(item.hDC, target, style_.background, style_.border, px(6));
        }
        HBRUSH dot = CreateSolidBrush(hot && hotPart_ == HeaderPart::grip ? style_.accent : style_.muted);
        for (int col = 0; col < 2; ++col)
            for (int row = 0; row < 3; ++row) {
                RECT p{px(12 + col * 5), px(15 + row * 5), px(14 + col * 5), px(17 + row * 5)};
                FillRect(item.hDC, &p, dot);
            }
        DeleteObject(dot);
        RECT number{px(32), px(10), px(60), px(34)};
        box(item.hDC, number, selected ? style_.accent : style_.field,
            selected ? style_.accent : style_.border, px(8));
        SetBkMode(item.hDC, TRANSPARENT);
        const auto old = SelectObject(item.hDC, style_.font);
        SetTextColor(item.hDC, selected ? RGB(255, 255, 255) : style_.muted);
        const auto ordinal = std::to_wstring(index + 1);
        DrawTextW(item.hDC, ordinal.c_str(), -1, &number, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        auto title = delegate_->title(index);
        RECT label{px(72), 0, rect.right - px(76), rect.bottom};
        SetTextColor(item.hDC, disabled ? style_.muted : style_.text);
        DrawTextW(
            item.hDC, title.c_str(), -1, &label, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        RECT more{rect.right - px(66), 0, rect.right - px(30), rect.bottom};
        SetTextColor(item.hDC, style_.muted);
        DrawTextW(item.hDC, L"...", -1, &more, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        const int x = rect.right - px(17), y = rect.bottom / 2;
        HPEN arrow = CreatePen(PS_SOLID, std::max(1, px(2)), style_.muted);
        const auto oldPen = SelectObject(item.hDC, arrow);
        const int direction = delegate_->expanded(index) ? -1 : 1;
        MoveToEx(item.hDC, x - px(4), y - direction * px(2), nullptr);
        LineTo(item.hDC, x, y + direction * px(2));
        LineTo(item.hDC, x + px(4), y - direction * px(2));
        SelectObject(item.hDC, oldPen);
        DeleteObject(arrow);
        SelectObject(item.hDC, old);
        if (item.itemState & ODS_FOCUS) {
            InflateRect(&rect, -px(4), -px(4));
            DrawFocusRect(item.hDC, &rect);
        }
    }

    void paintFooter(const DRAWITEMSTRUCT& item) const {
        const bool pressed = (item.itemState & ODS_SELECTED) != 0;
        HBRUSH background = CreateSolidBrush(style_.background);
        FillRect(item.hDC, &item.rcItem, background);
        DeleteObject(background);
        box(item.hDC, item.rcItem, pressed ? style_.accent : style_.field, style_.border, px(10));
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, pressed    ? RGB(255, 255, 255)
                               : enabled_ ? (style_.dark ? RGB(164, 177, 255) : style_.accent)
                                          : style_.muted);
        const auto old = SelectObject(item.hDC, style_.font);
        RECT rect = item.rcItem;
        DrawTextW(item.hDC, L"+ Add step \u25be", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(item.hDC, old);
        if (item.itemState & ODS_FOCUS) {
            InflateRect(&rect, -px(4), -px(4));
            DrawFocusRect(item.hDC, &rect);
        }
    }

    void paint() const {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(window_, &ps);
        const RECT viewport = client();
        HBRUSH background = CreateSolidBrush(style_.background);
        FillRect(dc, &viewport, background);
        DeleteObject(background);
        for (size_t i = 0; i < items_.size(); ++i) {
            const RECT rect = shown(items_[i].rect);
            if (rect.bottom < 0 || rect.top > viewport.bottom)
                continue;
            box(dc, rect, style_.background, delegate_->selected() == i ? style_.accent : style_.border,
                px(12));
            if (delegate_->expanded(i))
                delegate_->paintBody(i, dc, body(i));
        }
        if (dragging_ && PtInRect(&viewport, pointer_)) {
            const int y =
                (slot_ < items_.size() ? items_[slot_].rect.top - px(5) : items_.back().rect.bottom + px(5)) -
                scroll_;
            const size_t target = slot_ > source_ ? slot_ - 1 : slot_;
            if (target != source_) {
                RECT line{px(8), y, viewport.right - px(8), y + px(3)};
                HBRUSH brush = CreateSolidBrush(style_.accent);
                FillRect(dc, &line, brush);
                DeleteObject(brush);
            }
        }
        EndPaint(window_, &ps);
    }

    void updateSlot() {
        const int y = pointer_.y + scroll_;
        slot_ = items_.size();
        for (size_t i = 0; i < items_.size(); ++i)
            if (y < (items_[i].rect.top + items_[i].rect.bottom) / 2) {
                slot_ = i;
                break;
            }
        InvalidateRect(window_, nullptr, FALSE);
    }

    static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<View*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<View*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            self->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self)
            return DefWindowProcW(window, message, wParam, lParam);
        switch (message) {
        case WM_SIZE:
            self->reflow();
            return 0;
        case Relayout:
            self->queued_ = false;
            self->reflow();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            self->paint();
            return 0;
        case WM_VSCROLL: {
            SCROLLINFO info{sizeof(info), SIF_ALL};
            GetScrollInfo(window, SB_VERT, &info);
            int position = self->scroll_;
            switch (LOWORD(wParam)) {
            case SB_LINEUP:
                position -= self->px(32);
                break;
            case SB_LINEDOWN:
                position += self->px(32);
                break;
            case SB_PAGEUP:
                position -= static_cast<int>(info.nPage);
                break;
            case SB_PAGEDOWN:
                position += static_cast<int>(info.nPage);
                break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                position = info.nTrackPos;
                break;
            case SB_TOP:
                position = 0;
                break;
            case SB_BOTTOM:
                position = self->total_;
                break;
            default:
                return 0;
            }
            self->scrollTo(position);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL)
                return 0;
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            self->wheel_ += GET_WHEEL_DELTA_WPARAM(wParam);
            const int step = lines == WHEEL_PAGESCROLL
                                 ? std::max<int>(0, self->client().bottom - self->px(32))
                                 : self->px(24) * static_cast<int>(std::min<UINT>(lines, 256));
            self->scrollTo(self->scroll_ - (self->wheel_ / WHEEL_DELTA) * step);
            self->wheel_ %= WHEEL_DELTA;
            if (self->dragging_)
                self->updateSlot();
            return 0;
        }
        case WM_LBUTTONDOWN:
            for (size_t i = 0; i < self->items_.size(); ++i) {
                const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                RECT rect = self->shown(self->items_[i].rect);
                if (PtInRect(&rect, point)) {
                    if (self->delegate_->activate(i, false)) {
                        HWND child =
                            ChildWindowFromPointEx(window, point, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED);
                        if (child && child != window && child != self->items_[i].header) {
                            POINT local = point;
                            MapWindowPoints(window, child, &local, 1);
                            SendMessageW(child, WM_LBUTTONDOWN, wParam, MAKELPARAM(local.x, local.y));
                        }
                    }
                    return 0;
                }
            }
            return 0;
        case WM_MOUSEMOVE:
            if (self->pendingDrag_) {
                self->pointer_ = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (!self->dragging_ && (abs(self->pointer_.x - self->start_.x) >=
                                                GetSystemMetricsForDpi(SM_CXDRAG, self->style_.dpi) ||
                                            abs(self->pointer_.y - self->start_.y) >=
                                                GetSystemMetricsForDpi(SM_CYDRAG, self->style_.dpi))) {
                    self->dragging_ = true;
                    self->delegate_->capture(true);
                    SetTimer(window, 1, 30, nullptr);
                }
                if (self->dragging_)
                    self->updateSlot();
                if (self->dragging_) {
                    const RECT viewport = self->client();
                    SetCursor(
                        LoadCursorW(nullptr, PtInRect(&viewport, self->pointer_) ? IDC_SIZEALL : IDC_NO));
                }
                return 0;
            }
            break;
        case WM_TIMER:
            if (wParam == 1 && self->dragging_) {
                const POINT point = self->pointer_;
                if (point.x < 0 || point.x >= self->client().right)
                    return 0;
                const int before = self->scroll_;
                if (point.y < self->px(28))
                    self->scrollTo(self->scroll_ - self->px(16));
                else if (point.y > self->client().bottom - self->px(28))
                    self->scrollTo(self->scroll_ + self->px(16));
                if (before != self->scroll_)
                    self->updateSlot();
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (self->pendingDrag_) {
                self->pointer_ = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                const RECT viewport = self->client();
                const bool drop = self->dragging_ && PtInRect(&viewport, self->pointer_);
                if (drop)
                    self->updateSlot();
                const size_t from = self->source_, slot = self->slot_;
                self->cancelDrag();
                if (drop)
                    self->delegate_->reorder(from, slot > from ? slot - 1 : slot);
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                self->cancelDrag();
                return 0;
            }
            break;
        case WM_CANCELMODE:
        case WM_CAPTURECHANGED:
            self->cancelDrag();
            return 0;
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id >= HeaderBase && static_cast<size_t>(id - HeaderBase) < self->items_.size()) {
                if (HIWORD(wParam) == BN_CLICKED)
                    self->delegate_->activate(id - HeaderBase, true);
                return 0;
            }
            return SendMessageW(self->owner_, message, wParam, lParam);
        }
        case WM_DRAWITEM: {
            const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (item.hwndItem == self->footer_) {
                self->paintFooter(item);
                return TRUE;
            }
            if (item.CtlType == ODT_BUTTON && item.CtlID >= HeaderBase &&
                static_cast<size_t>(item.CtlID - HeaderBase) < self->items_.size()) {
                self->header(item, item.CtlID - HeaderBase);
                return TRUE;
            }
            return SendMessageW(self->owner_, message, wParam, lParam);
        }
        case WM_MEASUREITEM:
        case WM_NOTIFY:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN:
            return SendMessageW(self->owner_, message, wParam, lParam);
        case WM_NCDESTROY:
            self->cancelDrag();
            self->window_ = nullptr;
            self->items_.clear();
            break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

public:
    View() = default;
    View(const View&) = delete;
    View& operator=(const View&) = delete;

    bool create(HWND owner, HINSTANCE instance, Delegate& delegate) {
        items_.clear();
        footer_ = nullptr;
        scroll_ = total_ = wheel_ = 0;
        layingOut_ = queued_ = pendingDrag_ = dragging_ = false;
        enabled_ = true;
        hotHeader_ = nullptr;
        hotPart_ = HeaderPart::none;
        style_ = {};
        owner_ = owner;
        instance_ = instance;
        delegate_ = &delegate;
        WNDCLASSW type{};
        type.hInstance = instance;
        type.lpfnWndProc = proc;
        type.lpszClassName = L"MK-AutoTyper-StepFlow";
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
        return CreateWindowExW(WS_EX_CONTROLPARENT, type.lpszClassName, L"Snippet steps",
                   WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0, owner,
                   nullptr, instance, this) != nullptr;
    }

    HWND handle() const {
        return window_;
    }

    HWND header(size_t index) const {
        return index < items_.size() ? items_[index].header : nullptr;
    }

    RECT bounds(size_t index) const {
        return index < items_.size() ? shown(items_[index].rect) : RECT{};
    }

    int position() const {
        return scroll_;
    }

    bool dragging() const {
        return pendingDrag_;
    }

    void footer(HWND field) {
        footer_ = field;
        SetWindowSubclass(field, footerProc, 2, reinterpret_cast<DWORD_PTR>(this));
    }

    void theme(const dialogs::Style& style) {
        style_ = style;
        reflow();
    }

    void enabled(bool value) {
        enabled_ = value;
        if (!value)
            cancelDrag();
        for (const auto& item : items_)
            if (item.header)
                EnableWindow(item.header, value);
        EnableWindow(footer_, value);
    }

    void requestLayout() {
        if (!queued_ && window_) {
            queued_ = true;
            PostMessageW(window_, Relayout, 0, 0);
        }
    }

    void reset() {
        cancelDrag();
        for (auto& item : items_)
            destroyHeader(item);
        items_.clear();
        scroll_ = 0;
        wheel_ = 0;
    }

    void cancelDrag() {
        const bool captured = dragging_;
        pendingDrag_ = dragging_ = false;
        if (window_) {
            KillTimer(window_, 1);
            if (GetCapture() == window_)
                ReleaseCapture();
            InvalidateRect(window_, nullptr, FALSE);
        }
        if (captured && delegate_)
            delegate_->capture(false);
        if (captured && GetForegroundWindow() == owner_)
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    }

    void scrollTo(int position) {
        scroll_ = std::max(0, std::min(position, std::max<int>(0, total_ - client().bottom)));
        reflow(false);
    }

    void ensure(size_t index) {
        if (index >= items_.size())
            return;
        const auto& rect = items_[index].rect;
        const int visibleHeight =
            std::min<int>(rect.bottom - rect.top, std::max<int>(0, client().bottom - px(8)));
        if (rect.top < scroll_)
            scrollTo(rect.top - px(8));
        else if (rect.top + visibleHeight > scroll_ + client().bottom)
            scrollTo(rect.top - px(8));
    }

    void focusHeader(size_t index) {
        ensure(index);
        if (header(index))
            SetFocus(header(index));
    }

    void reflow(bool preserveAnchor = true) {
        if (!window_ || !delegate_ || layingOut_ || !footer_ || !style_.dpi)
            return;
        layingOut_ = true;
        const size_t count = delegate_->count();
        size_t anchor = count;
        int offset = 0;
        if (preserveAnchor && !pendingDrag_ && items_.size() == count) {
            for (size_t i = 0; i < count; ++i)
                if (items_[i].rect.bottom > scroll_) {
                    anchor = i;
                    offset = scroll_ - items_[i].rect.top;
                    break;
                }
        }
        while (items_.size() > count) {
            destroyHeader(items_.back());
            items_.pop_back();
        }
        items_.resize(count);
        for (int pass = 0; pass < 2; ++pass) {
            const RECT viewport = client();
            int top = px(8);
            for (size_t i = 0; i < count; ++i) {
                const int body =
                    delegate_->expanded(i)
                        ? delegate_->bodyHeight(i, std::max<int>(px(80), viewport.right - px(44))) + px(16)
                        : 0;
                items_[i].rect = {
                    px(8), top, std::max<int>(px(100), viewport.right - px(8)), top + px(44) + body};
                top = items_[i].rect.bottom + px(10);
            }
            total_ = top + px(48);
            if (anchor < count)
                scroll_ = items_[anchor].rect.top +
                          std::min<int>(offset, items_[anchor].rect.bottom - items_[anchor].rect.top - 1);
            scroll_ = std::max(0, std::min(scroll_, std::max<int>(0, total_ - viewport.bottom)));
            SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
            info.nMin = 0;
            info.nMax = std::max(0, total_ - 1);
            info.nPage = viewport.bottom;
            info.nPos = scroll_;
            SetScrollInfo(window_, SB_VERT, &info, TRUE);
            if (client().right == viewport.right)
                break;
        }
        const RECT viewport = client();
        controls::Layout batch(window_, style_.dpi);
        for (size_t i = 0; i < count; ++i) {
            const RECT rect = shown(items_[i].rect);
            const bool visible = rect.bottom >= 0 && rect.top <= viewport.bottom;
            const bool focused = items_[i].header && GetFocus() == items_[i].header;
            if (!visible && i != delegate_->selected() && !focused) {
                destroyHeader(items_[i]);
                continue;
            }
            if (!items_[i].header) {
                items_[i].header = CreateWindowExW(0, L"BUTTON", L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | WS_CLIPSIBLINGS, 0, 0, 0, 0, window_,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(HeaderBase + i)), instance_, nullptr);
                SetWindowSubclass(items_[i].header, headerProc, 1, reinterpret_cast<DWORD_PTR>(this));
            }
            auto title = delegate_->title(i);
            secure::Text accessible(512);
            snippetdocument::json::Writer writer(accessible.writable(), 512);
            writer.literal(L"Step ");
            writer.number(static_cast<unsigned>(i + 1));
            writer.literal(L". ");
            writer.literal(title.c_str());
            writer.literal(delegate_->expanded(i) ? L", expanded" : L", collapsed");
            accessible.length(writer.size());
            SetWindowTextW(items_[i].header, accessible.c_str());
            SendMessageW(items_[i].header, WM_SETFONT, reinterpret_cast<WPARAM>(style_.font), FALSE);
            EnableWindow(items_[i].header, enabled_);
            batch.movePixels(
                items_[i].header, rect.left + 1, rect.top + 1, rect.right - rect.left - 2, px(44) - 2);
        }
        if (count)
            delegate_->placeBody(batch, body(delegate_->selected()));
        batch.movePixels(footer_, px(8), total_ - px(48) - scroll_,
            std::max<int>(px(80), viewport.right - px(16)), px(36));
        batch.apply();
        for (size_t i = 0; i < count; ++i)
            if (items_[i].header)
                updateHeaderRegion(items_[i], delegate_->expanded(i));
        HWND previous = HWND_TOP;
        for (size_t i = 0; i < count; ++i)
            if (items_[i].header) {
                SetWindowPos(items_[i].header, previous, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
                previous = items_[i].header;
                if (i == delegate_->selected() && delegate_->expanded(i))
                    delegate_->tabOrder(previous);
            }
        SetWindowPos(footer_, previous, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
        layingOut_ = false;
        delegate_->afterLayout();
    }
};
} // namespace stepflow
