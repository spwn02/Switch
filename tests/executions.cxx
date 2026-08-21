import std;

import Miracle;
import Switch;

using namespace Miracle;
using namespace Switch;

namespace Tests::executions {

auto delayedValue() -> Task<i32> {
  constexpr i32 value{69};
  co_await yield();
  co_return value;
}

[[ = test, = group("framework"), = tag("executions") ]] auto executesVoidTestsInAnEnvironment() -> void {
  const TestExecution execution = run("void", [] -> void { require(currentEnvironment()); });

  check(execution.passed());
  check(execution.state.assertions == 1_exp);
  check(currentEnvironment());
}

[[ = test, = group("framework"), = tag("executions") ]] auto normalizesBooleanReturnValues() -> void {
  constexpr usize expectedAssertions{1};
  constexpr usize expectedFailures{1};
  const auto location = std::source_location::current();
  const TestExecution execution = run(
      TestDescriptor{
          .identifier = "returnsFalse",
          .location = location,
      },
      [] -> bool { return false; });

  require(execution.failed());
  require(execution.state.assertions == expectedAssertions);
  require(execution.state.failedAssertions == expectedFailures);
  require(execution.state.errors == 0_exp);
  require(execution.state.diagnostics.size() == expectedFailures);
  check(execution.state.diagnostics.front().header.code == DiagnosticCode::AssertionFailed);
  check(execution.state.diagnostics.front().description() == "test returned false"_exp);
  check(execution.state.diagnostics.front().details.spans.front().location.line() == location.line());
  check(execution.state.diagnostics.front().details.spans.front().label == "test return"_exp);
}

[[ = test, = group("framework"), = tag("executions") ]] auto normalizesResultReturnValues() -> void {
  constexpr usize expectedAssertions{1};
  constexpr usize expectedFailures{1};
  constexpr usize expectedErrors{1};
  const TestExecution returnedTrue = run("resultTrue", [] -> Result<bool> { return true; });
  const TestExecution returnedFalse = run("resultFalse", [] -> Result<bool> { return false; });
  const TestExecution returnedVoid = run("resultVoid", [] -> Result<void> { return {}; });
  const TestExecution returnedError =
      run("resultError", [] -> Result<void> { return bail{Error{"invalid user"}}; });

  require(returnedTrue.passed());
  require(returnedTrue.state.assertions == expectedAssertions);
  require(returnedFalse.failed());
  require(returnedFalse.state.assertions == expectedAssertions);
  require(returnedFalse.state.failedAssertions == expectedFailures);
  require(returnedVoid.passed());
  require(returnedVoid.state.assertions == 0_exp);
  require(returnedError.failed());
  require(returnedError.state.errors == expectedErrors);
  require(returnedError.state.assertions == 0_exp);
  require(returnedError.state.diagnostics.size() == expectedErrors);
  check(returnedError.state.diagnostics.front().header.code == DiagnosticCode::TestReturnedError);
  check(returnedError.state.diagnostics.front().details.notes.front().message == "invalid user"_exp);
}

[[ = test, = group("framework"), = tag("executions") ]] auto capturesFatalRequirementsAtTheTestBoundary()
    -> void {
  constexpr usize expectedAssertions{1};
  constexpr usize expectedFailures{1};
  bool continued{};
  const TestExecution execution = run("requires", [&continued] -> void {
    require(false);
    continued = true;
  });

  require(not continued);
  require(execution.failed());
  require(execution.state.aborted);
  require(execution.state.assertions == expectedAssertions);
  require(execution.state.failedAssertions == expectedFailures);
  require(execution.state.errors == 0_exp);
  check(execution.state.diagnostics.front().details.spans.front().label == "requirement"_exp);
}

[[ = test, = group("framework"), = tag("executions") ]] auto preservesTaskLocalBindingsAcrossAsyncResumption()
    -> void {
  constexpr usize expectedAssertions{4};
  bool completed{};
  const TestExecution execution = run("asyncBindings", [&completed] -> Task<void> { // NOLINT
    require(currentEnvironment());
    require(currentContext());

    co_await yield();

    require(currentEnvironment());
    require(currentContext());
    completed = true;
  });

  require(completed);
  require(execution.passed());
  require(execution.state.assertions == expectedAssertions);
}

[[ = test, = group("framework"), = tag("executions") ]] auto awaitsNestedAsyncTasks() -> void {
  constexpr usize expectedAssertions{1};
  const TestExecution execution = run("nestedAsync", [] -> Task<bool> {
    constexpr i32 expectedValue{69};
    const i32 value = co_await delayedValue();
    co_return value == expectedValue;
  });

  require(execution.passed());
  require(execution.state.assertions == expectedAssertions);
  require(execution.state.diagnostics.empty());
}

[[ = test, = group("framework"), = tag("executions") ]] auto normalizesAsyncReturnValues() -> void {
  constexpr usize expectedAssertions{1};
  constexpr usize expectedErrors{1};
  const TestExecution returnedTrue = run("asyncTrue", [] -> Task<bool> {
    co_await yield();
    co_return true;
  });
  const TestExecution returnedResult = run("asyncResult", [] -> Task<Result<bool>> {
    co_await yield();
    co_return Result<bool>{true};
  });
  const TestExecution returnedError = run("asyncError", [] -> Task<Result<void>> {
    co_await yield();
    Result<void> result = bail{Error{"async failure"}};
    co_return result;
  });

  require(returnedTrue.passed());
  require(returnedTrue.state.assertions == expectedAssertions);
  require(returnedResult.passed());
  require(returnedResult.state.assertions == expectedAssertions);
  require(returnedError.failed());
  require(returnedError.state.errors == expectedErrors);
  check(returnedError.state.diagnostics.front().header.code == DiagnosticCode::TestReturnedError);
  check(returnedError.state.diagnostics.front().details.notes.front().message == "async failure"_exp);
}

[[ = test, = group("framework"), = tag("executions") ]] auto capturesAsyncFatalRequirementsAtTheTestBoundary()
    -> void {
  constexpr usize expectedAssertions{1};
  constexpr usize expectedFailures{1};
  bool continued{};
  const TestExecution execution = run("asyncRequire", [&continued] -> Task<void> { // NOLINT
    co_await yield();
    require(false);
    continued = true;
  });

  require(not continued);
  require(execution.failed());
  require(execution.state.aborted);
  require(execution.state.assertions == expectedAssertions);
  require(execution.state.failedAssertions == expectedFailures);
  require(execution.state.errors == 0);
  check(execution.state.diagnostics.front().details.spans.front().label == "requirement"_exp);
}

[[ = test, = group("framework"), = tag("executions") ]] auto capturesAsyncExceptionsAtTheTestBoundary()
    -> void {
  constexpr usize expectedErrors{1};
  const TestExecution execution = run("asyncThrows", [] -> Task<void> {
    co_await yield();
    throw std::runtime_error{"broken async test"};
  });

  require(execution.failed());
  require(execution.state.errors == expectedErrors);
  check(execution.state.diagnostics.front().header.code == DiagnosticCode::UnhandledException);
  check(execution.state.diagnostics.front().details.notes.front().message ==
        "exception: broken async test"_exp);
}

[[ = test, = group("framework"), = tag("executions") ]] auto capturesUnhandledExceptions() -> void {
  constexpr usize expectedErrors{1};
  const TestExecution execution = run("throws", [] -> void { throw std::runtime_error{"broken test"}; });

  require(execution.failed());
  require(execution.state.errors == expectedErrors);
  require(execution.state.assertions == 0);
  require(execution.state.diagnostics.size() == expectedErrors);
  check(execution.state.diagnostics.front().header.code == DiagnosticCode::UnhandledException);
  check(execution.state.diagnostics.front().details.notes.front().message == "exception: broken test"_exp);
}

[[ = test, = group("framework"), = tag("executions") ]] auto reportsAggregateResults() -> void {
  constexpr usize progressBarWidth{80};
  constexpr usize expectedTests{3};
  constexpr usize expectedPassed{1};
  constexpr usize expectedFailed{2};
  constexpr usize expectedAssertions{2};
  constexpr usize expectedFailedAssertions{1};
  constexpr usize expectedErrors{1};
  const Vec<TestExecution> executions{
      run("passes", [] -> bool { return true; }),
      run("fails", [] -> bool { return false; }),
      run("errors", [] -> Result<void> { return bail{Error{"invalid query"}}; }),
  };
  Reporter reporter{
      ReporterOptions{
          .renderer =
              RendererOptions{
                  .color = ColorMode::Never,
                  .terminal = false,
                  .showSource = false,
              },
      },
  };
  std::ostringstream output{};
  RunAccumulator accumulator{RetentionPolicy::All};
  for (const TestExecution &execution : executions)
    accumulator.append(execution);
  const TestSummary summary = reporter.report(std::move(accumulator).finish(), output);
  const String text = output.str();

  require(summary.testCount == expectedTests);
  require(summary.passedCount == expectedPassed);
  require(summary.failedCount == expectedFailed);
  require(summary.assertionCount == expectedAssertions);
  require(summary.failedAssertionCount == expectedFailedAssertions);
  require(summary.errorCount == expectedErrors);
  require(summary.failed());
  check(text.contains("tests fails"));
  check(text.contains("error[E001]: test returned false"));
  check(text.contains("error[E011]: test returned an error"));
  check(text.contains("= note: test: errors"));
  check(text.contains(String(progressBarWidth, '=')));
  check(text.contains("FAIL"));
  check(text.contains("Execution"));
}

} // namespace Tests::executions

consteval {
  discover<^^Tests::executions>();
}
