#pragma once
#include "SecureMemory.h"
#include <wincrypt.h>
#include <dpapi.h>
#include <array>
#pragma comment(lib, "Crypt32.lib")

namespace encrypted {
// UI-thread owned. Only an authenticated vault key may enter this session.
// The retained allocation contains protected bytes; plaintext keys are temporary.
class VaultSession {
    secure::Buffer protectedKey_;
    std::array<BYTE, 16> salt_{};

public:
    VaultSession() = default;
    VaultSession(const VaultSession&) = delete;
    VaultSession& operator=(const VaultSession&) = delete;

    bool unlocked() const {
        return protectedKey_.size() == 32;
    }

    void lock() noexcept {
        protectedKey_.clear();
        SecureZeroMemory(salt_.data(), salt_.size());
    }

    bool remember(const secure::Buffer& key, const std::array<BYTE, 16>& salt) {
        lock();
        if (key.size() != 32)
            return false;
        secure::Buffer protectedKey(32);
        memcpy(protectedKey.data(), key.data(), 32);
        if (!CryptProtectMemory(protectedKey.data(), 32, CRYPTPROTECTMEMORY_SAME_PROCESS))
            return false;
        protectedKey_ = std::move(protectedKey);
        salt_ = salt;
        return true;
    }

    bool restore(const std::array<BYTE, 16>& salt, secure::Buffer& key) {
        key.clear();
        if (!unlocked() || salt != salt_) {
            lock();
            return false;
        }
        secure::Buffer temporary(32);
        memcpy(temporary.data(), protectedKey_.data(), 32);
        if (!CryptUnprotectMemory(temporary.data(), 32, CRYPTPROTECTMEMORY_SAME_PROCESS)) {
            lock();
            return false;
        }
        key = std::move(temporary);
        return true;
    }
};
} // namespace encrypted
