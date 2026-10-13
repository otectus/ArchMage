#include "archmage/ui/panels/LayersModel.h"

#include "archmage/commands/Command.h"
#include "archmage/ui/document/DocumentSession.h"

#include <QMimeData>

#include <algorithm>
#include <cmath>
#include <map>

namespace archmage::ui {
namespace {

constexpr const char *kLayerMimeType = "application/x-archmage-layer-id";

// Straight RGBA of one pixel of a tile (pinned by the caller when dense).
void readStraight(const tiles::Tile &tile, const std::size_t planes[4], int x, int y, float out[4]) {
    const tiles::SampleType type = tile.sampleType();
    const std::size_t bytes = tiles::bytesPerSample(type);
    for (int c = 0; c < 4; ++c) {
        const std::byte *sample = tile.isUniform()
                                      ? tile.uniformSample(planes[c]).data()
                                      : tile.plane(planes[c]).data() +
                                            (static_cast<std::size_t>(y) * tiles::kTileSize + static_cast<std::size_t>(x)) * bytes;
        out[c] = tiles::loadSample(type, sample);
    }
}

} // namespace

LayersModel::LayersModel(QObject *parent) : QAbstractListModel(parent) {}

void LayersModel::setSession(DocumentSession *session) {
    if (session == session_) {
        return;
    }
    beginResetModel();
    disconnect(revisionConnection_);
    session_ = session;
    state_ = session_ != nullptr ? session_->revision()->state : nullptr;
    if (session_ != nullptr) {
        revisionConnection_ =
            connect(session_, &DocumentSession::revisionChanged, this, &LayersModel::onRevisionChanged);
    }
    endResetModel();
}

void LayersModel::onRevisionChanged() {
    const auto next = session_->revision()->state;
    const bool sameStack = state_ && state_->layers.size() == next->layers.size() &&
                           std::equal(state_->layers.begin(), state_->layers.end(), next->layers.begin(),
                                      [](const auto &a, const auto &b) { return a->id == b->id; });
    if (sameStack) {
        state_ = next;
        if (!state_->layers.empty()) {
            emit dataChanged(index(0), index(rowCount() - 1));
        }
        return;
    }
    beginResetModel();
    state_ = next;
    endResetModel();
}

int LayersModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() || !state_ ? 0 : static_cast<int>(state_->layers.size());
}

const document::Layer *LayersModel::layer(int row) const {
    if (!state_ || row < 0 || row >= rowCount()) {
        return nullptr;
    }
    return state_->layers[state_->layers.size() - 1 - static_cast<std::size_t>(row)].get();
}

base::LayerId LayersModel::layerAt(int row) const {
    const document::Layer *found = layer(row);
    return found != nullptr ? found->id : base::LayerId{};
}

QModelIndex LayersModel::indexOf(base::LayerId id) const {
    if (!state_) {
        return {};
    }
    const auto stackIndex = state_->indexOf(id);
    return stackIndex ? index(rowCount() - 1 - static_cast<int>(*stackIndex)) : QModelIndex{};
}

QVariant LayersModel::data(const QModelIndex &index, int role) const {
    const document::Layer *found = layer(index.row());
    if (found == nullptr) {
        return {};
    }
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
    case Qt::ToolTipRole:
        return QString::fromStdString(found->name);
    case Qt::AccessibleTextRole:
        return QStringLiteral("%1, %2%3, opacity %4 %")
            .arg(QString::fromStdString(found->name), found->visible ? QStringLiteral("visible") : QStringLiteral("hidden"),
                 found->locked ? QStringLiteral(", locked") : QString())
            .arg(std::lround(found->opacity * 100.0f));
    case Qt::DecorationRole:
        return thumbnail(*found);
    case LayerIdRole:
        return QString::fromStdString(found->id.toString());
    case VisibleRole:
        return found->visible;
    case LockedRole:
        return found->locked;
    case OpacityRole:
        return static_cast<double>(found->opacity);
    default:
        return {};
    }
}

bool LayersModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    const document::Layer *found = layer(index.row());
    if (found == nullptr || session_ == nullptr) {
        return false;
    }
    const std::vector<std::string> target{found->id.toString()};
    switch (role) {
    case Qt::EditRole: {
        const QString name = value.toString().trimmed();
        if (name.isEmpty() || name.toStdString() == found->name) {
            return false;
        }
        session_->submit(commands::CommandEnvelope::make("archmage.layer.rename", {{"name", name.toStdString()}}, target));
        return true;
    }
    case VisibleRole:
        session_->submit(commands::CommandEnvelope::make("archmage.layer.setVisible", {{"visible", value.toBool()}}, target));
        return true;
    case LockedRole:
        session_->submit(commands::CommandEnvelope::make("archmage.layer.setLocked", {{"locked", value.toBool()}}, target));
        return true;
    default:
        return false;
    }
}

Qt::ItemFlags LayersModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) {
        return Qt::ItemIsDropEnabled;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled;
}

QStringList LayersModel::mimeTypes() const { return {QString::fromLatin1(kLayerMimeType)}; }

QMimeData *LayersModel::mimeData(const QModelIndexList &indexes) const {
    if (indexes.isEmpty()) {
        return nullptr;
    }
    auto *mime = new QMimeData;
    mime->setData(QString::fromLatin1(kLayerMimeType), layerAt(indexes.front().row()).toString().c_str());
    return mime;
}

