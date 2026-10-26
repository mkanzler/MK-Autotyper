#pragma once
#include "KeySelector.h"
#include "SnippetVariables.h"
#include "CommandMenu.h"
#include "StepFlow.h"
#include <richedit.h>

namespace actioneditor {
enum Control {
    AddText = 801,
    AddKey,
    AddPause,
    Remove,
    Up,
    Down,
    Variables,
    PauseValue,
    PauseLabel,
    VariableHint,
    AddStep
};

constexpr dialogs::MenuCommand AddCommands[] = {{AddText, L"Text", L"Write or paste text"},
    {AddKey, L"Key / shortcut", L"Press a key or a combination"},
    {AddPause, L"Pause", L"Wait before the next step"}};
constexpr dialogs::MenuCommand StepCommands[] = {{Up, L"Move up", L"Alt + Up on the step header"},
    {Down, L"Move down", L"Alt + Down on the step header"},
    {Remove, L"Remove step", L"Remove this step from the sequence"}};

// One native Rich Edit and key picker serve the selected card. Other text cards
// paint their document text, keeping long sequences light on native controls.
class Editor : public stepflow::Delegate {
    struct State {
        bool expanded = false;
        int textWidth = 0, measuredTextHeight = 0, requestedTextHeight = 0;
    };

    HWND owner_ = nullptr, text_ = nullptr, pause_ = nullptr, pauseLabel_ = nullptr, variables_ = nullptr,
         variableHint_ = nullptr, add_ = nullptr;
    HFONT textFont_ = nullptr;
    snippetdocument::Document* document_ = nullptr;
    std::vector<State> states_;
    std::function<void(bool)> changed_, captureChanged_;
    std::function<void(const wchar_t*)> error_;
    keyselector::Picker keys_;
    stepflow::View view_;
    dialogs::Style style_{};
    RECT formatting_{};
    size_t selected_ = 0;
    bool loading_ = false, ready_ = false, enabled_ = true, ensuringCaret_ = false, followCaret_ = false;

    int px(int value) const {
        return MulDiv(value, style_.dpi ? style_.dpi : 96, 96);
    }

    int textLimit() const {
        RECT viewport{};
        GetClientRect(view_.handle(), &viewport);
        return std::max(px(96), std::min<int>(px(480), viewport.bottom - px(80)));
    }

    bool editingText() const {
        return ready_ && textSelected() && expanded(selected_);
    }

    void changed(bool structure) {
        if (!loading_ && changed_)
            changed_(structure);
    }

    void bind(bool preserveText = false) {
        loading_ = true;
        const auto& action = document_->actions[selected_];
        if (!preserveText) {
            SetWindowTextW(text_, editingText() ? action.text.c_str() : L"");
            SendMessageW(text_, EM_EMPTYUNDOBUFFER, 0, 0);
            SendMessageW(text_, EM_SETMODIFY, FALSE, 0);
        }
        if (action.type == snippetdocument::ActionType::key && expanded(selected_))
            keys_.set(action.key);
        else
            keys_.clear();
        SetWindowTextW(pause_, std::to_wstring(action.milliseconds).c_str());
        SendMessageW(pause_, EM_EMPTYUNDOBUFFER, 0, 0);
        loading_ = false;
    }

