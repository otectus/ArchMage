#include "archmage/commands/Command.h"

namespace archmage::commands {
namespace {

using base::ErrorCategory;
using nlohmann::json;

template <class IdT> base::Result<IdT> parseOptionalId(const json &object, const char *key) {
    if (!object.contains(key) || object[key].is_null()) {
        return IdT{};
    }
    if (!object[key].is_string()) {
        return base::fail(ErrorCategory::DamagedInput, "envelope.field", "Malformed command",
                          std::string(key));
    }
    return IdT::parse(object[key].get<std::string>());
}

} // namespace

json CommandEnvelope::toJson() const {
    json out{{"protocolVersion", protocolVersion},
             {"commandId", commandId},
             {"parameterVersion", parameterVersion},
             {"requestId", requestId.isNull() ? json(nullptr) : json(requestId.toString())},
             {"documentId", documentId.isNull() ? json(nullptr) : json(documentId.toString())},
             {"baseRevision", baseRevision},
             {"targetIds", targetIds},
             {"parameters", parameters.is_null() ? json::object() : parameters}};
    if (!transactionId.isNull() || !transactionLabel.empty()) {
        out["transaction"] = {{"id", transactionId.isNull() ? json(nullptr) : json(transactionId.toString())},
                              {"label", transactionLabel}};
    }
    return out;
}

base::Result<CommandEnvelope> CommandEnvelope::fromJson(const json &in) {
    const auto malformed = [](std::string detail) {
        return base::fail(ErrorCategory::DamagedInput, "envelope.malformed", "Malformed command",
                          std::move(detail));
    };
    if (!in.is_object()) {
        return malformed("not an object");
    }
    CommandEnvelope e;
    if (!in.contains("commandId") || !in["commandId"].is_string()) {
        return malformed("commandId");
    }
    e.commandId = in["commandId"].get<std::string>();
    if (in.contains("protocolVersion")) {
        if (!in["protocolVersion"].is_number_integer()) {
            return malformed("protocolVersion");
        }
        e.protocolVersion = in["protocolVersion"].get<int>();
    }
    if (e.protocolVersion != 1) {
        return base::fail(ErrorCategory::UnsupportedInput, "envelope.protocol",
                          "Unsupported command protocol", std::to_string(e.protocolVersion));
    }
    if (in.contains("parameterVersion")) {
        if (!in["parameterVersion"].is_number_integer()) {
            return malformed("parameterVersion");
        }
        e.parameterVersion = in["parameterVersion"].get<int>();
    }
    auto request = parseOptionalId<base::RequestId>(in, "requestId");
    if (!request) {
        return base::unexpected(std::move(request).error());
    }
    e.requestId = *request;
    auto document = parseOptionalId<base::DocumentId>(in, "documentId");
    if (!document) {
        return base::unexpected(std::move(document).error());
    }
    e.documentId = *document;
    if (in.contains("baseRevision")) {
        if (!in["baseRevision"].is_number_unsigned() && !in["baseRevision"].is_number_integer()) {
            return malformed("baseRevision");
        }
        e.baseRevision = in["baseRevision"].get<std::uint64_t>();
    }
    if (in.contains("targetIds")) {
        if (!in["targetIds"].is_array()) {
            return malformed("targetIds");
        }
        for (const json &target : in["targetIds"]) {
            if (!target.is_string()) {
                return malformed("targetIds");
            }
            e.targetIds.push_back(target.get<std::string>());
        }
    }
    if (in.contains("parameters") && !in["parameters"].is_null()) {
        if (!in["parameters"].is_object()) {
            return malformed("parameters");
        }
        e.parameters = in["parameters"];
    }
    if (in.contains("transaction")) {
        const json &tx = in["transaction"];
        if (!tx.is_object()) {
            return malformed("transaction");
        }
        auto id = parseOptionalId<base::TransactionId>(tx, "id");
        if (!id) {
            return base::unexpected(std::move(id).error());
        }
        e.transactionId = *id;
        if (tx.contains("label") && tx["label"].is_string()) {
            e.transactionLabel = tx["label"].get<std::string>();
        }
    }
    return e;
}

CommandEnvelope CommandEnvelope::make(std::string commandId, json parameters,
                                      std::vector<std::string> targets) {
    CommandEnvelope e;
    e.commandId = std::move(commandId);
    e.requestId = base::RequestId::generate();
    e.parameters = parameters.is_null() ? json::object() : std::move(parameters); // `{}` arrives as null
    e.targetIds = std::move(targets);
    return e;
}

const CommandRegistry &CommandRegistry::builtIn() {
    static const CommandRegistry registry = [] {
        CommandRegistry r;
        registerLayerCommands(r);
        registerRasterCommands(r);
        return r;
    }();
    return registry;
}

void CommandRegistry::add(std::unique_ptr<CommandDefinition> definition) {
    const std::string id(definition->id());
    definitions_[id] = std::move(definition);
}

const CommandDefinition *CommandRegistry::find(std::string_view id) const {
    const auto it = definitions_.find(id);
    return it == definitions_.end() ? nullptr : it->second.get();
}

std::vector<std::string> CommandRegistry::ids() const {
    std::vector<std::string> out;
    for (const auto &[id, definition] : definitions_) {
        out.push_back(id);
    }
    return out;
}

namespace detail {

base::Unexpected<base::Error> badParameters(std::string_view commandId, std::string detail) {
    return base::fail(ErrorCategory::Validation, "command.parameters",
                      "The command's parameters are not valid",
                      std::string(commandId) + ": " + std::move(detail));
}

base::Result<base::LayerId> singleLayerTarget(const CommandEnvelope &envelope,
                                              const document::DocumentState &state) {
    if (envelope.targetIds.size() != 1) {
        return base::fail(ErrorCategory::Validation, "command.targets", "The command needs exactly one layer",
                          envelope.commandId + ": " + std::to_string(envelope.targetIds.size()) + " targets");
    }
    auto id = base::LayerId::parse(envelope.targetIds.front());
    if (!id) {
        return base::fail(ErrorCategory::Validation, "command.targets", "The command's target is not a layer",
                          envelope.targetIds.front());
    }
    if (state.findLayer(*id) == nullptr) {
        return base::fail(ErrorCategory::Validation, "layer.not_found", "The layer no longer exists",
                          envelope.targetIds.front());
    }
    return *id;
}

base::Status requireUnlocked(const document::Layer &layer) {
    if (layer.locked) {
        return base::fail(ErrorCategory::Validation, "layer.locked", "The layer is locked",
                          layer.name + " (" + layer.id.toString() + ")");
    }
    return {};
}

} // namespace detail

} // namespace archmage::commands
