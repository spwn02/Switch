export module Switch;

import std;
import Miracle;

export import :Annotations;
export import :Diagnostics;
export import :Render;
export import :Environment;
export import :Context;
export import :Task;
export import :Expressions;
export import :Assertions;
export import :Policies;
export import :Metadata;
export import :Providers;
export import :Execution;
export import :Fixtures;
export import :Runner;
export import :Reporting;
export import :Json;
export import :Discovery;
export import :Resources;
export import :Session;

// GCC compatibility: keep same-module implementation declarations directly in
// the primary interface. GCC 16 currently fails to make declarations from
// private module partitions reachable to implementation units reliably.
namespace Switch::detail {

using namespace Miracle;

template <class Function>
class ScopeGuard final {
public:
  constexpr explicit ScopeGuard(Function function) noexcept(std::is_nothrow_move_constructible_v<Function>)
      : function_(std::move(function)) {
  }

  ~ScopeGuard() noexcept(noexcept(std::invoke(function_))) {
    std::invoke(function_);
  }

  ScopeGuard(const ScopeGuard &) = delete;
  auto operator=(const ScopeGuard &) -> ScopeGuard & = delete;
  ScopeGuard(ScopeGuard &&) = delete;
  auto operator=(ScopeGuard &&) -> ScopeGuard & = delete;

private:
  [[no_unique_address]] Function function_;
};

template <class Function>
ScopeGuard(Function) -> ScopeGuard<Function>;

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

namespace Switch::detail::isolation {

using namespace Miracle;

inline constexpr u32 faultRecordMagic{0x4E595846};

/// Byte-exact record written directly by a native-fault handler.
struct FaultRecord final {
  Array<Byte, 4> magic{};
  u8 kind{};
  u8 signal{};
  Array<Byte, 4> code{};
  Array<Byte, 8> address{};
  Array<Byte, 8> instruction{};
  u8 symbolsAvailable{};
};

static_assert(sizeof(FaultRecord) == 27);

template <std::unsigned_integral Value, usize Size>
[[nodiscard]] constexpr auto faultBytes(Value value) noexcept -> Array<Byte, Size> {
  Array<Byte, Size> result{};
  std::ranges::for_each(std::views::indices(Size), [&](usize index) constexpr noexcept -> void {
    result.at(index) = static_cast<Byte>(value & 0xff);
    value >>= 8;
  });
  return result;
}

template <std::unsigned_integral Value, usize Size>
[[nodiscard]] constexpr auto faultValue(const Array<Byte, Size> &bytes) noexcept -> Value {
  Value result{};
  std::ranges::for_each(std::views::indices(Size), [&](usize index) constexpr noexcept -> void {
    result |= static_cast<Value>(std::to_integer<u8>(bytes.at(index))) << (index * 8);
  });
  return result;
}

[[nodiscard]] constexpr auto makeFaultRecord(const NativeFault &fault) noexcept -> FaultRecord {
  return FaultRecord{
      .magic = faultBytes<u32, 4>(faultRecordMagic),
      .kind = static_cast<u8>(fault.kind),
      .signal = static_cast<u8>(fault.signal),
      .code = faultBytes<u32, 4>(static_cast<u32>(fault.code)),
      .address = faultBytes<u64, 8>(fault.address),
      .instruction = faultBytes<u64, 8>(fault.instruction),
      .symbolsAvailable = static_cast<u8>(fault.symbolsAvailable),
  };
}

[[nodiscard]] constexpr auto decodeFaultRecord(const FaultRecord &record) noexcept -> Option<NativeFault> {
  if (faultValue<u32>(record.magic) != faultRecordMagic)
    return None;

  return NativeFault{
      .kind = static_cast<NativeFaultKind>(record.kind),
      .signal = static_cast<NativeSignal>(record.signal),
      .code = static_cast<i32>(faultValue<u32>(record.code)),
      .address = faultValue<u64>(record.address),
      .instruction = faultValue<u64>(record.instruction),
      .symbolsAvailable = record.symbolsAvailable != 0,
  };
}

/// Describes one child-process launch without exposing platform handles to Switch.
struct WorkerLaunch final {
  Path executable;
  Vec<Pair<String, String>> variables;
};

/// Describes how the worker ended from the parent's point of view.
struct WorkerOutcome final {
  bool launched{};
  i32 exitCode{};
  Option<NativeFault> fault;
  String error;
};

/// Returns the current test executable path.
[[nodiscard]] auto executablePath() -> Result<Path>;

/// Starts one isolated worker and waits for its terminal status.
[[nodiscard]] auto launchWorker(const WorkerLaunch &launch) -> WorkerOutcome;

/// Installs the platform-native crash boundary inside a worker process.
[[nodiscard]] auto installWorkerFaultHandler(const Path &faultPath) noexcept -> bool;

/// Converts a platform fault record into the public fault representation.
[[nodiscard]] auto readFaultRecord(const Path &path) noexcept -> Option<NativeFault>;

} // namespace Switch::detail::isolation
