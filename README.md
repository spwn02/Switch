Switch is a macro-free C++26 testing framework built around static reflection.

Tests are ordinary functions marked with annotations. Discovery, parameterized cases, fixtures, execution policy, diagnostics, measurement, crash isolation, and reporting are expressed through C++ rather than preprocessor registration macros.

> **Status:** pre-1.0. The framework is actively evolving and does not promise source compatibility until 1.0.

## Requirements

Switch currently targets:

- the complete C++26-and-earlier language and standard-library model
- CMake 4.4 or newer
- Ninja
- the [`spwn02/clang-p2996:p2996`](https://github.com/spwn02/clang-p2996/tree/p2996) compiler and matching libc++ as the current reference toolchain

Switch has one public production dependency: [Miracle](https://github.com/spwn02/Miracle).

`master` is intentionally free to adopt any standardized facility through C++26 as soon as it improves the design, even when mainstream compiler/library implementations have not caught up yet. GCC compatibility is developed separately and may trail `master`.

See [`docs/compiler-support.md`](docs/compiler-support.md) for the compiler, branch, compatibility, and release policy.

## Quick start

```cpp
import std;
import Miracle; // For type aliases. Not required
import Switch;

using namespace Miracle;
using namespace Switch;

namespace Tests {

[[= test]] auto arithmetic() -> void {
  check(2 + 2 == 4_exp);
}

auto fibonacci(u64 number) -> u64 {
  return number < 2 ? number : fibonacci(number - 1) + fibonacci(number - 2);
}

[[= test]] auto fibonacciTests() {
  check(fibonacci(1) == 1_exp);
  check(fibonacci(5) == 5_exp);
}

}

consteval {
  discover<^^Tests>();
}

auto main() -> int {
  Reporter reporter{};
  RunOptions options{
      .threads = 1,
      .isolation = CrashIsolation::InProcess,
  };

  const RunReport report = runAll(reporter, std::cout, TestSelection{}, options);
  return report.passed() ? 0 : 1;
}
```

The compilable version lives in [`examples/quickstart.cxx`](examples/quickstart.cxx).

## What Switch provides

- reflection-driven `[[= test]]` discovery
- macro-free assertions and expressions
- parameterized `Case{...}` expansion
- fixtures, explicit member subjects, groups, tags, and resource lanes
- synchronous and coroutine-based tests
- real and deterministic virtual time
- timeout/retry/warmup/repeat policies
- deterministic independent-case parallelism
- process-per-case native fault isolation
- bounded result retention and streaming reporting
- human diagnostics and JSON output
- benchmark/measurement summaries and profiling capture

## Public module and target

```cpp
import Switch;
```

```cmake
Switch::Switch
```

Switch publicly links `Miracle::Miracle`.

## Consuming Switch

### Existing source checkout

If `Miracle::Miracle` already exists, Switch reuses it:

```cmake
add_subdirectory(path/to/Miracle)
add_subdirectory(path/to/Switch)
target_link_libraries(my_tests PRIVATE Switch::Switch)
```

### FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
  Switch
  GIT_REPOSITORY https://github.com/spwn02/Switch.git
  GIT_TAG <commit-or-release-tag>
)
FetchContent_MakeAvailable(Switch)

target_link_libraries(my_tests PRIVATE Switch::Switch)
```

Switch resolves Miracle target-first, then through an installed package, a source override/sibling checkout, and finally FetchContent.

### Installed package

```cmake
find_package(Switch 0.1.0 CONFIG REQUIRED)
target_link_libraries(my_tests PRIVATE Switch::Switch)
```

The installed Switch package resolves its public Miracle dependency with `find_dependency`.

## Building Switch

```bash
cmake --preset debug
cmake --build --preset debug
```

Run self-tests and compile the quickstart example with:

```bash
cmake --preset tests --fresh
cmake --build --preset tests
ctest --preset tests
```

## Configuration

`SWITCH_WARNINGS_AS_ERRORS=ON` applies only while compiling Switch itself.

`SWITCH_BUILD_EXAMPLES=ON` builds repository examples.

`SWITCH_BUILD_TESTS=ON` builds Switch's self-test suite. It defaults to enabled when Switch is the top-level project and disabled when Switch is consumed as a dependency.

## Architecture

See [`docs/architecture.md`](docs/architecture.md).

See [`docs/compiler-support.md`](docs/compiler-support.md) for the C++26 reference-toolchain and deferred GCC compatibility contract.

## Contributing and security

See [`CONTRIBUTING.md`](CONTRIBUTING.md) and [`SECURITY.md`](SECURITY.md).

## License

Apache License 2.0.
