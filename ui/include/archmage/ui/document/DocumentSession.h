#pragma once

#include "archmage/commands/MutationExecutor.h"
#include "archmage/render/CpuEvaluator.h"

#include <QObject>
#include <QString>

#include <memory>

namespace archmage::ui {

/// One open document in the desktop shell: its executor, the revision the GUI last saw, and editing
/// context that is not document state, such as the active layer (spec §6.2).
///
/// The executor publishes revisions on its own thread; the session re-delivers them on the GUI thread as
/// revisionChanged, so widgets never touch executor threads.
///
/// Ownership: owns the executor; shares the evaluator with other sessions.
/// Thread safety: GUI thread only, except that submit() and execute() may be called from any thread.
class DocumentSession final : public QObject {
    Q_OBJECT

public:
    DocumentSession(document::DocumentState initial, const QString &initialLabel,
                    std::shared_ptr<render::CpuEvaluator> evaluator, QObject *parent = nullptr);
    ~DocumentSession() override;

    commands::MutationExecutor &executor() { return *executor_; }
    render::CpuEvaluator &evaluator() { return *evaluator_; }

    /// The latest revision delivered on the GUI thread.
    [[nodiscard]] std::shared_ptr<const document::Revision> revision() const { return revision_; }

    /// The layer tools act on; null when the document has no layers.
    [[nodiscard]] base::LayerId activeLayer() const { return activeLayer_; }
    void setActiveLayer(base::LayerId layer);

    /// Submits a command; failures arrive as commandFailed on the GUI thread.
    void submit(commands::CommandEnvelope envelope);

signals:
    void revisionChanged();
    void activeLayerChanged();
    void commandFailed(const QString &message);

private:
    void deliver(std::shared_ptr<const document::Revision> revision);

    std::shared_ptr<render::CpuEvaluator> evaluator_;
    std::unique_ptr<commands::MutationExecutor> executor_;
    std::uint64_t subscription_ = 0;
    std::shared_ptr<const document::Revision> revision_;
    base::LayerId activeLayer_;
};

} // namespace archmage::ui
