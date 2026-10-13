// S1-5.7 (spec §26.1): malformed native files fail fast with the right category and without allocating
// beyond declared limits. The corpus is generated from the committed fixture by
// tests/resilience/corpus/make_native_corpus.py.

#include "ChildProcess.h"

#include "archmage/native/NativeFile.h"

#include "../io/native/NativeTestSupport.h"

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>

#include <sys/resource.h>

namespace archmage::native {
namespace {

using nlohmann::json;

long maxResidentKiB() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss;
}

TEST(NativeCorpus, MalformedFilesFailFastWithTheRightCategory) {
    testing::ScratchDirectory dir(testing::tmpRoot());
    const auto generated = archmage::testing::runChild({ARCHMAGE_PYTHON, "-I", ARCHMAGE_CORPUS_SCRIPT,
                                                        ARCHMAGE_FIXTURE, dir.path().string()});
    ASSERT_EQ(generated.exitCode, 0) << generated.output;
    std::ifstream in(dir / "corpus.json");
    const json corpus = json::parse(in, nullptr, false);
    ASSERT_TRUE(corpus.is_object()) << generated.output;
    ASSERT_GE(corpus["cases"].size(), 10u);

    for (const json &entry : corpus["cases"]) {
        const std::string file = entry["file"].get<std::string>();
        const json &expect = entry["expect"];
        SCOPED_TRACE(file);
        const long rssBefore = maxResidentKiB();
        const auto started = std::chrono::steady_clock::now();
        auto opened = readDocument(dir / file);
        auto report = validateFile(dir / file, ValidationLevel::Deep);
        const auto elapsed = std::chrono::steady_clock::now() - started;
        EXPECT_LT(elapsed, std::chrono::seconds(2)) << "a damaged file must fail fast";
        EXPECT_LT(maxResidentKiB() - rssBefore, 64 * 1024) << "no allocation beyond the declared limits";

        if (expect.contains("readOnly")) {
            ASSERT_TRUE(opened) << base::format(opened.error());
            EXPECT_TRUE(opened->info.readOnly);
            EXPECT_EQ(opened->info.readOnlyReason, expect["readOnly"].get<std::string>());
            EXPECT_FALSE(opened->state);
            continue;
        }
        ASSERT_FALSE(opened) << "a malformed file must not open";
        EXPECT_EQ(std::string(base::categoryName(opened.error().category)), expect["category"].get<std::string>())
            << base::format(opened.error());
        if (expect.contains("code")) {
            EXPECT_EQ(opened.error().code, expect["code"].get<std::string>()) << base::format(opened.error());
        }
        // validate reports the same file as invalid (or, if it cannot examine it, as an error).
        if (report) {
            EXPECT_FALSE(report->valid());
        }
    }
}

} // namespace
} // namespace archmage::native
