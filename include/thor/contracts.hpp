#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "thor/models.hpp"
#include "thor/state.hpp"

namespace thor {

struct EngineRequest {
    std::string action;
    std::unordered_map<std::string, Value> arguments;
    std::string execution_id;
    std::size_t instruction_id = 0;
};

struct EngineResult {
    bool success = true;
    std::unordered_map<std::string, Value> output;
    StatePatch patch;
    std::vector<std::string> warnings;
};

class Engine {
public:
    virtual ~Engine() = default;
    virtual std::string name() const = 0;
    virtual std::string status() const = 0;
    virtual EngineResult execute(const EngineRequest& req) = 0;
};

} // namespace thor
