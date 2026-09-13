#pragma once

#include <cstdint>
#include <shared_mutex>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>

#include "thor/errors.hpp"
#include "thor/models.hpp"

namespace thor {

enum class StateMode { Strict, Permissive };

struct StatePatch {
    std::unordered_map<std::string, Value> metrics;
    std::unordered_map<std::string, Value> variables;
};

class RuntimeState {
public:
    using Schema = std::unordered_map<std::string, std::type_index>;

    RuntimeState(Schema s, StateMode m = StateMode::Strict);

    void  set_metric(const std::string& n, const Value& v);
    Value get_metric(const std::string& n) const;

    void  set_variable(const std::string& n, const Value& v);
    void  erase_variable(const std::string& n);
    bool  has_variable(const std::string& n) const;
    Value get_variable(const std::string& n) const;

    Value get_path(const std::string& d) const;
    Value require_path(const std::string& d) const;

    StateMode mode() const { return mode_; }

    void apply_patch(const StatePatch& p);

private:
    void validate_(const std::string& n, const Value& v) const;
    const Value* step_(const Value& v, const std::string& seg) const;
    Value resolve_(const std::string& d, bool strict) const;

    Schema    schema_;
    StateMode mode_;
    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, Value> metrics_;
    std::unordered_map<std::string, Value> variables_;
};

RuntimeState::Schema default_metric_schema();

} // namespace thor
