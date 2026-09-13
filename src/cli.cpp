#include "thor/cli.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

#include "thor/errors.hpp"
#include "thor/parser.hpp"
#include "thor/runtime.hpp"
#include "thor/state.hpp"
#include "thor/version.hpp"

namespace thor {

namespace {

void usage() {
    std::cout <<
R"(THOR Engine - VAS DSL runtime

Usage:
  thor run   <script.vas> [--permissive]
  thor check <script.vas>
  thor eval  "<expression>"
  thor version
  thor help
)";
}

} // namespace

int run_cli(const std::vector<std::string>& args) {
    if (args.size() < 2) { usage(); return 1; }
    const std::string& cmd = args[1];

    try {
        if (cmd == "version") {
            std::cout << VERSION << "\n" << COPYRIGHT << "\n";
            return 0;
        }
        if (cmd == "help") { usage(); return 0; }

        if (cmd == "check" || cmd == "run") {
            if (args.size() < 3) { usage(); return 1; }
            StateMode mode = StateMode::Strict;
            for (std::size_t i = 3; i < args.size(); ++i) {
                if (args[i] == "--permissive") mode = StateMode::Permissive;
            }
            Runtime rt(mode);
            rt.register_default_engines();
            if (cmd == "check") {
                std::ifstream in(args[2]);
                if (!in) { std::cerr << "Cannot open " << args[2] << "\n"; return 2; }
                Parser{}.parse(in);
                std::cout << "OK: " << args[2] << " parses cleanly.\n";
                return 0;
            }
            auto s = rt.execute_file(args[2]);
            std::cout << "Executed " << s.executed_instructions << " instructions.\n";
            return s.success ? 0 : 4;
        }

        if (cmd == "eval") {
            if (args.size() < 3) { usage(); return 1; }
            Runtime rt;
            rt.register_default_engines();
            auto v = rt.evaluate(args[2]);
            if (auto p = std::get_if<bool>(&v.data))              std::cout << (*p ? "true" : "false") << "\n";
            else if (auto p = std::get_if<std::int64_t>(&v.data)) std::cout << *p << "\n";
            else if (auto p = std::get_if<double>(&v.data))       std::cout << *p << "\n";
            else if (auto p = std::get_if<std::string>(&v.data))  std::cout << *p << "\n";
            else                                                  std::cout << "null\n";
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

} // namespace thor
