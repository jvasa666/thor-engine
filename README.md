# THOR Engine

A deterministic supervisory orchestration runtime for the VAS DSL.

THOR reads scripts written in a small line-oriented language, compiles
them to a program of instructions, and executes them against a typed
runtime state. It is not a control system for physical hardware. It has
no real-time guarantees, no hardware I/O, and no safety-rated
interlocks. It is the software layer that would sit *above* such
systems if they existed: sequencing, conditionals, loops, state
tracking, engine dispatch.

## Status

Modular C++17. Parser, executor, expression evaluator, state, engines,
and CLI each have their own translation unit. Builds clean under
`-Wall -Wextra -Wpedantic` on GCC 15.2.

Two smoke tests run under CTest and pass:

    ctest --test-dir build --output-on-failure

**No hardware integration, no scientific validation, no safety-rated
behavior.** The rotational dynamics engine is a simulation-only
point-mass calculator. It does not model a real apparatus.

## Build

Requires a C++17 compiler and CMake 3.20 or newer.

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel

Produces `build/thor`, `build/rotational_smoke`, `build/inputs_smoke`,
and copies `demo.vas` next to the binary.

To build without test targets:

    cmake -S . -B build -DTHOR_BUILD_TESTS=OFF
    cmake --build build --parallel

## Test

    cmake -S . -B build -DTHOR_BUILD_TESTS=ON
    cmake --build build --parallel
    ctest --test-dir build --output-on-failure

Expected:

    Test project .../build
        Start 1: rotational_smoke
    1/2 Test #1: rotational_smoke .........  Passed
        Start 2: inputs_smoke
    2/2 Test #2: inputs_smoke .............  Passed

    100% tests passed, 0 tests failed out of 2

## Run

    ./build/thor version
    ./build/thor help
    ./build/thor eval "2 + 3 * 4"
    ./build/thor check demo.vas
    ./build/thor run demo.vas

Expected output of `run demo.vas`:

    [Dialogue] Boot sequence started.
    [Dialogue] System degraded.
    [Dialogue] Tick 0
    [Dialogue] Tick 1
    [Dialogue] Tick 2
    Executed 12 instructions.

`System degraded` appears because `system_health` defaults to `0.0`,
which fails the `> 0.5` test in the script. That is the `Else` branch
running correctly, not a bug.

## Example scripts

`examples/rotational_literals.vas` calls the rotational engine with
literal arguments and prints the results:

    ./build/thor run examples/rotational_literals.vas

`examples/rotational_state_driven.vas` seeds input metrics via the
`Inputs` engine, then reads them back as `${...}` placeholders for the
rotational engine:

    ./build/thor run examples/rotational_state_driven.vas

Both scripts compute the same worked example:

    m = 12.5 kg, r = 0.8 m, omega = 9.0 rad/s
    v   = 7.2   m/s
    a_c = 64.8  m/s^2
    F_c = 810   N
    I   = 8     kg m^2     (point mass only)
    L   = 72    kg m^2 / s
    safe = true under a 1000 N limit

## Script language

Line-oriented. Comments start with `#` unless inside a quoted string.
Statements:

    Block <name>: Start
    Block <name>: End
    Dialogue Say message: "text"
    If <expression>:
    Else:
    If End
    Loop Iterations: <expression> AS: "<variable>"
    Loop End
    Break
    Continue
    <Pillar> <Action> [Key: value]...

Each statement fits on one line. Multi-line argument continuation is
not supported by the current parser.

Expressions support integer and floating literals, quoted strings,
booleans, `+ - * / %`, `== != < <= > >=`, `and or not`, parentheses,
and `${path.to.metric}` placeholders. String literals containing
`${...}` are interpolated at call time. `and`/`or` are evaluated
lazily. Division is floating-point; large integer operands may lose
precision.

## Engines

| Engine       | Actions                                   | Writes                |
|--------------|-------------------------------------------|-----------------------|
| Dialogue     | Say                                       | (none)                |
| Codex        | Store_Knowledge, Retrieve_Knowledge       | (internal store)      |
| Observe      | any                                       | Observe_Result        |
| Inputs       | Set_Rotor_Input, Set_Rotational_Limits    | Rotor_Input, Rotational_Limits |
| Rotational   | Compute_PointMass_State                   | Rotational_Result     |

The `Inputs` engine is deliberately narrow. It does not write arbitrary
metrics; only the two declared input maps. Other engines write only
their own result metrics.

**Known limitation:** metric ownership is enforced by convention, not
by the runtime. The schema records names and types; it does not record
which engine is allowed to write which metric. A future extension
should associate each metric with allowed writers and enforce that
during patch application.

## Architecture

    include/thor/
      errors.hpp              exception hierarchy
      version.hpp             version and copyright constants
      models.hpp              Value, Instruction, Program
      state.hpp/.cpp          RuntimeState: typed metrics and variables
      contracts.hpp           Engine, EngineRequest, EngineResult
      registry.hpp/.cpp       EngineRegistry
      expression_ast.hpp      AST node types
      expressions.hpp/.cpp    ExpressionEvaluator
      engines.hpp/.cpp        Dialogue, Codex, Observe
      parser.hpp/.cpp         source text -> Program
      executor.hpp/.cpp       Program -> RuntimeState mutations
      runtime.hpp/.cpp        binds state, registry, parser, executor
      cli.hpp/.cpp            argument dispatch
      engines/
        inputs.hpp/.cpp              Rotor_Input seeding
        rotational_dynamics.hpp/.cpp Point-mass rotational model

    src/
      main.cpp                argv -> run_cli()

    tests/
      rotational_smoke.cpp    arithmetic and input rejection
      inputs_smoke.cpp        input-engine behavior

The dependency graph is layered: nothing includes anything from a lower
layer. `parser.cpp` has no knowledge of `RuntimeState`; `executor.cpp`
has no knowledge of file I/O; `cli.cpp` is the only place that reads
`argv`.

CMake builds `thor_core` (all implementation files except the entry
point) as a static library. The `thor` executable and both smoke tests
link against it, so test and production code compile from identical
object files.

## Copyright

Copyright (c) 2026 Joseph Vasapolli. All rights reserved.
