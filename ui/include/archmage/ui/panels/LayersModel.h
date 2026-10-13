#pragma once

#include "archmage/base/Id.h"
#include "archmage/document/Document.h"

#include <QAbstractListModel>
#include <QImage>

#include <list>
#include <memory>
#include <string>
#include <unordered_map>

namespace archmage::ui {

class DocumentSession;

/// The current document's layer stack for the Layers panel (plan S1-7.3, spec §13.1): row 0 is the top
/// layer. The model reads the session's latest revision and never changes the document directly: rename,
/// visibility, lock and reorder become commands, and the rows update when the resulting revision arrives.
///
/// Thumbnails are computed only when a view asks for a row it draws and are kept in a cache of at most
/// kThumbnailCacheLimit entries keyed by layer content, so a document with thousands of layers costs what
/// is on screen (spec §13.1: no per-layer widgets, bounded thumbnails).
///
/// Thread safety: GUI thread only.
class LayersModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role { LayerIdRole = Qt::UserRole + 1, VisibleRole, LockedRole, OpacityRole };
    static constexpr int kThumbnailSize = 32; // logical pixels; rendered at twice that for high-density screens
    static constexpr std::size_t kThumbnailCacheLimit = 256;

    explicit LayersModel(QObject *parent = nullptr);

    void setSession(DocumentSession *session);
    [[nodiscard]] DocumentSession *session() const { return session_; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    /// Submits a reorder command and returns false, so the view does not remove the dragged row itself:
    /// the new order arrives with the revision.
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override;

    [[nodiscard]] QModelIndex indexOf(base::LayerId layer) const;
    [[nodiscard]] base::LayerId layerAt(int row) const;
    [[nodiscard]] const document::Layer *layer(int row) const;
    /// Moves `layer` so that it lands before view row `row` (0 to rowCount()); false when nothing moves.
    bool moveLayerToRow(base::LayerId layer, int row);

    [[nodiscard]] std::size_t thumbnailsCached() const { return cache_.size(); }
    [[nodiscard]] std::uint64_t thumbnailsRendered() const { return rendered_; }

private:
    void onRevisionChanged();
    [[nodiscard]] QImage thumbnail(const document::Layer &layer) const;

    DocumentSession *session_ = nullptr;
    QMetaObject::Connection revisionConnection_;
    std::shared_ptr<const document::DocumentState> state_;

    struct Cached {
        QImage image;
        std::list<std::string>::iterator lru;
    };
    mutable std::unordered_map<std::string, Cached> cache_;
    mutable std::list<std::string> lru_;
    mutable std::uint64_t rendered_ = 0;
};

} // namespace archmage::ui
