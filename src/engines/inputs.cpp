#include "thor/engines/inputs.hpp"
#include "thor/errors.hpp"
#include "thor/models.hpp"

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

void validate_mass(double mass_kg) {
    if (!std::isfinite(mass_kg) || mass_kg <= 0.0)
        throw SimulationInputError("mass_kg must be finite and greater than zero");
}

void validate_radius(double radius_m) {
    if (!std::isfinite(radius_m) || radius_m <= 0.0)
        throw SimulationInputError("radius_m must be finite and greater than zero");
}

void validate_omega(double omega_rad_s) {
    // Direction is meaningful; only non-finite values are rejected.
    if (!std::isfinite(omega_rad_s))
        throw SimulationInputError("angular_velocity_rad_s must be finite");
}

void validate_force_limit(double max_force_n) {
    if (!std::isfinite(max_force_n) || max_force_n < 0.0)
        throw SimulationInputError("max_force_n must be finite and non-negative");
}

class InputsEngine : public Engine {
public:
    std::string name() const override { return "Inputs"; }
    std::string status() const override { return "simulation input seeding"; }

    EngineResult execute(const EngineRequest& r) override {
        if (r.action == "Set_Rotor_Input")     return set_rotor_input(r);
        if (r.action == "Set_Rotational_Limits") return set_rotational_limits(r);

        EngineResult out;
        out.success = false;
        out.warnings.push_back("Inputs: unknown action '" + r.action + "'");
        return out;
    }

private:
    static EngineResult set_rotor_input(const EngineRequest& r) {
        EngineResult out;
        double mass_kg = 0.0, radius_m = 0.0, omega_rad_s = 0.0;
        if (!arg_double(r, "mass_kg", mass_kg) ||
            !arg_double(r, "radius_m", radius_m) ||
            !arg_double(r, "angular_velocity_rad_s", omega_rad_s)) {
            out.success = false;
            out.warnings.push_back(
                "Inputs: Set_Rotor_Input requires mass_kg, radius_m, angular_velocity_rad_s");
            return out;
        }
        validate_mass(mass_kg);
        validate_radius(radius_m);
        validate_omega(omega_rad_s);

        out.patch.metrics.emplace("Rotor_Input", make_map({
            {"mass_kg",                Value{mass_kg}},
            {"radius_m",               Value{radius_m}},
            {"angular_velocity_rad_s", Value{omega_rad_s}}
        }));
        return out;
    }

    static EngineResult set_rotational_limits(const EngineRequest& r) {
        EngineResult out;
        double max_force_n = 0.0;
        if (!arg_double(r, "max_force_n", max_force_n)) {
            out.success = false;
            out.warnings.push_back(
                "Inputs: Set_Rotational_Limits requires numeric max_force_n");
            return out;
        }
        validate_force_limit(max_force_n);

        out.patch.metrics.emplace("Rotational_Limits", make_map({
            {"max_force_n", Value{max_force_n}}
        }));
        return out;
    }
};

} // namespace

std::shared_ptr<Engine> make_inputs_engine() {
    return std::make_shared<InputsEngine>();
}

} // namespace thor
