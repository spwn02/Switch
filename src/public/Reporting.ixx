export module Switch:Reporting;

import std;
import Miracle;

import :Execution;
import :Render;

using namespace Miracle;

export namespace Switch {

struct TestSummary final {
  usize testCount{};
  usize caseCount{};
  usize attemptCount{};
  usize sampleCount{};
  usize retryCount{};
  usize warmupCount{};
  usize recoveredCount{};
  usize passedCaseCount{};
  usize failedCaseCount{};
  usize passedCount{};
  usize failedCount{};
  usize assertionCount{};
  usize failedAssertionCount{};
  usize errorCount{};
  std::chrono::steady_clock::duration duration{};
  std::chrono::steady_clock::duration wallDuration{};

  [[nodiscard]] auto passed() const noexcept -> bool;

  [[nodiscard]] auto failed() const noexcept -> bool;
};

struct SelectionMetadata final {
  Vec<String> include;
  Vec<String> exclude;
  Vec<String> tagsAll;
  Vec<String> tagsAny;
  Option<String> group;
};

/// Groups every physical attempt that belongs to one logical test case.
struct TestAttempt final {
  TestExecution execution;
  AttemptIndex index{};
  bool warmup{};
};

/// Describes the scheduler-time distribution of measured samples.
struct MeasurementSummary final {
  usize sampleCount{};
  std::chrono::steady_clock::duration total{};
  std::chrono::steady_clock::duration minimum{};
  std::chrono::steady_clock::duration maximum{};
  std::chrono::steady_clock::duration mean{};
  std::chrono::steady_clock::duration firstQuartile{};
  std::chrono::steady_clock::duration median{};
  std::chrono::steady_clock::duration thirdQuartile{};
  std::chrono::steady_clock::duration deviation{};
  bool approximate{};
  bool distributionAvailable{true};
  bool quantilesAvailable{};
};

namespace detail {

[[nodiscard]] auto currentProfileSink() noexcept -> profiling::ProfileSink & {
  return currentEnvironment()->get().profileSink();
}

/// Returns scheduler time when a test coroutine is active and host time otherwise.
[[nodiscard]] auto measurementNow() noexcept -> std::chrono::steady_clock::time_point {
  if (RunLoop *const runLoop = currentRunLoop())
    return runLoop->now();

  return std::chrono::steady_clock::now();
}

/// Reduces ordered sample durations into the stable public measurement summary.
[[nodiscard]] auto summarizeMeasurements(Vec<std::chrono::steady_clock::duration> values)
    -> MeasurementSummary {
  using Duration = std::chrono::steady_clock::duration;

  if (values.empty())
    return {};

  std::ranges::sort(values);

  Duration total = std::ranges::fold_left(values, Duration{}, std::plus<>{});
  const auto count = static_cast<Duration::rep>(std::ranges::distance(values));
  const auto mean = total / count;
  P2QuantileEstimator<Duration> firstQuartile{0.25L};
  P2QuantileEstimator<Duration> median{0.50L};
  P2QuantileEstimator<Duration> thirdQuartile{0.75L};
  std::ranges::for_each(values, [&firstQuartile, &median, &thirdQuartile](Duration value) -> void {
    firstQuartile.add(value);
    median.add(value);
    thirdQuartile.add(value);
  });
  const auto meanCount = static_cast<long double>(mean.count());
  long double variance{};
  std::ranges::for_each(values, [&variance, meanCount](const Duration value) -> void {
    const auto delta = static_cast<long double>(value.count()) - meanCount;
    variance += delta * delta;
  });
  variance /= static_cast<long double>(values.size());
  const auto deviation = static_cast<Duration::rep>(std::sqrt(variance));
  const Duration estimatedMedian = std::clamp(median.value(), values.front(), values.back());
  const Duration estimatedFirstQuartile = std::clamp(firstQuartile.value(), values.front(), estimatedMedian);
  const Duration estimatedThirdQuartile = std::clamp(thirdQuartile.value(), estimatedMedian, values.back());

  return MeasurementSummary{
      .sampleCount = values.size(),
      .total = total,
      .minimum = values.front(),
      .maximum = values.back(),
      .mean = mean,
      .firstQuartile = estimatedFirstQuartile,
      .median = estimatedMedian,
      .thirdQuartile = estimatedThirdQuartile,
      .deviation = Duration{deviation},
      .approximate = median.approximate(),
      .quantilesAvailable = true,
  };
}

template <class Function>
concept AsyncMeasurementFunction =
    std::invocable<Function &> and is_task_return_v<std::remove_cvref_t<std::invoke_result_t<Function &>>>;

template <class Function>
auto measureAsync(StringView name,
    usize samples,
    Function function,
    Vec<std::chrono::steady_clock::duration> values,
    usize index,
    std::source_location location) -> Task<MeasurementSummary> {
  if (index >= samples)
    co_return summarizeMeasurements(std::move(values));

  const auto started = measurementNow();
  {
    auto profile = profiling::profileScope(currentProfileSink(), name, location);
    using Return = std::remove_cvref_t<std::invoke_result_t<Function &>>;
    if constexpr (std::same_as<Return, Task<void>>) {
      co_await std::invoke(function);
    } else {
      static_cast<void>(co_await std::invoke(function));
    }
  }
  values.push_back(measurementNow() - started);
  co_return co_await measureAsync(name, samples, std::move(function), std::move(values), index + 1, location);
}

} // namespace detail

/// Measures a synchronous callable repeatedly using scheduler time when available.
template <class Function>
  requires(std::invocable<Function &> and
           not detail::is_task_return_v<std::remove_cvref_t<std::invoke_result_t<Function &>>>)
[[nodiscard]] auto measure(StringView name,
    usize samples,
    Function function,
    std::source_location location = std::source_location::current()) -> MeasurementSummary {
  if (samples == 0)
    fatal("Switch measure() requires at least one sample");

  Vec<std::chrono::steady_clock::duration> values{};
  values.reserve(samples);
  std::ranges::for_each(std::views::indices(samples), [&values, &function, name, location](usize) -> void {
    const auto started = detail::measurementNow();
    {
      auto profile = profiling::profileScope(detail::currentProfileSink(), name, location);
      static_cast<void>(std::invoke(function));
    }
    values.push_back(detail::measurementNow() - started);
  });
  return detail::summarizeMeasurements(std::move(values));
}

/// Measures an asynchronous callable repeatedly on the active Switch run loop.
template <detail::AsyncMeasurementFunction Function>
[[nodiscard]] auto measure(StringView name,
    usize samples,
    Function function,
    std::source_location location = std::source_location::current()) -> Task<MeasurementSummary> {
  if (samples == 0)
    fatal("Switch measure() requires at least one sample");

  return detail::measureAsync(name, samples, std::move(function), {}, 0, location);
}

/// Groups every physical attempt that belongs to one logical test case.
struct TestCaseResult final {
  TestDescriptor descriptor;
  Vec<TestAttempt> attempts;
  Option<MeasurementSummary> measurement;
  usize recoveredTimeouts{};
  usize suppressedAttemptCount{};
  bool failedCase{};

