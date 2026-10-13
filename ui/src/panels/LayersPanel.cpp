#include "archmage/ui/panels/LayersPanel.h"

#include "archmage/commands/Command.h"
#include "archmage/ui/Theme.h"
#include "archmage/ui/document/DocumentSession.h"
#include "archmage/ui/panels/LayersModel.h"

#include <QAction>
#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace archmage::ui {
namespace {

constexpr int kRowHeight = 40;

// Row geometry: [eye][lock][thumbnail][name], left to right.
QRect eyeRect(const QRect &row) { return {row.left() + 6, row.center().y() - 10, 20, 20}; }
QRect lockRect(const QRect &row) { return {row.left() + 28, row.center().y() - 10, 20, 20}; }
QRect thumbnailRect(const QRect &row) {
    return {row.left() + 54, row.center().y() - LayersModel::kThumbnailSize / 2, LayersModel::kThumbnailSize,
            LayersModel::kThumbnailSize};
}
QRect nameRect(const QRect &row) {
    const int left = thumbnailRect(row).right() + 10;
    return {left, row.top() + 4, std::max(10, row.right() - left - 6), row.height() - 8};
}

class LayerDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    // A fixed size, read without touching the model, so a long list lays out without computing thumbnails.
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override {
        return {option.rect.width() > 0 ? option.rect.width() : 200, kRowHeight};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        painter->save();
        const QRect row = option.rect;
        const QPalette &palette = option.palette;
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        painter->fillRect(row, selected ? palette.highlight() : palette.base());
        if (option.state.testFlag(QStyle::State_HasFocus)) {
            painter->setPen(QPen(palette.highlight().color().lighter(140), 1));
            painter->drawRect(row.adjusted(0, 0, -1, -1));
        }
        const bool visible = index.data(LayersModel::VisibleRole).toBool();
        const bool locked = index.data(LayersModel::LockedRole).toBool();
        theme::icon(visible ? QStringLiteral("visibility") : QStringLiteral("visibility-off"))
            .paint(painter, eyeRect(row), Qt::AlignCenter, visible ? QIcon::Normal : QIcon::Disabled);
        if (locked) {
            theme::icon(QStringLiteral("lock")).paint(painter, lockRect(row));
        }
        const QImage thumb = index.data(Qt::DecorationRole).value<QImage>();
        if (!thumb.isNull()) {
            painter->drawImage(thumbnailRect(row), thumb);
        }
        painter->setPen(QPen(palette.mid().color(), 1));
        painter->drawRect(thumbnailRect(row).adjusted(0, 0, -1, -1));
        QColor text = selected ? palette.highlightedText().color() : palette.text().color();
        if (!visible) {
            text = palette.color(QPalette::Disabled, QPalette::Text);
        }
        painter->setPen(text);
        const QRect name = nameRect(row);
        const QString label = option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight,
                                                            name.width());
        painter->drawText(name, Qt::AlignVCenter | Qt::AlignLeft, label);
        painter->restore();
    }

    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                     const QModelIndex &index) override {
        if (event->type() == QEvent::MouseButtonRelease) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                const QPoint at = mouse->position().toPoint();
                if (eyeRect(option.rect).contains(at)) {
                    model->setData(index, !index.data(LayersModel::VisibleRole).toBool(), LayersModel::VisibleRole);
                    return true;
                }
                if (lockRect(option.rect).contains(at)) {
                    model->setData(index, !index.data(LayersModel::LockedRole).toBool(), LayersModel::LockedRole);
                    return true;
                }
            }
        }
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            const QPoint at = mouse->position().toPoint();
            if (eyeRect(option.rect).contains(at) || lockRect(option.rect).contains(at)) {
                return true; // a toggle, not a selection or the start of a rename
            }
        }
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
        editor->setObjectName(QStringLiteral("archmageLayerNameEditor"));
        editor->setAccessibleName(QStringLiteral("Layer name"));
        return editor;
    }

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &) const override {
        editor->setGeometry(nameRect(option.rect));
    }
};

QToolButton *iconButton(QAction *action, QWidget *parent) {
    auto *button = new QToolButton(parent);
    button->setDefaultAction(action);
    button->setAutoRaise(true);
    button->setIconSize(QSize(20, 20));
    button->setAccessibleName(action->text().remove(QLatin1Char('&')));
    return button;
}

} // namespace

