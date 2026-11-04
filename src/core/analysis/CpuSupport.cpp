#include "Recognizer.h"
#if defined(_MSC_VER)
 #include <intrin.h>
#endif

namespace wers {

// The speech model is built for AVX2, so anything without it must not start the recognition process.
bool recognitionSupported() noexcept {
#if defined(_MSC_VER)
    int info[4]{};
    __cpuid(info, 0);
    if (info[0] < 7) return false;
    __cpuidex(info, 7, 0);
    const bool avx2 = (info[1] & (1 << 5)) != 0;
    __cpuid(info, 1);
    const bool fma = (info[2] & (1 << 12)) != 0, osxsave = (info[2] & (1 << 27)) != 0, f16c = (info[2] & (1 << 29)) != 0;
    if (!avx2 || !fma || !f16c || !osxsave) return false;
    return (_xgetbv(0) & 6) == 6;
#elif defined(__x86_64__)
    return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
#else
    return true;
#endif
}

} // namespace wers