  [[nodiscard]] auto passed() const noexcept -> bool;

  [[nodiscard]] auto failed() const noexcept -> bool;
};

class CaseAccumulator final {
public:
  CaseAccumulator(TestDescriptor descriptor, RetentionPolicy retention, usize maxRetainedFailures);
  auto setRetainSuccessful(bool retain) noexcept -> void;
  auto append(AttemptOutcome outcome) -> void;
  auto appendAggregate(const BatchExecutionContext &batch) -> void;
  [[nodiscard]] auto finish() && -> TestCaseResult;
  [[nodiscard]] auto identifier() const noexcept -> StringView;

private:
  TestCaseResult result_;
  RetentionPolicy retention_;
  usize maxRetainedFailures_{};
  usize retainedFailures_{};
  usize suppressedFailures_{};
  usize sampleCount_{};
  usize attemptCount_{};
  std::chrono::steady_clock::duration totalDuration_{};
  std::chrono::steady_clock::duration minimumDuration_{};
  std::chrono::steady_clock::duration maximumDuration_{};
  long double meanDuration_{};
  long double variableAccumulator_{};
  detail::P2QuantileEstimator<std::chrono::steady_clock::duration> firstQuartile_{0.25L};
  detail::P2QuantileEstimator<std::chrono::steady_clock::duration> median_{0.50L};
  detail::P2QuantileEstimator<std::chrono::steady_clock::duration> thirdQuartile_{0.75L};
  bool aggregateTiming_{};
  bool aggregateDistributionAvailable_{};
  std::chrono::steady_clock::duration aggregateMinimum_{};
  std::chrono::steady_clock::duration aggregateMaximum_{};
  long double aggregateMean_{};
  long double aggregateVariable_{};
  std::chrono::steady_clock::duration aggregateFirstQuartile_{};
  std::chrono::steady_clock::duration aggregateMedian_{};
  std::chrono::steady_clock::duration aggregateThirdQuartile_{};
  bool aggregateQuantilesAvailable_{};
  bool aggregateQuantilesApproximate_{};
  Vec<AttemptIndex> pendingTimeouts_;
  usize recoveredTimeouts_{};
  bool hardFailure_{};
  bool retainSuccessful_{};
  bool retainedFailure_{};
};

struct RunReport;

inline constexpr usize maxRetainedFailuresDefault = 1024;

class RunAccumulator final {
public:
  explicit RunAccumulator(RetentionPolicy retention = RetentionPolicy::Failures,
      usize maxRetainedFailures = maxRetainedFailuresDefault,
      SelectionMetadata selection = {},
      bool measurementsEnabled = true);
  ~RunAccumulator() = default;

