#pragma once

#include "archmage/base/Id.h"

#include <QWidget>

class QAction;
class QListView;
class QSlider;
class QSpinBox;
class QToolButton;

namespace archmage::ui {

class DocumentSession;
class LayersModel;

/// The Layers panel (plan S1-7.3, S1-7.9; spec §13.1 LAY-02, LAY-03 subsets): the stack of the current
/// document, top layer first, with inline rename (double-click or F2), drag reorder, visibility and lock
/// toggles (click the eye or the lock, or Space for visibility), add, duplicate and delete, and an
/// opacity slider. A slider drag is one history item: every change during the drag carries the same
/// transaction (spec §9.2).
///
/// Rows are painted by a delegate; the panel creates no widget per layer.
///
/// Thread safety: GUI thread only.
class LayersPanel final : public QWidget {
    Q_OBJECT

public:
    explicit LayersPanel(QWidget *parent = nullptr);
    ~LayersPanel() override;

    /// Shows the layers of `session` (null: no document).
    void setSession(DocumentSession *session);

    [[nodiscard]] LayersModel &model() { return *model_; }
    [[nodiscard]] QListView &list() { return *list_; }
    [[nodiscard]] QSlider &opacitySlider() { return *opacity_; }
    [[nodiscard]] QSpinBox &opacityValue() { return *opacityValue_; }
    [[nodiscard]] QAction *newLayerAction() const { return newLayer_; }
    [[nodiscard]] QAction *duplicateLayerAction() const { return duplicateLayer_; }
    [[nodiscard]] QAction *deleteLayerAction() const { return deleteLayer_; }

    /// Starts inline rename of the active layer.
    void renameActiveLayer();
    void toggleVisibility(int row);
    void toggleLock(int row);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void syncFromSession();
    void submitOpacity(int percent);
    void updateActions();

    DocumentSession *session_ = nullptr;
    LayersModel *model_ = nullptr;
    QListView *list_ = nullptr;
    QSlider *opacity_ = nullptr;
    QSpinBox *opacityValue_ = nullptr;
    QAction *newLayer_ = nullptr;
    QAction *duplicateLayer_ = nullptr;
    QAction *deleteLayer_ = nullptr;
    base::TransactionId dragTransaction_;
    bool syncing_ = false;
    QList<QMetaObject::Connection> sessionConnections_;
};

} // namespace archmage::ui
