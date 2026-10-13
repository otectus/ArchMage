// PSD decode probe (plan S1-6.5, M0-E4; spec §22.2, §26.1): a bounded parse of a layered PSD or PSB file.
//
//   psd_probe <file.psd> <output-directory>
//
// Prints (and writes to <output>/psd-probe.txt) the header, image resources, every layer with its blend key,
// opacity, flags, mask and group markers, every additional-information block, and an explicit
// "unsupported" record for everything ArchMage Stage 1 cannot represent. Writes the file's own merged
// composite to merged.png and renders the layers Stage 1 can represent (normal-mode raster layers without
// masks) through the CPU compositor to archmage-flat.png.
//
// Every length is checked against the bytes that remain before it is used; dimensions, channel counts,
// layer counts and decoded sizes are limited before anything is allocated.

#include "ProbePng.h"

#include "archmage/base/Sha256.h"
#include "archmage/commands/Command.h"
#include "archmage/render/CpuEvaluator.h"

#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

using archmage::probes::writePngRgba8;

constexpr std::uint64_t kMaxFileBytes = 2ull << 30;
constexpr std::int64_t kMaxPsdDimension = 30000;
constexpr std::int64_t kMaxPsbDimension = 300000;
constexpr int kMaxChannels = 56;
constexpr int kMaxLayers = 10000;

std::ostringstream report;

[[gnu::format(printf, 1, 2)]] void line(const char *format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    report << buffer << '\n';
}

// Big-endian reads that refuse to go past the end; the first failure stops everything.
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}

    bool need(std::size_t n, const char *what) {
        if (!ok_) {
            return false;
        }
        if (n > data_.size() - pos_) {
            ok_ = false;
            error_ = std::string(what) + ": needs " + std::to_string(n) + " bytes at offset " + std::to_string(pos_) +
                     ", " + std::to_string(data_.size() - pos_) + " remain";
            return false;
        }
        return true;
    }
    std::uint64_t uint(std::size_t bytes, const char *what) {
        if (!need(bytes, what)) {
            return 0;
        }
        std::uint64_t v = 0;
        for (std::size_t i = 0; i < bytes; ++i) {
            v = (v << 8u) | data_[pos_ + i];
        }
        pos_ += bytes;
        return v;
    }
    std::uint8_t u8(const char *what) { return static_cast<std::uint8_t>(uint(1, what)); }
    std::uint16_t u16(const char *what) { return static_cast<std::uint16_t>(uint(2, what)); }
    std::int16_t i16(const char *what) { return static_cast<std::int16_t>(u16(what)); }
    std::uint32_t u32(const char *what) { return static_cast<std::uint32_t>(uint(4, what)); }
    std::int32_t i32(const char *what) { return static_cast<std::int32_t>(u32(what)); }
    std::uint64_t u64(const char *what) { return uint(8, what); }
    std::string fourcc(const char *what) {
        if (!need(4, what)) {
            return "????";
        }
        std::string s(reinterpret_cast<const char *>(data_.data() + pos_), 4);
        pos_ += 4;
        return s;
    }
    std::span<const std::uint8_t> bytes(std::size_t n, const char *what) {
        if (!need(n, what)) {
            return {};
        }
        auto out = data_.subspan(pos_, n);
        pos_ += n;
        return out;
    }
    void skip(std::size_t n, const char *what) { (void)bytes(n, what); }
    /// Pascal string (length byte then text) padded to a multiple of `padding` bytes in total.
    std::string pascal(std::size_t padding, const char *what) {
        const std::uint8_t length = u8(what);
        auto text = bytes(length, what);
        std::string s(reinterpret_cast<const char *>(text.data()), text.size());
        const std::size_t total = 1 + length;
        skip((padding - total % padding) % padding, what);
        return s;
    }
    [[nodiscard]] std::size_t pos() const { return pos_; }
    void seek(std::size_t to) {
        if (to > data_.size()) {
            ok_ = false;
            error_ = "seek beyond the end";
            return;
        }
        pos_ = to;
    }
    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] const std::string &error() const { return error_; }
    [[nodiscard]] std::size_t remaining() const { return data_.size() - pos_; }

