#include "archmage/tiles/Tile.h"

#include "archmage/tiles/ResourceCoordinator.h"
#include "archmage/tiles/TileSpool.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace archmage::tiles {
namespace {

std::atomic<std::uint64_t> gNextVersion{1};
std::atomic<std::uint64_t> gNextUse{1};

} // namespace

Tile::Pin::Pin(Pin &&other) noexcept : tile_(std::exchange(other.tile_, nullptr)) {}

Tile::Pin &Tile::Pin::operator=(Pin &&other) noexcept {
    if (this != &other) {
        if (tile_ != nullptr) {
            tile_->unpin();
        }
        tile_ = std::exchange(other.tile_, nullptr);
    }
    return *this;
}

Tile::Pin::~Pin() {
    if (tile_ != nullptr) {
        tile_->unpin();
    }
}

Tile::Tile(SampleType type, std::size_t planes, bool uniform, std::vector<std::byte> data)
    : type_(type), planes_(planes), uniform_(uniform), version_(gNextVersion.fetch_add(1)),
      denseSize_(uniform ? 0 : data.size()), data_(std::move(data)) {
    if (!uniform_) {
        ResourceCoordinator::global().makeRoom(Budget::Ram, denseSize_);
        ResourceCoordinator::global().charge(Budget::Ram, denseSize_);
    }
}

Tile::~Tile() {
    if (!uniform_ && resident_.load(std::memory_order_acquire)) {
        ResourceCoordinator::global().release(Budget::Ram, denseSize_);
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

base::Result<Tile::Pin> Tile::pin() const {
    if (uniform_) {
        return Pin();
    }
    lastUse_.store(gNextUse.fetch_add(1, std::memory_order_relaxed), std::memory_order_relaxed);
    {
        const std::lock_guard lock(mutex_);
        if (resident_.load(std::memory_order_relaxed)) {
            pins_.fetch_add(1, std::memory_order_relaxed);
            return Pin(this);
        }
    }
    // Make room before taking this tile's lock: evictors try other tiles' locks under the coordinator's.
    ResourceCoordinator::global().makeRoom(Budget::Ram, denseSize_);
    const std::lock_guard lock(mutex_);
    if (!resident_.load(std::memory_order_relaxed)) {
        std::vector<std::byte> data(denseSize_);
        if (auto status = source_->load(spoolPlanes_, type_, data); !status) {
            return base::unexpected(std::move(status).error());
        }
        data_ = std::move(data);
        ResourceCoordinator::global().charge(Budget::Ram, denseSize_);
        resident_.store(true, std::memory_order_release);
        TileSpool::global().noteReload();
    }
    pins_.fetch_add(1, std::memory_order_relaxed);
    return Pin(this);
}

void Tile::unpin() const { pins_.fetch_sub(1, std::memory_order_release); }

std::span<const std::byte> Tile::plane(std::size_t index) const {
    assert(!uniform_ && index < planes_ && resident_.load(std::memory_order_relaxed));
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
    const auto pinned = pin();
    if (!pinned) {
        return std::numeric_limits<float>::quiet_NaN();
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
    const auto mine = pin();
    const auto theirs = other.pin();
    if (!mine || !theirs) {
        return false;
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

void Tile::setSpool(std::shared_ptr<SpoolSource> source, std::vector<SpoolPlane> planes) const {
    if (uniform_) {
        return;
    }
    const std::lock_guard lock(mutex_);
    if (!source_) {
        source_ = std::move(source);
        spoolPlanes_ = std::move(planes);
    }
}

base::Status Tile::detachSpool() const {
    if (uniform_) {
        return {};
    }
    auto pinned = pin();
    if (!pinned) {
        return base::unexpected(std::move(pinned).error());
    }
    const std::lock_guard lock(mutex_);
    source_.reset();
    spoolPlanes_.clear();
    return {};
}

bool Tile::isSpooled() const {
    const std::lock_guard lock(mutex_);
    return source_ != nullptr;
}

const SpoolSource *Tile::spoolSource() const {
    const std::lock_guard lock(mutex_);
    return source_.get();
}

std::vector<SpoolPlane> Tile::spoolPlanes() const {
    const std::lock_guard lock(mutex_);
    return spoolPlanes_;
}

std::uint64_t Tile::evict() const {
    if (uniform_) {
        return 0;
    }
    const std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock() || !source_ || !resident_.load(std::memory_order_relaxed) ||
        pins_.load(std::memory_order_acquire) != 0) {
        return 0;
    }
    std::vector<std::byte>().swap(data_);
    resident_.store(false, std::memory_order_release);
    ResourceCoordinator::global().release(Budget::Ram, denseSize_);
    return denseSize_;
}

TileBuffer::TileBuffer(SampleType type, std::size_t planes)
    : type_(type), planes_(planes), data_(planes * kTilePixels * bytesPerSample(type), std::byte{0}) {}

base::Result<TileBuffer> TileBuffer::copyOf(const Tile &source) {
    auto pinned = source.pin();
    if (!pinned) {
        return base::unexpected(std::move(pinned).error());
    }
    TileBuffer copy(source.sampleType(), source.planeCount());
    const std::size_t bytes = bytesPerSample(copy.type_);
    for (std::size_t p = 0; p < copy.planes_; ++p) {
        std::byte *out = copy.data_.data() + p * kTilePixels * bytes;
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
    return copy;
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
