# THOR Engine

A deterministic supervisory orchestration runtime for the VAS DSL.

THOR reads scripts written in a small line-oriented language, compiles
them to a program of instructions, and executes them against a typed
runtime state. It is not a control system for physical hardware. It has
no real-time guarantees, no hardware I/O, and no safety-rated
interlocks. It is the software layer that would sit *above* such
systems if they existed: sequencing, conditionals, loops, state
tracking, and engine dispatch.

## Status

Modular build. Parser, expression evaluator, state, executor, engines,
and CLI are each in their own translation unit. Builds under C++17
with `-Wall -Wextra -Wpedantic` and zero warnings on GCC 15.2.

**No test suite exists.** Behavior is verified only by running
`demo.vas` and observing its output. Do not trust it in a context
where silent regressions matter until tests are added.

## Build

Requires a C++17 compiler and CMake 3.20 or newer.

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel

Produces `build/thor` and copies `demo.vas` next to it.

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

## Script language

Line-oriented. Comments start with `#` unless inside a quoted string.
Statements:

    Block <name>: Start      # opens a block (currently ignored by runtime)
    Block <name>: End        # closes a block
    Dialogue Say message: "text"
    If <expression>:
        ...
    Else:
        ...
    If End
    Loop Iterations: <expression> AS: "<variable>"
        ...
    Loop End
    Break
    Continue
    <Pillar> <Action> [Key: value]...

Expressions support integer and floating literals, quoted strings,
booleans, `+ - * / %`, `== != < <= > >=`, `and or not`, parentheses,
and `${path.to.metric}` placeholders. String literals containing
`${...}` are interpolated at call time.

## Architecture

    errors.hpp          exception hierarchy
    version.hpp         version and copyright constants
    models.hpp          Value, Instruction, Program
    value_ops.cpp       structural equality for Value
    state.hpp/.cpp      RuntimeState: typed metrics and variables
    contracts.hpp       Engine interface, EngineRequest/Result
    registry.hpp/.cpp   EngineRegistry
    expression_ast.hpp  AST node types
    expressions.hpp/.cpp ExpressionEvaluator
    engines.hpp/.cpp    Dialogue, Codex, Observe
    parser.hpp/.cpp     source text -> Program
    executor.hpp/.cpp   Program -> side effects on RuntimeState
    runtime.hpp/.cpp    binds state + registry + parser + executor
    cli.hpp/.cpp        argument dispatch
    main.cpp            argv -> run_cli()

The dependency graph is layered: nothing includes anything from a
lower layer. `parser.cpp` has no knowledge of `RuntimeState`;
`executor.cpp` has no knowledge of file I/O; `cli.cpp` is the only
place that reads `argv`.

## Copyright

Copyright (c) 2026 Joseph Vasapolli. All rights reserved.
