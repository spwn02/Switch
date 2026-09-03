import std;

import Miracle;
import Switch;
import SwitchTests.Support;

using namespace Miracle;
using namespace Switch;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::stress {

[[ = test, = group("framework"), = tag("stress", "retention") ]] auto aggregatesMillionPassesBoundedly()
    -> void {
  constexpr usize attempts{1'000'000};
  RunAccumulator accumulator{RetentionPolicy::Failures, 8};
  const TestDescriptor descriptor{.identifier = "million-pass-case"};
  std::ranges::for_each(std::views::indices(attempts), [&accumulator, &descriptor](usize index) -> void {
    accumulator.append(AttemptOutcome{
        .descriptor = descriptor,
        .attempt = AttemptIndex{.sample = index},
        .duration = std::chrono::nanoseconds{index % 17},
        .passed = true,
    });
  });

  const RunReport report = std::move(accumulator).finish();
  require(report.cases.size() == 1);
  check(report.summary.attemptCount == attempts);
  check(report.summary.passedCount == attempts);
  check(report.retainedAttemptCount == 0);
  check(report.cases.front().attempts.empty());
  check(report.passed());
}

namespace ParallelSubjects {

inline constexpr usize caseCount{4};

struct CaseState final {
  std::atomic<usize> active;
  std::atomic<usize> overlaps;
};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<usize> invocations{};
inline std::atomic<usize> cleanups{};
inline std::array<CaseState, caseCount> states{};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

auto reset() -> void {
  invocations.store(0);
  cleanups.store(0);
  std::ranges::for_each(states, [](CaseState &state) -> void {
    state.active.store(0);
    state.overlaps.store(0);
  });
}

auto exercise(usize index) -> Task<void> {
  invocations.fetch_add(1, std::memory_order_relaxed);
  const usize previous = states.at(index).active.fetch_add(1, std::memory_order_relaxed);

  if (previous != 0)
    states.at(index).overlaps.fetch_add(1, std::memory_order_relaxed);

  const auto cleanup = Tests::support::ScopeExit([] -> void { cleanups.fetch_add(1, std::memory_order_relaxed); });

  const auto release =
      Tests::support::ScopeExit([index] -> void { states.at(index).active.fetch_sub(1, std::memory_order_relaxed); });

  co_await yield();
  co_await yield();
}

[[ = test, = group("framework"), = tag("stress", "parallel") ]] auto alpha() -> Task<void> {
  co_await exercise(0);
}

[[ = test, = group("framework"), = tag("stress", "parallel") ]] auto beta() -> Task<void> {
  co_await exercise(1);
}

[[ = test, = group("framework"), = tag("stress", "parallel") ]] auto gamma() -> Task<void> {
  co_await exercise(2);
}

[[ = test, = group("framework"), = tag("stress", "parallel") ]] auto delta() -> Task<void> {
  co_await exercise(3);
}

} // namespace ParallelSubjects

namespace ResourceSubjects {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<usize> active{};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<usize> peak{};

auto reset() -> void {
  active.store(0);
  peak.store(0);
}

auto observe() -> Task<void> {
  const usize current = active.fetch_add(1, std::memory_order_relaxed) + 1;

  usize previous = peak.load(std::memory_order_relaxed);
  while (
      previous < current and not peak.compare_exchange_weak(previous, current, std::memory_order_relaxed)) {
  }

  const auto release = Tests::support::ScopeExit([] -> void { active.fetch_sub(1, std::memory_order_relaxed); });

  co_await yield();
  co_await yield();
}

[[ = test, = group("framework"), = tag("stress", "resource_lane"), = resource("exclusive") ]] auto alpha()
    -> Task<void> {
  co_await observe();
}

[[ = test, = group("framework"), = tag("stress", "resource_lane"), = resource("exclusive") ]] auto beta()
    -> Task<void> {
  co_await observe();
}

} // namespace ResourceSubjects

namespace ParallelAttemptSubjects {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<usize> active{};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<usize> peak{};

inline constexpr usize expectedParallelAttempts{4};

auto reset() -> void {
  active.store(0);
  peak.store(0);
}

[[ = test, = group("framework"), = tag("stress", "parallel_attempts"), = parallelAttempts ]] auto
repeatsConcurrently() -> Task<void> {
  const usize current = active.fetch_add(1, std::memory_order_relaxed) + 1;

  usize previous = peak.load(std::memory_order_relaxed);
  while (
      previous < current and not peak.compare_exchange_weak(previous, current, std::memory_order_relaxed)) {
  }

  const auto release = Tests::support::ScopeExit([] -> void { active.fetch_sub(1, std::memory_order_relaxed); });

  // Gate on peak, not active: active can drop back down the instant any
  // attempt observes the target and departs, which can permanently strand
  // slower attempts spinning below a target that was in fact reached. peak
  // is monotonically non-decreasing, so once it reaches the target every
  // attempt is guaranteed to observe that and proceed, regardless of
  // arrival/departure order.
  while (peak.load(std::memory_order_relaxed) < expectedParallelAttempts)
    co_await yield();
  co_await yield();
}

} // namespace ParallelAttemptSubjects

namespace CancellationSubjects {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<usize> cleanups{};

auto reset() -> void {
  cleanups.store(0);
}

auto awaitBeyondTimeout() -> Task<void> {
  const auto cleanup = Tests::support::ScopeExit([] -> void { cleanups.fetch_add(1, std::memory_order_relaxed); });

  co_await sleepFor(std::chrono::hours{1});
}

[[
  = test,
  = group("framework"),
  = tag("stress", "cancellation"),
  = timeout(std::chrono::milliseconds{1})
]] auto alpha() -> Task<void> {
  co_await awaitBeyondTimeout();
}

[[
  = test,
  = group("framework"),
  = tag("stress", "cancellation"),
  = timeout(std::chrono::milliseconds{1})
]] auto beta() -> Task<void> {
  co_await awaitBeyondTimeout();
}

[[
  = test,
  = group("framework"),
  = tag("stress", "cancellation"),
  = timeout(std::chrono::milliseconds{1})
]] auto gamma() -> Task<void> {
  co_await awaitBeyondTimeout();
}

[[
  = test,
  = group("framework"),
  = tag("stress", "cancellation"),
  = timeout(std::chrono::milliseconds{1})
]] auto delta() -> Task<void> {
  co_await awaitBeyondTimeout();
}

} // namespace CancellationSubjects

namespace MeasurementSubjects {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<usize> active{};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<usize> peak{};

auto reset() -> void {
  active.store(0);
  peak.store(0);
}

[[
  = test,
  = group("framework"),
  = tag("stress", "measurement_serial"),
  = repeat(4),
  = parallelAttempts
]] auto
samplesRemainSerial() -> Task<void> {
  const usize current = active.fetch_add(1, std::memory_order_relaxed) + 1;

  usize previous = peak.load(std::memory_order_relaxed);
  while (
      previous < current and not peak.compare_exchange_weak(previous, current, std::memory_order_relaxed)) {
  }

  const auto release = Tests::support::ScopeExit([] -> void { active.fetch_sub(1, std::memory_order_relaxed); });

  co_await yield();
}

} // namespace MeasurementSubjects

[[ = test, = group("framework"), = tag("stress") ]] auto preservesRepeatedAsyncLifecycle() -> void {
  constexpr usize subjectCount{ParallelSubjects::caseCount};
  constexpr usize repeatCount{3};
  constexpr usize expectedExecutions{subjectCount * repeatCount};

  ParallelSubjects::reset();

  const Vec<TestExecution> executions = runAllDetailed<^^ParallelSubjects>(RunOptions{
      .threads = subjectCount,
      .timeMode = TimeMode::Virtual,
      .repeat = repeatCount,
      .seed = 0x51A7,
      .isolation = CrashIsolation::InProcess,
  });

  require(executions.size() == expectedExecutions);
  require(std::ranges::all_of(
      executions, [](const TestExecution &execution) -> bool { return execution.passed(); }));

  check(ParallelSubjects::invocations.load() == expectedExecutions);
  check(ParallelSubjects::cleanups.load() == expectedExecutions);
  check(std::ranges::all_of(ParallelSubjects::states,
      [](const ParallelSubjects::CaseState &state) -> bool { return state.overlaps.load() == 0; }));
}

[[ = test, = group("framework"), = tag("stress") ]] auto cleansEveryParallelTimeoutExactlyOnce() -> void {
  constexpr usize subjectCount{4};
  constexpr usize repeatCount{3};
  constexpr usize expectedExecutions{subjectCount * repeatCount};

  CancellationSubjects::reset();

  const Vec<TestExecution> executions = runAllDetailed<^^CancellationSubjects>(RunOptions{
      .threads = subjectCount,
      .timeMode = TimeMode::Virtual,
      .repeat = repeatCount,
      .seed = 0xCA11CE,
      .isolation = CrashIsolation::InProcess,
  });

  require(executions.size() == expectedExecutions);
  require(std::ranges::all_of(
      executions, [](const TestExecution &execution) -> bool { return execution.failed(); }));

  check(CancellationSubjects::cleanups.load() == expectedExecutions);
  check(std::ranges::all_of(executions, [](const TestExecution &execution) -> bool {
    return execution.state.errors == 1 and execution.duration == std::chrono::milliseconds{1};
  }));
}

[[ = test, = group("framework"), = tag("stress", "resource_lane") ]] auto serializesSharedResourceLanes()
    -> void {
  ResourceSubjects::reset();

  const Vec<TestExecution> executions = runAllDetailed<^^ResourceSubjects>(RunOptions{
      .threads = 2,
      .timeMode = TimeMode::Virtual,
      .isolation = CrashIsolation::InProcess,
  });

  require(executions.size() == 2);
  require(std::ranges::all_of(
      executions, [](const TestExecution &execution) -> bool { return execution.passed(); }));
  check(ResourceSubjects::peak.load() == 1);
}

[[ = test, = group("framework"), = tag("stress", "parallel_attempts") ]] auto
permitsExplicitParallelAttempts() -> void {
  constexpr usize repeatCount{4};
  ParallelAttemptSubjects::reset();

  const Vec<TestExecution> executions = runAllDetailed<^^ParallelAttemptSubjects>(RunOptions{
      .threads = repeatCount,
      .timeMode = TimeMode::Virtual,
      .repeat = repeatCount,
      .isolation = CrashIsolation::InProcess,
  });

  require(executions.size() == repeatCount);
  require(std::ranges::all_of(
      executions, [](const TestExecution &execution) -> bool { return execution.passed(); }));
  check(ParallelAttemptSubjects::peak.load() > 1);
}

[[ = test, = group("framework"), = tag("stress", "measurement_serial") ]] auto keepsMeasurementSamplesSerial()
    -> void {
  MeasurementSubjects::reset();

  const Vec<TestExecution> executions = runAllDetailed<^^MeasurementSubjects>(RunOptions{
      .threads = 4,
      .timeMode = TimeMode::Virtual,
      .isolation = CrashIsolation::InProcess,
  });

  require(executions.size() == 4);
  require(std::ranges::all_of(executions,
      [](const TestExecution &execution) constexpr noexcept -> bool { return execution.passed(); }));

  check(MeasurementSubjects::peak.load() == 1);
}

[[ = test, = group("framework"), = tag("stress", "parallel_order") ]] auto preservesParallelReportOrder()
    -> void {
  const RunReport report = runAll<^^ParallelSubjects>(RunOptions{
      .threads = ParallelSubjects::caseCount,
      .timeMode = TimeMode::Virtual,
      .isolation = CrashIsolation::InProcess,
  });

  require(report.cases.size() == ParallelSubjects::caseCount);
  check(std::ranges::is_sorted(
      report.cases, {}, [](const TestCaseResult &testCase) constexpr noexcept -> StringView {
        return testCase.descriptor.identifier;
      }));
}

} // namespace Tests::stress
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::stress>();
}
