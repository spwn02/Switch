# Switch architecture

## Product boundary

Switch is an independent idiomatic C++26 testing framework.

Canonical identities:

```cpp
import Switch;
```

```cmake
Switch::Switch
```

There are no `Nyx::Test` compatibility aliases.

## Language and toolchain philosophy

`master` targets the complete standardized C++26-and-earlier capability set, not merely the subset implemented by today's compilers or the subset Switch currently happens to use.

Switch may adopt any standardized facility through C++26 whenever it produces a better testing model. If the current reference toolchain lacks that facility, the preferred response is to implement or port the missing standard behavior into the toolchain rather than weaken `master`.

The current reference implementation is [`spwn02/clang-p2996:p2996`](https://github.com/spwn02/clang-p2996/tree/p2996) together with its matching libc++. GCC compatibility is a deferred implementation concern and may trail `master`.

Where GCC lacks direct reflected parameter annotations, the existing `[[= arg<"...">(...)]]` representation is the explicit compatibility path. It must normalize into the same parameter-binding model as the canonical direct parameter annotation API.

The complete branch and compiler policy is defined in [`compiler-support.md`](compiler-support.md).


## Production dependency graph

```text
Switch::Switch
      |
      +-- PUBLIC --> Miracle::Miracle
```

Dependency resolution is target-first so a parent build can provide one Miracle instance and Switch will reuse it.

## Execution pipeline

```text
Discovery
  -> RunSession
  -> InvocationPlan
  -> AttemptOutcome
  -> CaseAccumulator
  -> RunAccumulator
  -> RunReport
  -> human / JSON renderers
```

Registration metadata describes tests. Scheduling state, subjects, fixtures, resources, attempts, captured diagnostics, and measurements belong to a run.

## Capabilities and subjects

Member tests require an explicit `subject(...)`; Switch does not invent an implicit object.

Scheduling eligibility is capability-driven. Relevant capabilities include shared `once` fixture lifetime, mutable subject ownership, exclusive resource lanes, measurement dependencies, crash/isolation requirements, and attempt-level parallelism permission.

## Deterministic parallelism

Independent logical cases may execute concurrently only when their capabilities are compatible.

Warmups, retry chains, and repeated measurement samples remain serial by default. Per-task seeds are derived deterministically from the run seed and case identity. Parallel diagnostics are retained/rendered in deterministic logical-case order.

## Retention and reporting

The default retention policy favors compact success results and full failure diagnostics. `RetentionPolicy::All` is explicit. Human and JSON reporting consume the same presentation-independent `RunReport` tree.

## Crash isolation

`CrashIsolation::ProcessPerCase` places a native-fault boundary around logical cases. Trusted tests and microbenchmarks can explicitly select `InProcess`.

## Package identity

Source checkout, FetchContent, and installed-package consumers all receive `Switch::Switch`. The installed package declares Miracle as a public dependency.

## Compatibility

Switch is pre-1.0. Breaking changes are permitted while the API is being stabilized, but they should be recorded in `CHANGELOG.md` and covered by tests.

Compiler compatibility follows a separate rule: `master` defines the C++26-first product, while the non-release-bearing `gcc` compatibility line adapts that design to the capabilities available in GCC/libstdc++. Compatibility work flows from `master` toward `gcc`, not back into the primary architecture.
