#pragma once

#include <cstdint>
#include <string>

#include "thor/expression_ast.hpp"
#include "thor/state.hpp"

namespace thor {

class ExpressionEvaluator {
public:
    explicit ExpressionEvaluator(const RuntimeState& s) : st_(s) {}

    Value eval(const std::string& s) const;
    bool  eval_bool(const std::string& s) const;
    std::int64_t eval_int(const std::string& s) const;

private:
    const RuntimeState& st_;
};

} // namespace thor
