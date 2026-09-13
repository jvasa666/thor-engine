// Standalone smoke test for InputsEngine.

#include "thor/engines/inputs.hpp"
#include "thor/errors.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <variant>

using namespace thor;

namespace {

int failures = 0;

void check(bool condition, const char* label) {
    std::printf("  %s  %s\n", condition ? "PASS" : "FAIL", label);
    if (!condition) ++failures;
}

EngineRequest rotor_req(double m, double r, double w) {
    EngineRequest q;
    q.action = "Set_Rotor_Input";
    q.arguments["mass_kg"]                = Value{m};
    q.arguments["radius_m"]               = Value{r};
    q.arguments["angular_velocity_rad_s"] = Value{w};
    return q;
}

EngineRequest limits_req(double f) {
    EngineRequest q;
    q.action = "Set_Rotational_Limits";
    q.arguments["max_force_n"] = Value{f};
    return q;
}

} // namespace

int main() {
    auto eng = make_inputs_engine();

    std::printf("Test: WritesRotorInputMap\n");
    {
        auto r = eng->execute(rotor_req(12.5, 0.8, 9.0));
        check(r.success, "success");
        auto it = r.patch.metrics.find("Rotor_Input");
        check(it != r.patch.metrics.end(), "Rotor_Input present");
        if (it != r.patch.metrics.end()) {
            auto mp = std::get_if<Value::MapPtr>(&it->second.data);
            check(mp && *mp, "Rotor_Input is a map");
            if (mp && *mp) {
                check((*mp)->count("mass_kg") == 1, "mass_kg present");
                check((*mp)->count("radius_m") == 1, "radius_m present");
                check((*mp)->count("angular_velocity_rad_s") == 1, "omega present");
            }
        }
    }

    std::printf("Test: RejectsMissingRotorInputArgument\n");
    {
        EngineRequest q; q.action = "Set_Rotor_Input";
        q.arguments["mass_kg"] = Value{12.5};
        // radius_m and omega omitted
        auto r = eng->execute(q);
        check(!r.success, "success == false on missing arg");
        check(r.patch.metrics.empty(), "no patch produced");
    }

    std::printf("Test: WritesRotationalLimitsMap\n");
    {
        auto r = eng->execute(limits_req(1000.0));
        check(r.success, "success");
        auto it = r.patch.metrics.find("Rotational_Limits");
        check(it != r.patch.metrics.end(), "Rotational_Limits present");
        if (it != r.patch.metrics.end()) {
            auto mp = std::get_if<Value::MapPtr>(&it->second.data);
            check(mp && *mp, "Rotational_Limits is a map");
            if (mp && *mp) {
                auto fit = (*mp)->find("max_force_n");
                check(fit != (*mp)->end(), "max_force_n present");
                if (fit != (*mp)->end())
                    check(std::holds_alternative<double>(fit->second.data),
                          "max_force_n is double");
            }
        }
    }

    std::printf("Test: RejectsNegativeForceLimit\n");
    {
        bool threw = false;
        try { eng->execute(limits_req(-1.0)); }
        catch (const SimulationInputError&) { threw = true; }
        check(threw, "SimulationInputError on negative limit");
    }

    std::printf("Test: AcceptsNegativeAngularVelocity\n");
    {
        auto r = eng->execute(rotor_req(12.5, 0.8, -9.0));
        check(r.success, "success");
        auto it = r.patch.metrics.find("Rotor_Input");
        if (it != r.patch.metrics.end()) {
            auto mp = std::get_if<Value::MapPtr>(&it->second.data);
            if (mp && *mp) {
                auto w = (*mp)->find("angular_velocity_rad_s");
                check(w != (*mp)->end(), "omega present");
                if (w != (*mp)->end()) {
                    auto d = std::get_if<double>(&w->second.data);
                    check(d && *d == -9.0, "omega preserved as -9.0");
                }
            }
        }
    }

    std::printf("Test: RejectsZeroMass\n");
    {
        bool threw = false;
        try { eng->execute(rotor_req(0.0, 0.8, 9.0)); }
        catch (const SimulationInputError&) { threw = true; }
        check(threw, "SimulationInputError on zero mass");
    }

    std::printf("Test: RejectsNegativeRadius\n");
    {
        bool threw = false;
        try { eng->execute(rotor_req(12.5, -0.8, 9.0)); }
        catch (const SimulationInputError&) { threw = true; }
        check(threw, "SimulationInputError on negative radius");
    }

    std::printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
