#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <core/helper.hpp>
#include <game/timecontrol/timecontrol.hpp>
#include <types/enums.hpp>

#include <json.hpp>

namespace fastchess {

struct Limit {
    TimeControl::Limits tc;

    uint64_t nodes = 0;
    uint64_t plies = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Limit, tc, nodes, plies)

struct EngineConfiguration {
    // the limit for the engines "go" command
    Limit limit;

    // engine name
    std::string name;

    // Path to engine
    std::string dir;

    // Engine binary name
    std::string cmd;

    // Custom args that should be sent
    std::string args;

    // Should process be restarted after every game
    bool restart = false;

    // UCI options
    std::vector<std::pair<std::string, std::string>> options;

    // Chess variant
    VariantType variant = VariantType::STANDARD;

    // "xboard": a WinBoard engine, started through engine::xboard_adapter; anything else: a UCI engine
    std::string protocol;

    // fastchess-gui: how long the engine may take to answer uciok when started and the isready of a new game, in
    // milliseconds, for an engine slow to load; 0: the tournament's startup_time and ucinewgame_time.
    int64_t start_timeout_ms = 0;

    template <typename T, typename Predicate>
    std::optional<T> getOption(std::string_view option_name, Predicate transform) const {
        const auto it = std::find_if(options.begin(), options.end(),
                                     [&option_name](const auto& option) { return option.first == option_name; });

        if (it != options.end()) {
            return std::optional<T>(transform(it->second));
        }

        return std::nullopt;
    }

    std::filesystem::path getEnginePath() const {
        if (!dir.empty()) {
            return std::filesystem::path(dir) / cmd;
        }

        return std::filesystem::path(cmd);
    }
};
// With defaults, so that configurations saved before a field existed still load. start_timeout_ms is written only
// when set, so that the configuration of an engine without one is saved as before.
#define FCG_ENGINE_CONFIGURATION_FIELDS name, dir, cmd, args, restart, options, limit, variant, protocol
template <typename BasicJsonType,
          nlohmann::detail::enable_if_t<nlohmann::detail::is_basic_json<BasicJsonType>::value, int> = 0>
void to_json(BasicJsonType& nlohmann_json_j, const EngineConfiguration& nlohmann_json_t) {
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, FCG_ENGINE_CONFIGURATION_FIELDS))
    if (nlohmann_json_t.start_timeout_ms > 0) nlohmann_json_j["start_timeout_ms"] = nlohmann_json_t.start_timeout_ms;
}
template <typename BasicJsonType,
          nlohmann::detail::enable_if_t<nlohmann::detail::is_basic_json<BasicJsonType>::value, int> = 0>
void from_json(const BasicJsonType& nlohmann_json_j, EngineConfiguration& nlohmann_json_t) {
    const EngineConfiguration nlohmann_json_default_obj{};
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM_WITH_DEFAULT, FCG_ENGINE_CONFIGURATION_FIELDS))
    NLOHMANN_JSON_FROM_WITH_DEFAULT(start_timeout_ms)
}
#undef FCG_ENGINE_CONFIGURATION_FIELDS

}  // namespace fastchess
