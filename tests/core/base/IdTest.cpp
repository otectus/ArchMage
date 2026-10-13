// S1-2.1 (D-16): typed identifiers round-trip and do not collide.

#include "archmage/base/Id.h"
#include "archmage/base/Sha256.h"

#include <gtest/gtest.h>

#include <regex>
#include <unordered_set>

namespace archmage::base {
namespace {

TEST(Id, FormatAndParseRoundTrip) {
    for (int i = 0; i < 10000; ++i) {
        const LayerId id = LayerId::generate();
        const std::string text = id.toString();
        ASSERT_TRUE(std::regex_match(text, std::regex("layer-[0-7][0-9A-HJKMNP-TV-Z]{25}"))) << text;
        const Result<LayerId> parsed = LayerId::parse(text);
        ASSERT_TRUE(parsed) << format(parsed.error());
        ASSERT_EQ(*parsed, id);
    }
}

TEST(Id, EncodingIsBigEndianCrockford) {
    EXPECT_EQ(RawId{}.encode(), std::string(26, '0'));
    EXPECT_EQ((RawId{0, 1}.encode()), "0000000000000000000000000"
                                      "1");
    EXPECT_EQ((RawId{0, 32}.encode()), "00000000000000000000000010");
    EXPECT_EQ((RawId{~0ULL, ~0ULL}.encode()), "7ZZZZZZZZZZZZZZZZZZZZZZZZZ");
    EXPECT_EQ(RawId::decode("7zzzzzzzzzzzzzzzzzzzzzzzzz"), (RawId{~0ULL, ~0ULL}));
}

TEST(Id, ParseRejectsMalformedText) {
    const std::string good = DocumentId::generate().toString();
    EXPECT_FALSE(LayerId::parse(good)); // wrong prefix
    EXPECT_FALSE(DocumentId::parse(good.substr(0, good.size() - 1)));
    EXPECT_FALSE(DocumentId::parse("doc-8ZZZZZZZZZZZZZZZZZZZZZZZZZ")); // more than 128 bits
    EXPECT_FALSE(DocumentId::parse("doc-0000000000000000000000000U")); // U is not Crockford
    EXPECT_FALSE(DocumentId::parse("doc_01234567890123456789012345"));
    const auto error = DocumentId::parse("garbage").error();
    EXPECT_EQ(error.category, ErrorCategory::DamagedInput);
    EXPECT_EQ(error.code, "id.malformed");
}

TEST(Id, NoCollisionsOverAMillionDraws) {
    std::unordered_set<RawId> seen;
    seen.reserve(1'000'000);
    for (int i = 0; i < 1'000'000; ++i) {
        ASSERT_TRUE(seen.insert(RawId::random()).second) << "collision after " << i;
    }
}

TEST(Id, NullAndPrefixes) {
    EXPECT_TRUE(RevisionId{}.isNull());
    EXPECT_FALSE(RevisionId::generate().isNull());
    EXPECT_EQ(idPrefix(IdKind::Document), "doc");
    EXPECT_EQ(idPrefix(IdKind::Transaction), "tx");
    EXPECT_EQ(idPrefix(IdKind::Session), "sess");
    EXPECT_EQ(TransactionId::generate().toString().substr(0, 3), "tx-");
}

TEST(Sha256, KnownVectors) {
    EXPECT_EQ(Sha256::of({}).hex(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    Sha256 hasher;
    hasher.update(std::string_view("abc"));
    const Sha256Digest abc = hasher.finish();
    EXPECT_EQ(abc.hex(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(Sha256Digest::fromHex(abc.hex()), abc);
    EXPECT_FALSE(Sha256Digest::fromHex("XYZ"));
}

} // namespace
} // namespace archmage::base
