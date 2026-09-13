#include "thor/models.hpp"

namespace thor {

bool value_equal(const Value& a, const Value& b) {
    if (a.data.index() != b.data.index()) return false;
    if (std::holds_alternative<std::monostate>(a.data)) return true;
    if (auto p = std::get_if<bool>(&a.data))         return *p == std::get<bool>(b.data);
    if (auto p = std::get_if<std::int64_t>(&a.data)) return *p == std::get<std::int64_t>(b.data);
    if (auto p = std::get_if<double>(&a.data))       return *p == std::get<double>(b.data);
    if (auto p = std::get_if<std::string>(&a.data))  return *p == std::get<std::string>(b.data);
    if (auto p = std::get_if<Value::ListPtr>(&a.data)) {
        const auto& la = *p;
        const auto& lb = std::get<Value::ListPtr>(b.data);
        if (!la || !lb) return la == lb;
        if (la->size() != lb->size()) return false;
        for (std::size_t i = 0; i < la->size(); ++i)
            if (!value_equal((*la)[i], (*lb)[i])) return false;
        return true;
    }
    if (auto p = std::get_if<Value::MapPtr>(&a.data)) {
        const auto& ma = *p;
        const auto& mb = std::get<Value::MapPtr>(b.data);
        if (!ma || !mb) return ma == mb;
        if (ma->size() != mb->size()) return false;
        for (const auto& kv : *ma) {
            auto it = mb->find(kv.first);
            if (it == mb->end() || !value_equal(kv.second, it->second)) return false;
        }
        return true;
    }
    return false;
}

} // namespace thor