bool LayersModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int, const QModelIndex &parent) {
    if (action != Qt::MoveAction || data == nullptr || !data->hasFormat(QString::fromLatin1(kLayerMimeType))) {
        return false;
    }
    const auto id = base::LayerId::parse(data->data(QString::fromLatin1(kLayerMimeType)).toStdString());
    if (!id) {
        return false;
    }
    if (row < 0) {
        row = parent.isValid() ? parent.row() : rowCount(); // dropped onto a row, or below the last one
    }
    moveLayerToRow(*id, row);
    return false; // the reorder arrives as a revision; the view must not remove the dragged row
}

bool LayersModel::moveLayerToRow(base::LayerId id, int row) {
    if (!state_ || session_ == nullptr) {
        return false;
    }
    const auto stackIndex = state_->indexOf(id);
    if (!stackIndex) {
        return false;
    }
    const int count = rowCount();
    const int from = count - 1 - static_cast<int>(*stackIndex);
    int to = std::clamp(row, 0, count);
    if (to > from) {
        --to; // the dragged row leaves its place first
    }
    const int targetStack = count - 1 - to;
    if (targetStack == static_cast<int>(*stackIndex)) {
        return false;
    }
    session_->submit(
        commands::CommandEnvelope::make("archmage.layer.reorder", {{"index", targetStack}}, {id.toString()}));
    return true;
}

QImage LayersModel::thumbnail(const document::Layer &layer) const {
    const std::string key = layer.content->id.toString() + ':' + std::to_string(layer.content->tiles.generation()) +
                            ':' + std::to_string(layer.offset.x) + ',' + std::to_string(layer.offset.y) + ':' +
                            std::to_string(state_->width) + 'x' + std::to_string(state_->height);
    if (const auto it = cache_.find(key); it != cache_.end()) {
        lru_.splice(lru_.begin(), lru_, it->second.lru);
        return it->second.image;
    }

    // The layer alone, over a checkerboard, scaled to fit while keeping the canvas aspect ratio.
    constexpr int kPixels = kThumbnailSize * 2;
    QImage image(kPixels, kPixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    const double scale = std::min(static_cast<double>(kPixels) / static_cast<double>(state_->width),
                                  static_cast<double>(kPixels) / static_cast<double>(state_->height));
    const int width = std::max(1, static_cast<int>(std::lround(static_cast<double>(state_->width) * scale)));
    const int height = std::max(1, static_cast<int>(std::lround(static_cast<double>(state_->height) * scale)));
    const int left = (kPixels - width) / 2;
    const int top = (kPixels - height) / 2;
    const tiles::TileMap &map = layer.content->tiles;
    const tiles::ChannelLayout &layout = map.layout();
    const std::size_t planes[4] = {
        layout.planeOf(tiles::ChannelSemantic::Red).value_or(0), layout.planeOf(tiles::ChannelSemantic::Green).value_or(1),
        layout.planeOf(tiles::ChannelSemantic::Blue).value_or(2), layout.planeOf(tiles::ChannelSemantic::Alpha).value_or(3)};
    std::map<tiles::TileCoord, std::pair<std::shared_ptr<const tiles::Tile>, tiles::Tile::Pin>> pinned;
    for (int py = 0; py < height; ++py) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(top + py));
        for (int px = 0; px < width; ++px) {
            const bool light = (((px / 4) + (py / 4)) & 1) == 0;
            const float checker = light ? 0.80f : 0.60f;
            const auto lx = static_cast<std::int64_t>(std::floor((px + 0.5) / scale)) - layer.offset.x;
            const auto ly = static_cast<std::int64_t>(std::floor((py + 0.5) / scale)) - layer.offset.y;
            const tiles::TileCoord coord = tiles::tileContaining({lx, ly});
            auto it = pinned.find(coord);
            if (it == pinned.end()) {
                auto tile = map.at(coord);
                auto pin = tile->pin();
                it = pinned.emplace(coord, std::make_pair(tile, pin ? std::move(*pin) : tiles::Tile::Pin{})).first;
                if (!pin) {
                    it->second.first = map.defaultTile(); // unreadable: draw it as the default
                }
            }
            float rgba[4];
            readStraight(*it->second.first, planes, static_cast<int>(lx - coord.tx * tiles::kTileSize),
                         static_cast<int>(ly - coord.ty * tiles::kTileSize), rgba);
            const float alpha = std::clamp(rgba[3], 0.0f, 1.0f);
            const auto mix = [&](float c) {
                return static_cast<int>(std::lround(std::clamp(std::clamp(c, 0.0f, 1.0f) * alpha + checker * (1.0f - alpha), 0.0f, 1.0f) * 255.0f));
            };
            row[left + px] = qRgb(mix(rgba[0]), mix(rgba[1]), mix(rgba[2]));
        }
    }
    image.setDevicePixelRatio(2.0);
    ++rendered_;

    lru_.push_front(key);
    cache_.emplace(key, Cached{image, lru_.begin()});
    while (cache_.size() > kThumbnailCacheLimit) {
        cache_.erase(lru_.back());
        lru_.pop_back();
    }
    return image;
}

} // namespace archmage::ui
