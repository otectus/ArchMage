#include "Payloads.h"

#include <array>
#include <cmath>

namespace archmage::native {
namespace {

using nlohmann::json;

// Reads typed fields from an untrusted object, remembering the first failure.
class Reader {
public:
    Reader(const json &payload, std::string_view type, std::string_view id)
        : payload_(payload), where_(std::string(type) + " " + std::string(id)) {
        if (!payload_.is_object()) {
            fail("payload is not an object");
        }
    }

    enum class Kind { String, Boolean, Integer, Number, Object, Array };

    const json *field(std::string_view key, Kind kind) {
        if (!ok_) {
            return nullptr;
        }
        const auto it = payload_.find(key);
        bool matches = false;
        if (it != payload_.end()) {
            switch (kind) {
            case Kind::String:
                matches = it->is_string();
                break;
            case Kind::Boolean:
                matches = it->is_boolean();
                break;
            case Kind::Integer:
                matches = it->is_number_integer();
                break;
            case Kind::Number:
                matches = it->is_number();
                break;
            case Kind::Object:
                matches = it->is_object();
                break;
            case Kind::Array:
                matches = it->is_array();
                break;
            }
        }
        if (!matches) {
            fail("field '" + std::string(key) + "' is missing or has the wrong type");
            return nullptr;
        }
        return &*it;
    }

    std::string string(std::string_view key) {
        const json *value = field(key, Kind::String);
        return value != nullptr ? value->get<std::string>() : std::string();
    }
    bool boolean(std::string_view key) {
        const json *value = field(key, Kind::Boolean);
        return value != nullptr && value->get<bool>();
    }
    std::int64_t integer(std::string_view key, std::int64_t min, std::int64_t max) {
        const json *value = field(key, Kind::Integer);
        if (value == nullptr) {
            return min;
        }
        if (value->is_number_unsigned() && value->get<std::uint64_t>() > static_cast<std::uint64_t>(max)) {
            fail("field '" + std::string(key) + "' is out of range");
            return min;
        }
        const auto v = value->get<std::int64_t>();
        if (v < min || v > max) {
            fail("field '" + std::string(key) + "' is out of range");
            return min;
        }
        return v;
    }
    double number(std::string_view key, double min, double max) {
        const json *value = field(key, Kind::Number);
        if (value == nullptr) {
            return min;
        }
        const double v = value->get<double>();
        if (!std::isfinite(v) || v < min || v > max) {
            fail("field '" + std::string(key) + "' is out of range");
            return min;
        }
        return v;
    }
    json object(std::string_view key) {
        const json *value = field(key, Kind::Object);
        return value != nullptr ? *value : json::object();
    }
    std::vector<std::string> strings(std::string_view key) {
        std::vector<std::string> out;
        const json *value = field(key, Kind::Array);
        if (value == nullptr) {
            return out;
        }
        for (const json &item : *value) {
            if (!item.is_string()) {
                fail("field '" + std::string(key) + "' holds a non-string");
                return {};
            }
            out.push_back(item.get<std::string>());
        }
        return out;
    }

    void fail(std::string what) {
        if (ok_) {
            ok_ = false;
            error_ = damaged("file.invalid_object", where_ + ": " + std::move(what));
        }
    }
    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] const base::Error &error() const { return error_; }

private:
    const json &payload_;
    std::string where_;
    bool ok_ = true;
    base::Error error_;
};

} // namespace

bool isKnownManifestKey(std::string_view key) {
    static constexpr std::array kKeys{manifest::kAppId,      manifest::kSchemaMajor, manifest::kSchemaMinor,
                                      manifest::kRequiredCapabilities, manifest::kTileSize,
                                      manifest::kDocumentId, manifest::kDerivedFrom,
                                      manifest::kCurrentRevision, manifest::kColorPolicy, manifest::kWriter,
                                      manifest::kSavedAt};
    for (const std::string_view known : kKeys) {
        if (known == key) {
            return true;
        }
    }
    return false;
}

base::Error damaged(std::string code, std::string detail) {
    return {base::ErrorCategory::DamagedInput, std::move(code), "The document file is damaged", std::move(detail)};
}

std::string hexOf(std::span<const std::byte> bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::byte b : bytes) {
        out.push_back(kHex[static_cast<unsigned>(b) >> 4u]);
        out.push_back(kHex[static_cast<unsigned>(b) & 15u]);
    }
    return out;
}

std::optional<std::vector<std::byte>> bytesOfHex(std::string_view hex) {
    if (hex.size() % 2 != 0) {
        return std::nullopt;
    }
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        return -1;
    };
    std::vector<std::byte> out(hex.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int hi = nibble(hex[2 * i]);
        const int lo = nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        out[i] = static_cast<std::byte>(hi << 4 | lo);
    }
    return out;
}

json colorPolicyPayload(const color::ColorPolicy &policy, const std::string &profileHash) {
    return {{"profile", profileHash},
            {"profileDescription", policy.profile->description()},
            {"blendPolicy", std::string(color::blendPolicyName(policy.blendPolicy))}};
}

json documentPayload(const document::DocumentState &state, const std::string &profileHash) {
    return {{"title", state.title},
            {"width", state.width},
            {"height", state.height},
            {"resolution", state.resolution},
            {"unit", std::string(document::lengthUnitName(state.unit))},
            {"sampleType", std::string(tiles::sampleTypeName(state.sampleType))},
            {"colorPolicy", colorPolicyPayload(state.colorPolicy, profileHash)},
            {"capabilities", state.capabilities},
            {"extensions", state.extensions}};
}

