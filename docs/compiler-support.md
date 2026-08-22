# Compiler support

Switch is intentionally developed against the complete C++26-and-earlier standard model rather than the intersection of features implemented by today's mainstream compilers.

## Master contract

`master` represents Switch as it should exist in a future where the complete C++26 language and standard-library surface, together with every earlier C++ standard facility, is implemented and available.

This is deliberately broader than "the capabilities Switch currently happens to use." Switch may adopt any standardized facility through C++26 whenever it produces a better testing model or API. Missing implementation support is a toolchain problem, not a reason to weaken `master`.

The consequences are intentional:

- C++26 is the language baseline.
- Facilities from C++26 and every earlier standard may be used when appropriate.
- Reflection-first APIs remain reflection-first even when mainstream compilers are temporarily behind.
- `master` does not add macro registration or older-language fallbacks to preserve compatibility with incomplete implementations.
- Missing standard facilities may be implemented or ported into the reference toolchain while compiler ecosystems catch up.
- No missing standard-library facility is emulated by injecting declarations or implementations into `namespace std`.

The question for `master` is:

> What should this testing framework look like when C++26 is fully available?

not:

> What subset of the design can every compiler build today?

## Reference toolchain

The current reference implementation is:

```text
compiler:
  https://github.com/swpn02/clang-p2996
  branch: p2996

standard library:
  the matching libc++ tree from the same fork
```

The fork builds on Bloomberg's Clang/P2996 work and is being extended with a focus on compiler stability, real-world reflection-heavy workloads, and progressively broader C++26 libc++ coverage.

"Reference toolchain" does not mean Switch is permanently tied to one compiler. It is the implementation currently used to validate the complete `master` contract.

Other toolchains become eligible for `master` support when they implement the required standardized behavior faithfully. Support is ultimately capability-driven rather than vendor- or version-driven.

The compiler alone is not the reference unit: its matching libc++ headers, binaries, ABI runtime, and C++ module sources/metadata belong to the same validated toolchain build. Deliberately mixing components from unrelated toolchain revisions is unsupported.

The `p2996` branch is the mutable source-development channel, not a CI/release pin. It is planned to introduce immutable `p2996-YYYY.MM.DD` toolchain snapshots. CI and releases will pin those snapshots rather than following the branch.

See [`reference-toolchain.md`](reference-toolchain.md) for the complete identity, provenance, selection, component-coherence, and snapshot contract.

## GCC compatibility branch

The deferred GCC compatibility branch is named:

```text
gcc
```

It may span GCC 16, 17, 18, and later versions.

The branch may trait `master` indefinitely. It advances only as far as Switch's public semantics can be represented faithfully with the capabilities available in GCC and libstdc++.

Normal synchronization direction is:

```text
master --> gcc
```

The entire `gcc` branch is never merged back into `master`.

Compiler-independent fixes found while adapting GCC should be fixed or cherry-picked independently on `master`. GCC-specific compatibility machinery remains on `gcc`.

Once the compatibility branches exist, dependency alignment is:

```text
Switch:master --> Miracle:master
Switch:gcc    --> Miracle:gcc
```

A GCC Switch build must not silently combine `Switch:gcc` with `Miracle:master`

## Semantic compatibility lowering

A compatibility branch may use an alternate existing representation of the same public concept when the canonical C++26 spelling depends on a compiler feature GCC cannot yet expose, provided both forms normalize immediately into the same internal semantic model.

The explicit parameter-provider compatibility path is:

Canonical `master` spelling:

```cpp
auto test(i32 value [[= fromCase]]) -> void;
```

Temporary GCC spelling:

```cpp
[[= arg<"value">(fromCase)]]
auto test(i32 value) -> void;
```

`arg<"...">(...)` is the supported GCC compatibility spelling for provider metadata until GCC can faithfully expose annotations attached directly to reflected function parameters.

This is not a second provider system. Both representations must normalize into the same parameter-binding metadata before case expansion, provider resolution, invocation, diagnostics, or reporting.

Conceptually:

```text
direct parameter annotation ---\
                                +--> ParameterBinding --> shared execution
arg<"name">(...) --------------/
```

The same rule applies to `values`, `files`, `context`, `fromCase`, and other provider metadata supported by the legacy adapter.

Switch must not invent GCC-only public syntax such as preprocessor registration macros or `SWITCH_FROM_CASE(...)`.

When GCC gains the required direct parameter-annotation reflection capability:

1. the relevant capability probe turns green;
2. GCC tests move back to the canonical direct annotation spelling;
3. semantic equivalence with the temporary `arg<"...">(...)` path is verified;
4. the GCC-specific workaround is removed;
5. the `master` implementation replaces it.

Whether `arg<"...">(...)` remains as a general convenience API afterward is a separate API decision and must not be dictated by compiler compatibility.

## Compatibility rules

The GCC branch adapts implementation details to compiler reality; it does not redefine Miracle.

Compatibility work follows these rules:

1. Preserve Switch's public concepts and observable semantics.
2. Keep canonical syntax when GCC can represent it faithfully.
3. Alternate syntax is allowed only when it is an already-supported representation of the same concept and normalizes immediately into the same semantic model.
4. Never introduce macro-based registration or compiler-specific public concepts.
5. Never inject replacement facilities into `namespace std`.
6. Never weaken `master` because GCC/libstdc++ is incomplete.
7. If a feature cannot be represented faithfully, the `gcc` branch remains behind the `master` commit that requires it.
8. Temporary compatibility code is deleted as soon as the corresponding GCC capability becomes sufficient.
9. Compiler versions are hints; actual capabilities determine support.

## Release policy

Official Switch releases are cut from:

```text
master
```

The `gcc` branch is non-release-bearing for now. There are no `-gcc`, `-gcc16` or parallel compatibility releases.

The intended end state is convergence:

```text
GCC/libstdc++ implementation improves
              |
              v
compatibility delta shrinks
              |
              v
GCC passes the master capability contract
              |
              v
GCC joins master CI
              |
              v
gcc branch is retired
```

## Capability-driven support

Compiler support will progressively move from vendor/version checks to focused capability probes.

For Switch these probes must be fine-grained enough to distinguish, among other things:

- core reflection;
- parameter reflection;
- direct annotations;
- `annotations_of` on reflected function parameters;
- annotation extraction;
- `import std`;
- standard-library facilities required by Miracle and Switch.

"Parameter reflection supported" is not equivalent to "direct reflected parameter annotations work." Compatibility documentation and diagnostics should preserve that distinction.

## Module artifacts

C++ module BMI/PCM artifacts are toolchain-local build products, not Switch's distribution interface.

Switch distributes module source and CMake module metadata. Consumers build compiler-specific module artifacts with their own compatible toolchain.

## Nyx

Nyx does not receive a `gcc` branch as of the current moment.

Once `Miracle:gcc` and `Switch:gcc` are sufficiently mature, Nyx may gain a dedicated integration configuration that consumes those branches. A separate Nyx compatibility branch should be introduced only if the engine itself develops unavoidable source-level divergence.
