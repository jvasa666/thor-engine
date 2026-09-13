#pragma once

#include <memory>

#include "thor/contracts.hpp"

namespace thor {

std::shared_ptr<Engine> make_dialogue_engine();
std::shared_ptr<Engine> make_codex_engine();
std::shared_ptr<Engine> make_observe_engine();

} // namespace thor