private:
    std::span<const std::uint8_t> data_;
    std::size_t pos_ = 0;
    bool ok_ = true;
    std::string error_;
};

std::string blendName(const std::string &key) {
    static const std::map<std::string, std::string> names{
        {"pass", "pass through"}, {"norm", "normal"},        {"diss", "dissolve"},     {"dark", "darken"},
        {"mul ", "multiply"},     {"idiv", "color burn"},    {"lbrn", "linear burn"},  {"dkCl", "darker color"},
        {"lite", "lighten"},      {"scrn", "screen"},        {"div ", "color dodge"},  {"lddg", "linear dodge"},
        {"lgCl", "lighter color"}, {"over", "overlay"},      {"sLit", "soft light"},   {"hLit", "hard light"},
        {"vLit", "vivid light"},  {"lLit", "linear light"},  {"pLit", "pin light"},    {"hMix", "hard mix"},
        {"diff", "difference"},   {"smud", "exclusion"},     {"fsub", "subtract"},     {"fdiv", "divide"},
        {"hue ", "hue"},          {"sat ", "saturation"},    {"colr", "color"},        {"lum ", "luminosity"}};
    const auto it = names.find(key);
    return it != names.end() ? it->second : "unknown";
}

std::string resourceName(std::uint16_t id) {
    static const std::map<std::uint16_t, std::string> names{
        {1005, "resolution"},       {1006, "alpha channel names"}, {1010, "background color"},
        {1024, "layer state"},      {1026, "layer groups"},       {1028, "IPTC"},
        {1032, "grid and guides"},  {1036, "thumbnail"},          {1037, "global angle"},
        {1039, "ICC profile"},      {1041, "ICC untagged"},       {1043, "spot halftone"},
        {1044, "document ID seed"}, {1045, "unicode alpha names"}, {1049, "global altitude"},
        {1053, "alpha identifiers"}, {1054, "URL list"},          {1057, "version info"},
        {1058, "EXIF 1"},           {1060, "XMP"},                {1061, "caption digest"},
        {1064, "pixel aspect ratio"}, {1069, "layer selection IDs"}, {1072, "layer group enabled IDs"},
        {1077, "display info"},     {1082, "print information"},  {1083, "print style"},
        {10000, "print flags"}};
    const auto it = names.find(id);
    return it != names.end() ? it->second : "unknown";
}

// PackBits rows into `out` (one row of `width` bytes per call), bounded by both sizes.
bool unpackBits(std::span<const std::uint8_t> in, std::span<std::uint8_t> out) {
    std::size_t i = 0;
    std::size_t o = 0;
    while (i < in.size() && o < out.size()) {
        const auto n = static_cast<std::int8_t>(in[i++]);
        if (n >= 0) {
            const std::size_t count = static_cast<std::size_t>(n) + 1;
            if (i + count > in.size() || o + count > out.size()) {
                return false;
            }
            std::memcpy(out.data() + o, in.data() + i, count);
            i += count;
            o += count;
        } else if (n != -128) {
            const std::size_t count = static_cast<std::size_t>(1 - n);
            if (i >= in.size() || o + count > out.size()) {
                return false;
            }
            std::memset(out.data() + o, in[i++], count);
            o += count;
        }
    }
    return o == out.size();
}

