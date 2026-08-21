# Contributing to Switch

Switch is a C++26 reflection-first testing framework. Contributions should preserve the macro-free model and the separation between discovery metadata, scheduling, execution, and reporting.

## Toolchain

The verified public baseline is GCC 16+ with CMake 4.4+ and Ninja. A reflection-enabled Clang toolchain is also used for development.

## Core invariants

1. Switch production code may depend on Miracle, but never on [Nyx](https://github.com/spwn02/Nyx.git).
2. `Switch::Switch` must remain the canonical target in every consumption mode.
3. Discovery metadata remains immutable; run-specific state belongs to the run/session/attempt pipeline.
4. Parallelism is capability-driven, never guessed.
5. Diagnostics from parallel execution must remain deterministic.
6. Warmups, retries, and measurement samples stay serial unless their semantics explicitly change.
7. Idiomatic C++26 is the baseline; do not add legacy macro-registration fallbacks.

## Build and test

```bash
cmake --preset tests --fresh
cmake --build --preset tests
ctest --preset tests
```

Then validate the release build:

```bash
cmake --preset release --fresh
cmake --build --preset release
```

Before submitting a change:

```bash
git diff --check
```

Prefer focused tests under `tests/`. New public behavior should cover both execution results and, where relevant, deterministic reporting.

Update `CHANGELOG.md` for user-visible or breaking changes and keep examples synchronized with the compiled API.
