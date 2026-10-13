// `archmage-cli recover` (spec App. A.4, plan S1-5.9): lists recoverable documents, or writes one to a new
// file. It never overwrites an existing file and never touches the document's original.

#include "Commands.h"

#include "archmage/native/Platform.h"
#include "archmage/native/Recovery.h"
#include "archmage/native/SaveTransaction.h"

#include <filesystem>
#include <iostream>

namespace archmage::cli {
namespace {

using nlohmann::json;

json candidateJson(const native::RecoveryCandidate &candidate) {
    json out{{"session", candidate.sessionId.toString()},
             {"document", candidate.documentId.toString()},
             {"title", candidate.title},
             {"journaledAt", candidate.journaledAt},
             {"journal", candidate.journalPath.string()},
             {"source", candidate.sourcePath.string()},
             {"revision",
              {{"id", candidate.revisionId}, {"sequence", candidate.revisionSequence}, {"label", candidate.revisionLabel}}},
             {"savedRevision", candidate.savedRevisionId},
             {"upToDate", candidate.upToDate}};
    if (candidate.thumbnail) {
        out["thumbnail"] = {{"width", candidate.thumbnail->width}, {"height", candidate.thumbnail->height}};
    }
    if (candidate.problem) {
        out["problem"] = errorRecord(*candidate.problem);
    }
    return out;
}

} // namespace

int runRecover(const RecoverOptions &options) {
    std::filesystem::path root = options.root;
    if (root.empty()) {
        auto located = native::locateRecoveryRoot();
        if (!located) {
            return reportError(located.error(), options.asJson);
        }
        root = located->path;
    }
    auto candidates = native::listRecoveryCandidates(root);
    if (!candidates) {
        return reportError(candidates.error(), options.asJson);
    }

    if (options.session.empty()) {
        if (options.asJson) {
            json list = json::array();
            for (const auto &candidate : *candidates) {
                list.push_back(candidateJson(candidate));
            }
            std::cout << json{{"ok", true}, {"root", root.string()}, {"candidates", list}}.dump(2) << '\n';
            return kExitOk;
        }
        if (candidates->empty()) {
            std::cout << "No documents to recover in " << root.string() << '\n';
        }
        for (const auto &candidate : *candidates) {
            std::cout << candidate.sessionId.toString() << "  " << candidate.documentId.toString() << "  \""
                      << candidate.title << "\"  revision " << candidate.revisionSequence << " ("
                      << candidate.revisionLabel << ")  journaled " << candidate.journaledAt;
            if (!candidate.sourcePath.empty()) {
                std::cout << "  from " << candidate.sourcePath.string();
            }
            if (candidate.upToDate) {
                std::cout << "  [already saved]";
            }
            if (candidate.problem) {
                std::cout << "  [unreadable: " << candidate.problem->code << "]";
            }
            std::cout << '\n';
        }
        return kExitOk;
    }

    if (options.output.empty()) {
        return reportError({base::ErrorCategory::Validation, "recover.no_output", "Name a file to write with --output",
                            ""},
                           options.asJson);
    }
    std::vector<const native::RecoveryCandidate *> matches;
    for (const auto &candidate : *candidates) {
        if (candidate.sessionId.toString() == options.session &&
            (options.document.empty() || candidate.documentId.toString() == options.document)) {
            matches.push_back(&candidate);
        }
    }
    if (matches.empty()) {
        return reportError({base::ErrorCategory::Validation, "recover.not_found",
                            "No recoverable document matches that session", options.session},
                           options.asJson);
    }
    if (matches.size() > 1) {
        std::string ids;
        for (const auto *match : matches) {
            ids += (ids.empty() ? "" : ", ") + match->documentId.toString();
        }
        return reportError({base::ErrorCategory::Validation, "recover.ambiguous",
                            "The session holds several documents: choose one with --document", ids},
                           options.asJson);
    }
    const std::filesystem::path output = std::filesystem::absolute(options.output);
    // Claim the name first: the file must not exist, now or by the time the save replaces it.
    if (auto claimed = native::createExclusive(output); !claimed) {
        base::Error error = claimed.error();
        if (std::filesystem::exists(output)) {
            error = {base::ErrorCategory::Validation, "recover.output_exists",
                     "The output file already exists; recovery never overwrites a file", output.string()};
        }
        return reportError(error, options.asJson);
    }
    const auto unclaim = [&] {
        std::error_code ignored;
        std::filesystem::remove(output, ignored);
    };
    auto opened = native::recoverDocument(*matches.front());
    if (!opened || !opened->state) {
        unclaim();
        const base::Error error = opened ? base::Error{base::ErrorCategory::UnsupportedInput, "recover.read_only",
                                                       "The journal cannot be recovered by this version",
                                                       opened->info.readOnlyExplanation}
                                         : opened.error();
        reportError(error, options.asJson);
        return error.category == base::ErrorCategory::DamagedInput ||
                       error.category == base::ErrorCategory::UnsupportedInput
                   ? kExitInvalid
                   : kExitError;
    }
    auto revision = std::make_shared<document::Revision>();
    if (auto id = base::RevisionId::parse(opened->info.revisionId)) {
        revision->id = *id;
    }
    revision->sequence = opened->info.revisionSequence;
    revision->label = opened->info.revisionLabel;
    revision->state = opened->state;
    native::SaveRequest request;
    request.target = output;
    request.revision = revision;
    request.identity = native::identityForSave(*opened->state, std::nullopt);
    auto saved = native::saveDocument(request);
    if (!saved) {
        unclaim();
        return reportError(saved.error(), options.asJson);
    }
    if (options.asJson) {
        std::cout << json{{"ok", true},
                          {"output", output.string()},
                          {"document", opened->state->id.toString()},
                          {"revision", opened->info.revisionId},
                          {"bytes", saved->bytes}}
                         .dump(2)
                  << '\n';
    } else {
        std::cout << "recovered " << opened->state->id.toString() << " (revision " << opened->info.revisionSequence
                  << ") to " << output.string() << '\n';
    }
    return kExitOk;
}

} // namespace archmage::cli
