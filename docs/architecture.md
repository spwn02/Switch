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
