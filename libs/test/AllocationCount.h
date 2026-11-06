#pragma once
// Counts what the thread under test allocates. The timer thread of JUCE allocates now and then by itself,
// so only this thread counts. WERS_COUNT_ALLOCATIONS() goes into one file of a test program, outside any namespace.
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace wers::test {

inline std::atomic<long> allocationTotal{ 0 };
inline thread_local bool allocationsCounted = false;

class AllocationCounter {
public:
    AllocationCounter() noexcept {
        allocationTotal = 0;
        allocationsCounted = true;
    }
    ~AllocationCounter() { allocationsCounted = false; }
    long stop() noexcept {
        allocationsCounted = false;
        return allocationTotal.load();
    }
};

} // namespace wers::test

#define WERS_COUNT_ALLOCATIONS()                                                                     \
    void* operator new(std::size_t n) {                                                              \
        if (wers::test::allocationsCounted) wers::test::allocationTotal.fetch_add(1);                \
        if (void* p = std::malloc(n ? n : 1)) return p;                                              \
        throw std::bad_alloc();                                                                      \
    }                                                                                                \
    void* operator new[](std::size_t n) { return operator new(n); }                                  \
    void operator delete(void* p) noexcept { std::free(p); }                                         \
    void operator delete[](void* p) noexcept { std::free(p); }                                       \
    void operator delete(void* p, std::size_t) noexcept { std::free(p); }                            \
    void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
