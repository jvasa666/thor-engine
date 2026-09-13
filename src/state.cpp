#include "thor/state.hpp"

#include <mutex>

namespace thor {

namespace {

Value default_for(std::type_index t) {
    if (t == typeid(std::int64_t))  return Value{std::int64_t{0}};
    if (t == typeid(double))        return Value{0.0};
    if (t == typeid(bool))          return Value{false};
    if (t == typeid(std::string))   return Value{std::string{}};
    if (t == typeid(Value::MapPtr)) return make_map({});
    return Value{};
}

std::pair<std::string, std::string> split_path(const std::string& s) {
    auto p = s.find('.');
    if (p == std::string::npos) return {s, {}};
    return {s.substr(0, p), s.substr(p + 1)};
}

} // namespace

RuntimeState::RuntimeState(Schema s, StateMode m)
    : schema_(std::move(s)), mode_(m) {
    for (auto& kv : schema_) metrics_[kv.first] = default_for(kv.second);
}

void RuntimeState::set_metric(const std::string& n, const Value& v) {
    std::unique_lock<std::shared_mutex> lk(mu_);
    validate_(n, v);
    metrics_[n] = v;
}

Value RuntimeState::get_metric(const std::string& n) const {
    std::shared_lock<std::shared_mutex> lk(mu_);
    auto it = metrics_.find(n);
    return it == metrics_.end() ? Value{} : it->second;
}

void RuntimeState::set_variable(const std::string& n, const Value& v) {
    std::unique_lock<std::shared_mutex> lk(mu_);
    variables_[n] = v;
}

void RuntimeState::erase_variable(const std::string& n) {
    std::unique_lock<std::shared_mutex> lk(mu_);
    variables_.erase(n);
}

bool RuntimeState::has_variable(const std::string& n) const {
    std::shared_lock<std::shared_mutex> lk(mu_);
    return variables_.count(n) != 0;
}

Value RuntimeState::get_variable(const std::string& n) const {
    std::shared_lock<std::shared_mutex> lk(mu_);
    auto it = variables_.find(n);
    return it == variables_.end() ? Value{} : it->second;
}

Value RuntimeState::get_path(const std::string& d) const {
    return resolve_(d, false);
}

Value RuntimeState::require_path(const std::string& d) const {
    return resolve_(d, true);
}

void RuntimeState::apply_patch(const StatePatch& p) {
    std::unique_lock<std::shared_mutex> lk(mu_);
    for (auto& kv : p.metrics) validate_(kv.first, kv.second);
    for (auto& kv : p.metrics) metrics_[kv.first] = kv.second;
    for (auto& kv : p.variables) variables_[kv.first] = kv.second;
}

void RuntimeState::validate_(const std::string& n, const Value& v) const {
    auto it = schema_.find(n);
    if (it == schema_.end()) {
        if (mode_ == StateMode::Strict)
            throw UnknownMetricError("Unknown metric '" + n + "'");
        return;
    }
    auto t = it->second;
    bool ok =
        (t == typeid(std::int64_t)  && std::holds_alternative<std::int64_t>(v.data)) ||
        (t == typeid(double)        && std::holds_alternative<double>(v.data)) ||
        (t == typeid(bool)          && std::holds_alternative<bool>(v.data)) ||
        (t == typeid(std::string)   && std::holds_alternative<std::string>(v.data)) ||
        (t == typeid(Value::MapPtr) && std::holds_alternative<Value::MapPtr>(v.data));
    if (!ok) throw TypeMismatchError("Metric '" + n + "' type mismatch");
}

const Value* RuntimeState::step_(const Value& v, const std::string& seg) const {
    if (auto mp = std::get_if<Value::MapPtr>(&v.data)) {
        if (!*mp) return nullptr;
        auto it = (*mp)->find(seg);
        return it == (*mp)->end() ? nullptr : &it->second;
    }
    if (auto lp = std::get_if<Value::ListPtr>(&v.data)) {
        if (!*lp || seg.empty()) return nullptr;
        for (char c : seg) if (c < '0' || c > '9') return nullptr;
        try {
            std::size_t i = static_cast<std::size_t>(std::stoull(seg));
            return i < (*lp)->size() ? &(*lp)->at(i) : nullptr;
        } catch (...) { return nullptr; }
    }
    return nullptr;
}

Value RuntimeState::resolve_(const std::string& d, bool strict) const {
    std::shared_lock<std::shared_mutex> lk(mu_);
    auto head_tail = split_path(d);
    const std::string& head = head_tail.first;
    const std::string& tail = head_tail.second;
    const Value* cur = nullptr;

    auto mit = metrics_.find(head);
    if (mit != metrics_.end()) cur = &mit->second;
    else {
        auto vit = variables_.find(head);
        if (vit == variables_.end()) {
            if (strict)
                throw UnknownMetricError("Unknown metric or variable '" + head + "'");
            return Value{};
        }
        cur = &vit->second;
    }

    if (tail.empty()) return *cur;

    std::string rem = tail, walked = head;
    while (!rem.empty()) {
        auto ht = split_path(rem);
        walked += "." + ht.first;
        cur = step_(*cur, ht.first);
        if (!cur) {
            if (strict)
                throw UnknownPathError("Unknown path '" + d + "' at '" + walked + "'");
            return Value{};
        }
        rem = ht.second;
    }
    return *cur;
}

RuntimeState::Schema default_metric_schema() {
    return {
        {"simulation_time",      typeid(std::int64_t)},
        {"system_health",        typeid(double)},
        {"adaptive_responses",   typeid(std::int64_t)},
        {"empathy_level",        typeid(double)},
        {"current_agents",       typeid(std::int64_t)},
        {"threat_detected_flag", typeid(bool)},
        {"Observe_Result",       typeid(Value::MapPtr)},
    };
}

} // namespace thor
