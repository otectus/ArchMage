// Commands (D-15, spec §9.1, App. A.2): every document mutation is a typed command with a stable ID,
// schema-versioned parameters, explicit targets and preconditions. A command is a pure function from an
// immutable document state to a new one; the executor commits the result as a revision. Menus,
// shortcuts, the CLI, scripts and jobs all go through the same commands.
//
// Thread safety: envelopes and effects are values; definitions are immutable and safe to share; the
// registry is filled at start-up and read-only afterwards.
#pragma once

#include "archmage/base/Error.h"
#include "archmage/base/Id.h"
#include "archmage/document/Document.h"
#include "archmage/tiles/TileMap.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace archmage::commands {

/// Spec App. A.2.
struct CommandEnvelope {
    int protocolVersion = 1;
    std::string commandId;
    int parameterVersion = 1;
    base::RequestId requestId;          // repeated IDs apply once (idempotency)
    base::DocumentId documentId;        // null: the executor's document
    std::uint64_t baseRevision = 0;     // 0: the current revision; otherwise must equal it
    std::vector<std::string> targetIds; // serialized typed IDs, e.g. "layer-..."
    nlohmann::json parameters = nlohmann::json::object();
    base::TransactionId transactionId; // null: a transaction of its own
    std::string transactionLabel;      // overrides the command's default history label

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static base::Result<CommandEnvelope> fromJson(const nlohmann::json &json);
    /// A fresh envelope for `commandId` with a new request ID.
    [[nodiscard]] static CommandEnvelope make(std::string commandId,
                                              nlohmann::json parameters = nlohmann::json::object(),
                                              std::vector<std::string> targets = {});
};

/// What a command produces.
struct CommandEffect {
    document::DocumentState state;
    std::string label;                                // history label
    nlohmann::json result = nlohmann::json::object(); // command-specific, e.g. {"layerId": "..."}
};

class CommandDefinition {
public:
    virtual ~CommandDefinition() = default;
    [[nodiscard]] virtual std::string_view id() const = 0;
    [[nodiscard]] virtual int parameterVersion() const { return 1; }
    /// Validates targets, parameters and preconditions (locks, ranges) and computes the new state.
    [[nodiscard]] virtual base::Result<CommandEffect> apply(const CommandEnvelope &envelope,
                                                            const document::DocumentState &state) const = 0;
};

class CommandRegistry {
public:
    /// The registry with every built-in command (plan §8.1 inventory as implemented so far).
    static const CommandRegistry &builtIn();

    void add(std::unique_ptr<CommandDefinition> definition);
    [[nodiscard]] const CommandDefinition *find(std::string_view id) const;
    [[nodiscard]] std::vector<std::string> ids() const;

private:
    std::map<std::string, std::unique_ptr<CommandDefinition>, std::less<>> definitions_;
};

/// Builds the initial state of a new document from an `archmage.document.create` envelope.
/// Parameters: width, height (pixels), sampleType ("uint8" or "float32"), resolution (ppi, default 72),
/// title, background ("white", the default, or "transparent").
[[nodiscard]] base::Result<CommandEffect> createDocument(const CommandEnvelope &envelope);

/// Registers the Stage 1 layer commands (S1-2.6); builtIn() calls this.
void registerLayerCommands(CommandRegistry &registry);
/// Registers the raster commands (S1-3.3: archmage.raster.paintStroke); builtIn() calls this.
void registerRasterCommands(CommandRegistry &registry);

/// Helpers shared by command implementations.
namespace detail {
[[nodiscard]] base::Result<base::LayerId> singleLayerTarget(const CommandEnvelope &envelope,
                                                            const document::DocumentState &state);
[[nodiscard]] base::Status requireUnlocked(const document::Layer &layer);
[[nodiscard]] base::Unexpected<base::Error> badParameters(std::string_view commandId, std::string detail);
} // namespace detail

} // namespace archmage::commands