// One channel of `rows` × `width` 8-bit samples starting at the reader's position (compression word first).
// `psb` widens RLE row counts to 4 bytes. Returns nullopt with a reason in `why`.
std::optional<std::vector<std::uint8_t>> channelData(Reader &r, std::size_t length, std::int64_t width,
                                                     std::int64_t rows, bool psb, std::string &why) {
    const std::size_t start = r.pos();
    const std::uint16_t compression = r.u16("channel compression");
    const auto samples = static_cast<std::size_t>(width * rows);
    std::vector<std::uint8_t> out(samples);
    if (compression == 0) {
        auto raw = r.bytes(samples, "raw channel");
        if (!r.ok()) {
            why = r.error();
            return std::nullopt;
        }
        std::memcpy(out.data(), raw.data(), samples);
    } else if (compression == 1) {
        std::vector<std::size_t> counts(static_cast<std::size_t>(rows));
        for (auto &count : counts) {
            count = psb ? r.u32("RLE row count") : r.u16("RLE row count");
        }
        for (std::int64_t y = 0; y < rows && r.ok(); ++y) {
            auto packed = r.bytes(counts[static_cast<std::size_t>(y)], "RLE row");
            if (r.ok() && !unpackBits(packed, std::span(out).subspan(static_cast<std::size_t>(y * width),
                                                                     static_cast<std::size_t>(width)))) {
                why = "RLE row " + std::to_string(y) + " does not decode to the row width";
                return std::nullopt;
            }
        }
        if (!r.ok()) {
            why = r.error();
            return std::nullopt;
        }
    } else {
        why = compression == 2 ? "ZIP compression" : compression == 3 ? "ZIP with prediction" : "unknown compression";
        r.seek(start + length);
        return std::nullopt;
    }
    if (length != 0 && r.pos() != start + length) {
        r.seek(start + length); // trust the declared length for the next channel
    }
    return out;
}

struct Layer {
    std::int32_t top = 0, left = 0, bottom = 0, right = 0;
    std::vector<std::pair<std::int16_t, std::uint64_t>> channels; // id, length
    std::string blend;
    std::uint8_t opacity = 255;
    std::uint8_t clipping = 0;
    std::uint8_t flags = 0;
    std::uint32_t maskBytes = 0;
    std::string name;
    std::string unicodeName;
    int section = 0; // lsct: 0 none, 1 open group, 2 closed group, 3 end of group
    std::vector<std::string> unsupported;
    std::map<std::int16_t, std::vector<std::uint8_t>> pixels;
    std::vector<std::string> decodeProblems;

    [[nodiscard]] std::int64_t width() const { return std::int64_t{right} - left; }
    [[nodiscard]] std::int64_t height() const { return std::int64_t{bottom} - top; }
    [[nodiscard]] bool hidden() const { return (flags & 0x02u) != 0; }
};

