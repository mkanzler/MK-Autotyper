#pragma once
#include <Windows.h>
#include <vector>

namespace controls {
// Move siblings together without copying pixels from their previous positions.
// The parent paints its cards; every child must also repaint after the batch.
class Layout {
    struct Position {
        HWND control;
        int x, y, width, height;
        UINT flags;
    };

    HWND owner_;
    UINT dpi_;
    std::vector<Position> positions_;

public:
    enum class Visibility {
        keep,
        show,
        hide
    };

    Layout(HWND owner, UINT dpi) : owner_(owner), dpi_(dpi) {}

    Layout(const Layout&) = delete;
    Layout& operator=(const Layout&) = delete;

    ~Layout() {
        apply();
    }

    void move(HWND control, int x, int y, int width, int height, Visibility visibility = Visibility::keep) {
        movePixels(control, MulDiv(x, dpi_, 96), MulDiv(y, dpi_, 96), MulDiv(width, dpi_, 96),
            MulDiv(height, dpi_, 96), visibility);
    }

    void movePixels(
        HWND control, int x, int y, int width, int height, Visibility visibility = Visibility::keep) {
        if (!control)
            return;
        const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW |
                           (visibility == Visibility::show      ? SWP_SHOWWINDOW
                               : visibility == Visibility::hide ? SWP_HIDEWINDOW
                                                                : 0);
        positions_.push_back({control, x, y, width, height, flags});
    }

    void apply() {
        if (positions_.empty())
            return;
        HDWP batch = BeginDeferWindowPos(static_cast<int>(positions_.size()));
        for (const auto& position : positions_) {
            if (!batch)
                break;
            batch = DeferWindowPos(batch, position.control, nullptr, position.x, position.y, position.width,
                position.height, position.flags);
        }
        if (!batch || !EndDeferWindowPos(batch))
            for (const auto& position : positions_)
                SetWindowPos(position.control, nullptr, position.x, position.y, position.width,
                    position.height, position.flags);
        positions_.clear();
        RedrawWindow(owner_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    }
};
} // namespace controls
