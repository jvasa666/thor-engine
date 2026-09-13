// Standalone smoke test for RotationalDynamicsEngine.
// Verifies numeric output against the worked example from the formula sheet:
//   m = 12.5 kg, r = 0.8 m, omega = 9 rad/s
//   v   = 7.2   m/s
//   a_c = 64.8  m/s^2
//   F_c = 810   N
//   I   = 8     kg m^2
//   L   = 72    kg m^2 / s

#include "thor/engines/rotational_dynamics.hpp"
#include "thor/errors.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <variant>

using namespace thor;

namespace {

int failures = 0;

void check(bool condition, const char* label) {
    if (condition) {
        std::printf("  PASS  %s\n", label);
    } else {
        std::printf("  FAIL  %s\n", label);
        ++failures;
    }
}

void check_close(double got, double want, double tol, const char* label) {
    const double diff = std::fabs(got - want);
    if (diff <= tol) {
        std::printf("  PASS  %s (got %.12g)\n", label, got);
    } else {
        std::printf("  FAIL  %s (got %.12g, want %.12g, diff %.3g)\n",
                    label, got, want, diff);
        ++failures;
    }
}

double get_num(const EngineResult& r, const char* key) {
    auto it = r.patch.metrics.find("Rotational_Result");
    if (it == r.patch.metrics.end()) { std::printf("missing metric\n"); return NAN; }
    auto p = std::get_if<Value::MapPtr>(&it->second.data);
    if (!p || !*p) { std::printf("metric not a map\n"); return NAN; }
    auto it2 = (*p)->find(key);
    if (it2 == (*p)->end()) { std::printf("missing key %s\n", key); return NAN; }
    if (auto d = std::get_if<double>(&it2->second.data)) return *d;
    if (auto i = std::get_if<std::int64_t>(&it2->second.data)) return static_cast<double>(*i);
    return NAN;
}

bool get_bool(const EngineResult& r, const char* key) {
    auto it = r.patch.metrics.find("Rotational_Result");
    if (it == r.patch.metrics.end()) return false;
    auto p = std::get_if<Value::MapPtr>(&it->second.data);
    if (!p || !*p) return false;
    auto it2 = (*p)->find(key);
    if (it2 == (*p)->end()) return false;
    if (auto b = std::get_if<bool>(&it2->second.data)) return *b;
    return false;
}

EngineRequest make_request(double mass, double radius, double omega,
                           double force_limit) {
    EngineRequest r;
    r.action = "Compute_PointMass_State";
    r.arguments["mass_kg"]                 = Value{mass};
    r.arguments["radius_m"]                = Value{radius};
    r.arguments["angular_velocity_rad_s"]  = Value{omega};
    r.arguments["force_limit_n"]           = Value{force_limit};
    return r;
}

} // namespace

int main() {
    auto eng = make_rotational_dynamics_engine();

    // ---- Test 1: worked example from the formula sheet ----
    std::printf("Test: worked example m=12.5 r=0.8 omega=9 limit=1000\n");
    {
        auto rq = make_request(12.5, 0.8, 9.0, 1000.0);
        auto r  = eng->execute(rq);
        check(r.success, "engine reports success");
        check_close(get_num(r, "tangential_speed_m_s"),          7.2,  1e-12, "v = r*omega");
        check_close(get_num(r, "centripetal_acceleration_m_s2"), 64.8, 1e-12, "a_c = r*omega^2");
        check_close(get_num(r, "centripetal_force_n"),           810.0, 1e-12, "F_c = m*r*omega^2");
        check_close(get_num(r, "point_mass_inertia_kg_m2"),      8.0,  1e-12, "I = m*r^2");
        check_close(get_num(r, "angular_momentum_kg_m2_s"),      72.0, 1e-12, "L = I*omega");
        check(get_bool(r, "safe"),                                     "safe = (F_c <= limit)");
    }

    // ---- Test 2: force limit exceeded ----
    std::printf("Test: force limit exceeded\n");
    {
        auto rq = make_request(12.5, 0.8, 9.0, 500.0);
        auto r  = eng->execute(rq);
        check(r.success, "engine reports success");
        check(!get_bool(r, "safe"), "safe = false when F_c > limit");
    }

    // ---- Test 3: zero mass rejected ----
    std::printf("Test: zero mass rejected\n");
    {
        auto rq = make_request(0.0, 0.8, 9.0, 1000.0);
        bool threw = false;
        try { eng->execute(rq); } catch (const SimulationInputError&) { threw = true; }
        check(threw, "SimulationInputError on mass_kg = 0");
    }

    // ---- Test 4: negative radius rejected ----
    std::printf("Test: negative radius rejected\n");
    {
        auto rq = make_request(12.5, -0.8, 9.0, 1000.0);
        bool threw = false;
        try { eng->execute(rq); } catch (const SimulationInputError&) { threw = true; }
        check(threw, "SimulationInputError on radius_m < 0");
    }

    // ---- Test 5: non-finite angular velocity rejected ----
    std::printf("Test: non-finite omega rejected\n");
    {
        auto rq = make_request(12.5, 0.8,
                               std::numeric_limits<double>::quiet_NaN(), 1000.0);
        bool threw = false;
        try { eng->execute(rq); } catch (const SimulationInputError&) { threw = true; }
        check(threw, "SimulationInputError on NaN omega");
    }

    // ---- Test 6: missing required argument ----
    std::printf("Test: missing required argument\n");
    {
        EngineRequest rq;
        rq.action = "Compute_PointMass_State";
        rq.arguments["mass_kg"] = Value{12.5};
        // radius_m and angular_velocity_rad_s omitted
        auto r = eng->execute(rq);
        check(!r.success, "engine reports failure on missing args");
    }

    std::printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
