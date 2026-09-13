#include "thor/runtime.hpp"
#include "thor/errors.hpp"
#include "thor/engines/rotational_dynamics.hpp"
#include "thor/engines/inputs.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace thor {

Runtime::Runtime(StateMode m)
    : st_(default_metric_schema(), m), ev_(st_) {}

void Runtime::register_default_engines() {
    reg_.add(make_dialogue_engine());
    reg_.add(make_codex_engine());
    reg_.add(make_observe_engine());
    reg_.add(make_rotational_dynamics_engine());
    reg_.add(make_inputs_engine());
}

ExecutionSummary Runtime::execute_text(const std::string& src) {
    Program p = Parser{}.parse_text(src);
    Executor ex(st_, reg_, ev_);
    return ex.run(p, "exec");
}

ExecutionSummary Runtime::execute_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw ThorError("Cannot open " + path);
    std::stringstream ss; ss << in.rdbuf();
    return execute_text(ss.str());
}

} // namespace thor
