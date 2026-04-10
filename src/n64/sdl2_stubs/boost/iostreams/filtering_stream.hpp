/* Stub for N64 build — minimal boost::iostreams::filtering_stream */
#pragma once
#include <iostream>

namespace boost {
namespace iostreams {

struct output {};
struct input {};

template<typename Mode>
class filtering_stream : public std::ostream {
public:
    filtering_stream() : std::ostream(nullptr) {}

    template<typename T>
    void push(T&) {}

    template<typename T>
    void push(T&&) {}

    void pop() {}
    bool empty() const { return true; }
    void reset() {}
};

template<>
class filtering_stream<input> : public std::istream {
public:
    filtering_stream() : std::istream(nullptr) {}

    template<typename T>
    void push(T&) {}

    template<typename T>
    void push(T&&) {}

    void pop() {}
    bool empty() const { return true; }
    void reset() {}
};

} // namespace iostreams
} // namespace boost