json layerPayload(const document::Layer &layer) {
    return {{"name", layer.name},
            {"visible", layer.visible},
            {"locked", layer.locked},
            {"opacity", layer.opacity},
            {"blendMode", std::string(document::blendModeName(layer.blendMode))},
            {"offset", {layer.offset.x, layer.offset.y}},
            {"extensions", layer.extensions}};
}

json rasterPayload(const tiles::TileMap &map) {
    const tiles::ChannelLayout &layout = map.layout();
    json planes = json::array();
    json defaults = json::array();
    for (std::size_t p = 0; p < layout.planeCount(); ++p) {
        planes.push_back(std::string(tiles::channelName(layout.semantic(p))));
        defaults.push_back(hexOf(map.defaultTile()->uniformSample(p)));
    }
    return {{"sampleType", std::string(tiles::sampleTypeName(layout.sampleType()))},
            {"planes", planes},
            {"tileSize", tiles::kTileSize},
            {"defaultSamples", defaults}};
}

base::Result<DocumentFields> parseDocument(const json &payload, std::string_view id) {
    Reader read(payload, kDocumentType, id);
    DocumentFields fields;
    fields.title = read.string("title");
    fields.width = read.integer("width", 1, document::kMaxDimension);
    fields.height = read.integer("height", 1, document::kMaxDimension);
    fields.resolution = read.number("resolution", 1.0e-3, 1.0e6);
    const std::string unit = read.string("unit");
    const std::string sampleType = read.string("sampleType");
    const json policy = read.object("colorPolicy");
    fields.capabilities = read.strings("capabilities");
    fields.extensions = read.object("extensions");
    if (read.ok()) {
        Reader policyRead(policy, "colorPolicy of document", id);
        fields.profileHash = policyRead.string("profile");
        const std::string blend = policyRead.string("blendPolicy");
        if (!policyRead.ok()) {
            return base::unexpected(policyRead.error());
        }
        const auto blendPolicy = color::blendPolicyFromName(blend);
        if (!blendPolicy) {
            read.fail("unknown blend policy '" + blend + "'");
        } else {
            fields.blendPolicy = *blendPolicy;
        }
    }
    const auto parsedUnit = document::lengthUnitFromName(unit);
    const auto parsedType = tiles::sampleTypeFromName(sampleType);
    if (read.ok() && (!parsedUnit || !parsedType)) {
        read.fail("unknown unit or sample type");
    }
    if (!read.ok()) {
        return base::unexpected(read.error());
    }
    fields.unit = *parsedUnit;
    fields.sampleType = *parsedType;
    return fields;
}

base::Result<document::Layer> parseLayer(const json &payload, std::string_view id) {
    Reader read(payload, kRasterLayerType, id);
    document::Layer layer;
    layer.name = read.string("name");
    layer.visible = read.boolean("visible");
    layer.locked = read.boolean("locked");
    layer.opacity = static_cast<float>(read.number("opacity", 0.0, 1.0));
    const std::string blend = read.string("blendMode");
    layer.extensions = read.object("extensions");
    const json *offset = read.field("offset", Reader::Kind::Array);
    if (offset != nullptr) {
        if (offset->size() != 2 || !(*offset)[0].is_number_integer() || !(*offset)[1].is_number_integer()) {
            read.fail("field 'offset' is not two integers");
        } else {
            layer.offset = {(*offset)[0].get<std::int64_t>(), (*offset)[1].get<std::int64_t>()};
        }
    }
    const auto mode = document::blendModeFromName(blend);
    if (read.ok() && !mode) {
        return base::fail(base::ErrorCategory::UnsupportedInput, "file.unsupported_blend_mode",
                          "The document uses a blend mode this version does not support",
                          "layer.raster " + std::string(id) + ": '" + blend + "'");
    }
    if (!read.ok()) {
        return base::unexpected(read.error());
    }
    layer.blendMode = *mode;
    return layer;
}

base::Result<RasterFields> parseRaster(const json &payload, std::string_view id) {
    Reader read(payload, kRasterType, id);
    const std::string sampleType = read.string("sampleType");
    const std::vector<std::string> planeNames = read.strings("planes");
    const std::int64_t tileSize = read.integer("tileSize", 1, 1 << 16);
    const std::vector<std::string> defaults = read.strings("defaultSamples");
    if (!read.ok()) {
        return base::unexpected(read.error());
    }
    if (tileSize != tiles::kTileSize) {
        return base::fail(base::ErrorCategory::UnsupportedInput, "file.unsupported_tile_size",
                          "The document uses a tile size this version does not support",
                          "raster " + std::string(id) + ": " + std::to_string(tileSize));
    }
    const auto type = tiles::sampleTypeFromName(sampleType);
    if (!type || planeNames.empty() || planeNames.size() > 64 || defaults.size() != planeNames.size()) {
        return base::unexpected(damaged("file.invalid_object", "raster " + std::string(id) + ": layout"));
    }
    std::vector<tiles::ChannelSemantic> planes;
    RasterFields fields;
    for (std::size_t p = 0; p < planeNames.size(); ++p) {
        const auto semantic = tiles::channelFromName(planeNames[p]);
        auto sample = bytesOfHex(defaults[p]);
        if (!semantic || !sample || sample->size() != tiles::bytesPerSample(*type)) {
            return base::unexpected(damaged("file.invalid_object", "raster " + std::string(id) + ": plane " +
                                                                       std::to_string(p)));
        }
        planes.push_back(*semantic);
        fields.defaultSamples.push_back(std::move(*sample));
    }
    fields.layout = tiles::ChannelLayout(std::move(planes), *type);
    return fields;
}

} // namespace archmage::native
