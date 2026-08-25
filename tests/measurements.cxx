import std;

import Miracle;
import Switch;

using namespace Miracle;
using namespace Switch;

namespace Tests::measurements {

namespace Subjects {

[[ = test, = group("framework"), = tag("measurements", "samples"), = repeat(3), = warmup(1) ]] auto
collectsSamples(const Context &context [[= context]]) -> void {
  if (context.warmup)
    return;

  check(context.retry == 0_exp);
  check(context.sample < 3_exp);
}

[[
  = test,
  = group("framework"),
  = tag("measurements", "retry"),
  = timeout(std::chrono::milliseconds{0}),
  = retry(1)
]] auto retriesTimeouts(const Context &context [[= context]]) -> Task<void> { // NOLINT
  if (context.retry == 0) {
    co_await sleepFor(std::chrono::hours{1});
    co_await yield();
  }

  require(context.retry == 1_exp);
}

} // namespace Subjects

namespace NoRetrySubjects {

[[ = test, = retry(3) ]] auto assertionFailure() -> void {
  check(false);
}

} // namespace NoRetrySubjects

[[ = test, = group("framework"), = tag("measurements", "reporting") ]] auto reportsSamplesAndRecovery()
    -> void {
  const Vec<TestExecution> executions = runAllDetailed<^^Subjects>(RunOptions{
      .timeMode = TimeMode::Virtual,
      .seed = 42,
      .isolation = CrashIsolation::InProcess,
  });
  RunAccumulator accumulator{RetentionPolicy::All};
  for (const TestExecution &execution : executions)
    accumulator.append(execution);
  const RunReport report = std::move(accumulator).finish();
  const TestSummary summary = Reporter::summarize(report);
  const auto samples = std::ranges::find_if(report.cases,
      [](const TestCaseResult &result) -> bool { return result.descriptor.name == "collectsSamples"; });
  const auto retries = std::ranges::find_if(report.cases,
      [](const TestCaseResult &result) -> bool { return result.descriptor.name == "retriesTimeouts"; });

  require(samples != report.cases.end());
  require(retries != report.cases.end());
  require(samples->measurement);
  require(samples->measurement->sampleCount == 3_exp);
  require(samples->attempts.size() == 4_exp);
  require(retries->attempts.size() == 2_exp);
  require(retries->measurement);
  check(retries->measurement->sampleCount == 1_exp);
  check(retries->measurement->approximate == false);
  require(retries->recoveredTimeouts == 1_exp);
  require(retries->passed());
  check(summary.passed());
  check(summary.recoveredCount == 1_exp);
}

[[ = test, = group("framework"), = tag("measurements", "resources") ]] auto cleansTestResources() -> void {
  Path filePath{};
  Path directoryPath{};
  bool deferred{};
  const TestExecution execution = run("resource arena", [&filePath, &directoryPath, &deferred] -> void {
    const Option<Ref<TestResources>> current = currentResources();
    require(current);

    TestResources &resources = current->get();
    TemporaryDirectory &directory = resources.temporaryDirectory("case");
    TemporaryFile &file = resources.temporaryFile(".txt");
    filePath = file.path();
    directoryPath = directory.path();
    require(file.file().write("payload"));
    auto _ = resources.defer([&deferred] -> void { deferred = true; });
    check(std::filesystem::exists(filePath));
    check(std::filesystem::exists(directoryPath));
  });

  require(execution.passed());
  require(deferred);
  require(not std::filesystem::exists(filePath));
  check(not std::filesystem::exists(directoryPath));
  check(execution.resources.cleanupCount == 3_exp);
  check(execution.resources.peakResources == 3_exp);
}

[[ = test, = group("framework"), = tag("measurements", "profiling") ]] auto capturesProfileScopes() -> void {
  const TestExecution execution = run("profile scopes", [] -> void {
    TestEnvironment &environment = currentEnvironment()->get();
    auto outer = profiling::profileScope(environment.profileSink(), "outer");
    auto inner = profiling::profileScope(environment.profileSink(), "inner");
    check(true);
  });

  require(execution.passed());
  require(execution.profile.events.size() >= 3_exp);
  require(execution.profile.aggregates.contains("outer"));
  check(execution.profile.aggregates.contains("inner"));
}

[[ = test, = group("framework"), = tag("measurements", "profiling", "async") ]] auto
preservesProfileBindingAcrossAsyncResumption() -> void {
  const TestExecution execution = run("async profile scopes", [] -> Task<void> {
    TestEnvironment &environment = currentEnvironment()->get();
    auto outer = profiling::profileScope(environment.profileSink(), "async outer");
    co_await yield();
    auto inner = profiling::profileScope(environment.profileSink(), "async inner");
    co_await yield();
  });

  require(execution.passed());
  require(execution.profile.aggregates.contains("async outer"));
  check(execution.profile.aggregates.contains("async inner"));
}

[[ = test, = group("framework"), = tag("measurements", "callable") ]] auto measuresCallables() -> Task<void> {
  const MeasurementSummary synchronous = measure("sync work", 3, [] -> void {});
  require(synchronous.sampleCount == 3_exp);
  require(synchronous.minimum <= synchronous.maximum);

  const MeasurementSummary asynchronous =
      co_await measure("async work", 2, [] -> Task<void> { co_await yield(); });
  check(asynchronous.sampleCount == 2_exp);
  check(asynchronous.minimum <= asynchronous.maximum);
}

[[ = test, = group("framework"), = tag("measurements", "median") ]] auto boundsLargeMedianStorage() -> void {
  Vec<std::chrono::steady_clock::duration> samples{};
  samples.reserve(1025);
  for (usize index{}; index != 1025; ++index)
    samples.push_back(std::chrono::steady_clock::duration{static_cast<isize>(index)});

  const MeasurementSummary summary = detail::summarizeMeasurements(std::move(samples));
  require(summary.sampleCount == 1025_exp);
  check(summary.approximate);
}

[[ = test, = group("framework"), = tag("measurements", "quantiles") ]] auto computesStartupQuantiles()
    -> void {
  using Duration = std::chrono::steady_clock::duration;
  const MeasurementSummary summary =
      detail::summarizeMeasurements(Vec<Duration>{Duration{4}, Duration{1}, Duration{3}, Duration{2}});

  require(summary.quantilesAvailable);
  check(summary.firstQuartile == Duration{1} + Duration{3} / 4);
  check(summary.median == Duration{5} / 2);
  check(summary.thirdQuartile == Duration{13} / 4);
  check(not summary.approximate);
}

[[ = test, = group("framework"), = tag("measurements", "retry") ]] auto doesNotRetryAssertions() -> void {
  const Vec<TestExecution> executions = runAllDetailed<^^NoRetrySubjects>(RunOptions{
      .timeMode = TimeMode::Virtual,
      .seed = 43,
      .isolation = CrashIsolation::InProcess,
  });

  require(executions.size() == 1_exp);
  check(executions.front().failed());
}

} // namespace Tests::measurements

consteval {
  discover<^^Tests::measurements>();
}
