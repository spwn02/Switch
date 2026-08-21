import std;

import Miracle;
import Switch;

using namespace Miracle;
using namespace Switch;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::policies {

namespace PolicySubjects {

[[ = test, = group("framework"), = tag("policies", "subjects"), = shouldPanic("explicit panic") ]] auto
explicitPanic() -> void {
  panic("explicit panic");
}

[[ = test, = group("framework"), = tag("policies", "subjects"), = shouldPanic("returned error") ]] auto
returnedError() -> Result<void> {
  return bail({"returned error"});
}

[[ = test, = group("framework"), = tag("policies", "subjects"), = shouldPanic("exception panic") ]] auto
thrownException() -> void {
  throw std::runtime_error{"exception panic"};
}

[[ = test, = group("framework"), = tag("policies", "subjects"), = trace ]] auto traced() -> void {
  traceEvent("created request");
  require(true);
}

[[
  = test,
  = group("framework"),
  = tag("policies", "subjects"),
  = timeout(std::chrono::milliseconds(1))
]] auto
slow() -> void {
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

[[ = test, = group("framework"), = tag("policies", "subjects"), = shouldPanic("missing panic") ]] auto calm()
    -> void {
}

} // namespace PolicySubjects

namespace {

[[nodiscard]]
auto executionNamed(const Vec<TestExecution> &executions, StringView identifier)
    -> Option<Ref<const TestExecution>> {
  const auto execution =
      std::ranges::find_if(executions, [identifier](const TestExecution &candidate) -> bool {
        return candidate.descriptor.identifier == identifier;
      });
  if (execution == executions.end())
    return None;

  return std::cref(*execution);
}

} // namespace

[[ = test, = group("framework"), = tag("policies") ]] auto appliesDeclarativePolicies() -> void {
  constexpr usize expectedTests{6};
  constexpr usize expectedPassed{4};
  constexpr usize expectedFailed{2};
  const Vec<TestExecution> executions =
      runAllDetailed<^^PolicySubjects>(RunOptions{.isolation = CrashIsolation::InProcess});
  RunAccumulator accumulator{RetentionPolicy::All};
  for (const TestExecution &execution : executions)
    accumulator.append(execution);
  const TestSummary summary = Reporter::summarize(std::move(accumulator).finish());
  const Option<Ref<const TestExecution>> traced =
      executionNamed(executions, "Tests::policies::PolicySubjects::traced");
  const Option<Ref<const TestExecution>> slow =
      executionNamed(executions, "Tests::policies::PolicySubjects::slow");
  const Option<Ref<const TestExecution>> calm =
      executionNamed(executions, "Tests::policies::PolicySubjects::calm");

  require(eq(executions.size(), expectedTests));
  require(eq(summary.passedCount, expectedPassed));
  require(eq(summary.failedCount, expectedFailed));
  require(traced);
  require(slow);
  require(calm);
  check(traced->get().passed());
  check(traced->get().descriptor.policy.trace);
  check(traced->get().state.traces.size() == 2_exp);
  check(traced->get().state.traces[1].message == "created request"_exp);
  check(slow->get().failed());
  check(slow->get().state.diagnostics.front().header.code == DiagnosticCode::TimeoutExceeded);
  check(calm->get().failed());
  check(calm->get().state.diagnostics.front().header.code == DiagnosticCode::ExpectedPanicNotObserved);
}

[[ = test, = group("framework"), = tag("policies") ]] auto keepsThePanicCallSite() -> void {
  const auto location = std::source_location::current();
  const TestExecution execution = run(
      TestDescriptor{
          .identifier = "panicsAtCallSite",
          .location = location,
      },
      [location] -> void { panic("call-site panic", location); });

  require(execution.failed());
  require(execution.state.errors == 1_exp);
  require(execution.state.diagnostics.size() == 1_exp);
  check(execution.state.diagnostics.front().header.code == DiagnosticCode::TestPanicked);
  check(execution.state.diagnostics.front().details.spans.front().location.line() == location.line());
  check(execution.state.diagnostics.front().details.spans.front().label == "panic"_exp);
}

[[ = test, = group("framework"), = tag("policies") ]] auto preservesFailuresBeforeAnExpectedPanic() -> void {
  const auto location = std::source_location::current();
  const TestExecution execution = run(
      TestDescriptor{
          .identifier = "checkThenPanic",
          .location = location,
          .policy =
              TestPolicy{
                  .expectedPanic = "expected panic",
              },
      },
      [] -> void {
        check(false);
        panic("expected panic");
      });

  require(execution.failed());
  require(execution.state.errors == 0_exp);
  require(execution.state.failedAssertions == 1_exp);
  require(execution.state.diagnostics.size() == 1_exp);
  check(execution.state.diagnostics.front().header.code == DiagnosticCode::AssertionFailed);
}

[[ = test, = group("framework"), = tag("policies") ]] auto rendersCapturedTrace() -> void {
  const TestExecution execution = run(
      TestDescriptor{
          .identifier = "traceOutput",
          .policy =
              TestPolicy{
                  .trace = true,
              },
      },
      [] -> void { traceEvent("connected database"); });
  Reporter reporter{
      ReporterOptions{
          .renderer =
              RendererOptions{
                  .color = ColorMode::Never,
                  .terminal = false,
                  .details = DetailMode::Trace,
              },
      },
  };
  std::ostringstream output{};

  RunAccumulator accumulator{RetentionPolicy::All};
  accumulator.append(execution);
  static_cast<void>(reporter.report(std::move(accumulator).finish(), output));

  require(execution.passed());
  check(output.str().contains("tests traceOutput ... ok"));
  check(output.str().contains("= trace: connected database"));
}

[[ = test, = group("framework"), = tag("policies") ]] auto exposesStopTokenAtTimeoutBoundary() -> void {
  constexpr auto timeout{std::chrono::milliseconds{20}};
  bool observedStop{};
  bool completed{};
  const TestExecution execution = run(
      TestDescriptor{
          .identifier = "cooperativeTimeout",
          .policy =
              TestPolicy{
                  .timeout = timeout,
              },
      },
      [&observedStop, &completed](const Context &context) -> Task<void> { // NOLINT
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
        co_await yield();
        observedStop = context.stopToken.stop_requested();
        completed = true;
      });

  require(observedStop);
  require(completed);
  require(execution.failed());
  require(execution.state.errors == 1_exp);
  require(execution.state.diagnostics.size() == 1_exp);
  check(execution.state.diagnostics.front().header.code == DiagnosticCode::TimeoutExceeded);
}

[[ = test, = group("framework"), = tag("policies") ]] auto cancelsIgnoredTimeoutAtNextYield() -> void {
  constexpr auto timeout{std::chrono::milliseconds{20}};
  bool continued{};
  const TestExecution execution = run(
      TestDescriptor{
          .identifier = "ignoredTimeout",
          .policy =
              TestPolicy{
                  .timeout = timeout,
              },
      },
      [&continued] -> Task<void> { // NOLINT
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
        co_await yield();
        co_await yield();
        continued = true;
      });

  require(not continued);
  require(execution.failed());
  require(execution.state.errors == 1_exp);
  require(execution.state.diagnostics.size() == 1_exp);
  check(execution.state.diagnostics.front().header.code == DiagnosticCode::TimeoutExceeded);
}

} // namespace Tests::policies
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::policies>();
}