LayersPanel::LayersPanel(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("archmageLayersPanel"));
    model_ = new LayersModel(this);

    auto *opacityLabel = new QLabel(QStringLiteral("Opacity"), this);
    opacity_ = new QSlider(Qt::Horizontal, this);
    opacity_->setObjectName(QStringLiteral("archmageLayerOpacitySlider"));
    opacity_->setRange(0, 100);
    opacity_->setAccessibleName(QStringLiteral("Layer opacity"));
    opacity_->setToolTip(QStringLiteral("Opacity of the active layer"));
    opacityValue_ = new QSpinBox(this);
    opacityValue_->setObjectName(QStringLiteral("archmageLayerOpacityValue"));
    opacityValue_->setRange(0, 100);
    opacityValue_->setSuffix(QStringLiteral(" %"));
    opacityValue_->setAccessibleName(QStringLiteral("Layer opacity value"));
    opacityLabel->setBuddy(opacityValue_);
    auto *opacityRow = new QHBoxLayout;
    opacityRow->addWidget(opacityLabel);
    opacityRow->addWidget(opacity_, 1);
    opacityRow->addWidget(opacityValue_);

    list_ = new QListView(this);
    list_->setObjectName(QStringLiteral("archmageLayersList"));
    list_->setAccessibleName(QStringLiteral("Layers"));
    list_->setModel(model_);
    list_->setItemDelegate(new LayerDelegate(list_));
    list_->setUniformItemSizes(true);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    list_->setDragDropMode(QAbstractItemView::InternalMove);
    list_->setDefaultDropAction(Qt::MoveAction);
    list_->setDragEnabled(true);
    list_->setDropIndicatorShown(true);
    list_->installEventFilter(this);

    newLayer_ = new QAction(theme::icon(QStringLiteral("layer-add")), QStringLiteral("&New Layer"), this);
    newLayer_->setObjectName(QStringLiteral("layer.new"));
    duplicateLayer_ = new QAction(theme::icon(QStringLiteral("layer-duplicate")), QStringLiteral("&Duplicate Layer"), this);
    duplicateLayer_->setObjectName(QStringLiteral("layer.duplicate"));
    deleteLayer_ = new QAction(theme::icon(QStringLiteral("layer-delete")), QStringLiteral("De&lete Layer"), this);
    deleteLayer_->setObjectName(QStringLiteral("layer.delete"));
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    for (QAction *action : {newLayer_, duplicateLayer_, deleteLayer_}) {
        buttons->addWidget(iconButton(action, this));
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(opacityRow);
    layout->addWidget(list_, 1);
    layout->addLayout(buttons);

    connect(opacity_, &QSlider::sliderPressed, this, [this] { dragTransaction_ = base::TransactionId::generate(); });
    connect(opacity_, &QSlider::sliderReleased, this, [this] { dragTransaction_ = {}; });
    connect(opacity_, &QSlider::valueChanged, this, [this](int value) {
        if (syncing_) {
            return;
        }
        const QSignalBlocker block(opacityValue_);
        opacityValue_->setValue(value);
        submitOpacity(value);
    });
    connect(opacityValue_, &QSpinBox::valueChanged, this, [this](int value) {
        if (syncing_) {
            return;
        }
        const QSignalBlocker block(opacity_);
        opacity_->setValue(value);
        submitOpacity(value);
    });
    connect(model_, &QAbstractItemModel::modelReset, this, &LayersPanel::syncFromSession);
    connect(list_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current) {
                if (session_ != nullptr && current.isValid() && !syncing_) {
                    session_->setActiveLayer(model_->layerAt(current.row()));
                }
            });

    connect(newLayer_, &QAction::triggered, this, [this] {
        if (session_ == nullptr) {
            return;
        }
        std::vector<std::string> above;
        if (!session_->activeLayer().isNull()) {
            above.push_back(session_->activeLayer().toString()); // the new layer goes above the active one
        }
        session_->submit(commands::CommandEnvelope::make("archmage.layer.add", {}, above),
                         [session = session_](const commands::CommandResult &result) {
                             if (const auto id = base::LayerId::parse(result.result.value("layerId", std::string()))) {
                                 session->setActiveLayer(*id);
                             }
                         });
    });
    connect(duplicateLayer_, &QAction::triggered, this, [this] {
        if (session_ == nullptr || session_->activeLayer().isNull()) {
            return;
        }
        session_->submit(commands::CommandEnvelope::make("archmage.layer.duplicateInstance", {},
                                                         {session_->activeLayer().toString()}),
                         [session = session_](const commands::CommandResult &result) {
                             if (const auto id = base::LayerId::parse(result.result.value("layerId", std::string()))) {
                                 session->setActiveLayer(*id);
                             }
                         });
    });
    connect(deleteLayer_, &QAction::triggered, this, [this] {
        if (session_ != nullptr && !session_->activeLayer().isNull()) {
            session_->submit(
                commands::CommandEnvelope::make("archmage.layer.remove", {}, {session_->activeLayer().toString()}));
        }
    });
    setSession(nullptr);
}

