#include "archmage/base/Random.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>

#if defined(__linux__)
#include <sys/random.h>
#else
#include <random>
#endif

namespace archmage::base {

void secureRandomBytes(std::span<std::byte> out) {
#if defined(__linux__)
    std::size_t filled = 0;
    while (filled < out.size()) {
        const ssize_t n = getrandom(out.data() + filled, out.size() - filled, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::fprintf(stderr, "archmage: getrandom failed (errno %d)\n", errno);
            std::abort();
        }
        filled += static_cast<std::size_t>(n);
    }
#else
    // Portability stub for platforms not built in Stage 1 (ADR-0021): std::random_device is a CSPRNG on
    // the MSVC and libc++ implementations ArchMage targets; replace with BCryptGenRandom / arc4random_buf
    // when those platforms are built.
    std::random_device device;
    for (std::byte &b : out) {
        b = static_cast<std::byte>(device() & 0xffu);
    }
#endif
}

} // namespace archmage::base
