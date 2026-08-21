module Switch;

import std;
import Miracle;

import :Diagnostics;
import :Execution;
import :Render;
import :Reporting;

using namespace Miracle;

namespace Switch {

namespace {

constexpr inline StringView green = "\x1b[1;32m";
constexpr inline StringView red = "\x1b[1;31m";
constexpr inline StringView yellow = "\x1b[1;33m";
constexpr inline StringView dim = "\x1b[2m";
constexpr inline StringView cyan = "\x1b[1;36m";
constexpr inline StringView reset = "\x1b[1;0m";
constexpr inline usize progressBarWidth{80};

[[nodiscard]] constexpr auto paint(StringView text, StringView color, bool useColor) -> String {
  if (not useColor)
    return String{text};

  String result{};
  result.reserve(color.size() + text.size() + reset.size());
  result.append(color);
  result.append(text);
  result.append(reset);
  return result;
}

[[nodiscard]] constexpr auto countLabel(usize count, StringView singular, StringView plural) -> String {
  return std::format("{} {}", count, count == 1 ? singular : plural);
}

[[nodiscard]] auto compactCount(usize count) -> String {
  if (count >= 1'000'000)
    return std::format("{:.3g}M", static_cast<double>(count) / 1'000'000.0);
  if (count >= 1'000)
    return std::format("{:.3g}K", static_cast<double>(count) / 1'000.0);
  return std::format("{}", count);
}

[[nodiscard]] constexpr auto durationLabel(std::chrono::steady_clock::duration duration) -> String {
  const long double nanoseconds =
      static_cast<long double>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
  const long double magnitude = std::abs(nanoseconds);
  StringView unit = "ns";
  long double value = nanoseconds;
  if (magnitude >= 1'000'000'000.0L) {
    value /= 1'000'000'000.0L;
    unit = "s";
  } else if (magnitude >= 1'000'000.0L) {
    value /= 1'000'000.0L;
    unit = "ms";
  } else if (magnitude >= 1'000.0L) {
    value /= 1'000.0L;
    unit = "μs";
  }
  if (std::floor(value) == value)
    return std::format("{} {}", static_cast<i64>(value), unit);
  return std::format("{:.3g} {}", static_cast<double>(value), unit);
}

[[nodiscard]] auto measurementLabel(std::chrono::steady_clock::duration duration) -> String {
  const long double nanoseconds =
      static_cast<long double>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
  const long double magnitude = std::abs(nanoseconds);
  StringView unit = "ns";
  long double value = nanoseconds;
  if (magnitude >= 1'000'000'000.0L) {
    value /= 1'000'000'000.0L;
    unit = "s";
  } else if (magnitude >= 1'000'000.0L) {
    value /= 1'000'000.0L;
    unit = "ms";
  } else if (magnitude >= 1'000.0L) {
    value /= 1'000.0L;
    unit = "μs";
  }
  if (std::floor(value) == value)
    return std::format("{} {}", static_cast<i64>(value), unit);
  return std::format("{:.3g} {}", static_cast<double>(value), unit);
}

[[nodiscard]] auto relativeDeviation(const MeasurementSummary &measurement) -> String {
  if (measurement.mean.count() == 0)
    return {};
  const long double percentage = 100.0L * static_cast<long double>(measurement.deviation.count()) /
                                 static_cast<long double>(measurement.mean.count());
  return std::format("{:.3g}%", static_cast<double>(percentage));
}

[[nodiscard]] auto deviationColor(const MeasurementSummary &measurement) -> StringView {
  if (measurement.mean.count() == 0)
    return dim;
  const long double ratio = static_cast<long double>(measurement.deviation.count()) /
                            static_cast<long double>(measurement.mean.count());
  return ratio <= 0.02L ? green : ratio <= 0.10L ? yellow : red;
}

[[nodiscard]] auto withTestContext(StringView identifier, const Diagnostic &diagnostic) -> Diagnostic {
  Diagnostic result = diagnostic;
  result.addNote(std::format("test: {}", identifier));
  return result;
}

[[nodiscard]] auto normalizePath(const Path &path) -> String {
  String res = path.generic_string();

  if (path.is_relative())
    return res;

  std::error_code error{};
  Path cwd = std::filesystem::current_path(error);
  if (error)
    return res;
  String cwdstr = cwd.generic_string();

  usize pos = res.find(cwdstr);

  if (pos == String::npos)
    return res;

  res.replace(res.begin(), res.begin() + static_cast<String::difference_type>(cwdstr.size() + 1), "");

  return res;
}

[[nodiscard]] constexpr auto hasTimeout(const TestExecution &execution) noexcept -> bool {
  const bool hasTimeoutDiagnostic = std::ranges::any_of(
      execution.state.diagnostics, [](const Diagnostic &diagnostic) constexpr noexcept -> bool {
        return diagnostic.header.code == DiagnosticCode::TimeoutExceeded;
      });
  if (not hasTimeoutDiagnostic)
    return false;

  return std::ranges::all_of(
      execution.state.diagnostics, [](const Diagnostic &diagnostic) constexpr noexcept -> bool {
        return diagnostic.header.code == DiagnosticCode::TimeoutExceeded;
      });
}

[[nodiscard]] constexpr auto sameSample(const TestAttempt &left, const TestAttempt &right) noexcept -> bool {
  return not left.warmup and not right.warmup and left.index.runIteration == right.index.runIteration and
         left.index.sample == right.index.sample;
}

[[nodiscard]] constexpr auto recoveredBy(const TestAttempt &failed, const Vec<TestAttempt> &attempts) noexcept
    -> bool {
  return hasTimeout(failed.execution) and
         std::ranges::any_of(attempts, [&failed](const TestAttempt &candidate) constexpr noexcept -> bool {
           return sameSample(failed, candidate) and candidate.index.retry > failed.index.retry and
                  candidate.execution.passed();
         });
}

[[nodiscard]] auto finalSamples(const Vec<TestAttempt> &attempts) -> Vec<const TestAttempt *> {
  Vec<const TestAttempt *> samples =
      attempts | std::views::filter([](const TestAttempt &attempt) constexpr noexcept -> bool {
        return not attempt.warmup;
      }) |
      std::views::transform(
          [](const TestAttempt &attempt) -> const TestAttempt * { return std::addressof(attempt); }) |
      std::ranges::to<Vec<const TestAttempt *>>();
  std::ranges::sort(
      samples, [](const TestAttempt *left, const TestAttempt *right) constexpr noexcept -> bool {
        if (left->index.runIteration != right->index.runIteration)
          return left->index.runIteration < right->index.runIteration;
        if (left->index.sample != right->index.sample)
          return left->index.sample < right->index.sample;
        return left->index.retry < right->index.retry;
      });

  Vec<const TestAttempt *> result{};
  result.reserve(samples.size());
  std::ranges::for_each(samples, [&result](const TestAttempt *attempt) -> void {
    if (result.empty() or result.back()->index.runIteration != attempt->index.runIteration or
        result.back()->index.sample != attempt->index.sample) {
      result.push_back(attempt);
      return;
    }

    result.back() = attempt;
  });
  return result;
}

[[nodiscard]] auto makeMeasurement(const TestCaseResult &testCase) -> Option<MeasurementSummary> {
  if (testCase.descriptor.policy.repeat <= 1 and testCase.descriptor.policy.warmup == 0 and
      testCase.descriptor.policy.retry == 0)
    return None;

  const Vec<const TestAttempt *> samples = finalSamples(testCase.attempts);
  if (samples.empty())
    return None;

  Vec<std::chrono::steady_clock::duration> values =
      samples | std::views::transform([](const TestAttempt *attempt) -> std::chrono::steady_clock::duration {
        return attempt->execution.duration;
      }) |
      std::ranges::to<Vec<std::chrono::steady_clock::duration>>();
  return detail::summarizeMeasurements(std::move(values));
}

[[nodiscard]] auto attemptLabel(const TestExecution &execution) -> String {
  if (execution.warmup)
    return std::format("warmup {}", execution.attempt.sample + 1);

  if (execution.attempt.retry == 0 and execution.attempt.sample == 0 and execution.iteration == 0)
    return {};

  return std::format("sample {}, retry {}, run {}",
      execution.attempt.sample + 1,
      execution.attempt.retry,
      execution.attempt.runIteration + 1);
}

[[nodiscard]] auto durationText(std::chrono::steady_clock::duration duration) -> String {
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
  if (milliseconds.count() != 0)
    return std::format("{} ms", milliseconds.count());

  const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration);
  return std::format("{} μs", microseconds.count());
}

/// Builds the presentation-independent report tree from physical test executions.
class RunReportBuilder final {
public:
  explicit RunReportBuilder(usize expectedCases) {
    report_.cases.reserve(expectedCases);
  }

  auto operator()(const TestExecution &execution) -> void {
    const auto existing = std::ranges::find_if(
        report_.cases, [&execution](const TestCaseResult &testCase) constexpr noexcept -> bool {
          return testCase.descriptor.identifier == execution.descriptor.identifier;
        });

    const TestAttempt attempt{
        .execution = execution,
        .index = execution.attempt,
        .warmup = execution.warmup,
    };

    if (existing == report_.cases.end()) {
      report_.cases.push_back(TestCaseResult{
          .descriptor = execution.descriptor,
          .attempts = Vec<TestAttempt>{attempt},
      });
      return;
    }

    existing->attempts.push_back(attempt);
  }

  [[nodiscard]] auto finish() && -> RunReport {
    std::ranges::for_each(report_.cases, &RunReportBuilder::finalizeCase);
    return std::move(report_);
  }

private:
  static auto finalizeCase(TestCaseResult &testCase) -> void {
    testCase.measurement = makeMeasurement(testCase);
    testCase.recoveredTimeouts = static_cast<usize>(
        std::ranges::count_if(testCase.attempts, [&testCase](const TestAttempt &attempt) -> bool {
          return not attempt.warmup and not attempt.execution.passed() and
                 recoveredBy(attempt, testCase.attempts);
        }));
  }

  RunReport report_;
};

/// Accumulates summary counters without rebuilding or traversing the report tree through nested lambdas.
class SummaryAccumulator final {
public:
  auto operator()(const TestCaseResult &testCase) noexcept -> void {
    if (testCase.passed())
      ++summary_.passedCaseCount;
    else
      ++summary_.failedCaseCount;

    if (testCase.recoveredTimeouts != 0)
      ++summary_.recoveredCount;

    std::ranges::for_each(testCase.attempts, std::ref(*this));
  }

  auto operator()(const TestAttempt &attempt) noexcept -> void {
    ++summary_.testCount;
    ++summary_.attemptCount;
    summary_.duration += attempt.execution.duration;
    summary_.wallDuration += attempt.execution.wallDuration;
    summary_.assertionCount += attempt.execution.state.assertions;
    summary_.failedAssertionCount += attempt.execution.state.failedAssertions;
    summary_.errorCount += attempt.execution.state.errors;

    if (attempt.warmup)
      ++summary_.warmupCount;
    else
      ++summary_.sampleCount;

    if (attempt.index.retry != 0)
      ++summary_.retryCount;

    if (attempt.execution.passed())
      ++summary_.passedCount;
    else
      ++summary_.failedCount;
  }

  [[nodiscard]] auto finish() && noexcept -> TestSummary {
    return summary_;
  }

private:
  TestSummary summary_{};
};

/// Renders the fixed-width proress bar and the final human-readable run summary.
auto renderSummary(const TestSummary &summary,
    const RunReport &report,
    bool useColor,
    bool showProgress,
    std::ostream &output)
    -> void {
  if (showProgress and summary.testCount != 0) {
    const usize passedWidth = summary.passedCaseCount * progressBarWidth / summary.caseCount;
    const usize failedWidth = progressBarWidth - passedWidth;

    output << '\n'
           << paint(String(passedWidth, '='), green, useColor)
           << paint(String(failedWidth, '='), red, useColor) << '\n';
  }

  output << '\n'
         << paint(summary.passed() ? "PASS" : "FAIL", summary.passed() ? green : red, useColor) << "  "
         << (summary.failed() ? std::format("{} / {} tests", summary.passedCaseCount, summary.caseCount)
                              : countLabel(summary.caseCount, "test", "tests"))
         << " · " << countLabel(summary.assertionCount, "assertion", "assertions");
  if (report.measurementsEnabled)
    output << "  " << durationLabel(summary.wallDuration);
  output << '\n'
         << '\n'
         << "  " << paint("Execution", dim, useColor) << "    "
         << countLabel(summary.caseCount, "case", "cases") << " · "
         << countLabel(summary.attemptCount, "attempt", "attempts");
  if (report.measurementsEnabled and summary.sampleCount != 0)
    output << " · " << compactCount(summary.sampleCount) << " samples";
  if (report.measurementsEnabled and summary.warmupCount != 0)
    output << " · " << compactCount(summary.warmupCount) << " warmups";
  output << '\n';
  if (summary.recoveredCount != 0 or summary.retryCount != 0)
    output << "  " << paint("Recovery", dim, useColor) << "      " << summary.recoveredCount
           << " recovered · " << summary.retryCount << " retries\n";
  if (summary.errorCount != 0)
    output << "  " << paint("Errors", dim, useColor) << "        " << summary.errorCount << '\n';
  if (report.measurementsEnabled) {
    const usize measured = report.measuredCaseCount;
    if (measured != 0)
      output << "  " << paint("Measurements", dim, useColor) << "   " << measured << " measured "
             << (measured == 1 ? "case" : "cases") << '\n';
  }
  output << '\n';
}

} // namespace

struct Reporter::RenderState final {
  bool previousFailure{};
  bool measurementsEnabled{true};
  bool measurementHeader{};
  usize identityWidth{};
  usize samplesWidth{};
  usize timeWidth{};
  usize minimumWidth{};
  usize quartileWidth{};
  usize maximumWidth{};
  usize meanWidth{};
  usize deviationWidth{};
};

auto TestSummary::passed() const noexcept -> bool {
  return failedCaseCount == 0 and (caseCount != 0 or failedCount == 0);
}

auto TestSummary::failed() const noexcept -> bool {
  return not passed();
}

auto TestCaseResult::passed() const noexcept -> bool {
  if (failedCase)
    return false;
  return std::ranges::all_of(attempts, [this](const TestAttempt &attempt) constexpr noexcept -> bool {
    return attempt.execution.passed() or (not attempt.warmup and recoveredBy(attempt, attempts));
  });
}

auto TestCaseResult::failed() const noexcept -> bool {
  return not passed();
}

CaseAccumulator::CaseAccumulator(TestDescriptor descriptor,
    RetentionPolicy retention,
    usize maxRetainedFailures)
    : result_{.descriptor = std::move(descriptor)}
    , retention_(retention)
    , maxRetainedFailures_(maxRetainedFailures) {
}

auto CaseAccumulator::setRetainSuccessful(bool retain) noexcept -> void {
  retainSuccessful_ = retain;
}

auto CaseAccumulator::append(AttemptOutcome outcome) -> void {
  ++attemptCount_;
  // Only a successful terminal attempt represents a measured logical sample.
  // Failed and timed-out attempts remain available through the retention policy.
  const bool measured = not outcome.warmup and outcome.passed;
  if (measured)
    ++sampleCount_;
  if (measured)
    totalDuration_ += outcome.duration;
  if (sampleCount_ == 1 and measured) {
    minimumDuration_ = outcome.duration;
    maximumDuration_ = outcome.duration;
  } else if (measured) {
    minimumDuration_ = std::min(minimumDuration_, outcome.duration);
    maximumDuration_ = std::max(maximumDuration_, outcome.duration);
  }
  if (measured) {
    const auto sample = static_cast<long double>(outcome.duration.count());
    const long double delta = sample - meanDuration_;
    meanDuration_ += delta / static_cast<long double>(sampleCount_);
    variableAccumulator_ += delta * (sample - meanDuration_);
    firstQuartile_.add(outcome.duration);
    median_.add(outcome.duration);
    thirdQuartile_.add(outcome.duration);
  }

  if (outcome.failure) {
    if (retention_ == RetentionPolicy::All or retainedFailures_ < maxRetainedFailures_) {
      result_.attempts.push_back(TestAttempt{
          .execution = std::move(*outcome.failure),
          .index = outcome.attempt,
          .warmup = outcome.warmup,
      });
      ++retainedFailures_;
      retainedFailure_ = true;
    } else {
      ++suppressedFailures_;
    }
  } else if (outcome.passed and retainSuccessful_) {
    // Keep one successful representative for the ordinary live row. Repeated cases use the incremental
    // measurement summary instead of rendering this representative as a physical-attempt row.
    if (not std::ranges::any_of(result_.attempts,
            [](const TestAttempt &attempt) { return attempt.execution.passed(); })) {
      TestExecution execution{
          .descriptor = result_.descriptor,
          .duration = outcome.duration,
          .wallDuration = outcome.wallDuration,
          .runSeed = outcome.runSeed,
          .seed = outcome.seed,
          .iteration = outcome.iteration,
          .attempt = outcome.attempt,
          .warmup = outcome.warmup,
      };
      execution.state.assertions = outcome.assertions;
      execution.state.failedAssertions = outcome.failedAssertions;
      execution.state.errors = outcome.errors;
      result_.attempts.push_back(TestAttempt{
          .execution = std::move(execution),
          .index = outcome.attempt,
          .warmup = outcome.warmup,
      });
    }
  }
  if (outcome.passed and outcome.attempt.retry != 0) {
    const usize before = pendingTimeouts_.size();
    std::erase_if(pendingTimeouts_, [&outcome](const AttemptIndex &attempt) constexpr noexcept -> bool {
      return attempt.runIteration == outcome.attempt.runIteration and
             attempt.sample == outcome.attempt.sample and attempt.retry < outcome.attempt.retry;
    });
    recoveredTimeouts_ += before - pendingTimeouts_.size();
  } else if (not outcome.passed) {
    if (outcome.timeout)
      pendingTimeouts_.push_back(outcome.attempt);
    else
      hardFailure_ = true;
  }
}

auto CaseAccumulator::appendAggregate(const BatchExecutionContext &batch) -> void {
  attemptCount_ += batch.completed - (batch.firstFailure ? 1UZ : 0UZ);
  aggregateTiming_ = true;
  aggregateDistributionAvailable_ = batch.timingSamples != 0;
  aggregateMinimum_ = batch.minimumDuration;
  aggregateMaximum_ = batch.maximumDuration;
  aggregateMean_ = batch.meanDuration;
  aggregateVariable_ = batch.variableAccumulator;
  aggregateFirstQuartile_ = batch.firstQuartile;
  aggregateMedian_ = batch.median;
  aggregateThirdQuartile_ = batch.thirdQuartile;
  aggregateQuantilesAvailable_ = batch.quantilesAvailable;
  aggregateQuantilesApproximate_ = batch.quantilesApproximate;
  sampleCount_ += batch.passed;
  totalDuration_ += batch.duration;
  if (batch.firstFailure) {
    AttemptOutcome failure = makeAttemptOutcome(*batch.firstFailure, true);
    failure.attempt = batch.firstFailureAttempt.value_or(AttemptIndex{});
    append(std::move(failure));
  }
}

auto CaseAccumulator::finish() && -> TestCaseResult {
  if (sampleCount_ != 0 and
      (aggregateTiming_ or (retainSuccessful_ and attemptCount_ > 1) or result_.descriptor.policy.repeat > 1 or
          result_.descriptor.policy.warmup != 0 or result_.descriptor.policy.retry != 0)) {
    result_.measurement = MeasurementSummary{
        .sampleCount = sampleCount_,
        .total = totalDuration_,
        .minimum = aggregateTiming_ ? aggregateMinimum_ : minimumDuration_,
        .maximum = aggregateTiming_ ? aggregateMaximum_ : maximumDuration_,
        .mean =
            aggregateTiming_
                ? (aggregateDistributionAvailable_
                          ? std::chrono::steady_clock::duration{static_cast<
                                std::chrono::steady_clock::duration::rep>(aggregateMean_)}
                          : totalDuration_ /
                                static_cast<std::chrono::steady_clock::duration::rep>(sampleCount_))
                : std::chrono::steady_clock::duration{static_cast<std::chrono::steady_clock::duration::rep>(
                      meanDuration_)},
        .firstQuartile = aggregateTiming_ ? aggregateFirstQuartile_ : firstQuartile_.value(),
        .median = aggregateTiming_ ? aggregateMedian_ : median_.value(),
        .thirdQuartile = aggregateTiming_ ? aggregateThirdQuartile_ : thirdQuartile_.value(),
        .deviation =
            std::chrono::steady_clock::duration{static_cast<std::chrono::steady_clock::duration::rep>(
                std::sqrt((aggregateTiming_ ? aggregateVariable_ : variableAccumulator_) /
                          static_cast<long double>(sampleCount_)))},
        .approximate = aggregateTiming_ ? aggregateQuantilesApproximate_ : median_.approximate(),
        .distributionAvailable = not aggregateTiming_ or aggregateDistributionAvailable_,
        .quantilesAvailable = aggregateTiming_ ? aggregateQuantilesAvailable_ : median_.available(),
    };
  }
  result_.failedCase = hardFailure_ or not pendingTimeouts_.empty();
  result_.recoveredTimeouts = recoveredTimeouts_;
  result_.suppressedAttemptCount = suppressedFailures_;
  return std::move(result_);
}

auto CaseAccumulator::identifier() const noexcept -> StringView {
  return result_.descriptor.identifier;
}

RunAccumulator::RunAccumulator(RetentionPolicy retention,
    usize maxRetainedFailures,
    SelectionMetadata selection,
    bool measurementsEnabled)
    : retention_(retention)
    , maxRetainedFailures_(maxRetainedFailures)
    , selection_(std::move(selection))
    , measurementsEnabled_(measurementsEnabled) {
}

auto RunAccumulator::setCompletionObserver(std::function<void(const TestCaseResult &)> observer) -> void {
  std::scoped_lock lock{mutex_};
  completionObserver_ = std::move(observer);
  retainSuccessful_ = static_cast<bool>(completionObserver_);
  for (UPtr<CaseAccumulator> &caseAccumulator : cases_)
    caseAccumulator->setRetainSuccessful(retainSuccessful_);
}

auto RunAccumulator::expectCaseCompletion(const TestDescriptor &descriptor, usize completions) -> void {
  std::scoped_lock lock{mutex_};
  expectedCompletions_.push_back(Pair<String, usize>{descriptor.identifier, completions});
}

auto RunAccumulator::completeCase(StringView identifier) -> void {
  std::unique_lock lock{mutex_, std::defer_lock};
  if (concurrent_)
    lock.lock();

  auto expected = std::ranges::find_if(expectedCompletions_, [identifier](const Pair<String, usize> &item) {
    return item.first == identifier;
  });
  auto observed = std::ranges::find_if(observedCompletions_, [identifier](const Pair<String, usize> &item) {
    return item.first == identifier;
  });
  if (observed == observedCompletions_.end()) {
    observedCompletions_.push_back(Pair<String, usize>{String{identifier}, 1});
    observed = std::prev(observedCompletions_.end());
  } else {
    ++observed->second;
  }
  if (expected == expectedCompletions_.end() or observed->second < expected->second)
    return;

  auto existing = std::ranges::find_if(cases_, [identifier](const UPtr<CaseAccumulator> &candidate) {
    return candidate->identifier() == identifier;
  });
  if (existing == cases_.end())
    return;
  TestCaseResult result = std::move(**existing).finish();
  cases_.erase(existing);
  if (completionObserver_)
    completionObserver_(result);
  completedCases_.push_back(std::move(result));
}

auto RunAccumulator::append(const TestExecution &execution) -> void {
  append(makeAttemptOutcome(execution, retention_ == RetentionPolicy::All));
}

auto RunAccumulator::append(AttemptOutcome outcome) -> void {
  // Copy before moving the outcome: the descriptor reference must not observe the moved-from argument.
  TestDescriptor descriptor = outcome.descriptor;
  append(descriptor, std::move(outcome));
}

auto RunAccumulator::append(const TestDescriptor &descriptor, AttemptOutcome outcome) -> void {
  std::unique_lock lock{mutex_, std::defer_lock};
  if (concurrent_)
    lock.lock();
  ++summary_.testCount;
  ++summary_.attemptCount;
  summary_.duration += outcome.duration;
  summary_.wallDuration += outcome.wallDuration;
  summary_.assertionCount += outcome.assertions;
  summary_.failedAssertionCount += outcome.failedAssertions;
  summary_.errorCount += outcome.errors;

  if (outcome.warmup)
    ++summary_.warmupCount;
  else
    ++summary_.sampleCount;

  if (outcome.attempt.retry != 0)
    ++summary_.retryCount;

  if (outcome.passed)
    ++summary_.passedCount;
  else
    ++summary_.failedCount;

  if (not runSeed_)
    runSeed_ = outcome.runSeed;

  auto existing = std::ranges::find_if(
      cases_, [&descriptor](const UPtr<CaseAccumulator> &candidate) constexpr noexcept -> bool {
        return candidate->identifier() == descriptor.identifier;
      });
  if (existing == cases_.end()) {
    cases_.push_back(std::make_unique<CaseAccumulator>(descriptor, retention_, maxRetainedFailures_));
    existing = std::prev(cases_.end());
    (*existing)->setRetainSuccessful(retainSuccessful_);
  }
  (*existing)->append(std::move(outcome));
}

auto RunAccumulator::appendAggregate(const TestDescriptor &descriptor,
    const BatchExecutionContext &batch,
    u64 runSeed) -> void {
  std::unique_lock lock{mutex_, std::defer_lock};
  if (concurrent_)
    lock.lock();
  summary_.testCount += batch.completed;
  summary_.attemptCount += batch.completed;
  summary_.sampleCount += batch.completed;
  summary_.passedCount += batch.passed;
  summary_.failedCount += batch.completed - batch.passed;
  summary_.assertionCount += batch.assertions;
  summary_.failedAssertionCount += batch.failedAssertions;
  summary_.errorCount += batch.errors;
  summary_.duration += batch.duration;
  summary_.wallDuration += batch.wallDuration;
  if (not runSeed_)
    runSeed_ = runSeed;
  auto existing = std::ranges::find_if(cases_, [&descriptor](const UPtr<CaseAccumulator> &candidate) -> bool {
    return candidate->identifier() == descriptor.identifier;
  });
  if (existing == cases_.end()) {
    cases_.push_back(std::make_unique<CaseAccumulator>(descriptor, retention_, maxRetainedFailures_));
    existing = std::prev(cases_.end());
    (*existing)->setRetainSuccessful(retainSuccessful_);
  }
  (*existing)->appendAggregate(batch);
}

auto RunAccumulator::setConcurrent(bool concurrent) noexcept -> void {
  concurrent_ = concurrent;
}

auto RunAccumulator::finish() && -> RunReport {
  RunReport report{
      .measurementsEnabled = measurementsEnabled_,
      .selection = std::move(selection_),
      .runSeed = runSeed_,
      .retention = retention_,
  };
  report.cases.reserve(cases_.size());
  report.cases.append_range(std::move(completedCases_));
  std::ranges::for_each(cases_, [&report](UPtr<CaseAccumulator> &caseAccumulator) -> void {
    report.cases.push_back(std::move(*caseAccumulator).finish());
  });

  // Completion order is nondeterministic under parallel dispatch. Restore logical-case and physical-attempt
  // order before reporters or serializers observe the report.
  std::ranges::stable_sort(report.cases, [](const TestCaseResult &lhs, const TestCaseResult &rhs) -> bool {
    return std::tie(lhs.descriptor.identifier, lhs.descriptor.testCase) <
           std::tie(rhs.descriptor.identifier, rhs.descriptor.testCase);
  });

  std::ranges::for_each(report.cases, [](TestCaseResult &testCase) -> void {
    std::ranges::stable_sort(testCase.attempts, [](const TestAttempt &lhs, const TestAttempt &rhs) -> bool {
      return std::tie(lhs.index.runIteration, lhs.index.sample, lhs.index.retry, lhs.warmup) <
             std::tie(rhs.index.runIteration, rhs.index.sample, rhs.index.retry, rhs.warmup);
    });
  });

  report.retainedAttemptCount = std::ranges::fold_left(report.cases,
      usize{},
      [](usize count, const TestCaseResult &testCase) -> usize { return count + testCase.attempts.size(); });
  report.suppressedAttemptCount =
      std::ranges::fold_left(report.cases, usize{}, [](usize count, const TestCaseResult &testCase) -> usize {
        return count + testCase.suppressedAttemptCount;
      });
  report.measuredCaseCount = static_cast<usize>(std::ranges::count_if(
      report.cases, [](const TestCaseResult &testCase) { return testCase.measurement.has_value(); }));

  summary_.caseCount = report.cases.size();
  summary_.passedCaseCount = static_cast<usize>(std::ranges::count_if(report.cases,
      [](const TestCaseResult &testCase) constexpr noexcept -> bool { return testCase.passed(); }));
  summary_.failedCaseCount = summary_.caseCount - summary_.passedCaseCount;
  summary_.recoveredCount = static_cast<usize>(
      std::ranges::count_if(report.cases, [](const TestCaseResult &testCase) constexpr noexcept -> bool {
        return testCase.recoveredTimeouts != 0;
      }));
  report.summary = summary_;
  return report;
}

auto RunReport::passed() const noexcept -> bool {
  return std::ranges::all_of(
      cases, [](const TestCaseResult &result) constexpr noexcept -> bool { return result.passed(); });
}

auto RunReport::failed() const noexcept -> bool {
  return not passed();
}

Reporter::Reporter(ReporterOptions options)
    : options_(options) {
}

Reporter::~Reporter() = default;

auto Reporter::addRoot(Path root) -> void {
  roots_.push_back(std::move(root));
}

auto Reporter::summarize(const RunReport &report) noexcept -> TestSummary {
  if (report.summary.caseCount != 0 or report.cases.empty())
    return report.summary;
  SummaryAccumulator accumulator{};
  std::ranges::for_each(report.cases, std::ref(accumulator));
  TestSummary result = std::move(accumulator).finish();
  result.caseCount = report.cases.size();
  return result;
}

auto Reporter::report(const RunReport &report, std::ostream &output) const -> TestSummary {
  const bool useColor = colorEnabled();
  const SourceManager sources{roots_};
  const TestSummary summary = summarize(report);
  RenderState state{};
  state.measurementsEnabled = report.measurementsEnabled;
  for (usize index{}; index < report.cases.size();) {
    const bool measured = state.measurementsEnabled and report.cases[index].measurement.has_value();
    usize end = index + 1;
    if (measured) {
      while (end < report.cases.size() and report.cases[end].measurement.has_value())
        ++end;
      if (end - index >= 2) {
        state.identityWidth = 0;
        for (usize row = index; row < end; ++row)
          state.identityWidth = std::max(state.identityWidth,
              std::format("tests {}", report.cases[row].descriptor.identifier).size() + 3);
        output << String(state.identityWidth, ' ')
                << paint("samples   time │ min ├─[q1 · med · q3]─┤ max │ mean · deviation", dim, useColor)
                << '\n';
      }
    }
    for (; index < end; ++index)
      renderCase(report.cases[index], sources, output, useColor, state);
    state.identityWidth = 0;
  }

  if (options_.showSummary)
    renderSummary(summary, report, useColor, options_.showProgress, output);
  return summary;
}

auto Reporter::beginLive(std::ostream &output, bool measurementsEnabled) -> void {
  liveOutput_ = std::addressof(output);
  liveState_ = std::make_unique<RenderState>();
  liveState_->measurementsEnabled = measurementsEnabled;
}

auto Reporter::consumeLive(const TestCaseResult &testCase) -> void {
  if (liveOutput_ == nullptr or liveState_ == nullptr)
    return;
  renderLiveCase(testCase, *liveOutput_);
  liveOutput_->flush();
}

auto Reporter::finishLive(const RunReport &report) -> TestSummary {
  const TestSummary summary = summarize(report);
  if (liveOutput_ != nullptr and options_.showSummary)
    renderSummary(summary, report, colorEnabled(), options_.showProgress, *liveOutput_);
  if (liveOutput_ != nullptr)
    liveOutput_->flush();
  liveOutput_ = nullptr;
  liveState_.reset();
  return summary;
}

auto Reporter::renderLiveCase(const TestCaseResult &testCase, std::ostream &output) const -> void {
  if (liveState_ == nullptr)
    return;

  const bool useColor = colorEnabled();
  const SourceManager sources{roots_};
  if (testCase.measurement or testCase.attempts.size() <= 1) {
    renderCase(testCase, sources, output, useColor, *liveState_);
    return;
  }

  // A logical case may contain many outer repetitions and retries. They remain in the retained report,
  // but live output is one case-level summary rather than one physical-attempt row.
  const TestAttempt *representative = nullptr;
  for (const TestAttempt &attempt : testCase.attempts) {
    if (not attempt.warmup)
      representative = std::addressof(attempt);
  }
  if (representative == nullptr)
    representative = std::addressof(testCase.attempts.back());

  TestCaseResult summaryCase{
      .descriptor = testCase.descriptor,
      .attempts = Vec<TestAttempt>{*representative},
      .failedCase = testCase.failedCase,
  };
  summaryCase.attempts.front().execution.duration = std::ranges::fold_left(
      testCase.attempts,
      std::chrono::steady_clock::duration{},
      [](std::chrono::steady_clock::duration total, const TestAttempt &attempt) {
        return total + attempt.execution.duration;
      });
  summaryCase.attempts.front().execution.wallDuration = std::ranges::fold_left(
      testCase.attempts,
      std::chrono::steady_clock::duration{},
      [](std::chrono::steady_clock::duration total, const TestAttempt &attempt) {
        return total + attempt.execution.wallDuration;
      });
  renderCase(summaryCase, sources, output, useColor, *liveState_);
}

auto Reporter::colorEnabled() const noexcept -> bool {
  switch (options_.renderer.color) {
    case ColorMode::Always: return true;
    case ColorMode::Automatic: return options_.renderer.terminal;
    case ColorMode::Never:
    default: return false;
  }

  std::unreachable();
}

auto Reporter::shouldRenderTrace(const TestExecution &execution) const noexcept -> bool {
  if (execution.state.traces.empty())
    return false;

  if (has(options_.renderer.effectiveSections(), DiagnosticSection::Trace) or
      execution.traceMode == TraceMode::ForcedAll)
    return true;

  return execution.traceMode == TraceMode::ForcedFailures and execution.failed();
}

auto Reporter::renderFailure(const TestExecution &execution,
    const SourceManager &sources,
    std::ostream &output) const -> void {
  std::ranges::for_each(execution.state.diagnostics, [&](const Diagnostic &diagnostic) -> void {
    render(withTestContext(execution.descriptor.identifier, diagnostic), sources, output, options_.renderer);
  });
}

auto Reporter::renderTrace(const TestExecution &execution, std::ostream &output) const -> void {
  const bool useColor = colorEnabled();
  std::ranges::for_each(execution.state.traces, [&](const TraceEvent &event) -> void {
    output << paint("  = trace:", cyan, useColor) << ' ' << event.message;
    if (event.remoteLocation) {
      output << std::format(
          " ({}:{})", normalizePath(event.remoteLocation->file), event.remoteLocation->line);
    } else {
      output << std::format(" ({}:{})", normalizePath(event.location.file_name()), event.location.line());
    }

    output << '\n';
  });
}

auto Reporter::renderProfile(const TestExecution &execution, std::ostream &output) const -> void {
  if (not has(options_.renderer.effectiveSections(), DiagnosticSection::Profile))
    return;

  std::ranges::for_each(execution.profile.events, [&output](const profiling::ProfileEvent &event) -> void {
    output << std::format("  = profile: {} ({})\n", event.name, durationText(event.duration));
  });
}

auto Reporter::renderMeasurement(const TestCaseResult &testCase,
    std::ostream &output,
    RenderState &state) const -> void {
  const bool useColor = colorEnabled();
  if (not testCase.measurement)
    return;
  const MeasurementSummary &measurement = *testCase.measurement;
  const bool passed = testCase.passed();
  const String status = String{passed ? "ok" : "failed"};
  if (not measurement.distributionAvailable) {
    output << std::format("tests {} ... {} · {} samples · {}\n",
        testCase.descriptor.identifier,
        paint(status, passed ? green : red, useColor),
        compactCount(measurement.sampleCount),
        durationLabel(measurement.total));
    return;
  }
  const String minimum = measurementLabel(measurement.minimum);
  const String firstQuartile =
      measurement.quantilesAvailable ? measurementLabel(measurement.firstQuartile) : "?";
  const String median = measurement.quantilesAvailable ? measurementLabel(measurement.median) : "?";
  const String thirdQuartile =
      measurement.quantilesAvailable ? measurementLabel(measurement.thirdQuartile) : "?";
  const String maximum = measurementLabel(measurement.maximum);
  const String mean = measurementLabel(measurement.mean);
  const String deviation = measurementLabel(measurement.deviation);
  const String identity = std::format("tests {}", testCase.descriptor.identifier);
  const usize leader = state.identityWidth > identity.size() ? state.identityWidth - identity.size() : 3;
  output << identity << paint(String(leader, '.'), dim, useColor) << ' '
         << paint(status, passed ? green : red, useColor) << ' ' << compactCount(measurement.sampleCount)
         << " samples " << durationLabel(measurement.total) << " │ " << minimum << " ├─[" << firstQuartile
         << " · " << median << " · " << thirdQuartile << "]─┤ " << maximum << " │ μ " << mean << " · σ "
         << deviation;
  if (not relativeDeviation(measurement).empty())
    output << " · " << paint(relativeDeviation(measurement), deviationColor(measurement), useColor);
  output << '\n';
}

auto Reporter::renderMeasuredCase(const TestCaseResult &testCase,
    const SourceManager &sources,
    std::ostream &output,
    RenderState &state) const -> void {
  renderMeasurement(testCase, output, state);

  if (options_.showAttempts) {
    const bool useColor = colorEnabled();
    std::ranges::for_each(
        testCase.attempts, [this, &sources, &output, useColor, &state](const TestAttempt &attempt) -> void {
          renderAttempt(attempt, sources, output, useColor, state);
        });
    return;
  }

  const auto representative =
      std::ranges::find_if(testCase.attempts, [this](const TestAttempt &attempt) -> bool {
        return not attempt.warmup and shouldRenderTrace(attempt.execution);
      });
  if (representative != testCase.attempts.end())
    renderTrace(representative->execution, output);

  std::ranges::for_each(testCase.attempts, [&](const TestAttempt &attempt) -> void {
    if (not attempt.execution.failed() or recoveredBy(attempt, testCase.attempts))
      return;

    output << '\n';
    if (shouldRenderTrace(attempt.execution))
      renderTrace(attempt.execution, output);
    renderProfile(attempt.execution, output);
    renderFailure(attempt.execution, sources, output);
    state.previousFailure = true;
  });
}

auto Reporter::renderAttempt(const TestAttempt &attempt,
    const SourceManager &sources,
    std::ostream &output,
    bool useColor,
    RenderState &state) const -> void {
  const TestExecution &execution = attempt.execution;
  if (execution.warmup and execution.passed())
    return;

  if (execution.warmup) {
    if (state.previousFailure)
      output << '\n';

    output << std::format("tests {} ({}) ... {}{}\n",
        execution.descriptor.identifier,
        attemptLabel(execution),
        paint("FAILED", red, useColor),
        state.measurementsEnabled ? std::format(" {}", durationLabel(execution.duration)) : String{});
    if (shouldRenderTrace(execution))
      renderTrace(execution, output);
    renderProfile(execution, output);
    renderFailure(execution, sources, output);
    state.previousFailure = true;
    return;
  }

  const bool passed = execution.passed();
  const bool showTrace = shouldRenderTrace(execution);
  if (passed and not options_.showPassedTests and not showTrace and execution.attempt.retry == 0)
    return;

  if (state.previousFailure)
    output << '\n';

  const String label = attemptLabel(execution);
  const String status = passed and execution.attempt.retry != 0
                            ? std::format("passed after {} timeout {}",
                                  execution.attempt.retry,
                                  countLabel(execution.attempt.retry, "retry", "retries"))
                            : String{passed ? "ok" : "failed"};
  output << std::format("tests {}{} ... {}{}\n",
      execution.descriptor.identifier,
      label.empty() ? String{} : std::format(" ({}) ", label),
      paint(status, passed ? green : red, useColor),
      state.measurementsEnabled ? std::format(" {}", durationLabel(execution.duration)) : String{});

  if (passed) {
    if (showTrace)
      renderTrace(execution, output);
    renderProfile(execution, output);

    state.previousFailure = false;
    return;
  }

  output << '\n';
  if (showTrace)
    renderTrace(execution, output);
  renderProfile(execution, output);
  renderFailure(execution, sources, output);
  state.previousFailure = true;
}

auto Reporter::renderCase(const TestCaseResult &testCase,
    const SourceManager &sources,
    std::ostream &output,
    bool useColor,
    RenderState &state) const -> void {
  if (state.measurementsEnabled and testCase.measurement) {
    renderMeasuredCase(testCase, sources, output, state);
    return;
  }

  std::ranges::for_each(
      testCase.attempts, [this, &sources, &output, useColor, &state](const TestAttempt &attempt) -> void {
        renderAttempt(attempt, sources, output, useColor, state);
      });
}

} // namespace Switch
