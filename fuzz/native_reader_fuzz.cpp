// libFuzzer harness for the native reader (plan S1-5.8, spec §26.1): any bytes presented as an .archmage
// file must produce a structured result, never a crash, a leak, undefined behavior or an unbounded
// allocation. Seeds: the malformed corpus (tests/resilience/corpus) and the committed fixtures.

#include "archmage/native/NativeFile.h"
#include "archmage/tiles/ResourceCoordinator.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include <unistd.h>

namespace {

const std::string &scratchPath() {
    static const std::string path = "/tmp/archmage-fuzz-" + std::to_string(::getpid()) + ".archmage";
    return path;
}

void removeScratch() {
    for (const char *suffix : {"", "-journal", "-wal", "-shm"}) {
        std::remove((scratchPath() + suffix).c_str());
    }
}

} // namespace

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
    // A small RAM budget turns oversized claims into resource_exhausted early.
    archmage::tiles::ResourceCoordinator::global().setLimit(archmage::tiles::Budget::Ram, 256ull << 20);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
    removeScratch();
    if (std::FILE *file = std::fopen(scratchPath().c_str(), "wb")) {
        std::fwrite(data, 1, size, file);
        std::fclose(file);
    } else {
        return 0;
    }
    (void)archmage::native::readDocument(scratchPath());
    (void)archmage::native::validateFile(scratchPath(), archmage::native::ValidationLevel::Deep);
    (void)archmage::native::inspectFile(scratchPath());
    removeScratch();
    return 0;
}
