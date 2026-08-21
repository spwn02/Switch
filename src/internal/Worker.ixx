module Switch:Worker;

import std;
import Miracle;

import :Diagnostics;
import :Execution;
import :Policies;
import :Task;

using namespace Miracle;

namespace Switch::detail {

/// Environment-encoded request used when the current executable is acting as a worker.
struct WorkerRequest final {
  Path resultPath;
  Path faultPath;
  /// Stable identity used to select the filtered case after the child rebuilds the full reflected plan.
  String identifier;
  usize plannedCase{};
  usize runIteration{};
  usize repeat{1};
  u64 runSeed{};
  TimeMode timeMode{TimeMode::Real};
  TraceMode traceMode{TraceMode::Annotations};
  bool captureMemory{};
  bool captureProfile{true};
  CapturePolicy captureTiming{CapturePolicy::PerAttempt};
};

/// Partial journal state read after a worker exits normally or by native fault.
struct WorkerJournalResult final {
  Vec<TestExecution> executions;
  usize compactPassCount{};
  usize compactAssertionCount{};
  std::chrono::steady_clock::duration compactDuration{};
  std::chrono::steady_clock::duration compactWallDuration{};
  std::chrono::steady_clock::duration compactMinimumDuration{};
  std::chrono::steady_clock::duration compactMaximumDuration{};
  long double compactMeanDuration{};
  long double compactVariableAccumulator{};
  usize compactTimingSamples{};
  std::chrono::steady_clock::duration compactFirstQuartile{};
  std::chrono::steady_clock::duration compactMedian{};
  std::chrono::steady_clock::duration compactThirdQuartile{};
  bool compactQuantilesAvailable{};
  bool compactQuantilesApproximate{};
  Option<AttemptIndex> activeAttempt;
  bool activeWarmup{};
  bool completed{};
};

/// Appends attempt boundaries and completed executions to a worker journal.
class WorkerJournal final {
public:
  explicit WorkerJournal(const Path &path) noexcept;
  ~WorkerJournal() noexcept;

  WorkerJournal(const WorkerJournal &) = delete ("WorkerJournal owns active file stream");
  auto operator=(const WorkerJournal &) -> WorkerJournal & = delete ("WorkerJournal owns active file stream");
  WorkerJournal(WorkerJournal &&) noexcept = delete ("WorkerJournal owns active file stream");
  auto operator=(WorkerJournal &&) noexcept
      -> WorkerJournal & = delete ("WorkerJournal owns active file stream");

  [[nodiscard]] auto ready() const noexcept -> bool;

  auto setBuffered(bool buffered) noexcept -> void;

  auto attemptStarted(AttemptIndex attempt, bool warmup) noexcept -> void;

  auto attemptCompleted(const TestExecution &execution) noexcept -> void;
  auto attemptCompleted(const AttemptOutcome &outcome) noexcept -> void;
  auto batchCompleted(const BatchExecutionContext &batch) noexcept -> void;

  auto complete() noexcept -> void;

private:
  UPtr<std::ofstream> output_;
  bool ready_{};
  bool buffered_{};
  usize recordsSinceFlush_{};
  usize compactPassCount_{};
  usize compactAssertionCount_{};
  std::chrono::steady_clock::duration compactDuration_{};
  std::chrono::steady_clock::duration compactWallDuration_{};
  std::chrono::steady_clock::duration compactMinimumDuration_{};
  std::chrono::steady_clock::duration compactMaximumDuration_{};
  long double compactMeanDuration_{};
  long double compactVariableAccumulator_{};
  usize compactTimingSamples_{};
  std::chrono::steady_clock::duration compactFirstQuartile_{};
  std::chrono::steady_clock::duration compactMedian_{};
  std::chrono::steady_clock::duration compactThirdQuartile_{};
  bool compactQuantilesAvailable_{};
  bool compactQuantilesApproximate_{};
};

/// Consumes the one worker request inherited from the parent process.
[[nodiscard]] auto consumeWorkerRequest() -> Option<WorkerRequest>;

/// Reads all complete journal records, preserving attempts completed before a worker fault.
[[nodiscard]] auto readWorkerJournal(const Path &path, const TestDescriptor &fallback) -> WorkerJournalResult;

/// Writes a diagnostic-safe worker failure when the journal cannot be opened or decoded.
[[nodiscard]] auto workerProtocolDiagnostic(StringView message, std::source_location location) -> Diagnostic;

} // namespace Switch::detail
