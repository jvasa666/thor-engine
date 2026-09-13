#include "thor/executor.hpp"
#include "thor/errors.hpp"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace thor {

ExecutionSummary Executor::run(const Program& prog, const std::string& exec_id) {
    ExecutionSummary sum;
    std::vector<LoopFrame> loops;
    std::size_t pc = 0;
    const auto& ins = prog.instructions;
    while (pc < ins.size()) {
        if (sum.executed_instructions >= prog.step_limit)
            throw ExecutionError("Step limit exceeded");
        const auto& cur = ins[pc];
        ++sum.executed_instructions;

        switch (cur.kind) {
            case OpKind::LoopStart: {
                auto n = ev_.eval_int(cur.loop_count_expr);
                if (n < 0) throw ExecutionError("Negative loop count");
                if (n == 0) { pc = *cur.jump + 1; break; }
                LoopFrame f;
                f.variable_name = cur.loop_var;
                f.total = n;
                f.loop_start_index = pc;
                f.loop_end_index = *cur.jump;
                f.had_prior_variable = st_.has_variable(cur.loop_var);
                if (f.had_prior_variable) f.prior_value = st_.get_variable(cur.loop_var);
                st_.set_variable(cur.loop_var, Value{std::int64_t{0}});
                loops.push_back(std::move(f));
                ++pc; break;
            }
            case OpKind::LoopEnd: {
                if (loops.empty()) throw ExecutionError("LoopEnd without frame");
                auto& f = loops.back();
                ++f.iteration;
                if (f.iteration < f.total) {
                    st_.set_variable(f.variable_name, Value{f.iteration});
                    pc = *cur.jump + 1;
                } else {
                    if (f.had_prior_variable) st_.set_variable(f.variable_name, f.prior_value);
                    else st_.erase_variable(f.variable_name);
                    loops.pop_back();
                    ++pc;
                }
                break;
            }
            case OpKind::IfStart: {
                bool cond = ev_.eval_bool(cur.condition_expr);
                if (cond) ++pc;
                else pc = *cur.jump + 1;
                break;
            }
            case OpKind::Else: {
                pc = *cur.jump + 1; break;
            }
            case OpKind::IfEnd: { ++pc; break; }
            case OpKind::Break: {
                if (loops.empty()) throw ExecutionError("Break without frame");
                auto f = loops.back(); loops.pop_back();
                if (f.had_prior_variable) st_.set_variable(f.variable_name, f.prior_value);
                else st_.erase_variable(f.variable_name);
                pc = f.loop_end_index + 1; break;
            }
            case OpKind::Continue: {
                if (loops.empty()) throw ExecutionError("Continue without frame");
                auto& f = loops.back();
                ++f.iteration;
                if (f.iteration < f.total) {
                    st_.set_variable(f.variable_name, Value{f.iteration});
                    pc = f.loop_start_index + 1;
                } else {
                    if (f.had_prior_variable) st_.set_variable(f.variable_name, f.prior_value);
                    else st_.erase_variable(f.variable_name);
                    pc = f.loop_end_index + 1;
                    loops.pop_back();
                }
                break;
            }
            case OpKind::Call: {
                EngineRequest rq;
                rq.action = cur.action;
                rq.execution_id = exec_id;
                rq.instruction_id = pc;
                for (auto& kv : cur.args)
                    rq.arguments.emplace(kv.first, ev_.eval(kv.second));
                auto eng = reg_.resolve(cur.pillar);
                auto res = eng->execute(rq);
                if (res.success) st_.apply_patch(res.patch);
                for (auto& w : res.warnings) std::cerr << "[WARN] " << w << "\n";
                ++pc; break;
            }
        }
    }
    sum.final_pc = pc;
    return sum;
}

} // namespace thor
