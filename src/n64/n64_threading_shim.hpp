/**
 * n64_threading_shim.hpp
 *
 * Replaces std::thread, std::future, std::promise, std::mutex, std::condition_variable
 * for N64 builds. The N64 is single-threaded — all "async" work executes immediately.
 *
 * Force-included via -include in CMakeLists.txt.
 *
 * Strategy: we intercept #include <thread>, <future>, <mutex>, <condition_variable>
 * by defining their include guards and providing compatible stub implementations.
 */

#ifndef N64_THREADING_SHIM_HPP
#define N64_THREADING_SHIM_HPP

#ifdef N64_BUILD

// -----------------------------------------------------------------------
// Block standard threading headers by defining their include guards
// -----------------------------------------------------------------------
// GCC/libstdc++ guard macros
#define _GLIBCXX_THREAD
#define _GLIBCXX_FUTURE
#define _GLIBCXX_MUTEX
#define _GLIBCXX_CONDITION_VARIABLE
#define _GLIBCXX_SHARED_MUTEX

#include <functional>
#include <chrono>
#include <stdexcept>
#include <utility>
#include <type_traits>

namespace std {

// -----------------------------------------------------------------------
// std::thread stub — runs the callable immediately on construction
// -----------------------------------------------------------------------
class thread {
public:
    class id {
        unsigned long id_;
    public:
        id() : id_(0) {}
        explicit id(unsigned long i) : id_(i) {}
        friend bool operator==(id a, id b) { return a.id_ == b.id_; }
        friend bool operator!=(id a, id b) { return a.id_ != b.id_; }
        friend bool operator<(id a, id b) { return a.id_ < b.id_; }
    };

    thread() noexcept = default;

    template<typename F, typename... Args>
    explicit thread(F&& f, Args&&... args) {
        // Execute immediately — N64 is single-threaded
        try {
            std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
        } catch (...) {
            // Swallow exceptions from thread body
        }
        joined_ = false;
    }

    ~thread() {
        // No-op: already ran synchronously
    }

    thread(thread&& other) noexcept : joined_(other.joined_) {
        other.joined_ = true;
    }

    thread& operator=(thread&& other) noexcept {
        joined_ = other.joined_;
        other.joined_ = true;
        return *this;
    }

    thread(const thread&) = delete;
    thread& operator=(const thread&) = delete;

    bool joinable() const noexcept { return !joined_; }
    void join() { joined_ = true; }
    void detach() { joined_ = true; }
    id get_id() const noexcept { return id(1); }

    static unsigned hardware_concurrency() noexcept { return 1; }

private:
    bool joined_ = true;
};

namespace this_thread {
    inline thread::id get_id() noexcept { return thread::id(1); }

    template<typename Rep, typename Period>
    inline void sleep_for(const chrono::duration<Rep, Period>&) {
        // No-op on N64; could add a busy-wait but not needed
    }

    template<typename Clock, typename Duration>
    inline void sleep_until(const chrono::time_point<Clock, Duration>&) {}

    inline void yield() noexcept {}
}

// -----------------------------------------------------------------------
// std::mutex stubs — no-op on single-threaded N64
// -----------------------------------------------------------------------
class mutex {
public:
    mutex() = default;
    ~mutex() = default;
    mutex(const mutex&) = delete;
    mutex& operator=(const mutex&) = delete;
    void lock() {}
    bool try_lock() { return true; }
    void unlock() {}
};

class recursive_mutex {
public:
    recursive_mutex() = default;
    ~recursive_mutex() = default;
    recursive_mutex(const recursive_mutex&) = delete;
    recursive_mutex& operator=(const recursive_mutex&) = delete;
    void lock() {}
    bool try_lock() { return true; }
    void unlock() {}
};

class timed_mutex {
public:
    timed_mutex() = default;
    void lock() {}
    bool try_lock() { return true; }
    void unlock() {}
    template<typename Rep, typename Period>
    bool try_lock_for(const chrono::duration<Rep, Period>&) { return true; }
    template<typename Clock, typename Duration>
    bool try_lock_until(const chrono::time_point<Clock, Duration>&) { return true; }
};

template<typename Mutex>
class lock_guard {
public:
    explicit lock_guard(Mutex&) {}
    lock_guard(Mutex&, adopt_lock_t) {}
    ~lock_guard() = default;
    lock_guard(const lock_guard&) = delete;
    lock_guard& operator=(const lock_guard&) = delete;
};

template<typename Mutex>
class unique_lock {
    Mutex* pm_ = nullptr;
    bool owns_ = false;
public:
    unique_lock() noexcept = default;
    explicit unique_lock(Mutex& m) : pm_(&m), owns_(true) {}
    unique_lock(Mutex& m, defer_lock_t) noexcept : pm_(&m), owns_(false) {}
    unique_lock(Mutex& m, try_to_lock_t) : pm_(&m), owns_(true) {}
    unique_lock(Mutex& m, adopt_lock_t) : pm_(&m), owns_(true) {}
    ~unique_lock() = default;

    unique_lock(unique_lock&& other) noexcept : pm_(other.pm_), owns_(other.owns_) {
        other.pm_ = nullptr; other.owns_ = false;
    }
    unique_lock& operator=(unique_lock&& other) noexcept {
        pm_ = other.pm_; owns_ = other.owns_;
        other.pm_ = nullptr; other.owns_ = false;
        return *this;
    }

