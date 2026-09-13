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
// ---------- parser ----------
class Parser {
public:
    Program parse_text(const std::string& text) const {
        std::istringstream in(text);
        return parse(in);
    }
    Program parse(std::istream& in) const {
        Program prog;
        std::vector<std::size_t> loops;   // LoopStart indexes
        std::vector<std::size_t> ifs;     // IfStart indexes
        std::vector<bool> if_has_else;

        std::string raw; std::size_t line = 0;
        while (std::getline(in, raw)) {
            ++line;
            std::string s = strip_(raw);
            if (s.empty()) continue;

            if (s.rfind("Block ", 0) == 0) continue; // skip block markers

            if (s == "Break") {
                if (loops.empty()) throw ValidationError("Break outside loop at line " + std::to_string(line));
                Instruction i; i.kind = OpKind::Break; i.loc = {line, 1};
                prog.instructions.push_back(i); continue;
            }
            if (s == "Continue") {
                if (loops.empty()) throw ValidationError("Continue outside loop at line " + std::to_string(line));
                Instruction i; i.kind = OpKind::Continue; i.loc = {line, 1};
                prog.instructions.push_back(i); continue;
            }

            if (s.rfind("Loop Iterations:", 0) == 0) {
                std::string body = s.substr(16);
                auto as_pos = body.find(" AS:");
                if (as_pos == std::string::npos)
                    throw ParseError("Loop missing ' AS:' at line " + std::to_string(line));
                std::string count = trim_(body.substr(0, as_pos));
                std::string varname = trim_(body.substr(as_pos + 4));
                if (varname.size() >= 2 && varname.front() == '"' && varname.back() == '"')
                    varname = varname.substr(1, varname.size() - 2);
                if (count.empty())
                    throw ParseError("Loop count empty at line " + std::to_string(line));
                Instruction i; i.kind = OpKind::LoopStart; i.loc = {line, 1};
                i.loop_count_expr = count; i.loop_var = varname;
                loops.push_back(prog.instructions.size());
                prog.instructions.push_back(i); continue;
            }
            if (s == "Loop End") {
                if (loops.empty()) throw ValidationError("Loop End without Loop at line " + std::to_string(line));
                std::size_t st = loops.back(); loops.pop_back();
                Instruction i; i.kind = OpKind::LoopEnd; i.loc = {line, 1};
                i.jump = st;
                std::size_t end_idx = prog.instructions.size();
                prog.instructions.push_back(i);
                prog.instructions[st].jump = end_idx;
                continue;
            }

            if (s.rfind("If ", 0) == 0 && s.back() == ':') {
                std::string cond = s.substr(3, s.size() - 4);
                if (trim_(cond).empty())
                    throw ParseError("Empty If condition at line " + std::to_string(line));
                Instruction i; i.kind = OpKind::IfStart; i.loc = {line, 1};
                i.condition_expr = cond;
                ifs.push_back(prog.instructions.size());
                if_has_else.push_back(false);
                prog.instructions.push_back(i); continue;
            }
            if (s == "Else:") {
                if (ifs.empty()) throw ValidationError("Else without If at line " + std::to_string(line));
                if (if_has_else.back()) throw ValidationError("Duplicate Else at line " + std::to_string(line));
                if_has_else.back() = true;
                Instruction i; i.kind = OpKind::Else; i.loc = {line, 1};
                i.jump = ifs.back();
                std::size_t else_idx = prog.instructions.size();
                prog.instructions.push_back(i);
                prog.instructions[ifs.back()].alt = else_idx;
                continue;
            }
            if (s == "If End") {
                if (ifs.empty()) throw ValidationError("If End without If at line " + std::to_string(line));
                std::size_t st = ifs.back(); ifs.pop_back();
                bool has_else = if_has_else.back(); if_has_else.pop_back();
                Instruction i; i.kind = OpKind::IfEnd; i.loc = {line, 1};
                std::size_t end_idx = prog.instructions.size();
                prog.instructions.push_back(i);
                if (has_else) {
                    auto else_idx = *prog.instructions[st].alt;
                    prog.instructions[else_idx].jump = end_idx;
                    prog.instructions[st].jump = else_idx;
                } else {
                    prog.instructions[st].jump = end_idx;
                }
                continue;
            }

            // Pillar/Action statement
            std::istringstream ss(s);
            std::string pillar, action;
            ss >> pillar >> action;
            if (pillar.empty() || action.empty())
                throw ParseError("Cannot parse line " + std::to_string(line) + ": " + s);
            Instruction i; i.kind = OpKind::Call; i.loc = {line, 1};
            i.pillar = pillar; i.action = action;

            std::string rest; std::getline(ss, rest);
            // Very small arg parser: Key: "value" or Key: value
            std::size_t p = 0;
            while (p < rest.size()) {
                while (p < rest.size() && std::isspace((unsigned char)rest[p])) ++p;
                std::size_t kstart = p;
                while (p < rest.size() && (std::isalnum((unsigned char)rest[p]) || rest[p] == '_')) ++p;
                if (p == kstart) break;
                std::string key = rest.substr(kstart, p - kstart);
                while (p < rest.size() && std::isspace((unsigned char)rest[p])) ++p;
                if (p >= rest.size() || rest[p] != ':') break;
                ++p;
                while (p < rest.size() && std::isspace((unsigned char)rest[p])) ++p;
                std::string val;
                if (p < rest.size() && (rest[p] == '"' || rest[p] == '\'')) {
                    char q = rest[p];
                    val.push_back(q);              // keep opening quote
                    ++p;
                    while (p < rest.size() && rest[p] != q) val.push_back(rest[p++]);
                    if (p < rest.size()) { val.push_back(q); ++p; }  // keep closing quote
                } else {
                    while (p < rest.size() && !std::isspace((unsigned char)rest[p])) val.push_back(rest[p++]);
                }
                i.args.emplace_back(key, val);
            }
            prog.instructions.push_back(i);
        }
        if (!loops.empty()) throw ValidationError("Unterminated Loop");
        if (!ifs.empty())   throw ValidationError("Unterminated If");
        return prog;
    }

private:
    static std::string strip_(const std::string& raw) {
        // Remove # comment unless inside a quote
        std::string s; bool dq = false, sq = false;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            char c = raw[i];
            if (c == '\\' && i+1 < raw.size() && (dq || sq)) { s.push_back(c); s.push_back(raw[++i]); continue; }
            if (c == '"' && !sq) dq = !dq;
            else if (c == '\'' && !dq) sq = !sq;
            else if (c == '#' && !dq && !sq) break;
            s.push_back(c);
        }
        return trim_(s);
    }
    static std::string trim_(const std::string& s) {
        std::size_t a = 0, b = s.size();
        while (a < b && std::isspace((unsigned char)s[a])) ++a;
        while (b > a && std::isspace((unsigned char)s[b-1])) --b;
        return s.substr(a, b - a);
    }
};

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
