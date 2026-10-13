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
    if (revision_ && revision->sequence == revision_->sequence && revision->id == revision_->id) {
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

void DocumentSession::submit(commands::CommandEnvelope envelope) { submit(std::move(envelope), {}); }

void DocumentSession::submit(commands::CommandEnvelope envelope,
                             std::function<void(const commands::CommandResult &)> onSuccess) {
    // The completion runs on the executor thread. Posting to `this` is safe: the destructor joins the
    // executor before the QObject goes, and Qt drops events still queued for a destroyed receiver.
    executor_->submit(std::move(envelope), [this, onSuccess = std::move(onSuccess)](
                                               const base::Result<commands::CommandResult> &result) {
        if (!result) {
            const QString message = QString::fromStdString(result.error().message);
            QMetaObject::invokeMethod(this, [this, message] { emit commandFailed(message); }, Qt::QueuedConnection);
            return;
        }
        if (onSuccess) {
            QMetaObject::invokeMethod(this, [onSuccess, value = *result] { onSuccess(value); }, Qt::QueuedConnection);
        }
    });
}

void DocumentSession::setFile(FileState file) {
    file_ = std::move(file);
    emit fileStateChanged();
}

QString DocumentSession::displayName() const {
    if (!file_.path.empty()) {
        return QString::fromStdString(file_.path.filename().string());
    }
    return QString::fromStdString(revision_->state->title);
}

void DocumentSession::setSaving(bool saving) {
    if (saving != saving_) {
        saving_ = saving;
        emit fileStateChanged();
    }
}

void DocumentSession::markSaved(const document::Revision &revision, const std::filesystem::path &path,
                                native::FileIdentity identity) {
    executor_->markSaved(revision.sequence);
    if (journal_) {
        (void)journal_->markSaved(revision, path); // recovery bookkeeping; a failure shows up as lastError()
    }
    file_.path = path;
    file_.identity = identity;
    file_.readOnlyExplanation.clear(); // the new file is this version's own
    emit fileStateChanged();
}

} // namespace archmage::ui
