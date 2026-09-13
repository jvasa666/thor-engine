#pragma once

#include <memory>

#include "thor/contracts.hpp"

namespace thor {

// Simulation-only point-mass rotational dynamics engine.
// Computes quantities from standard classical mechanics:
//   v   = r * omega
//   a_c = r * omega^2
//   F_c = m * r * omega^2 = m * v^2 / r
//   I   = m * r^2           (point-mass only)
//   L   = I * omega
//
// This engine does not model any real apparatus. The moment-of-inertia
// formula is valid only for a point mass; distributed bodies (disk,
// ring, cylinder, sphere, fluid volume) require a different formula.
std::shared_ptr<Engine> make_rotational_dynamics_engine();

} // namespace thor
