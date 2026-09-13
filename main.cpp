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
#include "thor/executor.hpp"
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
