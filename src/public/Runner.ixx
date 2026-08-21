export module Switch:Runner;

import std;
import Miracle;

import :Execution;
import :Session;

using namespace Miracle;

namespace Switch {

inline constexpr usize maxRetainedFailuresDefault{1024};

} // namespace Switch

export namespace Switch {

/// Selects the safety boundary used for one logical test case.
///
/// ProcessPerCase is the safe DevelopmentTests default: a native fault terminates only the worker that owns
/// the current logical case. InProcess is intentionally explicit and is useful for trusted microbenchmarks.
enum class[[= debug::derive]] CrashIsolation : u8 {
  InProcess[[= debug::rename("in_process")]],
  ProcessPerCase[[= debug::rename("process_per_case")]],
};

/// Determines how expanded test cases are arranged before dispatch.
enum class ExecutionOrder : u8 {
  Declaration,
  Shuffled,
};

/// Selects between attempt-level diagnostics and aggregate high-volume execution.
enum class[[= debug::derive]] ExecutionMode : u8 {
  Diagnostic[[= debug::rename("diagnostic")]],
  Benchmark[[= debug::rename("benchmark")]],
};

/// Configures execution of independent reflected test cases.
///
/// jobs == 1 selects single-threaded dispatch. Declaration order remains the default and can be replaced by
/// ExecutionOrder::Shuffled. jobs == 0 selects the available logical processor count, falling back to one
/// worker when it cannot be determined. Higher values cap dispatch to that many workers. Each case retains
/// its own deterministic Task<T> run loop.
struct RunOptions final {
  ExecutionMode executionMode{ExecutionMode::Diagnostic};
  /// Number of independent scheduler workers. One is deterministic serial execution; zero selects hardware
  /// concurrency.
  usize threads{1};

  RetentionPolicy retention{RetentionPolicy::Failures};
  usize maxRetainedFailures{maxRetainedFailuresDefault};

  /// Selects the independent scheduler clock supplied to every test execution in this run.
  TimeMode timeMode{TimeMode::Real};

  /// Captures trace events for every case; the default reporter renders them only for failures.
  TraceMode traceMode{TraceMode::ForcedFailures};

  /// Captures advisory process-memory snapshots for each attempt.
  bool captureMemory{};

  /// Captures profiling scopes for each attempt.
  bool captureProfile{true};

  /// Controls aggregate/per-attempt execution timing. Use None for pass/fail-only stress runs.
  CapturePolicy captureTiming{CapturePolicy::PerAttempt};

  /// Preserves declaration order by default; shuffled order is reproducible with seed.
  ExecutionOrder order{ExecutionOrder::Declaration};

  /// Runs every selected case this many times. Zero is invalid.
  usize repeat{1};

  /// Stops dispatch after the first failed execution. Already-running parallel cases are allowed to finish.
  bool failFast{};

  /// Optional root seed for ordering and per-case Context::seed derivation.
  Option<u64> seed;

  /// Contains native faults at the logical-case process boundary by default.
  CrashIsolation isolation{CrashIsolation::ProcessPerCase};
};

class RunAccumulator;

namespace detail {

struct WorkerRequest;

[[nodiscard]] auto executePlannedCases(RunSession &session,
    const RunOptions &options,
    Option<Ref<RunAccumulator>> accumulator = None) -> Vec<TestExecution>;

/// Executes one worker request after the child process has rebuilt the reflected plan.
auto executeWorkerCase(RunSession &session, const WorkerRequest &request, RunOptions options) -> void;

} // namespace detail

} // namespace Switch
