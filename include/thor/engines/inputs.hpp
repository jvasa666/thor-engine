#pragma once

#include <memory>

#include "thor/contracts.hpp"

namespace thor {

// Simulation input seeding engine.
//
// Writes configuration/input metrics that scripts are permitted to set.
// Deliberately narrow: only the two actions below are supported, and each
// writes exactly one declared metric. This is not a general-purpose
// state-mutation primitive. Scripts must not be able to write engine-owned
// results (Rotational_Result, Validation_Result, safety state) directly.
//
// Known limitation: metric ownership is enforced by convention, not by the
// runtime. The schema records names and types; it does not record who is
// allowed to write each metric. A future schema extension should add that.
std::shared_ptr<Engine> make_inputs_engine();

} // namespace thor
