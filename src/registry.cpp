#include "thor/registry.hpp"
#include "thor/errors.hpp"

#include <cctype>

namespace thor {

namespace {

std::string to_lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

void EngineRegistry::add(std::shared_ptr<Engine> e) {
    engines_[to_lower(e->name())] = std::move(e);
}

std::shared_ptr<Engine> EngineRegistry::resolve(const std::string& n) const {
    auto it = engines_.find(to_lower(n));
    if (it == engines_.end())
        throw UnknownEngineError("Unknown engine '" + n + "'");
    return it->second;
}

} // namespace thor