    void lock() { owns_ = true; }
    bool try_lock() { owns_ = true; return true; }
    void unlock() { owns_ = false; }
    bool owns_lock() const noexcept { return owns_; }
    explicit operator bool() const noexcept { return owns_; }
    Mutex* mutex() const noexcept { return pm_; }
    Mutex* release() noexcept { auto* m = pm_; pm_ = nullptr; owns_ = false; return m; }
};

template<typename Mutex>
class shared_lock {
public:
    shared_lock() noexcept = default;
    explicit shared_lock(Mutex&) {}
    ~shared_lock() = default;
    void lock() {}
    void unlock() {}
    bool owns_lock() const noexcept { return true; }
};

class shared_mutex {
public:
    shared_mutex() = default;
    void lock() {}
    bool try_lock() { return true; }
    void unlock() {}
    void lock_shared() {}
    bool try_lock_shared() { return true; }
    void unlock_shared() {}
};

// -----------------------------------------------------------------------
// std::condition_variable — no-op
// -----------------------------------------------------------------------
enum class cv_status { no_timeout, timeout };

class condition_variable {
public:
    condition_variable() = default;
    ~condition_variable() = default;
    condition_variable(const condition_variable&) = delete;
    condition_variable& operator=(const condition_variable&) = delete;
    void notify_one() noexcept {}
    void notify_all() noexcept {}
    template<typename Lock>
    void wait(Lock&) {}
    template<typename Lock, typename Pred>
    void wait(Lock&, Pred pred) { while (!pred()) {} }
    template<typename Lock, typename Rep, typename Period>
    cv_status wait_for(Lock&, const chrono::duration<Rep, Period>&) { return cv_status::no_timeout; }
    template<typename Lock, typename Rep, typename Period, typename Pred>
    bool wait_for(Lock&, const chrono::duration<Rep, Period>&, Pred pred) { return pred(); }
    template<typename Lock, typename Clock, typename Duration>
    cv_status wait_until(Lock&, const chrono::time_point<Clock, Duration>&) { return cv_status::no_timeout; }
};

class condition_variable_any {
public:
    void notify_one() noexcept {}
    void notify_all() noexcept {}
    template<typename Lock> void wait(Lock&) {}
    template<typename Lock, typename Pred> void wait(Lock&, Pred pred) { while (!pred()) {} }
};

// -----------------------------------------------------------------------
// std::once_flag / std::call_once
// -----------------------------------------------------------------------
struct once_flag {
    bool called = false;
};

template<typename Callable, typename... Args>
void call_once(once_flag& flag, Callable&& f, Args&&... args) {
    if (!flag.called) {
        flag.called = true;
        std::invoke(std::forward<Callable>(f), std::forward<Args>(args)...);
    }
}

// -----------------------------------------------------------------------
// std::future / std::promise — synchronous execution
// -----------------------------------------------------------------------
enum class future_status { ready, timeout, deferred };

template<typename T>
class future {
    T value_{};
    bool valid_ = false;
public:
    future() noexcept = default;
    future(future&& other) noexcept : value_(std::move(other.value_)), valid_(other.valid_) {
        other.valid_ = false;
    }
    future& operator=(future&& other) noexcept {
        value_ = std::move(other.value_);
        valid_ = other.valid_;
        other.valid_ = false;
        return *this;
    }
    future(const future&) = delete;
    future& operator=(const future&) = delete;

    T get() { valid_ = false; return std::move(value_); }
    bool valid() const noexcept { return valid_; }
    void wait() const {}
    template<typename Rep, typename Period>
    future_status wait_for(const chrono::duration<Rep, Period>&) const {
        return future_status::ready;
    }
    template<typename Clock, typename Duration>
    future_status wait_until(const chrono::time_point<Clock, Duration>&) const {
        return future_status::ready;
    }

    // Internal: set by promise
    void _set(T v) { value_ = std::move(v); valid_ = true; }
};

template<>
class future<void> {
    bool valid_ = false;
public:
    future() noexcept = default;
    future(future&& other) noexcept : valid_(other.valid_) { other.valid_ = false; }
    future& operator=(future&& other) noexcept { valid_ = other.valid_; other.valid_ = false; return *this; }
    future(const future&) = delete;
    future& operator=(const future&) = delete;

    void get() { valid_ = false; }
    bool valid() const noexcept { return valid_; }
    void wait() const {}
    template<typename Rep, typename Period>
    future_status wait_for(const chrono::duration<Rep, Period>&) const { return future_status::ready; }

    void _set() { valid_ = true; }
};

template<typename T>
class promise {
    future<T>* fut_ = nullptr;
public:
    promise() = default;
    promise(promise&&) noexcept = default;
    promise& operator=(promise&&) noexcept = default;
    promise(const promise&) = delete;
    promise& operator=(const promise&) = delete;

    future<T> get_future() {
        future<T> f;
        // Store pointer for set_value
        return f;
    }
    void set_value(T value) {}
    void set_exception(std::exception_ptr) {}
};

template<>
class promise<void> {
public:
    promise() = default;
    promise(promise&&) noexcept = default;
    promise& operator=(promise&&) noexcept = default;

    future<void> get_future() { future<void> f; f._set(); return f; }
    void set_value() {}
    void set_exception(std::exception_ptr) {}
};

// std::async — just run immediately
template<typename F, typename... Args>
auto async(F&& f, Args&&... args) -> future<decltype(f(args...))> {
    using R = decltype(f(args...));
    future<R> fut;
    if constexpr (std::is_void_v<R>) {
        std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
        fut._set();
    } else {
        fut._set(std::invoke(std::forward<F>(f), std::forward<Args>(args)...));
    }
    return fut;
}

// Launch policy (ignored — always synchronous)
enum class launch {
    async = 1,
    deferred = 2
};

template<typename F, typename... Args>
auto async(launch, F&& f, Args&&... args) -> future<decltype(f(args...))> {
    return async(std::forward<F>(f), std::forward<Args>(args)...);
}

} // namespace std

#endif // N64_BUILD
#endif // N64_THREADING_SHIM_HPP
