module Switch;

import std;
import Miracle;

using namespace Miracle;

namespace Switch::detail {

namespace {

struct ScheduledCase final {
  usize plannedCase{};
  usize runIteration{};
};

/// Serializes repeated invocations whose factory has shared fixture or mutable object state.
///
/// Different planned cases retain full worker parallelism. Repetitions of one case use one lane when its
/// factory has shared state and is not concurrently invocable.
///
/// FixtureScope remains safe to initialize concurrently, but serializing fixture-backed cases preserves
/// established ordering for fixture construction and user code that exposes mutable fixture substate.
struct ExecutionLane final {
  [[nodiscard]] auto mutex() noexcept -> std::mutex & {
    return mutex_;
  }

private:
  std::mutex mutex_;
};

/// Carries every value needed to execute one scheduled case.
///
/// The scheduler resolves the planned-case index once and stores the effective isolation policy here. The
/// lower-level attempt and worker functions therefore consume one coherent plan instead of independently
/// combining a case reference, capabilities, index, schedule entry, options, seed, and resource lane.
/// All references in this value are non-owning. The dispatch context and optional worker journal must outlive
/// the plan and every operation that consumes it.
struct InvocationPlan final {
  Ref<const PlannedCase> plannedCase;
  InvocationCapabilities capabilities{};
  usize plannedCaseIndex{};
  ScheduledCase scheduledCase{};
  Ref<const RunOptions> options;
  u64 runSeed{};
  CrashIsolation isolation{};
  bool captureMemory{};
  bool captureProfile{true};
  CapturePolicy captureTiming{CapturePolicy::PerAttempt};
  Option<Ref<ExecutionLane>> lane;
  Option<Ref<WorkerJournal>> journal;
  bool compactJournal{};
  Option<Ref<RunAccumulator>> accumulator;
  Option<Ref<std::atomic<bool>>> failureObserved;

  [[nodiscard]] constexpr auto processIsolated() const noexcept -> bool {
    return isolation == CrashIsolation::ProcessPerCase;
  }
};

class SplitMix64 final {
public:
  explicit constexpr SplitMix64(u64 seed) noexcept
      : state_(seed) {
  }

  [[nodiscard]] constexpr auto next() noexcept -> u64 {
    state_ += increment_;
    u64 value = state_;
    value = (value ^ (value >> firstShift_)) * firstMultiplier_;
    value = (value ^ (value >> secondShift_)) * secondMultiplier_;
    return value ^ (value >> finalShift_);
  }

private:
  static constexpr u64 increment_{0x9E3779B97F4A7C15ULL};
  static constexpr u64 firstMultiplier_{0xBF58476D1CE4E5B9ULL};
  static constexpr u64 secondMultiplier_{0x94D049BB133111EBULL};
  static constexpr u32 firstShift_{30};
  static constexpr u32 secondShift_{27};
  static constexpr u32 finalShift_{31};

