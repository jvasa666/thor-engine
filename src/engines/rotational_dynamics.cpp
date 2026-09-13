#include "thor/engines/rotational_dynamics.hpp"
#include "thor/errors.hpp"
#include "thor/models.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <variant>

namespace thor {

namespace {

bool arg_double(const EngineRequest& r, const char* key, double& out) {
    auto it = r.arguments.find(key);
    if (it == r.arguments.end()) return false;
    if (auto p = std::get_if<double>(&it->second.data))       { out = *p; return true; }
    if (auto p = std::get_if<std::int64_t>(&it->second.data)) { out = static_cast<double>(*p); return true; }
    return false;
}

void validate_input(double mass_kg, double radius_m,
                    double omega_rad_s, double force_limit_n) {
    if (!std::isfinite(mass_kg) || mass_kg <= 0.0)
        throw SimulationInputError("mass_kg must be finite and greater than zero");
    if (!std::isfinite(radius_m) || radius_m <= 0.0)
        throw SimulationInputError("radius_m must be finite and greater than zero");
    if (!std::isfinite(omega_rad_s))
        throw SimulationInputError("angular_velocity_rad_s must be finite");
    if (!std::isfinite(force_limit_n) || force_limit_n < 0.0)
        throw SimulationInputError("force_limit_n must be finite and non-negative");
}

class RotationalDynamicsEngine : public Engine {
public:
    std::string name() const override { return "Rotational"; }
    std::string status() const override { return "simulation-only (point mass)"; }

    EngineResult execute(const EngineRequest& r) override {
        EngineResult out;
        if (r.action != "Compute_PointMass_State") {
            out.success = false;
            out.warnings.push_back("Rotational: unknown action '" + r.action + "'");
            return out;
        }

        double mass_kg = 0.0, radius_m = 0.0, omega_rad_s = 0.0, force_limit_n = 0.0;
        if (!arg_double(r, "mass_kg", mass_kg) ||
            !arg_double(r, "radius_m", radius_m) ||
            !arg_double(r, "angular_velocity_rad_s", omega_rad_s)) {
            out.success = false;
            out.warnings.push_back(
                "Rotational: mass_kg, radius_m, angular_velocity_rad_s required");
            return out;
        }
        arg_double(r, "force_limit_n", force_limit_n);

        validate_input(mass_kg, radius_m, omega_rad_s, force_limit_n);

        const double v  = radius_m * omega_rad_s;
        const double ac = radius_m * omega_rad_s * omega_rad_s;
        const double Fc = mass_kg * ac;
        const double I  = mass_kg * radius_m * radius_m;
        const double L  = I * omega_rad_s;

        // Cross-check the alternate centripetal-force form F_c = m v^2 / r.
        // The two must agree to floating-point tolerance because v = r*omega.
        const double Fc_check = mass_kg * v * v / radius_m;
        const double scale = std::max({1.0, std::fabs(Fc), std::fabs(Fc_check)});
        if (std::fabs(Fc - Fc_check) > 1e-12 * scale)
            throw SimulationInputError(
                "internal consistency check failed: F_c = m*r*w^2 and F_c = m*v^2/r disagree");

        out.patch.metrics.emplace("Rotational_Result", make_map({
            {"model",                         Value{"point_mass"}},
            {"mass_kg",                       Value{mass_kg}},
            {"radius_m",                      Value{radius_m}},
            {"angular_velocity_rad_s",        Value{omega_rad_s}},
            {"tangential_speed_m_s",          Value{v}},
            {"centripetal_acceleration_m_s2", Value{ac}},
            {"centripetal_force_n",           Value{Fc}},
            {"point_mass_inertia_kg_m2",      Value{I}},
            {"angular_momentum_kg_m2_s",      Value{L}},
            {"force_limit_n",                 Value{force_limit_n}},
            {"safe",                          Value{Fc <= force_limit_n}}
        }));
        return out;
    }
};

} // namespace

std::shared_ptr<Engine> make_rotational_dynamics_engine() {
    return std::make_shared<RotationalDynamicsEngine>();
}

} // namespace thor