void additionalInfo(Reader &r, std::size_t end, bool psb, Layer *layer, std::vector<std::string> &records,
                    const std::string &where) {
    static const std::set<std::string> eightByteInPsb{"LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn",
                                                      "Alph", "FMsk", "lnk2", "FEid", "FXid", "PxSD"};
    while (r.ok() && r.pos() + 12 <= end) {
        const std::string signature = r.fourcc("block signature");
        if (signature != "8BIM" && signature != "8B64") {
            records.push_back(where + ": stray bytes where a block was expected (" + signature + ")");
            r.seek(end);
            return;
        }
        const std::string key = r.fourcc("block key");
        const std::uint64_t length = psb && eightByteInPsb.contains(key) ? r.u64("block length") : r.u32("block length");
        const std::size_t start = r.pos();
        if (!r.need(static_cast<std::size_t>(length), "block data")) {
            return;
        }
        bool understood = false;
        if (layer != nullptr && key == "luni" && length >= 4) {
            const std::uint32_t chars = r.u32("unicode name length");
            std::string text;
            for (std::uint32_t i = 0; i < chars && r.pos() + 2 <= start + length; ++i) {
                const std::uint16_t unit = r.u16("unicode name");
                text += unit < 0x80 ? static_cast<char>(unit) : '?';
            }
            layer->unicodeName = text;
            understood = true;
        } else if (layer != nullptr && key == "lsct" && length >= 4) {
            layer->section = static_cast<int>(r.u32("section type"));
            understood = true;
        } else if (key == "lyid") {
            understood = true; // a layer ID: ArchMage assigns its own
        }
        line("    %s block '%s' (%s), %llu bytes%s", where.c_str(), key.c_str(), signature.c_str(),
             static_cast<unsigned long long>(length), understood ? "" : " -> unsupported");
        if (!understood) {
            records.push_back(where + ": additional information '" + key + "' (" + std::to_string(length) +
                              " bytes) is not interpreted");
        }
        r.seek(start + static_cast<std::size_t>(length));
        // Blocks are padded to an even length (Photoshop also pads some to four).
        if (r.pos() % 2 != 0 && r.pos() < end) {
            r.skip(1, "block padding");
        }
    }
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: psd_probe <file.psd> <output-directory>\n");
        return 2;
    }
    const std::filesystem::path input = argv[1];
    const std::filesystem::path output = argv[2];
    std::error_code ec;
    const auto size = std::filesystem::file_size(input, ec);
    if (ec || size > kMaxFileBytes) {
        std::fprintf(stderr, "psd_probe: cannot read %s (or larger than 2 GiB)\n", input.c_str());
        return 2;
    }
    std::ifstream file(input, std::ios::binary);
    const std::vector<std::uint8_t> data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    std::filesystem::create_directories(output, ec);
    Reader r(data);
    std::vector<std::string> unsupported;

    line("file: %s", input.filename().c_str());
    line("size: %zu bytes, sha256 %s", data.size(),
         archmage::base::Sha256::of(std::as_bytes(std::span(data))).hex().c_str());

    // Header.
    const std::string signature = r.fourcc("signature");
    const std::uint16_t version = r.u16("version");
    r.skip(6, "reserved");
    const std::uint16_t channels = r.u16("channels");
    const std::uint32_t height = r.u32("height");
    const std::uint32_t width = r.u32("width");
    const std::uint16_t depth = r.u16("depth");
    const std::uint16_t mode = r.u16("color mode");
    const bool psb = version == 2;
    const std::int64_t maxDimension = psb ? kMaxPsbDimension : kMaxPsdDimension;
    if (!r.ok() || signature != "8BPS" || (version != 1 && version != 2) || channels < 1 || channels > kMaxChannels ||
        width < 1 || height < 1 || width > maxDimension || height > maxDimension) {
        std::fprintf(stderr, "psd_probe: not a valid PSD/PSB header (%s)\n", r.ok() ? signature.c_str() : r.error().c_str());
        return 1;
    }
    static const char *modes[] = {"bitmap", "grayscale", "indexed", "RGB", "CMYK", "?", "?", "multichannel", "duotone", "Lab"};
    line("header: %s version %u, %u x %u, %u channels, %u bits, %s", signature.c_str(), version, width, height, channels,
         depth, mode < 10 ? modes[mode] : "unknown");
    if (mode != 3) {
        unsupported.push_back(std::string("color mode ") + (mode < 10 ? modes[mode] : "unknown") + ": Stage 1 renders RGB only");
    }
    if (depth != 8) {
        unsupported.push_back("depth " + std::to_string(depth) + " bits: the probe decodes 8-bit channels only");
    }

    // Color mode data.
    const std::uint32_t colorData = r.u32("color mode data length");
    r.skip(colorData, "color mode data");
    line("color mode data: %u bytes", colorData);

    // Image resources.
    const std::uint32_t resourcesLength = r.u32("image resources length");
    const std::size_t resourcesEnd = r.pos() + resourcesLength;
    line("image resources: %u bytes", resourcesLength);
    while (r.ok() && r.pos() + 12 <= resourcesEnd) {
        const std::string tag = r.fourcc("resource signature");
        const std::uint16_t id = r.u16("resource id");
        const std::string name = r.pascal(2, "resource name");
        const std::uint32_t length = r.u32("resource length");
        r.skip(length + (length % 2), "resource data");
        line("  resource %u (%s)%s%s, %u bytes", id, resourceName(id).c_str(), name.empty() ? "" : " named ",
             name.c_str(), length);
        if (tag != "8BIM") {
            unsupported.push_back("image resource with signature " + tag);
        } else if (id == 1039) {
            unsupported.push_back("embedded ICC profile (resource 1039): the probe renders as sRGB");
        } else if (resourceName(id) == "unknown") {
            unsupported.push_back("image resource " + std::to_string(id) + " is not interpreted");
        }
    }
    r.seek(resourcesEnd);

    // Layer and mask information.
    const std::uint64_t layerMaskLength = psb ? r.u64("layer and mask length") : r.u32("layer and mask length");
    const std::size_t layerMaskEnd = r.pos() + static_cast<std::size_t>(layerMaskLength);
    std::vector<Layer> layers;
    if (layerMaskLength > 0 && r.need(static_cast<std::size_t>(layerMaskLength), "layer and mask section")) {
        const std::uint64_t layerInfoLength = psb ? r.u64("layer info length") : r.u32("layer info length");
        const std::size_t layerInfoEnd = r.pos() + static_cast<std::size_t>(layerInfoLength);
        if (layerInfoLength > 0) {
            const std::int16_t count = r.i16("layer count");
            const int layerCount = count < 0 ? -count : count;
            line("layers: %d%s", layerCount, count < 0 ? " (the first alpha channel is the merged transparency)" : "");
            if (layerCount > kMaxLayers) {
                std::fprintf(stderr, "psd_probe: %d layers exceeds the probe's limit\n", layerCount);
                return 1;
            }
            for (int i = 0; i < layerCount && r.ok(); ++i) {
                Layer layer;
                layer.top = r.i32("top");
                layer.left = r.i32("left");
                layer.bottom = r.i32("bottom");
                layer.right = r.i32("right");
                const std::uint16_t channelCount = r.u16("layer channels");
                if (channelCount > kMaxChannels) {
                    std::fprintf(stderr, "psd_probe: layer %d has %u channels\n", i, channelCount);
                    return 1;
                }
                for (std::uint16_t c = 0; c < channelCount; ++c) {
                    const std::int16_t id = r.i16("channel id");
                    const std::uint64_t length = psb ? r.u64("channel length") : r.u32("channel length");
                    layer.channels.emplace_back(id, length);
                }
                const std::string blendSignature = r.fourcc("blend signature");
                layer.blend = r.fourcc("blend key");
                layer.opacity = r.u8("opacity");
                layer.clipping = r.u8("clipping");
                layer.flags = r.u8("flags");
                r.skip(1, "filler");
                const std::uint32_t extra = r.u32("extra data length");
                const std::size_t extraEnd = r.pos() + extra;
                layer.maskBytes = r.u32("mask data length");
                r.skip(layer.maskBytes, "mask data");
                const std::uint32_t ranges = r.u32("blending ranges length");
                r.skip(ranges, "blending ranges");
                layer.name = r.pascal(4, "layer name");
                const std::string where = "layer " + std::to_string(i);
                line("  layer %d: '%s' rect (%d, %d)-(%d, %d), %zu channels, blend %s (%s), opacity %u, clipping %u, flags 0x%02x%s, mask %u bytes",
                     i, layer.name.c_str(), layer.left, layer.top, layer.right, layer.bottom, layer.channels.size(),
                     layer.blend.c_str(), blendName(layer.blend).c_str(), layer.opacity, layer.clipping, layer.flags,
                     layer.hidden() ? " (hidden)" : "", layer.maskBytes);
                if (blendSignature != "8BIM") {
                    unsupported.push_back(where + ": blend signature " + blendSignature);
                }
                additionalInfo(r, extraEnd, psb, &layer, unsupported, where);
                r.seek(extraEnd);
                layers.push_back(std::move(layer));
            }
            // Channel image data, layer by layer, channel by channel.
            for (std::size_t i = 0; i < layers.size() && r.ok(); ++i) {
                Layer &layer = layers[i];
                for (const auto &[id, length] : layer.channels) {
                    const std::size_t start = r.pos();
                    // The mask channel (-2) has the mask's rectangle; the probe keeps only colour and alpha.
                    if (id == -2 || id == -3 || layer.width() <= 0 || layer.height() <= 0 || depth != 8) {
                        r.seek(start + static_cast<std::size_t>(length));
                        continue;
                    }
                    if (layer.width() > maxDimension || layer.height() > maxDimension) {
                        layer.decodeProblems.push_back("rectangle exceeds the size limit");
                        r.seek(start + static_cast<std::size_t>(length));
                        continue;
                    }
                    std::string why;
                    auto pixels = channelData(r, static_cast<std::size_t>(length), layer.width(), layer.height(), psb, why);
                    if (pixels) {
                        layer.pixels[id] = std::move(*pixels);
                    } else {
                        layer.decodeProblems.push_back("channel " + std::to_string(id) + ": " + why);
                    }
                    r.seek(start + static_cast<std::size_t>(length));
                }
            }
        }
        r.seek(layerInfoEnd);
        // Global layer mask info and global additional information.
        if (r.pos() + 4 <= layerMaskEnd) {
            const std::uint32_t globalMask = r.u32("global mask length");
            r.skip(globalMask, "global mask");
            line("global layer mask info: %u bytes", globalMask);
        }
        additionalInfo(r, layerMaskEnd, psb, nullptr, unsupported, "global");
        r.seek(layerMaskEnd);
    }

    // Merged composite.
    std::vector<std::uint8_t> merged;
    if (r.ok() && depth == 8 && mode == 3) {
        const std::uint16_t compression = r.u16("merged compression");
        const std::size_t plane = static_cast<std::size_t>(width) * height;
        std::vector<std::vector<std::uint8_t>> planes;
        if (compression == 1) {
            std::vector<std::size_t> counts(static_cast<std::size_t>(height) * channels);
            for (auto &count : counts) {
                count = psb ? r.u32("merged RLE count") : r.u16("merged RLE count");
            }
            for (std::uint16_t c = 0; c < channels && r.ok(); ++c) {
                std::vector<std::uint8_t> out(plane);
                for (std::uint32_t y = 0; y < height && r.ok(); ++y) {
                    auto packed = r.bytes(counts[static_cast<std::size_t>(c) * height + y], "merged RLE row");
                    if (r.ok() && !unpackBits(packed, std::span(out).subspan(static_cast<std::size_t>(y) * width, width))) {
                        unsupported.push_back("merged composite: an RLE row does not decode");
                        break;
                    }
                }
                planes.push_back(std::move(out));
            }
        } else if (compression == 0) {
            for (std::uint16_t c = 0; c < channels && r.ok(); ++c) {
                auto raw = r.bytes(plane, "merged raw plane");
                planes.emplace_back(raw.begin(), raw.end());
            }
        } else {
            unsupported.push_back("merged composite compression " + std::to_string(compression));
        }
        if (r.ok() && planes.size() >= 3) {
            merged.resize(plane * 4);
            for (std::size_t p = 0; p < plane; ++p) {
                merged[p * 4] = planes[0][p];
                merged[p * 4 + 1] = planes[1][p];
                merged[p * 4 + 2] = planes[2][p];
                merged[p * 4 + 3] = planes.size() > 3 ? planes[3][p] : 255;
            }
        }
    }
    if (!r.ok()) {
        line("parse stopped: %s", r.error().c_str());
    }

    // What Stage 1 can render: normal-mode raster layers without masks; groups are flattened (their
    // members composite as if the group were absent, which is exact for pass-through groups of normal layers).
    auto created = archmage::commands::createDocument(archmage::commands::CommandEnvelope::make(
        "archmage.document.create", {{"width", width}, {"height", height}, {"background", "transparent"}}));
    if (!created) {
        std::fprintf(stderr, "psd_probe: %s\n", archmage::base::format(created.error()).c_str());
        return 2;
    }
    archmage::document::DocumentState doc = created->state;
    doc.layers.clear();
    int rendered = 0;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const Layer &layer = layers[i];
        const std::string name = layer.unicodeName.empty() ? layer.name : layer.unicodeName;
        const std::string where = "layer " + std::to_string(i) + " '" + name + "'";
        if (layer.section != 0) {
            unsupported.push_back(where + ": group marker (" +
                                  (layer.section == 3 ? std::string("end") : std::string("start")) +
                                  ", blend " + blendName(layer.blend) + "): groups are flattened");
            continue;
        }
        if (layer.blend != "norm") {
            unsupported.push_back(where + ": blend mode " + blendName(layer.blend) + " (Stage 1 renders normal only); not rendered");
            continue;
        }
        if (layer.maskBytes > 0) {
            unsupported.push_back(where + ": layer mask (M2); not rendered");
            continue;
        }
        if (!layer.decodeProblems.empty() || !layer.pixels.contains(0) || !layer.pixels.contains(1) ||
            !layer.pixels.contains(2)) {
            unsupported.push_back(where + ": channels not decoded" +
                                  (layer.decodeProblems.empty() ? std::string() : " (" + layer.decodeProblems.front() + ")"));
            continue;
        }
        auto target = std::make_shared<archmage::document::Layer>(*archmage::document::makeRasterLayer(doc, name));
        target->offset = {layer.left, layer.top};
        target->opacity = static_cast<float>(layer.opacity) / 255.0f;
        target->visible = !layer.hidden();
        auto content = std::make_shared<archmage::document::RasterContent>(*target->content);
        const auto w = layer.width();
        const auto h = layer.height();
        for (std::int64_t ty = 0; ty * 128 < h; ++ty) {
            for (std::int64_t tx = 0; tx * 128 < w; ++tx) {
                archmage::tiles::TileBuffer buffer(archmage::tiles::SampleType::UInt8, 4);
                for (int y = 0; y < 128 && ty * 128 + y < h; ++y) {
                    for (int x = 0; x < 128 && tx * 128 + x < w; ++x) {
                        const auto p = static_cast<std::size_t>((ty * 128 + y) * w + tx * 128 + x);
                        const std::int16_t ids[4] = {0, 1, 2, -1};
                        for (std::size_t c = 0; c < 4; ++c) {
                            const bool present = layer.pixels.contains(ids[c]);
                            const std::uint8_t v = present ? layer.pixels.at(ids[c])[p] : 255;
                            buffer.planeAs<std::uint8_t>(c)[static_cast<std::size_t>(y * 128 + x)] = v;
                        }
                    }
                }
                content->tiles.set({tx, ty}, std::move(buffer).seal());
            }
        }
        target->content = std::move(content);
        doc.layers.push_back(std::move(target));
        ++rendered;
    }
    line("rendered by the CPU compositor: %d of %zu layer records", rendered, layers.size());
    line("unsupported records: %zu", unsupported.size());
    for (const std::string &record : unsupported) {
        line("  unsupported: %s", record.c_str());
    }

    archmage::render::CpuEvaluator evaluator;
    archmage::render::RenderRequest request;
    request.document = doc.id;
    request.region = doc.canvasRect();
    auto composite = evaluator.render(doc, request);
    if (composite) {
        std::vector<std::uint8_t> rgba(composite->pixels.size());
        std::memcpy(rgba.data(), composite->pixels.data(), rgba.size());
        if (auto error = writePngRgba8(output / "archmage-flat.png", width, height, rgba); !error.empty()) {
            line("archmage-flat.png: %s", error.c_str());
        }
    }
    if (!merged.empty()) {
        if (auto error = writePngRgba8(output / "merged.png", width, height, merged); !error.empty()) {
            line("merged.png: %s", error.c_str());
        }
        line("merged composite written to merged.png");
    }
    const std::string text = report.str();
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::ofstream(output / "psd-probe.txt") << text;
    return r.ok() ? 0 : 1;
}