  RunAccumulator(const RunAccumulator &) = delete ("RunAccumulator holds mutex state.");
  auto operator=(const RunAccumulator &) -> RunAccumulator & = delete ("RunAccumulator holds mutex state.");
  RunAccumulator(RunAccumulator &&) noexcept = delete ("RunAccumulator holds mutex state.");
  auto operator=(RunAccumulator &&) noexcept
      -> RunAccumulator & = delete ("RunAccumulator holds mutex state.");

  auto append(const TestExecution &execution) -> void;
  auto append(AttemptOutcome outcome) -> void;
  auto append(const TestDescriptor &descriptor, AttemptOutcome outcome) -> void;
  auto appendAggregate(const TestDescriptor &descriptor, const BatchExecutionContext &batch, u64 runSeed)
      -> void;
  auto expectCaseCompletion(const TestDescriptor &descriptor, usize completions) -> void;
  auto completeCase(StringView identifier) -> void;
  auto setCompletionObserver(std::function<void(const TestCaseResult &)> observer) -> void;
  /// Allows the runner to omit synchronization when dispatch is single-threaded.
  auto setConcurrent(bool concurrent) noexcept -> void;
  [[nodiscard]] auto finish() && -> RunReport;

private:
  std::mutex mutex_;
  bool concurrent_{true};
  RetentionPolicy retention_;
  usize maxRetainedFailures_{};
  TestSummary summary_;
  SelectionMetadata selection_;
  bool measurementsEnabled_{true};
  Option<u64> runSeed_;
  Vec<UPtr<CaseAccumulator>> cases_;
  Vec<TestCaseResult> completedCases_;
  Vec<Pair<String, usize>> expectedCompletions_;
  Vec<Pair<String, usize>> observedCompletions_;
  std::function<void(const TestCaseResult &)> completionObserver_;
  bool retainSuccessful_{};
};

/// Presentation-independent result tree shared by human and machine reporters.
struct RunReport final {
  Vec<TestCaseResult> cases;
  TestSummary summary;
  /// False when the run used CapturePolicy::None. Human reporters must omit measurement presentation.
  bool measurementsEnabled{true};
  SelectionMetadata selection;
  Option<u64> runSeed;
  RetentionPolicy retention{RetentionPolicy::Failures};
  usize retainedAttemptCount{};
  usize suppressedAttemptCount{};
  usize measuredCaseCount{};

  [[nodiscard]] auto passed() const noexcept -> bool;

  [[nodiscard]] auto failed() const noexcept -> bool;
};

struct ReporterOptions final {
  RendererOptions renderer{};
  bool showPassedTests{};
  bool showAttempts{};
  bool showSummary{true};
  bool showProgress{true};
};

class Reporter final {
public:
  explicit Reporter(ReporterOptions options = {});
  ~Reporter();

  auto addRoot(Path root) -> void;

  [[nodiscard]] static auto summarize(const RunReport &report) noexcept -> TestSummary;

  auto report(const RunReport &report, std::ostream &output) const -> TestSummary;

  auto beginLive(std::ostream &output, bool measurementsEnabled) -> void;
  auto consumeLive(const TestCaseResult &testCase) -> void;
  auto finishLive(const RunReport &report) -> TestSummary;

private:
  struct RenderState;

  [[nodiscard]] auto colorEnabled() const noexcept -> bool;

  [[nodiscard]] auto shouldRenderTrace(const TestExecution &execution) const noexcept -> bool;

  auto renderCase(const TestCaseResult &testCase,
      const SourceManager &sources,
      std::ostream &output,
      bool useColor,
      RenderState &state) const -> void;

  auto renderMeasuredCase(const TestCaseResult &testCase,
      const SourceManager &sources,
      std::ostream &output,
      RenderState &state) const -> void;

  auto renderAttempt(const TestAttempt &attempt,
      const SourceManager &sources,
      std::ostream &output,
      bool useColor,
      RenderState &state) const -> void;

  auto renderFailure(const TestExecution &execution, const SourceManager &sources, std::ostream &output) const
      -> void;

  auto renderTrace(const TestExecution &execution, std::ostream &output) const -> void;

  auto renderProfile(const TestExecution &execution, std::ostream &output) const -> void;

  auto renderMeasurement(const TestCaseResult &testCase, std::ostream &output, RenderState &state) const
      -> void;

  auto renderLiveCase(const TestCaseResult &testCase, std::ostream &output) const -> void;

  Vec<Path> roots_;
  ReporterOptions options_{};
  std::ostream *liveOutput_{};
  UPtr<RenderState> liveState_;
};

} // namespace Switch
