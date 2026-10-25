#pragma once
#include <Windows.h>
#include <cstring>
#include <cwchar>
#include <new>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace secure {
// Dedicated allocations avoid small-string copies and reallocations of secrets.
// Locking is best effort; wiping and releasing the allocation is unconditional.
class Buffer {
    BYTE* data_ = nullptr;
    size_t size_ = 0;
    bool locked_ = false;

public:
    explicit Buffer(size_t size = 0) : size_(size) {
        if (!size)
            return;
        data_ = static_cast<BYTE*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!data_)
            throw std::bad_alloc();
        locked_ = VirtualLock(data_, size) != FALSE;
    }

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    Buffer(Buffer&& other) noexcept {
        swap(other);
    }

    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            clear();
            swap(other);
        }
        return *this;
    }

    ~Buffer() {
        clear();
    }

    void swap(Buffer& other) noexcept {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
        std::swap(locked_, other.locked_);
    }

    void clear() noexcept {
        if (data_) {
            SecureZeroMemory(data_, size_);
            if (locked_)
                VirtualUnlock(data_, size_);
            VirtualFree(data_, 0, MEM_RELEASE);
        }
        data_ = nullptr;
        size_ = 0;
        locked_ = false;
    }

    BYTE* data() const {
        return data_;
    }

    size_t size() const {
        return size_;
    }
};

class Text {
    Buffer memory_;
    size_t length_ = 0;

    static size_t bytes(size_t capacity) {
        if (capacity > (std::numeric_limits<size_t>::max)() / sizeof(wchar_t) - 1)
            throw std::length_error("Secret text is too large");
        return (capacity + 1) * sizeof(wchar_t);
    }

public:
    Text() = default;

    explicit Text(size_t capacity) : memory_(bytes(capacity)) {}

    Text(const wchar_t* data, size_t length) : Text(length) {
        if (length)
            memcpy(memory_.data(), data, length * sizeof(wchar_t));
        length_ = length;
    }

    explicit Text(const std::wstring& value) : Text(value.data(), value.size()) {}

    Text(const Text&) = delete;
    Text& operator=(const Text&) = delete;

    Text(Text&& other) noexcept
        : memory_(std::move(other.memory_)), length_(std::exchange(other.length_, 0)) {}

    Text& operator=(Text&& other) noexcept {
        if (this != &other) {
            memory_ = std::move(other.memory_);
            length_ = std::exchange(other.length_, 0);
        }
        return *this;
    }

    size_t size() const {
        return length_;
    }

    bool empty() const {
        return length_ == 0;
    }

    const wchar_t* data() const {
        return memory_.data() ? reinterpret_cast<wchar_t*>(memory_.data()) : L"";
    }

    const wchar_t* c_str() const {
        return data();
    }

    wchar_t* writable() {
        return reinterpret_cast<wchar_t*>(memory_.data());
    }

    wchar_t operator[](size_t i) const {
        return data()[i];
    }

    void length(size_t value) {
        if (value && value >= memory_.size() / sizeof(wchar_t))
            throw std::length_error("Secret text exceeds its buffer");
        length_ = value;
        if (memory_.data())
            writable()[value] = 0;
    }

    void clear() noexcept {
        memory_.clear();
        length_ = 0;
    }

    static Text read(HWND edit) {
        Text result(static_cast<size_t>(GetWindowTextLengthW(edit)));
        result.length(GetWindowTextW(
            edit, result.writable(), static_cast<int>(result.memory_.size() / sizeof(wchar_t))));
        return result;
    }
};

inline bool equal(const Text& a, const Text& b) {
    if (a.size() != b.size())
        return false;
    unsigned difference = 0;
    for (size_t i = 0; i < a.size(); ++i)
        difference |= static_cast<unsigned>(a[i] ^ b[i]);
    return difference == 0;
}

inline void wipe(std::wstring& value) noexcept {
    // Include spare capacity and the small-string storage, including after moves.
    try {
        value.resize(value.capacity(), L'\0');
    } catch (...) {
    }
    if (!value.empty())
        SecureZeroMemory(&value[0], value.size() * sizeof(wchar_t));
    value.clear();
}

struct StringGuard {
    std::wstring& value;

    ~StringGuard() {
        wipe(value);
    }
};
} // namespace secure
