#pragma once

#include <string>

#include "thor/engines.hpp"
#include "thor/executor.hpp"
#include "thor/expressions.hpp"
#include "thor/models.hpp"
#include "thor/parser.hpp"
#include "thor/registry.hpp"
#include "thor/state.hpp"

namespace thor {

class Runtime {
public:
    explicit Runtime(StateMode m = StateMode::Strict);

    void register_default_engines();

    ExecutionSummary execute_text(const std::string& src);
    ExecutionSummary execute_file(const std::string& path);

    RuntimeState&   state()    { return st_; }
    EngineRegistry& registry() { return reg_; }
    Value           evaluate(const std::string& s) { return ev_.eval(s); }

private:
    RuntimeState        st_;
    EngineRegistry      reg_;
    ExpressionEvaluator ev_;
};

} // namespace thor
