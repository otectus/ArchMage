#include "archmage/ui/document/DocumentSession.h"

#include <QMetaObject>

namespace archmage::ui {

DocumentSession::DocumentSession(document::DocumentState initial, const QString &initialLabel,
                                 std::shared_ptr<render::CpuEvaluator> evaluator, QObject *parent)
    : QObject(parent), evaluator_(std::move(evaluator)),
      executor_(
          std::make_unique<commands::MutationExecutor>(std::move(initial), initialLabel.toStdString())) {
    revision_ = executor_->current();
    if (!revision_->state->layers.empty()) {
        activeLayer_ = revision_->state->layers.back()->id;
    }
    subscription_ = executor_->subscribe([this](const std::shared_ptr<const document::Revision> &revision) {
        QMetaObject::invokeMethod(this, [this, revision] { deliver(revision); }, Qt::QueuedConnection);
    });
}

DocumentSession::~DocumentSession() {
    executor_->unsubscribe(subscription_);
    executor_.reset(); // joins the executor thread before members it might reach are gone
}

void DocumentSession::deliver(std::shared_ptr<const document::Revision> revision) {
    if (revision_ && revision->sequence == revision_->sequence) {
        return;
    }
    revision_ = std::move(revision);
    const document::DocumentState &state = *revision_->state;
    if (state.findLayer(activeLayer_) == nullptr) {
        activeLayer_ = state.layers.empty() ? base::LayerId{} : state.layers.back()->id;
        emit activeLayerChanged();
    }
    emit revisionChanged();
}

void DocumentSession::setActiveLayer(base::LayerId layer) {
    if (layer == activeLayer_ || revision_->state->findLayer(layer) == nullptr) {
        return;
    }
    activeLayer_ = layer;
    emit activeLayerChanged();
}

void DocumentSession::submit(commands::CommandEnvelope envelope) {
    executor_->submit(std::move(envelope), [this](const base::Result<commands::CommandResult> &result) {
        if (!result) {
            const QString message = QString::fromStdString(base::format(result.error()));
            QMetaObject::invokeMethod(
                this, [this, message] { emit commandFailed(message); }, Qt::QueuedConnection);
        }
    });
}

} // namespace archmage::ui
