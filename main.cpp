// THOR Engine - single-file build
// Copyright (c) 2026 Joseph Vasapolli. All rights reserved.

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>


#include "thor/errors.hpp"
#include "thor/version.hpp"



#include "thor/models.hpp"
#include "thor/state.hpp"
#include "thor/expressions.hpp"
#include "thor/contracts.hpp"
#include "thor/registry.hpp"
using namespace thor;

#include "thor/engines.hpp"
#include "thor/parser.hpp"
// ---------- executor ----------
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

    ExecutionSummary run(const Program& prog, const std::string& exec_id) {
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

private:
    RuntimeState& st_;
    EngineRegistry& reg_;
    const ExpressionEvaluator& ev_;
};

// ---------- runtime ----------
class Runtime {
public:
    explicit Runtime(StateMode m = StateMode::Strict)
        : st_(default_metric_schema(), m), ev_(st_) {}

    void register_default_engines() {
        reg_.add(make_dialogue_engine());
        reg_.add(make_codex_engine());
        reg_.add(make_observe_engine());
    }

    ExecutionSummary execute_text(const std::string& src) {
        Program p = Parser{}.parse_text(src);
        Executor ex(st_, reg_, ev_);
        return ex.run(p, "exec");
    }

    ExecutionSummary execute_file(const std::string& path) {
        std::ifstream in(path);
        if (!in) throw ThorError("Cannot open " + path);
        std::stringstream ss; ss << in.rdbuf();
        return execute_text(ss.str());
    }

    RuntimeState& state() { return st_; }
    EngineRegistry& registry() { return reg_; }
    Value evaluate(const std::string& s) { return ev_.eval(s); }

private:
    RuntimeState st_;
    EngineRegistry reg_;
    ExpressionEvaluator ev_;
};


// ---------- CLI ----------
static void usage() {
    std::cout <<
R"(THOR Engine - VAS DSL runtime

Usage:
  thor run   <script.vas> [--permissive] [--fail-fast]
  thor check <script.vas>
  thor eval  "<expression>"
  thor version
  thor help
)";
}

int main(int argc, char** argv) {
    using namespace thor;
    if (argc < 2) { usage(); return 1; }
    std::string cmd = argv[1];

    try {
        if (cmd == "version") {
            std::cout << thor::VERSION << "\n" << thor::COPYRIGHT << "\n";
            return 0;
        }
        if (cmd == "help") { usage(); return 0; }

        if (cmd == "check" || cmd == "run") {
            if (argc < 3) { usage(); return 1; }
            StateMode mode = StateMode::Strict;
            for (int i = 3; i < argc; ++i) {
                std::string a = argv[i];
                if (a == "--permissive") mode = StateMode::Permissive;
            }
            Runtime rt(mode);
            rt.register_default_engines();
            if (cmd == "check") {
                std::ifstream in(argv[2]);
                if (!in) { std::cerr << "Cannot open " << argv[2] << "\n"; return 2; }
                Parser{}.parse(in);
                std::cout << "OK: " << argv[2] << " parses cleanly.\n";
                return 0;
            }
            auto s = rt.execute_file(argv[2]);
            std::cout << "Executed " << s.executed_instructions << " instructions.\n";
            return s.success ? 0 : 4;
        }

        if (cmd == "eval") {
            if (argc < 3) { usage(); return 1; }
            Runtime rt;
            rt.register_default_engines();
            auto v = rt.evaluate(argv[2]);
            if (auto p = std::get_if<bool>(&v.data))           std::cout << (*p ? "true" : "false") << "\n";
            else if (auto p = std::get_if<std::int64_t>(&v.data)) std::cout << *p << "\n";
            else if (auto p = std::get_if<double>(&v.data))    std::cout << *p << "\n";
            else if (auto p = std::get_if<std::string>(&v.data)) std::cout << *p << "\n";
            else std::cout << "null\n";
            return 0;
        }
    } catch (const ThorError& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 5;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 5;
    }

    usage();
    return 1;
}