    void order(HWND field, HWND& previous) {
        SetWindowPos(field, previous, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
        previous = field;
    }

public:
    Editor() = default;
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    bool create(HWND owner, HINSTANCE instance, HWND text, snippetdocument::Document& document,
        const std::function<void(HWND)>& decorate, std::function<void(bool)> changed,
        std::function<void(const wchar_t*)> error, std::function<void(bool)> captureChanged) {
        owner_ = owner;
        text_ = text;
        document_ = &document;
        changed_ = std::move(changed);
        error_ = std::move(error);
        captureChanged_ = std::move(captureChanged);
        ready_ = false;
        enabled_ = true;
        followCaret_ = false;
        states_.clear();
        if (!view_.create(owner, instance, *this))
            return false;
        SetParent(text_, view_.handle());
        auto add = [&](HWND parent, const wchar_t* type, const wchar_t* label, DWORD flags, int id) {
            HWND field = CreateWindowExW(0, type, label, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | flags, 0,
                0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
            if (!wcscmp(type, L"BUTTON"))
                decorate(field);
            return field;
        };
        add_ = add(view_.handle(), L"BUTTON", L"+ Add step \u25be", WS_TABSTOP | BS_OWNERDRAW, AddStep);
        view_.footer(add_);
        variables_ = add(owner, L"BUTTON", L"Use variables", WS_TABSTOP | BS_AUTOCHECKBOX, Variables);
        variableHint_ =
            add(owner, L"STATIC", L"{name}   {date}   {time}", SS_NOPREFIX | SS_ENDELLIPSIS, VariableHint);
        pauseLabel_ =
            add(view_.handle(), L"STATIC", L"Pause in milliseconds (1000 = one second)", SS_LEFT, PauseLabel);
        pause_ = add(view_.handle(), L"EDIT", L"1000", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, PauseValue);
        SendMessageW(pause_, EM_SETLIMITTEXT, 10, 0);
        SetWindowSubclass(pause_, dialogs::nameEditProc, 1, 0);
        if (!keys_.create(
                view_.handle(), instance, decorate,
                [this] {
                    if (!ready_ || loading_)
                        return;
                    document_->actions[selected_].key = keys_.value();
                    this->changed(true);
                    view_.requestLayout();
                },
                [this](bool active) { capture(active); }))
            return false;
        SendMessageW(text_, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_REQUESTRESIZE | ENM_SELCHANGE);
        ready_ = true;
        set(snippetdocument::Document{});
        return add_ && variables_ && variableHint_ && pause_ && pauseLabel_;
    }

    HWND control(int id) const {
        return GetDlgItem(view_.handle(), id);
    }

    HWND viewport() const {
        return view_.handle();
    }

    HWND header(size_t index) const {
        return view_.header(index);
    }

    RECT card(size_t index) const {
        return view_.bounds(index);
    }

    int scrollPosition() const {
        return view_.position();
    }

    bool dragging() const {
        return view_.dragging();
    }

    void scrollTo(int position) {
        view_.scrollTo(position);
    }

    size_t count() const override {
        return ready_ ? document_->actions.size() : 0;
    }

    size_t selected() const override {
        return selected_;
    }

    bool expanded(size_t index) const override {
        return index < states_.size() && states_[index].expanded;
    }

    bool recording() const {
        return keys_.recording();
    }

    keyselector::Picker& keyPicker() {
        return keys_;
    }

    bool loading() const {
        return loading_;
    }

    bool textSelected() const {
        return ready_ && selected_ < document_->actions.size() &&
               document_->actions[selected_].type == snippetdocument::ActionType::text;
    }

    bool field(HWND field) const {
        return field == pause_ || keys_.field(field);
    }

    void capture(bool active) override {
        if (captureChanged_)
            captureChanged_(active);
    }

    void cancelInteraction() {
        view_.cancelDrag();
        keys_.stopRecording();
    }

    void fonts(HFONT body, HFONT smallFont, HFONT textFont, UINT dpi) {
        style_.font = body;
        style_.dpi = dpi;
        textFont_ = textFont;
        for (HWND field : {variables_, pause_, add_})
            SendMessageW(field, WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
        for (HWND field : {pauseLabel_, variableHint_})
            SendMessageW(field, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont), TRUE);
        keys_.fonts(body, smallFont, dpi);
        for (auto& state : states_) {
            state.textWidth = state.measuredTextHeight = state.requestedTextHeight = 0;
        }
        view_.theme(style_);
    }

    void theme(const dialogs::Style& style) {
        style_ = style;
        view_.theme(style);
    }

    void layout(controls::Layout& batch, int x, int width, int height, UINT dpi) {
        style_.dpi = dpi;
        batch.move(view_.handle(), x, 219, width, std::max(140, height - 373));
        batch.move(variables_, x, height - 138, 164, 30);
        batch.move(variableHint_, x + 176, height - 133, width - 176, 24,
            document_->expandVariables ? controls::Layout::Visibility::show
                                       : controls::Layout::Visibility::hide);
    }

    void refresh() {
        view_.reflow();
    }

    void focus() {
        if (!ready_)
            return;
        if (!expanded(selected_)) {
            view_.focusHeader(selected_);
            return;
        }
        const auto type = document_->actions[selected_].type;
        SetFocus(type == snippetdocument::ActionType::text  ? text_
                 : type == snippetdocument::ActionType::key ? control(keyselector::Capture)
                                                            : pause_);
    }

    bool hasContent() const {
        if (!ready_)
            return GetWindowTextLengthW(text_) > 0;
        for (size_t i = 0; i < count(); ++i) {
            const auto& action = document_->actions[i];
            if (action.type != snippetdocument::ActionType::text)
                return true;
            if (i == selected_ && editingText() ? GetWindowTextLengthW(text_) > 0 : !action.text.empty())
                return true;
        }
        return false;
    }

    size_t characterCount() const {
        size_t result = 0;
        for (size_t i = 0; i < count(); ++i)
            if (document_->actions[i].type == snippetdocument::ActionType::text)
                result += i == selected_ && editingText() ? static_cast<size_t>(GetWindowTextLengthW(text_))
                                                          : document_->actions[i].text.size();
        return result;
    }

    bool commit() {
        if (!ready_ || loading_ || !expanded(selected_))
            return true;
        auto& action = document_->actions[selected_];
        if (action.type == snippetdocument::ActionType::text) {
            // The native control limits one field; the document limit covers
            // every text card, including the current uncommitted contents.
            if (characterCount() > snippetdocument::MaximumTextCharacters) {
                if (error_)
                    error_(L"A snippet can contain at most 1,048,576 text characters across all steps.");
                return false;
            }
            auto text = secure::Text::read(text_);
            if (!snippetdocument::validText(text)) {
                if (error_)
                    error_(L"This text contains invalid Unicode.");
                return false;
            }
            action.text = std::move(text);
            if (SendMessageW(text_, EM_GETMODIFY, 0, 0))
                changed(true);
        } else if (action.type == snippetdocument::ActionType::key)
            action.key = keys_.value();
        else if (!number(pause_, 0, snippetdocument::MaximumDelayMilliseconds, action.milliseconds)) {
            if (error_)
                error_(L"Pause must be between 0 and 86,400,000 milliseconds.");
            return false;
        }
        return true;
    }

    bool read(snippetdocument::Document& output) {
        output.clear();
        keys_.stopRecording();
        if (!commit())
            return false;
        if (snippetdocument::validate(*document_) != snippetdocument::Result::ok) {
            if (error_)
                error_(L"The sequence is too large or has invalid steps.");
            return false;
        }
        output = snippetdocument::clone(*document_);
        return true;
    }

    void set(snippetdocument::Document document) {
        cancelInteraction();
        loading_ = true;
        view_.reset();
        if (document.actions.empty())
            document.actions.push_back(snippetdocument::Action::typeText(secure::Text{}));
        *document_ = std::move(document);
        selected_ = 0;
        states_.assign(count(), State{});
        for (size_t i = 0; i < count(); ++i)
            states_[i].expanded = document_->actions[i].type == snippetdocument::ActionType::text;
        bind();
        SendMessageW(variables_, BM_SETCHECK, document_->expandVariables ? BST_CHECKED : BST_UNCHECKED, 0);
        ShowWindow(variableHint_, document_->expandVariables ? SW_SHOW : SW_HIDE);
        loading_ = false;
        view_.reflow();
    }

    void clear() {
        keys_.clear();
        set(snippetdocument::Document{});
    }

    void enabled(bool enable) {
        if (enabled_ != enable) {
            enabled_ = enable;
            view_.enabled(enable);
            keys_.enabled(enable);
            EnableWindow(text_, enable);
            EnableWindow(pause_, enable);
            EnableWindow(variables_, enable);
        }
        EnableWindow(add_, enable && count() < snippetdocument::MaximumActions);
    }

    bool activate(size_t index, bool toggle) override {
        view_.cancelDrag();
        if (!enabled_ || index >= count())
            return false;
        keys_.stopRecording();
        // Reusing the current card must not validate unfinished input. Its menu
        // still needs to offer Remove when, for example, a pause is invalid.
        if (selected_ == index && !toggle)
            return true;
        if (!commit())
            return false;
        const bool preserveText = selected_ == index && textSelected();
        if (selected_ != index) {
            if (!textSelected())
                states_[selected_].expanded = false;
            selected_ = index;
            if (!toggle && textSelected())
                states_[index].expanded = true;
        }
        if (toggle)
            states_[index].expanded = !states_[index].expanded;
        bind(preserveText);
        view_.reflow();
        if (toggle)
            view_.ensure(index);
        focus();
        return true;
    }

    bool reorder(size_t from, size_t to) override {
        view_.cancelDrag();
        if (!enabled_ || from >= count() || to >= count() || from == to)
            return false;
        keys_.stopRecording();
        if (!commit())
            return false;
        auto& actions = document_->actions;
        if (from < to) {
            std::rotate(actions.begin() + from, actions.begin() + from + 1, actions.begin() + to + 1);
            std::rotate(states_.begin() + from, states_.begin() + from + 1, states_.begin() + to + 1);
        } else {
            std::rotate(actions.begin() + to, actions.begin() + from, actions.begin() + from + 1);
            std::rotate(states_.begin() + to, states_.begin() + from, states_.begin() + from + 1);
        }
        if (selected_ == from)
            selected_ = to;
        else if (from < selected_ && selected_ <= to)
            --selected_;
        else if (to <= selected_ && selected_ < from)
            ++selected_;
        changed(true);
        view_.reflow();
        view_.focusHeader(to);
        return true;
    }

    void menu(size_t index, HWND anchor) override {
        if (!enabled_ || !activate(index, false))
            return;
        dialogs::MenuCommand choices[] = {StepCommands[0], StepCommands[1], StepCommands[2]};
        choices[0].enabled = index > 0;
        choices[1].enabled = index + 1 < count();
        choices[2].enabled = count() > 1 || hasContent();
        const UINT choice = dialogs::commandMenu(owner_, anchor, style_, choices);
        command(static_cast<int>(choice), BN_CLICKED);
    }

    void addMenu(const dialogs::Style& style, decltype(&TrackPopupMenuEx) track = &TrackPopupMenuEx) {
        keys_.stopRecording();
        const UINT choice = dialogs::commandMenu(owner_, add_, style, AddCommands, track);
        if (choice)
            command(choice, BN_CLICKED);
    }

    bool command(int id, int notification) {
        if (!ready_ || loading_)
            return false;
        if (keys_.command(id, notification))
            return true;
        if (id == GetDlgCtrlID(text_) && notification == EN_CHANGE) {
            if (editingText()) {
                states_[selected_].measuredTextHeight = states_[selected_].requestedTextHeight = 0;
                followCaret_ = true;
                SendMessageW(text_, EM_REQUESTRESIZE, 0, 0);
                changed(false);
                view_.requestLayout();
            }
            return true;
        }
        if (id == Variables && notification == BN_CLICKED) {
            document_->expandVariables = SendMessageW(variables_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            ShowWindow(variableHint_, document_->expandVariables ? SW_SHOW : SW_HIDE);
            changed(true);
            return true;
        }
        if (id == PauseValue && notification == EN_CHANGE) {
            DWORD value = 0;
            const bool valid = number(pause_, 0, snippetdocument::MaximumDelayMilliseconds, value);
            if (error_)
                error_(valid ? L"" : L"Pause must be between 0 and 86,400,000 milliseconds.");
            changed(true);
            view_.requestLayout();
            return true;
        }
        if (id == PauseValue && (notification == EN_SETFOCUS || notification == EN_KILLFOCUS)) {
            InvalidateRect(view_.handle(), nullptr, FALSE);
            return true;
        }
        if (id >= AddText && id <= Down && notification == BN_CLICKED) {
            view_.cancelDrag();
            if (!enabled_)
                return true;
            keys_.stopRecording();
            if (id == Up || id == Down) {
                reorder(selected_, id == Up ? (selected_ ? selected_ - 1 : selected_)
                                            : std::min(selected_ + 1, count() - 1));
                return true;
            }
            if (id != Remove && !commit())
                return true;
            if (id <= AddPause) {
                if (count() >= snippetdocument::MaximumActions) {
                    if (error_)
                        error_(L"A snippet can contain at most 4096 steps.");
                    return true;
                }
                if (!textSelected())
                    states_[selected_].expanded = false;
                auto action = id == AddText ? snippetdocument::Action::typeText(secure::Text{})
                              : id == AddKey
                                  ? snippetdocument::Action::pressKey(
                                        {snippetdocument::KeyEncoding::virtualKey, VK_TAB, 0, false})
                                  : snippetdocument::Action::delay(1000);
                document_->actions.push_back(std::move(action));
                states_.push_back(State{});
                selected_ = count() - 1;
                states_[selected_].expanded = id == AddText;
            } else if (id == Remove) {
                document_->actions.erase(document_->actions.begin() + selected_);
                states_.erase(states_.begin() + selected_);
                if (!count()) {
                    document_->actions.push_back(snippetdocument::Action::typeText(secure::Text{}));
                    states_.push_back(State{});
                    states_[0].expanded = true;
                }
                selected_ = std::min(selected_, count() - 1);
            }
            bind();
            changed(true);
            view_.reflow();
            view_.ensure(selected_);
            focus();
            return true;
        }
        return false;
    }

    bool notify(const NMHDR& header) {
        if (header.hwndFrom != text_ || !editingText() || loading_)
            return false;
        if (header.code == EN_SELCHANGE && GetFocus() == text_) {
            followCaret_ = true;
            view_.requestLayout();
            return true;
        }
        if (header.code != EN_REQUESTRESIZE)
            return false;
        const auto& request = *reinterpret_cast<const REQRESIZE*>(&header);
        const int height = std::max<int>(px(96), request.rc.bottom - request.rc.top + px(8));
        if (states_[selected_].requestedTextHeight != height) {
            states_[selected_].requestedTextHeight = height;
            view_.requestLayout();
        }
        return true;
    }

    int bodyHeight(size_t index, int width) override {
        const auto& action = document_->actions[index];
        if (action.type == snippetdocument::ActionType::key)
            return px(280);
        if (action.type == snippetdocument::ActionType::delay)
            return px(96);
        auto& state = states_[index];
        if (state.textWidth != width) {
            state.textWidth = width;
            state.measuredTextHeight = state.requestedTextHeight = 0;
        }
        if (state.requestedTextHeight)
            return std::min(textLimit(), state.requestedTextHeight);
        if (!state.measuredTextHeight) {
            HDC dc = GetDC(view_.handle());
            const auto old = SelectObject(dc, textFont_ ? textFont_ : style_.font);
            RECT rect{0, 0, std::max(1, width - px(6)), 0};
            if (index == selected_ && editingText()) {
                TEXTMETRICW metrics{};
                GetTextMetricsW(dc, &metrics);
                rect.bottom =
                    static_cast<LONG>(SendMessageW(text_, EM_GETLINECOUNT, 0, 0)) * metrics.tmHeight;
            } else if (!action.text.empty())
                DrawTextW(dc, action.text.data(), static_cast<int>(action.text.size()), &rect,
                    DT_CALCRECT | DT_WORDBREAK | DT_EXPANDTABS | DT_EDITCONTROL | DT_NOPREFIX);
            SelectObject(dc, old);
            ReleaseDC(view_.handle(), dc);
            state.measuredTextHeight = std::max<int>(px(96), rect.bottom + px(12));
        }
        return std::min(textLimit(), state.measuredTextHeight);
    }

    secure::Text title(size_t index) const override {
        const auto& action = document_->actions[index];
        secure::Text label(384);
        snippetdocument::json::Writer writer(label.writable(), 384);
        writer.literal(kind(action));
        writer.literal(L"  -  ");
        if (action.type == snippetdocument::ActionType::key) {
            auto key = keyName(action.key);
            writer.literal(key.c_str());
        } else if (action.type == snippetdocument::ActionType::delay) {
            DWORD value = action.milliseconds;
            if (index == selected_ && expanded(index))
                number(pause_, 0, snippetdocument::MaximumDelayMilliseconds, value);
            writer.number(value);
            writer.literal(L" ms");
        } else {
            const size_t length =
                index == selected_ && editingText() ? GetWindowTextLengthW(text_) : action.text.size();
            writer.number(static_cast<unsigned>(length));
            writer.literal(length == 1 ? L" character" : L" characters");
        }
        label.length(writer.size());
        return label;
    }

    void paintBody(size_t index, HDC dc, RECT rect) const override {
        if (index == selected_) {
            for (HWND field : {pause_, control(keyselector::Capture), control(keyselector::Search)}) {
                if (!(GetWindowLongPtrW(field, GWL_STYLE) & WS_VISIBLE))
                    continue;
                RECT bounds{};
                GetWindowRect(field, &bounds);
                MapWindowPoints(nullptr, view_.handle(), reinterpret_cast<POINT*>(&bounds), 2);
                InflateRect(&bounds, px(10), px(5));
                HBRUSH brush = CreateSolidBrush(style_.field);
                HPEN pen = CreatePen(PS_SOLID, 1, GetFocus() == field ? style_.accent : style_.border);
                const auto oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
                RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom, px(8), px(8));
                SelectObject(dc, oldBrush);
                SelectObject(dc, oldPen);
                DeleteObject(brush);
                DeleteObject(pen);
            }
            return;
        }
        const auto& action = document_->actions[index];
        if (action.type != snippetdocument::ActionType::text)
            return;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, action.text.empty() ? style_.muted : style_.text);
        const auto old = SelectObject(dc, textFont_ ? textFont_ : style_.font);
        DrawTextW(dc, action.text.empty() ? L"Write or paste text..." : action.text.data(),
            action.text.empty() ? -1 : static_cast<int>(action.text.size()), &rect,
            DT_WORDBREAK | DT_EXPANDTABS | DT_EDITCONTROL | DT_NOPREFIX);
        SelectObject(dc, old);
        if (std::max(states_[index].measuredTextHeight, states_[index].requestedTextHeight) > textLimit()) {
            RECT hint = rect;
            hint.top = std::max<LONG>(hint.top, hint.bottom - px(22));
            HBRUSH background = CreateSolidBrush(style_.background);
            FillRect(dc, &hint, background);
            DeleteObject(background);
            const auto font = SelectObject(dc, style_.font);
            SetTextColor(dc, style_.muted);
            DrawTextW(dc, L"Click to edit longer text", -1, &hint,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dc, font);
        }
    }

    void placeBody(controls::Layout& batch, RECT rect) override {
        const auto type = document_->actions[selected_].type;
        const bool open = expanded(selected_);
        ShowWindow(text_, open && type == snippetdocument::ActionType::text ? SW_SHOW : SW_HIDE);
        keys_.visible(open && type == snippetdocument::ActionType::key);
        ShowWindow(pause_, open && type == snippetdocument::ActionType::delay ? SW_SHOW : SW_HIDE);
        ShowWindow(pauseLabel_, open && type == snippetdocument::ActionType::delay ? SW_SHOW : SW_HIDE);
        if (!open)
            return;
        if (type == snippetdocument::ActionType::text) {
            batch.movePixels(text_, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
            ShowScrollBar(text_, SB_VERT,
                std::max(states_[selected_].requestedTextHeight, states_[selected_].measuredTextHeight) >=
                    textLimit());
        } else if (type == snippetdocument::ActionType::key) {
            const int x = MulDiv(rect.left, 96, style_.dpi), y = MulDiv(rect.top, 96, style_.dpi),
                      width = MulDiv(rect.right - rect.left, 96, style_.dpi);
            keys_.layout(batch, x, y, width, y + 280);
        } else {
            batch.movePixels(pauseLabel_, rect.left, rect.top, rect.right - rect.left, px(28));
            batch.movePixels(pause_, rect.left + px(10), rect.top + px(42),
                std::min<int>(rect.right - rect.left - px(20), px(280)), px(28));
        }
    }

    void tabOrder(HWND& previous) override {
        const auto type = document_->actions[selected_].type;
        if (type == snippetdocument::ActionType::text)
            order(text_, previous);
        else if (type == snippetdocument::ActionType::delay)
            order(pause_, previous);
        else
            for (HWND field : keys_.readingOrder())
                order(field, previous);
    }

    bool wheel(WPARAM wParam, LPARAM lParam) {
        if (!editingText() || (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL))
            return false;
        if (std::max(states_[selected_].requestedTextHeight, states_[selected_].measuredTextHeight) >=
            textLimit()) {
            SCROLLINFO info{sizeof(info), SIF_ALL};
            GetScrollInfo(text_, SB_VERT, &info);
            const bool up = GET_WHEEL_DELTA_WPARAM(wParam) > 0;
            if ((up && info.nPos > info.nMin) ||
                (!up && info.nPos < info.nMax - static_cast<int>(info.nPage) + 1))
                return false;
        }
        SendMessageW(view_.handle(), WM_MOUSEWHEEL, wParam, lParam);
        return true;
    }

    void afterLayout() override {
        if (editingText()) {
            RECT rect{};
            GetClientRect(text_, &rect);
            InflateRect(&rect, -px(2), -px(2));
            if (!EqualRect(&rect, &formatting_)) {
                formatting_ = rect;
                SendMessageW(text_, EM_SETRECTNP, 0, reinterpret_cast<LPARAM>(&formatting_));
            }
        }
        if (ensuringCaret_ || !followCaret_ || !editingText() || GetFocus() != text_)
            return;
        followCaret_ = false;
        CHARRANGE selection{};
        SendMessageW(text_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
        POINT caret{};
        if (!GetCaretPos(&caret))
            SendMessageW(text_, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&caret), selection.cpMax);
        RECT rect{}, viewport{};
        GetWindowRect(text_, &rect);
        MapWindowPoints(nullptr, view_.handle(), reinterpret_cast<POINT*>(&rect), 2);
        GetClientRect(view_.handle(), &viewport);
        const int y = rect.top + caret.y;
        int delta = 0;
        if (y < px(12))
            delta = y - px(12);
        else if (y + px(26) > viewport.bottom)
            delta = y + px(26) - viewport.bottom;
        if (delta) {
            ensuringCaret_ = true;
            view_.scrollTo(view_.position() + delta);
            ensuringCaret_ = false;
        }
    }

    bool measure(const dialogs::Style& style, MEASUREITEMSTRUCT& item) const {
        return dialogs::measureCommand(style, item, AddCommands) ||
               dialogs::measureCommand(style, item, StepCommands);
    }

    bool draw(const dialogs::Style& style, const DRAWITEMSTRUCT& item) const {
        return keys_.draw(style, item) || dialogs::drawCommand(style, item, AddCommands) ||
               dialogs::drawCommand(style, item, StepCommands);
    }
};
} // namespace actioneditor
