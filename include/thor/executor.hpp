#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "thor/expressions.hpp"
#include "thor/models.hpp"
#include "thor/registry.hpp"
#include "thor/state.hpp"

namespace thor {

struct LoopFrame {
    std::string variable_name;
    std::int64_t iteration = 0, total = 0;
    std::size_t loop_start_index = 0, loop_end_index = 0;
    bool had_prior_variable = false;
    Value prior_value;
};

struct ExecutionSummary {
    bool success = true;
    std::size_t executed_instructions = 0, final_pc = 0;
    std::string error;
};

class Executor {
public:
    Executor(RuntimeState& s, EngineRegistry& r, const ExpressionEvaluator& e)
        : st_(s), reg_(r), ev_(e) {}

    ExecutionSummary run(const Program& prog, const std::string& exec_id);

private:
    RuntimeState& st_;
    EngineRegistry& reg_;
    const ExpressionEvaluator& ev_;
};

} // namespace thor
