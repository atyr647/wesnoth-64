#pragma once

/**
 * n64_memory.hpp
 *
 * Fixed-size memory pools for the N64 port.
 *
 * Wesnoth normally uses std::shared_ptr, std::map, std::string heavily.
 * On N64 with 8 MB RAM these are replaced with:
 *   - PoolAllocator<T, N>  : fixed-capacity object pool
 *   - InternedString       : integer handle into a global string table
 *   - LinearAllocator      : bump-pointer allocator for scratch memory
 *
 * All pools are statically allocated at startup to avoid heap fragmentation.
 */

#include "n64_platform.hpp"
#include <new>
#include <utility>
#include <cstring>

// -----------------------------------------------------------------------
// LinearAllocator — bump-pointer, no individual deallocation
// -----------------------------------------------------------------------
class LinearAllocator {
public:
    LinearAllocator() = default;
    explicit LinearAllocator(void* buf, size_t cap)
        : buf_(static_cast<u8*>(buf)), cap_(cap), used_(0) {}

    void* alloc(size_t bytes, size_t align = 8) {
        size_t offset = (used_ + align - 1) & ~(align - 1);
        N64_ASSERT(offset + bytes <= cap_);
        used_ = offset + bytes;
        return buf_ + offset;
    }

    void reset() { used_ = 0; }
    size_t used() const { return used_; }
    size_t capacity() const { return cap_; }

private:
    u8*    buf_  = nullptr;
    size_t cap_  = 0;
    size_t used_ = 0;
};

// -----------------------------------------------------------------------
// PoolAllocator<T, N> — fixed-capacity free-list pool for objects of type T
// -----------------------------------------------------------------------
template<typename T, size_t N>
class PoolAllocator {
public:
    PoolAllocator() {
        // Build free list
        for (size_t i = 0; i < N - 1; ++i)
            slots_[i].next = &slots_[i + 1];
        slots_[N - 1].next = nullptr;
        free_head_ = &slots_[0];
        count_ = 0;
    }

    template<typename... Args>
    T* alloc(Args&&... args) {
        N64_ASSERT(free_head_ != nullptr); // pool exhausted
        Slot* s = free_head_;
        free_head_ = s->next;
        ++count_;
        return new (&s->storage) T(std::forward<Args>(args)...);
    }

    void free(T* ptr) {
        N64_ASSERT(ptr != nullptr);
        ptr->~T();
        Slot* s = reinterpret_cast<Slot*>(ptr);
        s->next = free_head_;
        free_head_ = s;
        --count_;
    }

    size_t count() const { return count_; }
    static constexpr size_t capacity() { return N; }

private:
    union Slot {
        alignas(T) char storage[sizeof(T)];
        Slot* next;
    };
    Slot   slots_[N];
    Slot*  free_head_;
    size_t count_;
};

// -----------------------------------------------------------------------
// InternedString — store strings once, reference by u16 handle
// Max 4096 unique strings, total string data 128 KB
// -----------------------------------------------------------------------
class StringTable {
public:
    static constexpr u16 INVALID = 0xFFFF;
    static constexpr size_t MAX_STRINGS = 4096;
    static constexpr size_t DATA_SIZE   = 128 * 1024;

    StringTable() : data_used_(0), count_(0) {
        data_[0] = '\0'; // slot 0 = empty string
        offsets_[0] = 0;
        ++count_;
        data_used_ = 1;
    }

    // Intern a string; returns existing handle if already present
    u16 intern(const char* s) {
        if (!s || !*s) return 0;
        for (u16 i = 0; i < count_; ++i) {
            if (std::strcmp(data_ + offsets_[i], s) == 0)
                return i;
        }
        N64_ASSERT(count_ < MAX_STRINGS);
        size_t len = std::strlen(s) + 1;
        N64_ASSERT(data_used_ + len <= DATA_SIZE);
        std::memcpy(data_ + data_used_, s, len);
        offsets_[count_] = data_used_;
        data_used_ += len;
        return count_++;
    }

    const char* get(u16 handle) const {
        N64_ASSERT(handle < count_);
        return data_ + offsets_[handle];
    }

    u16 count() const { return count_; }

private:
    char   data_[DATA_SIZE];
    u32    offsets_[MAX_STRINGS];
    size_t data_used_;
    u16    count_;
};

// Global string table instance (defined in n64_memory.cpp)
extern StringTable g_strings;

// Convenience: intern from C++ std::string without including <string>
// (avoids pulling std::string into N64 code paths)
inline u16 n64_intern(const char* s) { return g_strings.intern(s); }
inline const char* n64_str(u16 handle) { return g_strings.get(handle); }

// -----------------------------------------------------------------------
// Global arena declarations (defined in n64_memory.cpp)
// -----------------------------------------------------------------------
extern LinearAllocator g_arena_game;   // game state scratch
extern LinearAllocator g_arena_wml;    // WML parse workspace
