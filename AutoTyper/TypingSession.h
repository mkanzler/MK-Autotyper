#pragma once
#include "KeyboardInput.h"
#include <mutex>
#include <thread>
#include <utility>

namespace typing_session {
struct Snapshot {
    keyboard::Progress progress;
    keyboard::Result result = keyboard::Result::cancelled;
    bool finished = false;
};

class Session {
    std::thread worker_;
    std::atomic_bool stop_{false};
    mutable std::mutex mutex_;
    Snapshot snapshot_;

public:
    using Runner = keyboard::Result (*)(
        secure::Text&, const keyboard::Options&, const std::atomic_bool&, const keyboard::Reporter&);
    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    ~Session() {
        requestStop();
        join();
    }

    bool start(secure::Text text, keyboard::Options options, Runner run = &keyboard::runSecure) {
        if (worker_.joinable())
            return false;
        stop_ = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_ = Snapshot{};
            snapshot_.progress.remaining = options.initialDelay;
        }
        try {
            worker_ = std::thread([this, text = std::move(text), options, run]() mutable {
                keyboard::Result result = keyboard::Result::inputBlocked;
                try {
                    result = run(text, options, stop_, [this](const keyboard::Progress& progress) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        snapshot_.progress = progress;
                    });
                } catch (...) {
                }
                text.clear(); // Release plaintext before advertising completion.
                std::lock_guard<std::mutex> lock(mutex_);
                snapshot_.result = result;
                snapshot_.finished = true;
            });
        } catch (...) {
            return false;
        }
        return true;
    }

    bool start(std::wstring text, keyboard::Options options, Runner run = &keyboard::runSecure) {
        secure::StringGuard wipe{text};
        return start(secure::Text(text), options, run);
    }

    void requestStop() {
        stop_ = true;
    }

    void join() {
        if (worker_.joinable())
            worker_.join();
    }

    bool stopping() const {
        return stop_.load();
    }

    Snapshot snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshot_;
    }
};
} // namespace typing_session
