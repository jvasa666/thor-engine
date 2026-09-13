#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "thor/contracts.hpp"

namespace thor {

class EngineRegistry {
public:
    void add(std::shared_ptr<Engine> e);
    std::shared_ptr<Engine> resolve(const std::string& name) const;

private:
    std::unordered_map<std::string, std::shared_ptr<Engine>> engines_;
};

} // namespace thor
