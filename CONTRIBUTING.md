# Contributing to Switch

Switch is a C++26 reflection-first testing framework. Contributions should preserve the macro-free model and the separation between discovery metadata, scheduling, execution, and reporting.

## Toolchain

Development of `master` targets the complete C++26-and-earlier model. The current reference implementation is [`spwn02/clang-p2996:p2996`](https://github.com/spwn02/clang-p2996/tree/p2996) together with the matching libc++ from that same toolchain build.

Use CMake 4.4+ and Ninja. Select the reference compiler before the top-level CMake configuration; do not add machine-specific compiler paths to checked-in presets.

See [`docs/compiler-support.md`](docs/compiler-support.md) and [`docs/reference-toolchain.md`](docs/reference-toolchain.md) before making compiler-compatibility, reflection-lowering, or standard-library fallback changes.

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