LayersPanel::~LayersPanel() = default;

void LayersPanel::setSession(DocumentSession *session) {
    for (const auto &connection : sessionConnections_) {
        disconnect(connection);
    }
    sessionConnections_.clear();
    session_ = session;
    model_->setSession(session);
    if (session_ != nullptr) {
        sessionConnections_.append(
            connect(session_, &DocumentSession::revisionChanged, this, &LayersPanel::syncFromSession));
        sessionConnections_.append(
            connect(session_, &DocumentSession::activeLayerChanged, this, &LayersPanel::syncFromSession));
    }
    syncFromSession();
}

void LayersPanel::syncFromSession() {
    syncing_ = true;
    const QModelIndex active = session_ != nullptr ? model_->indexOf(session_->activeLayer()) : QModelIndex{};
    if (active.isValid()) {
        if (list_->currentIndex() != active) {
            list_->setCurrentIndex(active);
        }
        if (!opacity_->isSliderDown()) {
            const int percent = static_cast<int>(std::lround(active.data(LayersModel::OpacityRole).toDouble() * 100.0));
            opacity_->setValue(percent);
            opacityValue_->setValue(percent);
        }
    }
    syncing_ = false;
    updateActions();
}

void LayersPanel::updateActions() {
    const bool hasDocument = session_ != nullptr;
    const bool hasLayer = hasDocument && !session_->activeLayer().isNull();
    newLayer_->setEnabled(hasDocument);
    duplicateLayer_->setEnabled(hasLayer);
    deleteLayer_->setEnabled(hasLayer);
    opacity_->setEnabled(hasLayer);
    opacityValue_->setEnabled(hasLayer);
}

void LayersPanel::submitOpacity(int percent) {
    if (session_ == nullptr || session_->activeLayer().isNull()) {
        return;
    }
    auto envelope = commands::CommandEnvelope::make("archmage.layer.setOpacity", {{"opacity", percent / 100.0}},
                                                    {session_->activeLayer().toString()});
    if (opacity_->isSliderDown() && !dragTransaction_.isNull()) {
        envelope.transactionId = dragTransaction_; // one history item per drag
        envelope.transactionLabel = "Layer Opacity";
    }
    session_->submit(std::move(envelope));
}

void LayersPanel::renameActiveLayer() {
    if (session_ == nullptr) {
        return;
    }
    const QModelIndex active = model_->indexOf(session_->activeLayer());
    if (active.isValid()) {
        list_->setFocus(Qt::ShortcutFocusReason);
        list_->setCurrentIndex(active);
        list_->edit(active);
    }
}

void LayersPanel::toggleVisibility(int row) {
    const QModelIndex index = model_->index(row);
    if (index.isValid()) {
        model_->setData(index, !index.data(LayersModel::VisibleRole).toBool(), LayersModel::VisibleRole);
    }
}

void LayersPanel::toggleLock(int row) {
    const QModelIndex index = model_->index(row);
    if (index.isValid()) {
        model_->setData(index, !index.data(LayersModel::LockedRole).toBool(), LayersModel::LockedRole);
    }
}

bool LayersPanel::eventFilter(QObject *watched, QEvent *event) {
    if (watched == list_ && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        // While a name is being edited the keys go to the editor, not here.
        if (key->key() == Qt::Key_Space && key->modifiers() == Qt::NoModifier) {
            toggleVisibility(list_->currentIndex().row()); // Space: show or hide the current layer (S1-7.9)
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace archmage::ui
