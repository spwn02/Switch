# Reference toolchain

This document defines the source-level identity and ownership contract of the C++26 reference toolchain used by this project.

Binary packaging, immutable downloadable snapshots, checksums, and CI distribution are intentionally deferred to the next toolchain phase. This document defines what those artifacts must represent.

## Identity

The reference toolchain is developed in:

```text
repository:
  https://github.com/spwn02/clang-p2996

development branch:
  p2996
```

The repository is a fork of the LLVM monorepo and builds on Bloomberg's Clang/P2996 implementation.

The fork is maintained with a broader practical goal than proposal experimentation alone: provide a stable enough implementation of the complete C++26-and-earlier model for reflection-heavy real-world projects while progressively porting missing language and libc++ facilities as they are encountered.

The `p2996` branch is a mutable development channel. It is not, by itself, an immutable release or CI dependency.

## One toolchain unit

The reference toolchain is not just a `clang++` executable.

A validated toolchain consists of a coherent set built from the same source revision and configuration:

```text
clang / clang++
matching libc++ headers and libraries
matching libc++ module sources and metadata
libc++abi
runtime support required by that libc++/libc++abi build
```

`lld` and other LLVM utilities may be distributed with reference snapshots and may be used by the build, but they are not part of the semantic C++ library contract unless the selected configuration requires them.

The important invariant is coherence:

> The compiler, C++ standard-library headers, standard-library binaries, ABI runtime, and module sources used for one validated build must belong to the same reference-toolchain build.

Do not intentionally combine, for example:

- the reference Clang frontend with unrelated system libstdc++;
- libc++ headers from one fork revision with libc++ binaries from another;
- module sources from one libc++ installation with headers from another;
- a reference compiler snapshot with arbitrary newer `p2996` runtime components.

C++ modules, reflection intrinsics, library feature macros, ABI configuration, and in-progress C++26 facilities make such mixes especially fragile.

## Standard model

The reference fork exists to implement the `master` contract:

```text
complete standardized C++26
+
all standardized facilities from earlier C++ revisions
```

This is intentionally an aspirational implementation target.

When `master` needs a standardized feature that the fork does not yet provide, the preferred development path is:

```text
standardized feature needed by master
              |
              v
implement / port / stabilize it in clang-p2996 or its libc++
              |
              v
validate the toolchain
              |
              v
use the feature normally from master
```

The library is not expected to permanently carry a substitute merely because mainstream implementations have not reached that part of the standard yet.

## Reflection mode

The reference Clang family exposes the current reflection feature set through:

```text
-freflection-latest
```

The project treats this as the reference-fork feature switch, not as part of the portable public API.

Source code should target standardized C++ syntax and semantics. Build configuration is responsible for selecting whatever compiler flags the current reference implementation requires.

As the implementation converges with upstream standardized compiler modes, reference-specific flags may disappear without changing the public library contract.

## Standard library and `import std`

The matching libc++ is part of the reference toolchain because the project deliberately uses modern standard-library facilities and C++ modules.

A usable reference installation must provide everything necessary for CMake's native C++ module support to compile:

```cpp
import std;
```

for C++26 mode, including the matching libc++ module sources/metadata expected by the compiler installation.

A compiler that supports the required reflection syntax but lacks the required C++26 standard-library surface is not a complete reference toolchain.

Likewise, a library implementation with the required headers but an incompatible compiler frontend is not sufficient.

## Toolchain ownership

Miracle, Switch, and Nyx do not own compiler installation.

Their normal CMake projects must not:

- clone LLVM automatically;
- build Clang or libc++ as an ordinary project dependency;
- change `CMAKE_CXX_COMPILER` after language enablement;
- silently replace the user's selected standard library;
- contain machine-specific absolute paths to a developer toolchain.

The toolchain must be selected before the top-level CMake `project()` enables C++.

For local development this may be done through environment selection:

```bash
CC=/path/to/reference/bin/clang \
CXX=/path/to/reference/bin/clang++ \
cmake --preset tests --fresh
```

or through a developer-owned `CMakeUserPresets.json` / CMake toolchain file.

Checked-in project presets remain machine-independent.

It is planned to make this selection reproducible for CI by defining immutable binary snapshots and a stable setup mechanism.

## Source channel versus snapshots

Two identities must remain distinct:

```text
p2996
  mutable development branch

p2996-YYYY.MM.DD
  immutable validated toolchain snapshot
```

If more than one snapshot is required on the same date, an additional monotonic suffix may be used:

```text
p2996-YYYY.MM.DD.2
```

The exact snapshot format will be implemented in the future, but these rules are locked now:

1. A snapshot identifies one exact source commit.
2. Published snapshot bytes are immutable.
3. Rebuilding different bytes requires a new snapshot identifier.
4. Snapshot metadata records the source commit.
5. Snapshot metadata records artifact checksums.
6. CI and released library versions pin snapshots, never the moving `p2996` branch.
7. The development branch remains free to advance independently after a snapshot is published.

## Validation scope

The initial reference validation platform is:

```text
host/target:
  Linux x86_64
```

This is a validation scope, not a permanent architecture restriction.

Additional hosts and targets may be added once the reference toolchain and project test suites are reproducible there. They should not be advertised as validated merely because the compiler can theoretically target them.

## Toolchain candidate promotion

A new reference snapshot should move through this validation direction:

```text
clang-p2996 / libc++ candidate
          |
          v
compiler + libc++ regression tests
          |
          v
Miracle
          |
          v
Switch
          |
          v
Nyx integration
          |
          v
publish immutable reference snapshot
```

A project regression discovered during this process may reveal either:

- a library/framework bug; or
- a compiler/libc++ regression.

The purpose of using Miracle, Switch, and Nyx in the toolchain validation stack is to catch both.

Switch is particularly valuable here because its reflection-heavy discovery and metadata pipeline exercises compiler behavior beyond isolated proposal tests.

## Release relationship

Official Miracle and Switch releases are built from `master`.

While the reference implementation remains ahead of mainstream compiler support, release metadata should record the exact validated reference snapshot.

Conceptually:

```text
Switch vX.Y.Z
  reference toolchain: p2996-YYYY.MM.DD
  source revision:     <toolchain commit>
```

The release does not vendor or redistribute compiler BMIs.

## BMI / PCM policy

Binary module interface artifacts are compiler- and configuration-specific build products.

They are never considered portable release artifacts of Miracle or Switch.

The projects distribute module source and CMake module metadata. A consumer's selected compatible toolchain builds the corresponding BMI/PCM artifacts locally.

## Status language

The reference fork is experimental relative to upstream Clang and the finalized implementation state of C++26.

That does not mean the project should describe it as disposable prototype infrastructure.

The intended description is:

> An actively maintained Clang/P2996 fork focused on stability, C++26 library coverage, and real-world reflection-heavy applications.

Miracle, Switch, and Nyx use it for serious development and regression validation.

Claims such as "production-ready compiler" should be reserved until a substantially stronger compatibility, platform, ABI, diagnostics, sanitizer, and regression-support bar has been intentionally established.

## Provenance

The fork owes its reflection foundation to Bloomberg's Clang/P2996 project and ultimately to LLVM/Clang/libc++.

That provenance should remain explicit in compiler-facing documentation. Stability fixes, C++26 library work, and real-world regression coverage added in `spwn02/clang-p2996` extend that work rather than erase its origin.
