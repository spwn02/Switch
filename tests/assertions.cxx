import std;
import Miracle;
import Switch;

using namespace Miracle;
using namespace Switch;

namespace Tests::assertions {

[[ = test, = group("framework"), = tag("assertions") ]] auto checksRecordDiagnostics() -> void {
  constexpr usize expectedAssertions{2};
  constexpr usize expectedFailures{1};
  const auto location = std::source_location::current();
  bool passed{};
  bool failed{};
  TestState state{};

  {
    TestEnvironment environment{};
    EnvironmentBinding binding{environment};
    passed = check(true);
    failed = check(false, location);
    state = environment.state();
  }

  require(passed);
  require(not failed);
  require(state.assertions == expectedAssertions);
  require(state.failedAssertions == expectedFailures);
  require(state.failed());
  require(state.diagnostics.size() == expectedFailures);
  check(state.diagnostics.front().header.code == DiagnosticCode::AssertionFailed);
  check(state.diagnostics.front().details.spans.front().location.line() == location.line());
  check(state.diagnostics.front().details.spans.front().label == "assertion"_exp);
}

[[ = test, = group("framework"), = tag("assertions") ]] auto successfulRequirementsAllowContinuation()
    -> void {
  constexpr usize expectedAssertions{1};
  i32 value{};
  TestState state{};

  {
    TestEnvironment environment{};
    EnvironmentBinding binding{environment};

    Result<i32> result{3};

    require(result);

    value = *result;
    state = environment.state();
  }

  check(value == 3);
  check(state.assertions == expectedAssertions);
  check(state.passed());
  check(state.diagnostics.empty());
}

[[ = test, = group("framework"), = tag("assertions") ]] auto failedRequirementsStopContinuation() -> void {
  constexpr usize expectedAssertions{1};
  constexpr usize expectedFailures{1};
  const auto location = std::source_location::current();

  bool aborted{};
  bool continued{};
  TestState state{};

  {
    TestEnvironment environment{};
    EnvironmentBinding binding{environment};
    Result<i32> result{bail{Error{"unavailable"}}};
    try {
      require(result, location);
      continued = true;
    } catch (const detail::TestAbort &) {
      aborted = true;
    }
    state = environment.state();
  }

  require(aborted);
  require(not continued);
  require(state.assertions == expectedAssertions);
  require(state.failedAssertions == expectedFailures);
  require(state.aborted);
  require(state.failed());
  require(state.diagnostics.size() == expectedFailures);
  check(state.diagnostics.front().description() == "requirement failed"_exp);
  check(state.diagnostics.front().details.spans.front().location.line() == location.line());
  check(state.diagnostics.front().details.spans.front().label == "requirement"_exp);
}

} // namespace Tests::assertions

consteval {
  discover<^^Tests::assertions>();
}