  u64 state_{};
};

[[nodiscard]] auto randomSeed() -> u64 {
  constexpr u32 halfSeedWidth{32};
  std::random_device device{};
  const u64 high = static_cast<u64>(device());
  const u64 low = static_cast<u64>(device());
  const u64 clock = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
  SplitMix64 mixer{(high << halfSeedWidth) ^ low ^ clock};
  return mixer.next();
}

[[nodiscard]] constexpr auto stableHash(StringView value) noexcept -> u64 {
  constexpr u64 offset{1469598103934665603ULL};
  constexpr u64 prime{1099511628211ULL};
  u64 hash = offset;

  std::ranges::for_each(value, [&hash](const char character) constexpr noexcept -> void {
    hash ^= static_cast<u64>(static_cast<unsigned char>(character));
    hash *= prime;
  });

  return hash;
}

[[nodiscard]] constexpr auto deriveSeed(u64 runSeed,
    StringView descriptorIdentity,
    usize caseIdentity,
    AttemptIndex attempt,
    bool warmup) -> u64 {
  constexpr u64 streamSalt{0xD1B54A32D192ED03ULL};
  SplitMix64 mixer{runSeed ^ streamSalt};
  const u64 descriptorSeed = mixer.next() ^ stableHash(descriptorIdentity);
  const u64 caseSeed = mixer.next() ^ static_cast<u64>(caseIdentity);
  const u64 runSeedPart = mixer.next() ^ static_cast<u64>(attempt.runIteration);
  const u64 sampleSeed = mixer.next() ^ static_cast<u64>(attempt.sample);
  const u64 retrySeed = mixer.next() ^ static_cast<u64>(attempt.retry);
  const u64 warmupSeed = mixer.next() ^ static_cast<u64>(warmup);
  SplitMix64 result{descriptorSeed ^ caseSeed ^ runSeedPart ^ sampleSeed ^ retrySeed ^ warmupSeed};
  return result.next();
}

[[nodiscard]] auto scheduleCases(usize plannedCaseCount, const RunOptions &options, u64 runSeed)
    -> Vec<ScheduledCase> {
  if (options.repeat == 0)
    fatal("Switch RunOptions::repeat must be greater than zero");

  Vec<ScheduledCase> scheduled{};
  scheduled.reserve(plannedCaseCount * options.repeat);
  std::ranges::for_each(
      std::views::indices(options.repeat), [&scheduled, plannedCaseCount](usize iteration) -> void {
        std::ranges::for_each(
            std::views::indices(plannedCaseCount), [&scheduled, iteration](usize plannedCase) -> void {
              scheduled.push_back(ScheduledCase{
                  .plannedCase = plannedCase,
                  .runIteration = iteration,
              });
            });
      });

  if (options.order == ExecutionOrder::Declaration or scheduled.size() < 2)
    return scheduled;

  SplitMix64 random{runSeed};
  std::ranges::for_each(std::views::iota(1U, scheduled.size()) | std::views::reverse,
      [&scheduled, &random](usize index) -> void {
        const auto selected = static_cast<usize>(random.next() % (index + 1));
        std::ranges::swap(scheduled[index], scheduled[selected]);
      });
  return scheduled;
}

[[nodiscard]] constexpr auto forceTrace(TraceMode traceMode) noexcept -> bool {
  return traceMode != TraceMode::Annotations;
}

[[nodiscard]] auto boundaryFailure(TestDescriptor descriptor, String message) -> TestExecution {
  TestExecution execution{
      .descriptor = std::move(descriptor),
  };
  execution.state.diagnostics.push_back(
      unhandledExceptionDiagnostic(std::move(message), execution.descriptor.location));
  execution.state.errors = 1;
  return execution;
}

[[nodiscard]] auto workerFailure(TestDescriptor descriptor, NativeFault fault, std::source_location location)
    -> TestExecution {
  TestExecution execution{
      .descriptor = std::move(descriptor),
      .fault = fault,
  };
  execution.state.diagnostics.push_back(nativeFaultDiagnostic(fault, location));
  execution.state.errors = 1;
  return execution;
}

[[nodiscard]] auto workerFailure(TestDescriptor descriptor, Diagnostic diagnostic) -> TestExecution {
  TestExecution execution{
      .descriptor = std::move(descriptor),
  };
  execution.state.diagnostics.push_back(std::move(diagnostic));
  execution.state.errors = 1;
  return execution;
}

auto executeAttemptDetailed(const InvocationPlan &plan, AttemptIndex attempt, bool warmup) -> TestExecution {
  const RunOptions &options = plan.options.get();
  const InvocationSettings settings{
      .seed = deriveSeed(plan.runSeed,
          plan.plannedCase.get().descriptor().identifier,
          plan.plannedCase.get().descriptor().testCase,
          attempt,
          warmup),
      .iteration = plan.scheduledCase.runIteration,
      .sample = attempt.sample,
      .retry = attempt.retry,
      .warmup = warmup,
      .forceTrace = forceTrace(options.traceMode),
      .captureMemory = plan.captureMemory,
      .captureProfile = plan.captureProfile,
      .captureTiming = plan.captureTiming,
  };

  const InvocationBinding binding{settings};
  TestExecution execution{};
  const PlannedCase &plannedCase = plan.plannedCase.get();

  try {
    execution = plannedCase.invoke(InvocationRequest(plannedCase.descriptor(), options.timeMode));
  } catch (const TestAbort &) {
    execution = TestExecution{
        .descriptor = TestDescriptor{plannedCase.descriptor()},
    };
    execution.state.aborted = true;
  } catch (const std::exception &exception) {
    const char *message = exception.what();
    execution = boundaryFailure(
        TestDescriptor{plannedCase.descriptor()}, message == nullptr ? "standard exception" : message);
  } catch (...) {
    execution = boundaryFailure(TestDescriptor{plannedCase.descriptor()}, "non-standard exception");
  }
  execution.runSeed = plan.runSeed;
  execution.attempt = attempt;
  execution.iteration = plan.scheduledCase.runIteration;
  execution.warmup = warmup;
  execution.traceMode = options.traceMode;
  return execution;
}

[[nodiscard]] auto executeAttemptOutcome(const InvocationPlan &plan,
    AttemptIndex attempt,
    bool warmup,
    bool retainExecution) -> AttemptOutcome {
  // The normal run path crosses this boundary as a compact outcome. A complete TestExecution is only
  // materialized inside the invocation boundary long enough to preserve diagnostics when retention or failure
  // policy requires it; successful attempts do not escape into a history vector.
  if (retainExecution)
    return makeAttemptOutcome(executeAttemptDetailed(plan, attempt, warmup), true);

  const RunOptions &options = plan.options.get();
  const InvocationSettings settings{
      .seed = deriveSeed(plan.runSeed,
          plan.plannedCase.get().descriptor().identifier,
          plan.plannedCase.get().descriptor().testCase,
          attempt,
          warmup),
      .iteration = plan.scheduledCase.runIteration,
      .sample = attempt.sample,
      .retry = attempt.retry,
      .warmup = warmup,
      .forceTrace = forceTrace(options.traceMode),
      .captureMemory = plan.captureMemory,
      .captureProfile = plan.captureProfile,
      .captureTiming = plan.captureTiming,
  };
  const InvocationBinding binding{settings};
  AttemptOutcome outcome = plan.plannedCase.get().invokeCompact(
      InvocationRequest(plan.plannedCase.get().descriptor(), options.timeMode));
  outcome.runSeed = plan.runSeed;
  outcome.attempt = attempt;
  outcome.iteration = plan.scheduledCase.runIteration;
  outcome.warmup = warmup;
  if (outcome.failure) {
    outcome.failure->runSeed = plan.runSeed;
    outcome.failure->attempt = attempt;
    outcome.failure->iteration = plan.scheduledCase.runIteration;
    outcome.failure->warmup = warmup;
  }
  return outcome;
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

[[nodiscard]] constexpr auto sameSample(const TestExecution &left, const TestExecution &right) noexcept
    -> bool {
  return not left.warmup and not right.warmup and left.attempt.runIteration == right.attempt.runIteration and
         left.attempt.sample == right.attempt.sample;
}

[[nodiscard]] auto batchFailed(const Vec<TestExecution> &executions) noexcept -> bool {
  return std::ranges::any_of(executions | std::views::enumerate,
      [&executions](const Tuple<usize, const TestExecution &> &item) -> bool {
        const auto &[index, execution] = item;
        if (execution.warmup)
          return execution.failed();

        if (execution.passed())
          return false;

        if (not hasTimeout(execution))
          return true;

        const auto recovered = std::ranges::find_if(executions.begin() + static_cast<isize>(index) + 1,
            executions.end(),
            [&execution](const TestExecution &candidate) -> bool {
              return sameSample(execution, candidate) and
                     candidate.attempt.retry > execution.attempt.retry and candidate.passed();
            });
        return recovered == executions.end();
      });
}

class CaseExecutor final {
public:
  explicit CaseExecutor(const InvocationPlan &plan)
      : plan_(plan) {
  }

  [[nodiscard]] auto run() -> Vec<TestExecution> {
    std::unique_lock<std::mutex> lock{};
    const InvocationPlan &plan = plan_.get();
    if (plan.lane)
      lock = std::unique_lock<std::mutex>{plan.lane->get().mutex()};

    runWarmups();
    runSamples();
    if (plan_.get().failureObserved and failed_)
      plan_.get().failureObserved->get().store(true, std::memory_order_relaxed);
    return std::move(executions_);
  }

private:
  struct AttemptObservation final {
    bool passed{};
    bool timeout{};
  };

  auto appendAttempt(Vec<TestExecution> &destination, AttemptIndex attempt, bool warmup)
      -> AttemptObservation {
    const InvocationPlan &plan = plan_.get();
    if (plan.journal)
      plan.journal->get().attemptStarted(attempt, warmup);

    if (plan.compactJournal) {
      AttemptOutcome outcome = executeAttemptOutcome(plan_, attempt, warmup, false);
      if (outcome.failure) {
        if (plan.journal)
          plan.journal->get().attemptCompleted(*outcome.failure);
        const AttemptObservation observation{
            .passed = false,
            .timeout = outcome.timeout,
        };
        destination.push_back(std::move(*outcome.failure));
        return observation;
      }
      if (plan.journal)
        plan.journal->get().attemptCompleted(outcome);
      return AttemptObservation{
          .passed = outcome.passed,
          .timeout = outcome.timeout,
      };
    }

    if (plan.accumulator) {
      AttemptOutcome outcome =
          executeAttemptOutcome(plan_, attempt, warmup, plan.options.get().retention == RetentionPolicy::All);
      const AttemptObservation observation{
          .passed = outcome.passed,
          .timeout = outcome.timeout,
      };
      plan.accumulator->get().append(plan.plannedCase.get().descriptor(), std::move(outcome));
      return observation;
    }

    TestExecution execution = executeAttemptDetailed(plan_, attempt, warmup);
    if (plan.journal)
      plan.journal->get().attemptCompleted(execution);

    const AttemptObservation observation{
        .passed = execution.passed(),
        .timeout = hasTimeout(execution),
    };
    destination.push_back(std::move(execution));

    return observation;
  }

  auto runWarmups() -> void {
    const InvocationPlan &plan = plan_.get();
    const usize warmups = plan.plannedCase.get().descriptor().policy.warmup;
    const usize runIteration = plan.scheduledCase.runIteration;

    std::ranges::for_each(std::views::indices(warmups), [this, runIteration](usize warmupIndex) -> void {
      const AttemptObservation observation = appendAttempt(executions_,
          AttemptIndex{
              .runIteration = runIteration,
              .sample = warmupIndex,
          },
          true);
      failed_ = failed_ or not observation.passed;
    });
  }

  auto runSamples() -> void {
    const InvocationPlan &plan = plan_.get();
    const usize samples = std::max(plan.plannedCase.get().descriptor().policy.repeat, 1UZ);

    if (samples < 2 or not plan.capabilities.allowsParallelAttempts()) {
      std::ranges::for_each(std::views::indices(samples),
          [this](usize sample) -> void { failed_ = runSample(executions_, sample) or failed_; });
      return;
    }

    Vec<Option<Vec<TestExecution>>> sampleExecutions(samples);
    Vec<u8> sampleFailures(samples);

    {
      Vec<std::jthread> workers{};
      workers.reserve(samples);

      std::ranges::for_each(std::views::indices(samples),
          [this, &workers, &sampleExecutions, &sampleFailures](usize sample) -> void {
            workers.emplace_back([this, &sampleExecutions, &sampleFailures, sample] -> void {
              Vec<TestExecution> executions{};
              sampleFailures[sample] = runSample(executions, sample);
              sampleExecutions[sample].emplace(std::move(executions));
            });
          });
    }

    std::ranges::for_each(sampleExecutions, [this](Option<Vec<TestExecution>> &sample) -> void {
      if (sample)
        executions_.append_range(std::move(*sample));
    });

    failed_ = std::ranges::any_of(sampleFailures, std::identity{});
  }

  auto runSample(Vec<TestExecution> &destination, usize sample) -> bool {
    const InvocationPlan &plan = plan_.get();
    usize retryIndex{};
    bool retrying{true};
    bool failed{};

    // The loop invariant is that every stored attempt for this sample has already completed. It exits after
    // the first non-timeout result or after the declared retry budget is exhausted.
    while (retrying) {
      const AttemptObservation observation = appendAttempt(destination,
          AttemptIndex{
              .runIteration = plan_.get().scheduledCase.runIteration,
              .sample = sample,
              .retry = retryIndex,
          },
          false);
      retrying = not observation.passed and observation.timeout and
                 retryIndex < plan.plannedCase.get().descriptor().policy.retry;

      if (retrying)
        ++retryIndex;
      else
        failed_ = not observation.passed;
    }

    return failed;
  }

  Ref<const InvocationPlan> plan_;
  Vec<TestExecution> executions_;
  bool failed_{};
};

auto executeCase(const InvocationPlan &plan) -> Vec<TestExecution> {
  return CaseExecutor{plan}.run();
}

[[nodiscard]] constexpr auto usesBenchmark(const InvocationPlan &plan) noexcept -> bool {
  const RunOptions &options = plan.options.get();
  const TestPolicy &policy = plan.plannedCase.get().descriptor().policy;
  return options.executionMode == ExecutionMode::Benchmark and options.retention != RetentionPolicy::All and
         not options.captureMemory and not options.captureProfile and not policy.trace and
         policy.warmup == 0 and policy.retry == 0;
}

auto executeBenchmarkCase(const InvocationPlan &plan, usize count) -> Vec<TestExecution> {
  BatchExecutionContext batch{};
  const AttemptIndex attempt{.runIteration = plan.scheduledCase.runIteration};
  const InvocationSettings settings{
      .seed = deriveSeed(plan.runSeed,
          plan.plannedCase.get().descriptor().identifier,
          plan.plannedCase.get().descriptor().testCase,
          attempt,
          false),
      .iteration = plan.scheduledCase.runIteration,
      .forceTrace = forceTrace(plan.options.get().traceMode),
      .captureProfile = false,
      .captureTiming = plan.captureTiming,
  };
  const InvocationBinding binding{settings};
  plan.plannedCase.get().invokeBatch(
      InvocationRequest(plan.plannedCase.get().descriptor(), plan.options.get().timeMode), count, batch);
  if (plan.accumulator) {
    plan.accumulator->get().appendAggregate(plan.plannedCase.get().descriptor(), batch, plan.runSeed);
    if (plan.failureObserved and batch.failed())
      plan.failureObserved->get().store(true, std::memory_order_relaxed);
    return {};
  }
  if (batch.firstFailure)
    return Vec<TestExecution>{std::move(*batch.firstFailure)};
  TestExecution aggregate{.descriptor = plan.plannedCase.get().descriptor(),
      .duration = batch.duration,
      .wallDuration = batch.wallDuration,
      .runSeed = plan.runSeed,
      .attempt = attempt};
  aggregate.state.assertions = batch.assertions;
  return Vec<TestExecution>{std::move(aggregate)};
}

[[nodiscard]] auto laneFor(Vec<std::shared_ptr<ExecutionLane>> &executionLanes, usize plannedCase) noexcept
    -> Option<Ref<ExecutionLane>> {
  if (executionLanes.empty() or plannedCase >= executionLanes.size())
    return None;

  std::shared_ptr<ExecutionLane> &lane = executionLanes[plannedCase];
  if (lane == nullptr)
    return None;

  return std::ref(*lane);
}

[[nodiscard]] constexpr auto isolationFor(const PlannedCase &plannedCase, const RunOptions &options) noexcept
    -> CrashIsolation {
  if (plannedCase.descriptor().policy.parent)
    return CrashIsolation::InProcess;

  if (plannedCase.descriptor().policy.isolated or plannedCase.capabilities().requiresIsolation)
    return CrashIsolation::ProcessPerCase;

  return options.isolation;
}

struct DispatchContext final { // NOLINT(cppcoreguidelines-pro-type-member-init)
  Ref<Vec<PlannedCase>> plannedCases;
  Ref<Vec<std::shared_ptr<ExecutionLane>>> executionLanes;
  Ref<const RunOptions> options;
  u64 runSeed{};
  bool captureMemory{};
  Option<Ref<RunAccumulator>> accumulator;
  std::atomic<bool> failureObserved;
};

[[nodiscard]] auto executeIsolatedCase(const InvocationPlan &plan) -> Vec<TestExecution>;

[[nodiscard]] auto makeInvocationPlan(DispatchContext &context, const ScheduledCase &scheduledCase)
    -> InvocationPlan {
  const PlannedCase &plannedCase = context.plannedCases.get()[scheduledCase.plannedCase];
  const CrashIsolation isolation = isolationFor(plannedCase, context.options.get());
  return InvocationPlan{
      .plannedCase = plannedCase,
      .capabilities = plannedCase.capabilities(),
      .plannedCaseIndex = scheduledCase.plannedCase,
      .scheduledCase = scheduledCase,
      .options = context.options,
      .runSeed = context.runSeed,
      .isolation = isolation,
      .captureMemory = context.captureMemory,
      .captureProfile = context.options.get().captureProfile,
      .lane = laneFor(context.executionLanes, scheduledCase.plannedCase),
      .accumulator = context.accumulator,
      .failureObserved = std::ref(context.failureObserved),
  };
}

[[nodiscard]] auto executeScheduledCase(DispatchContext &context, const ScheduledCase &scheduledCase)
    -> Vec<TestExecution> {
  const InvocationPlan plan = makeInvocationPlan(context, scheduledCase);
  if (plan.processIsolated()) {
    std::unique_lock<std::mutex> lock{};
    if (plan.lane)
      lock = std::unique_lock<std::mutex>{plan.lane->get().mutex()};
    return executeIsolatedCase(plan);
  }
  if (usesBenchmark(plan))
    return executeBenchmarkCase(plan,
        context.options.get().repeat * std::max(plan.plannedCase.get().descriptor().policy.repeat, 1UZ));
  return executeCase(plan);
}

[[nodiscard]] constexpr auto workerCount(usize requested, usize workCount) -> usize {
  if (workCount == 0)
    return 0;

  if (requested == 1)
    return 1;

  if (requested == 0) {
    const usize available = std::thread::hardware_concurrency();
    return std::min(std::max(available, 1UZ), workCount);
  }

  return std::min(requested, workCount);
}

[[nodiscard]] auto workerVariables(const WorkerRequest &request) -> Vec<Pair<String, String>> {
  return Vec<Pair<String, String>>{
      {"SWITCH_TEST_WORKER", "1"},
      {"SWITCH_TEST_WORKER_RESULT", request.resultPath.string()},
      {"SWITCH_TEST_WORKER_FAULT", request.faultPath.string()},
      {"SWITCH_TEST_WORKER_IDENTIFIER", request.identifier},
      {"SWITCH_TEST_WORKER_CASE", std::to_string(request.plannedCase)},
      {"SWITCH_TEST_WORKER_ITERATION", std::to_string(request.runIteration)},
      {"SWITCH_TEST_WORKER_REPEAT", std::to_string(request.repeat)},
      {"SWITCH_TEST_WORKER_SEED", std::to_string(request.runSeed)},
      {"SWITCH_TEST_WORKER_TIME", std::to_string(static_cast<u8>(request.timeMode))},
      {"SWITCH_TEST_WORKER_TRACE", std::to_string(static_cast<u8>(request.traceMode))},
      {"SWITCH_TEST_WORKER_MEMORY", request.captureMemory ? "1" : "0"},
      {"SWITCH_TEST_WORKER_PROFILE", request.captureProfile ? "1" : "0"},
      {"SWITCH_TEST_WORKER_TIMING", request.captureTiming == CapturePolicy::PerAttempt ? "1" : "0"},
  };
}

[[nodiscard]] auto workerPaths() -> Option<Pair<Path, Path>> {
  const Result<Path> root = fs::temporaryDirectory("switch-worker");
  if (not root)
    return None;

  return Pair<Path, Path>{*root / "result.bin", *root / "fault.bin"};
}

auto removeWorkerFiles(const Path &resultPath, const Path &faultPath) noexcept -> void {
  std::error_code error{};
  static_cast<void>(std::filesystem::remove(resultPath, error));
  error.clear();
  static_cast<void>(std::filesystem::remove(faultPath, error));
  error.clear();
  static_cast<void>(std::filesystem::remove(resultPath.parent_path(), error));
}

auto appendWorkerFailure(Vec<TestExecution> &executions,
    const InvocationPlan &plan,
    const WorkerJournalResult &journal,
    TestExecution execution) -> void {
  if (journal.activeAttempt and not journal.activeWarmup) {
    const auto completed = std::ranges::find_if(
        executions, [&journal](const TestExecution &execution) constexpr noexcept -> bool {
          return execution.attempt == *journal.activeAttempt;
        });
    if (completed != executions.end()) {
      completed->fault = execution.fault;
      completed->state = std::move(execution.state);
      return;
    }
  }

  execution.runSeed = plan.runSeed;
  execution.iteration = plan.scheduledCase.runIteration;
  execution.attempt = journal.activeAttempt.value_or(AttemptIndex{
      .runIteration = plan.scheduledCase.runIteration,
  });
  execution.warmup = journal.activeWarmup;
  execution.seed = deriveSeed(plan.runSeed,
      plan.plannedCase.get().descriptor().identifier,
      plan.plannedCase.get().descriptor().testCase,
      execution.attempt,
      execution.warmup);
  execution.traceMode = plan.options.get().traceMode;

  if (not execution.state.diagnostics.empty()) {
    Diagnostic &diagnostic = execution.state.diagnostics.front();
    const String status = execution.fault
                              ? std::format("native fault ({})", debug::enumName(execution.fault->kind))
                              : String{"protocol failure"};
    diagnostic.addNote(std::format("worker status: {}", status));
    diagnostic.addNote(std::format("test case: {}", execution.descriptor.identifier));
    diagnostic.addNote(std::format("attempt: run {}, sample {}, retry {}",
        execution.attempt.runIteration + 1,
        execution.attempt.sample + 1,
        execution.attempt.retry));
    diagnostic.addNote(std::format("seed: {}", execution.seed));
  }

  executions.push_back(std::move(execution));
}

auto appendWorkerFault(Vec<TestExecution> &executions,
    const InvocationPlan &plan,
    const WorkerJournalResult &journal,
    NativeFault fault) -> void {
  appendWorkerFailure(executions,
      plan,
      journal,
      workerFailure(
          plan.plannedCase.get().descriptor(), fault, plan.plannedCase.get().descriptor().location));
}

auto appendWorkerProtocolFailure(Vec<TestExecution> &executions,
    const InvocationPlan &plan,
    const WorkerJournalResult &journal,
    StringView message) -> void {
  appendWorkerFailure(executions,
      plan,
      journal,
      workerFailure(plan.plannedCase.get().descriptor(),
          workerProtocolDiagnostic(message, plan.plannedCase.get().descriptor().location)));
}

[[nodiscard]] auto unavailableWorker(const InvocationPlan &plan) -> Vec<TestExecution> {
  Vec<TestExecution> executions{};
  appendWorkerFault(
      executions, plan, WorkerJournalResult{}, NativeFault{.kind = NativeFaultKind::IsolationUnavailable});
  return executions;
}

[[nodiscard]] auto makeWorkerRequest(const InvocationPlan &plan, const Pair<Path, Path> &paths)
    -> WorkerRequest {
  return WorkerRequest{
      .resultPath = paths.first,
      .faultPath = paths.second,
      .identifier = plan.plannedCase.get().descriptor().identifier,
      .plannedCase = plan.plannedCaseIndex,
      .runIteration = plan.scheduledCase.runIteration,
      .repeat = usesBenchmark(plan) ? plan.options.get().repeat *
                                          std::max(plan.plannedCase.get().descriptor().policy.repeat, 1UZ)
                                    : 1,
      .runSeed = plan.runSeed,
      .timeMode = plan.options.get().timeMode,
      .traceMode = plan.options.get().traceMode,
      .captureMemory = plan.captureMemory,
      .captureProfile = plan.captureProfile,
      .captureTiming = plan.captureTiming,
  };
}

[[nodiscard]] auto launchWorkerSafely(const WorkerRequest &request, const Path &executable)
    -> isolation::WorkerOutcome {
  isolation::WorkerOutcome outcome{};
  try {
    outcome = isolation::launchWorker(isolation::WorkerLaunch{
        .executable = executable,
        .variables = workerVariables(request),
    });
  } catch (const std::exception &exception) {
    outcome.error = exception.what() != nullptr ? exception.what() : "worker launch threw an exception";
  } catch (...) {
    outcome.error = "worker launch threw a non-standard exception";
  }
  return outcome;
}

[[nodiscard]] auto readWorkerJournalSafely(const Path &resultPath, const TestDescriptor &descriptor)
    -> Result<WorkerJournalResult> {
  try {
    return readWorkerJournal(resultPath, descriptor);
  } catch (const std::exception &exception) {
    return bail(
        {exception.what() != nullptr ? exception.what() : "worker journal decoding threw an exception"});
  } catch (...) {
    return bail({"worker journal decoding threw a non-standard exception"});
  }
}

auto appendWorkerOutcome(Vec<TestExecution> &executions,
    const InvocationPlan &plan,
    const WorkerJournalResult &journal,
    const isolation::WorkerOutcome &outcome) -> void {
  if (outcome.fault and not journal.completed) {
    appendWorkerFault(executions, plan, journal, *outcome.fault);
    return;
  }

  if (not outcome.error.empty()) {
    appendWorkerProtocolFailure(executions, plan, journal, outcome.error);
    return;
  }

  if (not outcome.launched) {
    appendWorkerProtocolFailure(
        executions, plan, journal, "the process-per-case worker could not be launched");
    return;
  }

  if (journal.completed and executions.empty() and journal.compactPassCount == 0) {
    appendWorkerProtocolFailure(
        executions, plan, journal, "worker completed its journal without an execution record");
    return;
  }

  if (journal.completed)
    return;

  if (outcome.exitCode == 0) {
    appendWorkerProtocolFailure(executions, plan, journal, "worker exited without completing its journal");
    return;
  }

  appendWorkerFault(
      executions, plan, journal, NativeFault{.kind = NativeFaultKind::Terminated, .code = outcome.exitCode});
}

[[nodiscard]] auto executeIsolatedCase(const InvocationPlan &plan) -> Vec<TestExecution> {
  const Option<Pair<Path, Path>> paths = workerPaths();
  if (not paths)
    return unavailableWorker(plan);
  const auto &[resultPath, faultPath] = *paths;

  const auto cleanupWorkerFiles = ScopeGuard([&] -> void { removeWorkerFiles(resultPath, faultPath); });

  const Result<Path> executable = isolation::executablePath();
  if (not executable)
    return unavailableWorker(plan);

  const WorkerRequest request = makeWorkerRequest(plan, *paths);
  const isolation::WorkerOutcome outcome = launchWorkerSafely(request, *executable);

  Result<WorkerJournalResult> journal =
      readWorkerJournalSafely(resultPath, plan.plannedCase.get().descriptor());
  if (not journal) {
    Vec<TestExecution> executions{};
    appendWorkerProtocolFailure(executions, plan, WorkerJournalResult{}, journal.error().display());
    return executions;
  }

  Vec<TestExecution> executions = std::move(journal->executions);

  if (journal->compactPassCount != 0) {
    BatchExecutionContext batch{
        .completed = journal->compactPassCount,
        .passed = journal->compactPassCount,
        .assertions = journal->compactAssertionCount,
        .duration = journal->compactDuration,
        .wallDuration = journal->compactWallDuration,
        .minimumDuration = journal->compactMinimumDuration,
        .maximumDuration = journal->compactMaximumDuration,
        .meanDuration = journal->compactMeanDuration,
        .variableAccumulator = journal->compactVariableAccumulator,
        .timingSamples = journal->compactTimingSamples,
        .firstQuartile = journal->compactFirstQuartile,
        .median = journal->compactMedian,
        .thirdQuartile = journal->compactThirdQuartile,
        .quantilesAvailable = journal->compactQuantilesAvailable,
        .quantilesApproximate = journal->compactQuantilesApproximate,
    };
    if (plan.accumulator)
      plan.accumulator->get().appendAggregate(plan.plannedCase.get().descriptor(), batch, plan.runSeed);
    else {
      TestExecution aggregate{.descriptor = plan.plannedCase.get().descriptor(),
          .duration = batch.duration,
          .wallDuration = batch.wallDuration,
          .runSeed = plan.runSeed,
          .seed = deriveSeed(plan.runSeed,
              plan.plannedCase.get().descriptor().identifier,
              plan.plannedCase.get().descriptor().testCase,
              AttemptIndex{.runIteration = plan.scheduledCase.runIteration},
              false),
          .attempt = AttemptIndex{.runIteration = plan.scheduledCase.runIteration}};
      aggregate.state.assertions = batch.assertions;
      executions.push_back(std::move(aggregate));
    }
  }

  appendWorkerOutcome(executions, plan, *journal, outcome);

  return executions;
}

[[nodiscard]] auto makeExecutionLanes(const Vec<PlannedCase> &plannedCases, usize repeat)
    -> Vec<std::shared_ptr<ExecutionLane>> {
  Vec<std::shared_ptr<ExecutionLane>> executionLanes{};
  Vec<StringView> resourceNames{};
  executionLanes.reserve(plannedCases.size());
  resourceNames.reserve(plannedCases.size());

  std::ranges::for_each(plannedCases | std::views::enumerate,
      [&executionLanes, &resourceNames, repeat](const Pair<usize, PlannedCase> &item) -> void {
        const auto &[index, plannedCase] = item;
        static_cast<void>(index);

        const InvocationCapabilities &capabilities = plannedCase.capabilities();
        const bool repeatedState = repeat > 1 and not capabilities.attemptParallel;

        if (not repeatedState and capabilities.resourceLane.empty()) {
          executionLanes.emplace_back(nullptr);
          resourceNames.emplace_back();
          return;
        }

        if (not capabilities.resourceLane.empty()) {
          const auto found = std::ranges::find(resourceNames, capabilities.resourceLane);
          if (found != resourceNames.end()) {
            const usize laneIndex = static_cast<usize>(found - resourceNames.begin());
            executionLanes.push_back(executionLanes[laneIndex]);
            resourceNames.push_back(capabilities.resourceLane);
            return;
          }
        }

        executionLanes.push_back(std::make_shared<ExecutionLane>());
        resourceNames.push_back(capabilities.resourceLane);
      });

  return executionLanes;
}

[[nodiscard]] auto executeSerialCases(DispatchContext &context, const Vec<ScheduledCase> &scheduledCases)
    -> Vec<TestExecution> {
  Vec<TestExecution> executions{};
  executions.reserve(scheduledCases.size());
  bool stopped{};
  std::ranges::for_each(
      scheduledCases, [&context, &executions, &stopped](const ScheduledCase &scheduledCase) -> void {
        if (stopped)
          return;

        Vec<TestExecution> batch = executeScheduledCase(context, scheduledCase);
        stopped = context.options.get().failFast and
                  (not batch.empty() ? batchFailed(batch)
                                     : context.failureObserved.load(std::memory_order_relaxed));
        if (context.accumulator)
          std::ranges::for_each(
              batch, [&accumulator = context.accumulator](const TestExecution &execution) -> void {
                accumulator->get().append(execution);
              });
        if (context.accumulator)
          context.accumulator->get().completeCase(
              context.plannedCases.get()[scheduledCase.plannedCase].descriptor().identifier);
        else
          executions.append_range(std::move(batch));
      });
  return executions;
}

class ParallelCaseExecutor final {
public:
  ParallelCaseExecutor(DispatchContext &context, const Vec<ScheduledCase> &scheduledCases, usize workers)
      : context_(context)
      , scheduledCases_(scheduledCases)
      , executions_(scheduledCases.size())
      , workers_(workers)
      , ready_(static_cast<isize>(workers))
      , firstBatch_(static_cast<isize>(workers)) {
  }

  [[nodiscard]] auto run() -> Vec<TestExecution> {
    {
      Vec<std::jthread> threads{};
      threads.reserve(workers_);
      std::ranges::for_each(std::views::indices(workers_), [this, &threads](usize) -> void {
        threads.emplace_back(&ParallelCaseExecutor::executeNext, this);
      });
      ready_.wait();
      start_.count_down();
    }

    return executions_ | std::views::filter([](const Option<Vec<TestExecution>> &execution) -> bool {
      return execution.has_value();
    }) | std::views::transform([](Option<Vec<TestExecution>> &execution) -> Vec<TestExecution> {
      return std::move(*execution);
    }) | std::views::join |
           std::ranges::to<Vec<TestExecution>>();
  }

private:
  auto executeNext() -> void {
    DispatchContext &context = context_.get();
    const Vec<ScheduledCase> &scheduledCases = scheduledCases_.get();

    ready_.count_down();
    start_.wait();
    bool firstBatch{true};

    // Every iteration claims one unique schedule slot. The worker exits when the queue is exhausted or the
    // fail-fast flag is published; completed slots remain immutable for the final ordered flattening.
    while (true) {
      if (stopped_.load(std::memory_order_relaxed))
        return;

      const usize index = nextIndex_.fetch_add(1, std::memory_order_relaxed);
      if (index >= scheduledCases.size())
        return;

      if (firstBatch) {
        firstBatch = false;
        firstBatch_.arrive_and_wait();
      }

      Vec<TestExecution> batch = executeScheduledCase(context, scheduledCases[index]);
      const bool failed =
          batch.empty() ? context.failureObserved.load(std::memory_order_relaxed) : batchFailed(batch);
      if (context.accumulator) {
        std::ranges::for_each(
            batch, [&accumulator = context.accumulator](const TestExecution &execution) -> void {
              accumulator->get().append(execution);
            });
        context.accumulator->get().completeCase(
            context.plannedCases.get()[scheduledCases[index].plannedCase].descriptor().identifier);

      } else {
        executions_[index].emplace(std::move(batch));
      }

      if (context.options.get().failFast and failed)
        stopped_.store(true, std::memory_order_relaxed);
    }
  }

  Ref<DispatchContext> context_;
  Ref<const Vec<ScheduledCase>> scheduledCases_;
  Vec<Option<Vec<TestExecution>>> executions_;
  const usize workers_;
  std::latch ready_;
  std::latch start_{1};
  std::barrier<> firstBatch_;
  std::atomic<usize> nextIndex_;
  std::atomic<bool> stopped_;
};

[[nodiscard]] auto executeParallelCases(DispatchContext &context,
    const Vec<ScheduledCase> &scheduledCases,
    usize workers) -> Vec<TestExecution> {
  return ParallelCaseExecutor{context, scheduledCases, workers}.run();
}

} // namespace

auto executePlannedCases(RunSession &session,
    const RunOptions &options,
    Option<Ref<RunAccumulator>> accumulator) -> Vec<TestExecution> {
  Vec<PlannedCase> plannedCases = session.takePlannedCases();
  const u64 runSeed = options.seed ? *options.seed : randomSeed();
  const bool batchExecution = options.executionMode == ExecutionMode::Benchmark and options.repeat > 1 and
                              options.retention != RetentionPolicy::All and not options.captureMemory and
                              not options.captureProfile and
                              std::ranges::all_of(plannedCases, [](const PlannedCase &plannedCase) {
                                const TestPolicy &policy = plannedCase.descriptor().policy;
                                return not policy.trace and policy.warmup == 0 and policy.retry == 0;
                              });
  RunOptions schedulingOptions = options;
  if (batchExecution)
    schedulingOptions.repeat = 1;
  const Vec<ScheduledCase> scheduledCases = scheduleCases(plannedCases.size(), schedulingOptions, runSeed);
  usize workers = workerCount(options.threads, scheduledCases.size());
  if (workers == 0)
    return {};
  Vec<std::shared_ptr<ExecutionLane>> executionLanes = makeExecutionLanes(plannedCases, options.repeat);

  // Repetitions of a stateful single case are serialized by its execution lane. A hardware-sized worker
  // pool only makes those workers contend on the same mutex and on the accumulator.
  if (plannedCases.size() == 1 and not executionLanes.empty() and executionLanes.front() != nullptr)
    workers = 1;

  if (accumulator)
    accumulator->get().setConcurrent(workers > 1);

  if (accumulator) {
    const usize completions = batchExecution ? 1 : options.repeat;
    std::ranges::for_each(plannedCases, [accumulator, completions](const PlannedCase &plannedCase) -> void {
      accumulator->get().expectCaseCompletion(plannedCase.descriptor(), completions);
    });
  }

  DispatchContext context{
      .plannedCases = plannedCases,
      .executionLanes = executionLanes,
      .options = options,
      .runSeed = runSeed,
      .captureMemory = options.captureMemory,
      .accumulator = accumulator,
  };

  if (workers == 1)
    return executeSerialCases(context, scheduledCases);

  return executeParallelCases(context, scheduledCases, workers);
}

auto executeWorkerCase(RunSession &session, const WorkerRequest &request, RunOptions options) -> void {
  Vec<PlannedCase> plannedCases = session.takePlannedCases();
  WorkerJournal journal{request.resultPath};
  if (not journal.ready())
    return;
  journal.setBuffered(true);

  const auto requested =
      std::ranges::find_if(plannedCases, [&request](const PlannedCase &plannedCase) -> bool {
        return plannedCase.descriptor().identifier == request.identifier;
      });
  if (requested == plannedCases.end()) {
    TestExecution failure = workerFailure(TestDescriptor{.identifier = request.identifier},
        workerProtocolDiagnostic(
            "worker could not find the requested test case", std::source_location::current()));
    journal.attemptStarted(AttemptIndex{.runIteration = request.runIteration}, false);
    journal.attemptCompleted(failure);
    journal.complete();
    return;
  }

  const usize plannedCase = static_cast<usize>(std::ranges::distance(plannedCases.begin(), requested));

  options.isolation = CrashIsolation::InProcess;
  options.threads = 1;
  options.repeat = 1;
  options.seed = request.runSeed;
  options.timeMode = request.timeMode;
  options.traceMode = request.traceMode;

  const InvocationPlan plan{
      .plannedCase = std::cref(plannedCases[plannedCase]),
      .capabilities = plannedCases[plannedCase].capabilities(),
      .plannedCaseIndex = request.plannedCase,
      .scheduledCase =
          ScheduledCase{
              .plannedCase = request.plannedCase,
              .runIteration = request.runIteration,
          },
      .options = options,
      .runSeed = request.runSeed,
      .isolation = CrashIsolation::InProcess,
      .captureMemory = request.captureMemory,
      .captureProfile = request.captureProfile,
      .captureTiming = request.captureTiming,
      .journal = journal,
      .compactJournal = true,
  };
  if (not isolation::installWorkerFaultHandler(request.faultPath)) {
    const TestDescriptor descriptor = plannedCases[plannedCase].descriptor();
    const NativeFault fault{
        .kind = NativeFaultKind::IsolationUnavailable,
    };
    Vec<TestExecution> failures{};
    appendWorkerFailure(
        failures, plan, WorkerJournalResult{}, workerFailure(descriptor, fault, descriptor.location));
    journal.attemptStarted(failures.front().attempt, false);
    journal.attemptCompleted(failures.front());
    journal.complete();
    return;
  }

  if (request.repeat <= 1) {
    static_cast<void>(executeCase(plan));
  } else {
    BatchExecutionContext batch{};
    const InvocationSettings settings{
        .seed = deriveSeed(request.runSeed,
            plannedCases[plannedCase].descriptor().identifier,
            plannedCases[plannedCase].descriptor().testCase,
            AttemptIndex{.runIteration = request.runIteration},
            false),
        .iteration = request.runIteration,
        .captureProfile = false,
        .captureTiming = request.captureTiming,
    };
    const InvocationBinding binding{settings};
    plannedCases[plannedCase].invokeBatch(
        InvocationRequest(plannedCases[plannedCase].descriptor(), options.timeMode), request.repeat, batch);
    journal.batchCompleted(batch);
  }
  journal.complete();
}

} // namespace Switch::detail
