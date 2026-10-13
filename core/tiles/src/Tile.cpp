#include "archmage/tiles/Tile.h"

#include "archmage/tiles/ResourceCoordinator.h"

#include <atomic>
#include <cassert>
#include <cstring>

namespace archmage::tiles {
namespace {

std::atomic<std::uint64_t> gNextVersion{1};

} // namespace

Tile::Tile(SampleType type, std::size_t planes, bool uniform, std::vector<std::byte> data)
    : type_(type), planes_(planes), uniform_(uniform), version_(gNextVersion.fetch_add(1)),
      data_(std::move(data)) {
    if (!uniform_) {
        ResourceCoordinator::global().charge(Budget::Ram, data_.size());
    }
}

Tile::~Tile() {
    if (!uniform_) {
        ResourceCoordinator::global().release(Budget::Ram, data_.size());
    }
}

std::shared_ptr<const Tile> Tile::makeUniform(SampleType type, std::size_t planes,
                                              std::span<const std::byte> value) {
    assert(value.size() == planes * bytesPerSample(type));
    return std::shared_ptr<const Tile>(
        new Tile(type, planes, true, std::vector<std::byte>(value.begin(), value.end())));
}

std::shared_ptr<const Tile> Tile::makeZero(SampleType type, std::size_t planes) {
    return std::shared_ptr<const Tile>(
        new Tile(type, planes, true, std::vector<std::byte>(planes * bytesPerSample(type), std::byte{0})));
}

std::span<const std::byte> Tile::plane(std::size_t index) const {
    assert(!uniform_ && index < planes_);
    const std::size_t bytes = kTilePixels * bytesPerSample(type_);
    return {data_.data() + index * bytes, bytes};
}

std::span<const std::byte> Tile::uniformSample(std::size_t index) const {
    assert(uniform_ && index < planes_);
    const std::size_t bytes = bytesPerSample(type_);
    return {data_.data() + index * bytes, bytes};
}

float Tile::sample(std::size_t plane, int x, int y) const {
    const std::size_t bytes = bytesPerSample(type_);
    if (uniform_) {
        return loadSample(type_, data_.data() + plane * bytes);
    }
    const std::size_t offset =
        (plane * kTilePixels + static_cast<std::size_t>(y) * kTileSize + static_cast<std::size_t>(x)) * bytes;
    return loadSample(type_, data_.data() + offset);
}

bool Tile::sameContent(const Tile &other) const {
    if (type_ != other.type_ || planes_ != other.planes_) {
        return false;
    }
    if (uniform_ && other.uniform_) {
        return data_ == other.data_;
    }
    const std::size_t bytes = bytesPerSample(type_);
    for (std::size_t p = 0; p < planes_; ++p) {
        for (std::size_t i = 0; i < kTilePixels; ++i) {
            const std::byte *a =
                uniform_ ? data_.data() + p * bytes : data_.data() + (p * kTilePixels + i) * bytes;
            const std::byte *b = other.uniform_ ? other.data_.data() + p * bytes
                                                : other.data_.data() + (p * kTilePixels + i) * bytes;
            if (std::memcmp(a, b, bytes) != 0) {
                return false;
            }
        }
    }
    return true;
}

TileBuffer::TileBuffer(SampleType type, std::size_t planes)
    : type_(type), planes_(planes), data_(planes * kTilePixels * bytesPerSample(type), std::byte{0}) {}

TileBuffer::TileBuffer(const Tile &source)
    : type_(source.sampleType()), planes_(source.planeCount()),
      data_(source.planeCount() * kTilePixels * bytesPerSample(source.sampleType())) {
    const std::size_t bytes = bytesPerSample(type_);
    for (std::size_t p = 0; p < planes_; ++p) {
        std::byte *out = data_.data() + p * kTilePixels * bytes;
        if (source.isUniform()) {
            const std::span<const std::byte> value = source.uniformSample(p);
            for (std::size_t i = 0; i < kTilePixels; ++i) {
                std::memcpy(out + i * bytes, value.data(), bytes);
            }
        } else {
            const std::span<const std::byte> in = source.plane(p);
            std::memcpy(out, in.data(), in.size());
        }
    }
}

std::span<std::byte> TileBuffer::plane(std::size_t index) {
    const std::size_t bytes = kTilePixels * bytesPerSample(type_);
    return {data_.data() + index * bytes, bytes};
}

std::span<const std::byte> TileBuffer::plane(std::size_t index) const {
    const std::size_t bytes = kTilePixels * bytesPerSample(type_);
    return {data_.data() + index * bytes, bytes};
}

void TileBuffer::fill(std::size_t planeIndex, float value) {
    const std::size_t bytes = bytesPerSample(type_);
    std::byte sampleBytes[4];
    storeSample(type_, value, sampleBytes);
    std::byte *out = plane(planeIndex).data();
    for (std::size_t i = 0; i < kTilePixels; ++i) {
        std::memcpy(out + i * bytes, sampleBytes, bytes);
    }
}

float TileBuffer::sample(std::size_t planeIndex, int x, int y) const {
    const std::size_t bytes = bytesPerSample(type_);
    return loadSample(type_,
                      plane(planeIndex).data() +
                          (static_cast<std::size_t>(y) * kTileSize + static_cast<std::size_t>(x)) * bytes);
}

void TileBuffer::setSample(std::size_t planeIndex, int x, int y, float value) {
    const std::size_t bytes = bytesPerSample(type_);
    storeSample(type_, value,
                plane(planeIndex).data() +
                    (static_cast<std::size_t>(y) * kTileSize + static_cast<std::size_t>(x)) * bytes);
}

std::shared_ptr<const Tile> TileBuffer::seal() && {
    const std::size_t bytes = bytesPerSample(type_);
    bool uniform = true;
    for (std::size_t p = 0; p < planes_ && uniform; ++p) {
        const std::byte *first = data_.data() + p * kTilePixels * bytes;
        for (std::size_t i = 1; i < kTilePixels; ++i) {
            if (std::memcmp(first, first + i * bytes, bytes) != 0) {
                uniform = false;
                break;
            }
        }
    }
    if (uniform) {
        std::vector<std::byte> value(planes_ * bytes);
        for (std::size_t p = 0; p < planes_; ++p) {
            std::memcpy(value.data() + p * bytes, data_.data() + p * kTilePixels * bytes, bytes);
        }
        return std::shared_ptr<const Tile>(new Tile(type_, planes_, true, std::move(value)));
    }
    return std::shared_ptr<const Tile>(new Tile(type_, planes_, false, std::move(data_)));
}

} // namespace archmage::tiles
