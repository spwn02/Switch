# Reference toolchain

This document defines the source-level identity and ownership contract of the C++26 reference toolchain used by this project.

Immutable binary snapshots, checksums, manifests, and CI distribution are implemented. This document defines the coherence and provenance contract those artifacts must preserve.

## Identity

The reference toolchain is developed in:

```text
repository:
  https://github.com/spwn02/clang-cxx26

development branch:
  cxx26
```

The repository is an experimental LLVM fork with a broad C++26 scope. It preserves upstream LLVM history and the Bloomberg-originated reflection implementation; that provenance does not imply Bloomberg endorsement of the fork.

The current development line is synchronized with LLVM/Clang 22.1.8 and continues to expand compiler, libc++, modules, reflection, contracts, and tooling support for real-world C++26 workloads.

The `cxx26` branch is a mutable development channel. It is not, by itself, an immutable release or CI dependency.

## One toolchain unit

The reference toolchain is not just a `clang++` executable.

A validated toolchain consists of a coherent set built from the same source revision and configuration:

```text
clang / clang++
matching libc++ headers and libraries
matching libc++ module sources and metadata
libc++abi
runtime support required by that libc++/libc++abi build
compiler-compatible development tools distributed by the snapshot
```

The packaged `clangd` belongs to this coherence boundary for editor/module use: an unrelated system clangd is not expected to consume PCM/BMI artifacts produced by the reference compiler safely.

`lld` and other LLVM utilities may be distributed with reference snapshots and may be used by the build, but they are not part of the semantic C++ library contract unless the selected configuration requires them.

The important invariant is coherence:

> The compiler, C++ standard-library headers, standard-library binaries, ABI runtime, module sources, and compiler-coupled tooling used for one validated build must belong to the same reference-toolchain build.

Do not intentionally combine, for example:

- the reference Clang frontend with unrelated system libstdc++;
- libc++ headers from one fork revision with libc++ binaries from another;
- module sources from one libc++ installation with headers from another;
- a reference compiler snapshot with arbitrary newer `cxx26` runtime components.
- a system clangd with module artifacts produced by an incompatible reference snapshot.

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
implement / port / stabilize it in clang-cxx26 or its libc++
              |
              v
validate the toolchain
              |
              v
use the feature normally from master
```

Switch is not expected to permanently carry a substitute merely because mainstream implementations have not reached that part of the standard yet.

## Reference-specific mode selection

The current reference fork still requires implementation-specific mode selection for some standardized facilities. That policy belongs to the packaged toolchain, not to Switch's public source contract.

For reflection the reference family exposes the current feature set through:

```text
-freflection-latest
```

The `cxx26-2026.09.05` package also owns its contracts-mode defaults. Switch source should continue to target standardized syntax and semantics rather than reproducing those switches itself.

As implementation converges with upstream standardized compiler modes, reference-specific flags may disappear without changing the public Switch contract.

## Standard library and `import std`

The matching libc++ is part of the reference toolchain because Switch deliberately uses modern standard-library facilities and C++ modules.

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

For local development the packaged snapshot can be activated directly:

```bash
source /path/to/reference/share/clang-cxx26/activate.sh
cmake --preset tests --fresh \
  -DCMAKE_TOOLCHAIN_FILE="$CXX26_CMAKE_TOOLCHAIN_FILE"
```

or through a developer-owned `CMakeUserPresets.json` / CMake toolchain file.

Checked-in project presets remain machine-independent.

CI selects an immutable binary snapshot and verifies its manifest and checksums before use. Local development may still select an explicitly built `cxx26` toolchain.

## Source channel versus snapshots

Two identities must remain distinct:

```text
cxx26
  mutable development branch

cxx26-2026.09.05
  current immutable validated snapshot
```

The historical `p2996-2026.08.23.2` snapshot predates the `clang-cxx26` rebrand and remains immutable provenance for releases that recorded it. It has been superseded as the current validation baseline, not renamed or rewritten.

Current snapshots use the `cxx26-YYYY.MM.DD[.N]` identity established by the toolchain packager. Published snapshot identifiers remain immutable.

The snapshot rules are:

1. A snapshot identifies one exact source commit.
2. Published snapshot bytes are immutable.
3. Rebuilding different bytes requires a new snapshot identifier.
4. Snapshot metadata records the source commit.
5. Snapshot metadata records artifact checksums.
6. CI and future library/framework releases pin snapshots, never the moving `cxx26` branch.
7. The development branch remains free to advance independently after a snapshot is published.

The currently validated reference is:

```text
snapshot: cxx26-2026.09.05
source revision: 6c7ef6afbfd8456c964c7a2625b3ea2aaa7d613f
asset: clang-cxx26-2026.09.05-linux-x86_64
```

## Validation scope

The initial reference validation platform is:

```text
host/target:
  Linux x86_64
```

This is a validation scope, not a permanent architecture restriction.

Additional hosts and targets may be added once the reference toolchain and project test suites are reproducible there. They should not be advertised as validated merely because the compiler can theoretically target them.

## Toolchain candidate promotion

A new reference snapshot moves through this validation direction:

```text
clang-cxx26 / libc++ candidate
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

## Miracle dependency alignment

Switch has one public production dependency: Miracle.

Development validation and release provenance deliberately use different immutable identities:

```text
Switch master CI
  Miracle: exact tested master commit

Switch release
  Miracle: declared release tag
           + exact peeled release commit in release metadata

Switch gcc
  Miracle: exact tested gcc commit
```

This keeps development validation current without making a moving branch part of configuration or release provenance.

## Release relationship

Official Switch releases are built from `master`.

Release workflows verify that the tag resolves to `master` history and attach deterministic `release-metadata.json`. The metadata records the exact Switch source commit, the exact Miracle release commit, and the immutable validated reference snapshot, source revision, and artifact identity.

For future releases from the current baseline:

```text
Switch release
  source branch:         master
  reference repository: spwn02/clang-cxx26
  development branch:   cxx26
  reference snapshot:   cxx26-2026.09.05
  toolchain revision:   6c7ef6afbfd8456c964c7a2625b3ea2aaa7d613f
```

The already-published `v0.1.0-rc.1` remains immutable and continues to describe the historical reference snapshot recorded when it was released.

The release does not vendor or redistribute compiler BMIs, and the non-release-bearing `gcc` compatibility branch never produces parallel release artifacts.

## BMI / PCM policy

Binary module interface artifacts are compiler- and configuration-specific build products.

They are never considered portable release artifacts of Miracle or Switch.

The projects distribute module source and CMake module metadata. A consumer's selected compatible toolchain builds the corresponding BMI/PCM artifacts locally.

## Status language

The reference fork is experimental relative to upstream Clang and the finalized implementation state of C++26.

That does not mean the project should describe it as disposable prototype infrastructure.

The intended description is:

> An actively maintained experimental LLVM/Clang C++26 fork focused on standards coverage, stability, and real-world reflection-heavy applications.

Miracle, Switch, and Nyx use it for serious development and regression validation.

Claims such as "production-ready compiler" should be reserved until a substantially stronger compatibility, platform, ABI, diagnostics, sanitizer, and regression-support bar has been intentionally established.

## Provenance

The fork owes its reflection foundation to Bloomberg's Clang/P2996 project and ultimately to LLVM/Clang/libc++.

That provenance remains explicit even though the project is now `spwn02/clang-cxx26` and has progressed far beyond proposal-only work. The rebrand broadens scope; it does not erase the origin of the reflection implementation or imply Bloomberg endorsement.
