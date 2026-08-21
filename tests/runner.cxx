import std;

import Miracle;
import Switch;

using namespace Miracle;
using namespace Switch;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::runner {

namespace RepeatSubjects {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
inline Vec<u64> observedSeeds{};
inline Vec<usize> observedIterations{};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

auto reset() -> void {
  observedSeeds.clear();
  observedIterations.clear();
}

[[ = test, = group("framework"), = tag("runner", "subjects", "repeat") ]] auto first(
    const Context[[= context]] & context) -> void {
  observedSeeds.push_back(context.seed);
  observedIterations.push_back(context.iteration);
}

[[ = test, = group("framework"), = tag("runner", "subjects", "repeat") ]] auto second(
    const Context[[= context]] & context) -> void {
  observedSeeds.push_back(context.seed);
  observedIterations.push_back(context.iteration);
}

} // namespace RepeatSubjects

namespace OrderSubjects {

[[ = test, = group("framework"), = tag("runner", "subjects", "order") ]] auto alpha() -> void {
}
[[ = test, = group("framework"), = tag("runner", "subjects", "order") ]] auto beta() -> void {
}
[[ = test, = group("framework"), = tag("runner", "subjects", "order") ]] auto gamma() -> void {
}
[[ = test, = group("framework"), = tag("runner", "subjects", "order") ]] auto delta() -> void {
}
[[ = test, = group("framework"), = tag("runner", "subjects", "order") ]] auto epsilon() -> void {
}

} // namespace OrderSubjects

namespace FailFastSubjects {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline usize calls{};

auto reset() -> void {
  calls = 0;
}

[[ = test, = group("framework"), = tag("runner", "subjects", "failfast") ]] auto passes() -> void {
  ++calls;
}

[[ = test, = group("framework"), = tag("runner", "subjects", "failfast") ]] auto fails() -> bool {
  ++calls;
  return false;
}

[[ = test, = group("framework"), = tag("runner", "subjects", "failfast") ]] auto unreached() -> void {
  ++calls;
}

} // namespace FailFastSubjects

namespace TraceSubjects {

[[ = test, = group("framework"), = tag("runner", "subjects", "trace") ]] auto passing() -> void {
  traceEvent("passing trace");
}

[[ = test, = group("framework"), = tag("runner", "subjects", "trace") ]] auto failing() -> bool {
  traceEvent("failing trace");
  return false;
}

} // namespace TraceSubjects

namespace ThroughputSubjects {

[[ = test, = group("framework"), = tag("runner", "subjects", "throughput") ]] auto passing() -> void {
  check(true);
}

} // namespace ThroughputSubjects

namespace ThroughputFailureSubjects {

inline constexpr usize failingIteration{37};

[[ = test, = group("framework"), = tag("runner", "subjects", "throughput") ]] auto failsAtIndex(
    const Context[[= context]] & context) -> void {
  check(context.iteration != failingIteration);
}

} // namespace ThroughputFailureSubjects

[[ = test, = group("framework"), = tag("runner", "reporting", "live") ]] auto
liveReporterRendersCompletedCases() -> void {
  std::ostringstream output{};
  Reporter reporter{ReporterOptions{
      .renderer = RendererOptions{.color = ColorMode::Never},
      .showPassedTests = true,
      .showProgress = false,
  }};
  const RunReport report = runAll<^^OrderSubjects>(
      reporter, output, RunOptions{.repeat = 3, .isolation = CrashIsolation::InProcess});

  require(report.cases.size() == 5_exp);
  const String rendered = output.str();
  usize rows{};
  for (usize position{}; (position = rendered.find("tests ", position)) != String::npos; ++position)
    ++rows;
  check(rows == 6_exp);
  for (const TestCaseResult &testCase : report.cases) {
    check(rendered.find(std::format("tests {}", testCase.descriptor.identifier)) != String::npos);
  }
  check(rendered.find("PASS") != String::npos);
  check(rendered.find("================") == String::npos);
}

[[ = test, = group("framework"), = tag("runner", "throughput") ]] auto
throughputAggregatesPassingRepetitions() -> void {
  constexpr usize repetitions{10'000};
  const RunReport checked = runAll<^^ThroughputSubjects>(RunOptions{
      .threads = 1,
      .captureProfile = false,
      .repeat = repetitions,
      .isolation = CrashIsolation::InProcess,
  });
  const RunReport throughput = runAll<^^ThroughputSubjects>(RunOptions{
      .executionMode = ExecutionMode::Benchmark,
      .threads = 1,
      .captureProfile = false,
      .repeat = repetitions,
      .isolation = CrashIsolation::InProcess,
  });

  require(checked.summary.passed());
  require(throughput.summary.passed());
  check(throughput.summary.passedCount == checked.summary.passedCount);
  check(throughput.summary.sampleCount == checked.summary.sampleCount);
  check(throughput.summary.assertionCount == checked.summary.assertionCount);
  require(throughput.cases.size() == 1_exp);
  check(throughput.cases.front().attempts.empty());
  require(throughput.cases.front().measurement.has_value());
  check(throughput.cases.front().measurement->distributionAvailable);
  check(throughput.cases.front().measurement->quantilesAvailable);
  check(throughput.cases.front().measurement->firstQuartile <= throughput.cases.front().measurement->median);
  check(throughput.cases.front().measurement->median <= throughput.cases.front().measurement->thirdQuartile);
}

[[ = test, = group("framework"), = tag("runner", "throughput") ]] auto throughputStopsAtTheFirstFailure()
    -> void {
  const RunReport report = runAll<^^ThroughputFailureSubjects>(RunOptions{
      .executionMode = ExecutionMode::Benchmark,
      .threads = 1,
      .captureProfile = false,
      .repeat = 1'000,
      .isolation = CrashIsolation::InProcess,
  });

  require(report.summary.failed());
  check(report.summary.attemptCount == ThroughputFailureSubjects::failingIteration + 1);
  require(report.cases.size() == 1_exp);
  require(report.cases.front().attempts.size() == 1_exp);
  check(report.cases.front().attempts.front().index.runIteration ==
        ThroughputFailureSubjects::failingIteration);
}

[[ = test, = group("framework"), = tag("runner") ]] auto repeatsCasesWithStableContextSeeds() -> void {
  constexpr u64 seed{0xA11CE};
  constexpr usize expectedExecutions{4};
  const RunOptions options{
      .repeat = 2,
      .seed = seed,
      .isolation = CrashIsolation::InProcess,
  };

  RepeatSubjects::reset();
  const Vec<TestExecution> firstRun = runAllDetailed<^^RepeatSubjects>(options);
  const Vec<u64> firstObservedSeeds = RepeatSubjects::observedSeeds;
  const Vec<usize> firstObservedIterations = RepeatSubjects::observedIterations;

  RepeatSubjects::reset();
  const Vec<TestExecution> secondRun = runAllDetailed<^^RepeatSubjects>(options);

  require(firstRun.size() == expectedExecutions);
  require(secondRun.size() == expectedExecutions);
  require(std::ranges::equal(
      firstRun, secondRun, [](const TestExecution &left, const TestExecution &right) -> bool {
        return left.descriptor.name == right.descriptor.name and left.seed == right.seed and
               left.iteration == right.iteration;
      }));
  require(std::ranges::equal(
      firstObservedSeeds, firstRun, std::ranges::equal_to{}, std::identity{}, &TestExecution::seed));
  require(std::ranges::equal(firstObservedIterations,
      firstRun,
      std::ranges::equal_to{},
      std::identity{},
      &TestExecution::iteration));
  check(firstRun[0].descriptor.name == "first"_exp);
  check(firstRun[1].descriptor.name == "second"_exp);
  check(firstRun[2].descriptor.name == "first"_exp);
  check(firstRun[3].descriptor.name == "second"_exp);
  check(firstRun[0].iteration == 0_exp);
  check(firstRun[2].iteration == 1_exp);
  check(firstRun[0].runSeed == seed);
  check(firstRun[0].seed != firstRun[2].seed);
}

[[ = test, = group("framework"), = tag("runner") ]] auto shufflesCasesReproducibly() -> void {
  constexpr u64 seed{0x5EED};
  const RunOptions options{
      .order = ExecutionOrder::Shuffled,
      .seed = seed,
      .isolation = CrashIsolation::InProcess,
  };
  const Vec<TestExecution> firstRun = runAllDetailed<^^OrderSubjects>(options);
  const Vec<TestExecution> secondRun = runAllDetailed<^^OrderSubjects>(options);

  require(firstRun.size() == 5_exp);
  require(std::ranges::equal(
      firstRun, secondRun, [](const TestExecution &left, const TestExecution &right) -> bool {
        return left.descriptor.name == right.descriptor.name and left.seed == right.seed;
      }));
  check(firstRun[0].descriptor.name == "alpha"_exp);
  check(firstRun[1].descriptor.name == "delta"_exp);
  check(firstRun[2].descriptor.name == "epsilon"_exp);
  check(firstRun[3].descriptor.name == "beta"_exp);
  check(firstRun[4].descriptor.name == "gamma"_exp);
}

[[ = test, = group("framework"), = tag("runner") ]] auto stopsSerialDispatchAfterTheFirstFailure() -> void {
  FailFastSubjects::reset();
  const Vec<TestExecution> executions = runAllDetailed<^^FailFastSubjects>(RunOptions{
      .failFast = true,
      .isolation = CrashIsolation::InProcess,
  });

  require(executions.size() == 2_exp);
  require(executions.back().failed());
  require(FailFastSubjects::calls == 2_exp);
  check(executions.front().descriptor.name == "passes"_exp);
  check(executions.back().descriptor.name == "fails"_exp);
}

[[ = test, = group("framework"), = tag("runner") ]] auto
defaultTraceCapturesEveryCaseAndRendersFailuresByDefault() -> void {
  const Vec<TestExecution> executions =
      runAllDetailed<^^TraceSubjects>(RunOptions{.isolation = CrashIsolation::InProcess});
  Reporter reporter{
      ReporterOptions{
          .renderer =
              RendererOptions{
                  .color = ColorMode::Never,
                  .terminal = false,
                  .showSource = false,
              },
          .showPassedTests = true,
          .showSummary = false,
      },
  };
  std::ostringstream output{};

  RunAccumulator accumulator{RetentionPolicy::All};
  for (const TestExecution &execution : executions)
    accumulator.append(execution);
  static_cast<void>(reporter.report(std::move(accumulator).finish(), output));

  require(executions.size() == 2_exp);
  require(std::ranges::all_of(
      executions, [](const TestExecution &execution) -> bool { return not execution.state.traces.empty(); }));
  check(executions.front().traceMode == TraceMode::ForcedFailures);
  check(not output.str().contains("passing trace"));
  check(output.str().contains("failing trace"));
}

[[ = test, = group("framework"), = tag("runner") ]] auto forceTraceCanRenderSuccessfulCases() -> void {
  const Vec<TestExecution> executions = runAllDetailed<^^TraceSubjects>(RunOptions{
      .traceMode = TraceMode::ForcedAll,
      .isolation = CrashIsolation::InProcess,
  });
  Reporter reporter{
      ReporterOptions{
          .renderer =
              RendererOptions{
                  .color = ColorMode::Never,
                  .terminal = false,
                  .showSource = false,
              },
          .showSummary = false,
      },
  };
  std::ostringstream output{};

  RunAccumulator accumulator{RetentionPolicy::All};
  for (const TestExecution &execution : executions)
    accumulator.append(execution);
  static_cast<void>(reporter.report(std::move(accumulator).finish(), output));

  require(executions.size() == 2_exp);
  check(output.str().contains("passing trace"));
}

[[ = test, = group("framework"), = tag("runner", "threads") ]] auto threadsPreserveDeterministicResults()
    -> void {
  constexpr u64 seed{0x71EAD};

  const Vec<TestExecution> serial = runAllDetailed<^^OrderSubjects>(RunOptions{
      .threads = 1,
      .seed = seed,
      .isolation = CrashIsolation::InProcess,
  });

  const Vec<TestExecution> parallel = runAllDetailed<^^OrderSubjects>(RunOptions{
      .threads = 2,
      .seed = seed,
      .isolation = CrashIsolation::InProcess,
  });

  const Vec<TestExecution> automatic = runAllDetailed<^^OrderSubjects>(RunOptions{
      .threads = 0,
      .seed = seed,
      .isolation = CrashIsolation::InProcess,
  });

  require(serial.size() == parallel.size());
  require(serial.size() == automatic.size());

  check(std::ranges::equal(
      serial, parallel, [](const TestExecution &left, const TestExecution &right) constexpr noexcept -> bool {
        return left.descriptor.identifier == right.descriptor.identifier and left.seed == right.seed and
               left.attempt == right.attempt;
      }));

  check(std::ranges::equal(serial,
      automatic,
      [](const TestExecution &left, const TestExecution &right) constexpr noexcept -> bool {
        return left.descriptor.identifier == right.descriptor.identifier and left.seed == right.seed and
               left.attempt == right.attempt;
      }));
}

[[ = test, = group("framework"), = tag("runner", "diagnostics", "parallel") ]] auto
parallelDiagnosticsRenderInLogicalOrder() -> void {
  const auto render = [](usize threads) -> String {
    Vec<TestExecution> executions = runAllDetailed<^^TraceSubjects>(RunOptions{
        .threads = threads,
        .isolation = CrashIsolation::InProcess,
    });

    std::ranges::for_each(executions, [](TestExecution &execution) -> void {
      using std::operator""ns;
      execution.duration = 10ns;
    });

    Reporter reporter{
        ReporterOptions{
            .renderer =
                RendererOptions{
                    .color = ColorMode::Never,
                    .terminal = false,
                    .showSource = false,
                },
            .showPassedTests = true,
            .showSummary = false,
        },
    };

    std::ostringstream output{};
    RunAccumulator accumulator{RetentionPolicy::All};
    for (const TestExecution &execution : executions)
      accumulator.append(execution);
    static_cast<void>(reporter.report(std::move(accumulator).finish(), output));
    return output.str();
  };

  check(eq(render(1), render(2)));
}

} // namespace Tests::runner
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::runner>();
}
