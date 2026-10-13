#include "archmage/tiles/ChannelLayout.h"

#include <algorithm>

namespace archmage::tiles {

std::string_view channelName(ChannelSemantic semantic) {
    switch (semantic) {
    case ChannelSemantic::Red:
        return "red";
    case ChannelSemantic::Green:
        return "green";
    case ChannelSemantic::Blue:
        return "blue";
    case ChannelSemantic::Alpha:
        return "alpha";
    case ChannelSemantic::Gray:
        return "gray";
    case ChannelSemantic::Coverage:
        return "coverage";
    case ChannelSemantic::Cyan:
        return "cyan";
    case ChannelSemantic::Magenta:
        return "magenta";
    case ChannelSemantic::Yellow:
        return "yellow";
    case ChannelSemantic::Black:
        return "black";
    case ChannelSemantic::Spot:
        return "spot";
    case ChannelSemantic::Data:
        return "data";
    case ChannelSemantic::Index:
        return "index";
    }
    return "unknown";
}

std::optional<ChannelSemantic> channelFromName(std::string_view name) {
    for (int i = 0; i <= static_cast<int>(ChannelSemantic::Index); ++i) {
        const auto semantic = static_cast<ChannelSemantic>(i);
        if (channelName(semantic) == name) {
            return semantic;
        }
    }
    return std::nullopt;
}

ChannelLayout::ChannelLayout(std::vector<ChannelSemantic> planes, SampleType type)
    : planes_(std::move(planes)), type_(type) {}

ChannelLayout ChannelLayout::rgba(SampleType type) {
    return {{ChannelSemantic::Red, ChannelSemantic::Green, ChannelSemantic::Blue, ChannelSemantic::Alpha},
            type};
}

ChannelLayout ChannelLayout::coverage(SampleType type) { return {{ChannelSemantic::Coverage}, type}; }

std::optional<std::size_t> ChannelLayout::planeOf(ChannelSemantic semantic) const {
    const auto it = std::find(planes_.begin(), planes_.end(), semantic);
    if (it == planes_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(it - planes_.begin());
}

} // namespace archmage::tiles
