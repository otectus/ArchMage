#pragma once

#include "archmage/commands/MutationExecutor.h"
#include "archmage/native/Recovery.h"
#include "archmage/native/SaveTransaction.h"
#include "archmage/render/CpuEvaluator.h"

#include <QObject>
#include <QString>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace archmage::ui {

/// One open document in the desktop shell: its executor, the revision the GUI last saw, editing context
/// that is not document state (the active layer, spec §6.2), the file it belongs to and its recovery
/// journal.
///
/// The executor publishes revisions on its own thread; the session re-delivers them on the GUI thread as
/// revisionChanged, so widgets never touch executor threads.
///
/// Ownership: owns the executor; shares the evaluator with other sessions; shares the journal with the
/// window's recovery session, which must untrack it before this session is destroyed.
/// Thread safety: GUI thread only, except that submit() may be called from any thread.
class DocumentSession final : public QObject {
    Q_OBJECT

public:
    /// The file a document belongs to. A new document has no path and no identity until its first save.
    struct FileState {
        std::filesystem::path path;
        std::optional<native::FileIdentity> identity;
        /// Set when the file must not be saved over (spec §10.5, ADR-0103): the explanation shown to the user.
        QString readOnlyExplanation;
    };

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
    /// As above; `onSuccess` runs on the GUI thread with the result.
    void submit(commands::CommandEnvelope envelope, std::function<void(const commands::CommandResult &)> onSuccess);

    [[nodiscard]] const FileState &file() const { return file_; }
    void setFile(FileState file);
    /// The file name, or the document title when it has never been saved.
    [[nodiscard]] QString displayName() const;
    /// True when the current revision is not the one last saved. Call executor().drain() first to count
    /// commands still queued.
    [[nodiscard]] bool isModified() const { return executor_->isModified(); }
    [[nodiscard]] bool isReadOnly() const { return !file_.readOnlyExplanation.isEmpty(); }
    [[nodiscard]] bool isSaving() const { return saving_; }
    void setSaving(bool saving);
    /// Records a completed save of `revision` (spec §10.3 step 6): the clean point, the file and the journal.
    void markSaved(const document::Revision &revision, const std::filesystem::path &path, native::FileIdentity identity);

    void setJournal(std::shared_ptr<native::DocumentJournal> journal) { journal_ = std::move(journal); }
    [[nodiscard]] const std::shared_ptr<native::DocumentJournal> &journal() const { return journal_; }

signals:
    void revisionChanged();
    void activeLayerChanged();
    void commandFailed(const QString &message);
    /// The file, the saved point or the saving state changed.
    void fileStateChanged();

private:
    void deliver(std::shared_ptr<const document::Revision> revision);

    std::shared_ptr<render::CpuEvaluator> evaluator_;
    std::unique_ptr<commands::MutationExecutor> executor_;
    std::uint64_t subscription_ = 0;
    std::shared_ptr<const document::Revision> revision_;
    base::LayerId activeLayer_;
    FileState file_;
    bool saving_ = false;
    std::shared_ptr<native::DocumentJournal> journal_;
};

} // namespace archmage::ui
