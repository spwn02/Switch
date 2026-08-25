export module Switch:Execution;

import std;
import Miracle;

import :Assertions;
import :Context;
import :Diagnostics;
import :Environment;
import :Expressions;
import :Policies;
import :Task;

using namespace Miracle;

export namespace Switch {

namespace detail {

/// Fixed-memory P² estimator. The first five samples use exact R-7 interpolation; subsequent samples
/// update five markers without retaining the sample history.
template <class Duration>
class P2QuantileEstimator final {
public:
  explicit P2QuantileEstimator(long double probability = 0.5L) noexcept
      : probability_(probability) {
  }

  auto add(Duration value) noexcept -> void {
    const long double sample = static_cast<long double>(value.count());
    if (count_ < 5) {
      startup_[count_++] = sample;
      if (count_ == 5)
        initialize();
      return;
    }

    usize cell{};
    if (sample < heights_[0]) {
      heights_[0] = sample;
      cell = 0;
    } else if (sample >= heights_[4]) {
      heights_[4] = sample;
      cell = 3;
    } else {
      while (cell != 4 and sample >= heights_[cell + 1])
        ++cell;
    }
    for (usize index = cell + 1; index != 5; ++index)
      positions_[index] += 1.0L;
    for (usize index{}; index != 5; ++index)
      desired_[index] += increments_[index];

    for (usize index = 1; index != 4; ++index) {
      const long double delta = desired_[index] - static_cast<long double>(positions_[index]);
      const int direction = delta >= 1.0L ? 1 : delta <= -1.0L ? -1 : 0;
      // A marker may move only when the adjacent marker is more than one rank away. The signed
      // comparison must be reversed for a marker moving toward the lower-ranked neighbor.
      const long double gap = positions_[index + direction] - positions_[index];
      if (direction == 0 or (direction > 0 ? gap <= 1.0L : gap >= -1.0L))
        continue;

      const long double left = positions_[index] - positions_[index - 1];
      const long double right = positions_[index + 1] - positions_[index];
      const long double leftHeight = heights_[index] - heights_[index - 1];
      const long double rightHeight = heights_[index + 1] - heights_[index];
      const long double parabolic =
          heights_[index] + static_cast<long double>(direction) / (left + right) *
                                ((left + static_cast<long double>(direction)) * rightHeight / right +
                                    (right - static_cast<long double>(direction)) * leftHeight / left);
      const long double linear = heights_[index] + static_cast<long double>(direction) *
                                                       (heights_[index + direction] - heights_[index]) /
                                                       (positions_[index + direction] - positions_[index]);
      heights_[index] =
          parabolic > heights_[index - 1] and parabolic < heights_[index + 1] ? parabolic : linear;
      positions_[index] += static_cast<long double>(direction);
    }
  }

  [[nodiscard]] auto available() const noexcept -> bool {
    return count_ != 0;
  }
  [[nodiscard]] auto approximate() const noexcept -> bool {
    return count_ >= 5;
  }
  [[nodiscard]] auto count() const noexcept -> usize {
    return count_;
  }

  [[nodiscard]] auto value() const noexcept -> Duration {
    if (count_ == 0)
      return {};
    if (count_ < 5) {
      std::array<long double, 5> values = startup_;
      sortPrefix(values, count_);
      const long double index = probability_ * static_cast<long double>(count_ - 1);
      const usize lower = static_cast<usize>(index);
      const usize upper = std::min(lower + 1, count_ - 1);
      const long double fraction = index - static_cast<long double>(lower);
      return Duration{
          static_cast<typename Duration::rep>(values[lower] + fraction * (values[upper] - values[lower]))};
    }
    return Duration{static_cast<typename Duration::rep>(heights_[2])};
  }

private:
  static auto sortPrefix(std::array<long double, 5> &values, usize count) noexcept -> void {
    for (usize index = 1; index < count; ++index) {
      const long double value = values[index];
      usize position = index;
      while (position != 0 and value < values[position - 1]) {
        values[position] = values[position - 1];
        --position;
      }
      values[position] = value;
    }
  }

  auto initialize() noexcept -> void {
    sortPrefix(startup_, 5);
    for (usize index{}; index != 5; ++index) {
      heights_[index] = startup_[index];
      positions_[index] = static_cast<long double>(index + 1);
    }
    desired_ = {
        1.0L, 1.0L + 2.0L * probability_, 1.0L + 4.0L * probability_, 3.0L + 2.0L * probability_, 5.0L};
    increments_ = {0.0L, probability_, 0.5L, 1.0L - probability_, 1.0L};
  }

  long double probability_{};
  usize count_{};
  std::array<long double, 5> startup_{};
  std::array<long double, 5> heights_{};
  std::array<long double, 5> positions_{};
  std::array<long double, 5> desired_{};
  std::array<long double, 5> increments_{};
};

} // namespace detail

/// Immutable reflected labels attached to an expanded test case.
struct TestMetadata final {
  Option<String> group;
  Vec<String> tags;
};

struct TestDescriptor final {
  String identifier;
  std::source_location location;
  String name;
  String description;
  usize testCase{};
  TestPolicy policy{};
  TestMetadata metadata{};
};

/// Controls how much physical execution history a normal run result retains.
enum class[[= debug::derive]] RetentionPolicy : u8 {
  Failures[[= debug::rename("failures")]],
  All[[= debug::rename("all")]],
};

struct AttemptIndex final {
  usize runIteration{};
  usize sample{};
  usize retry{};

  [[nodiscard]] constexpr auto operator==(const AttemptIndex &) const noexcept -> bool = default;
};

/// Identifies the native mechanism that terminated an isolated worker.
enum class[[= debug::derive]] NativeFaultKind : u8 {
  Signal[[= debug::rename("signal")]],
  StructuredException[[= debug::rename("structured_exception")]],
  Terminated[[= debug::rename("terminated")]],
  IsolationUnavailable[[= debug::rename("isolation_unavailable")]],
};

/// Identifies the portable class of a POSIX signal that terminated a test worker.
enum class[[= debug::derive]] NativeSignal : u8 {
  Unknown[[= debug::rename("unknown signal")]],
  Abort[[= debug::rename("abort")]],
  BusError[[= debug::rename("bus error")]],
  FloatingPointException[[= debug::rename("floating-point exception")]],
  IllegalInstruction[[= debug::rename("illegal instruction")]],
  SegmentationFault[[= debug::rename("segmentation fault")]],
  Trap[[= debug::rename("trace or breakpoint Trap")]],
};

/// Minimal, allocation-free fault data collected by a worker boundary.
struct NativeFault final {
  NativeFaultKind kind{NativeFaultKind::Terminated};
  NativeSignal signal{NativeSignal::Unknown};
  i32 code{};
  u64 address{};
  u64 instruction{};
  bool symbolsAvailable{};
};

struct TestExecution final {
  TestDescriptor descriptor;
  TestState state;
  std::chrono::steady_clock::duration duration{};
  std::chrono::steady_clock::duration wallDuration{};
  profiling::ProfileSnapshot profile;
  ResourceSnapshot resources;
  Option<memory::ProcessMemorySnapshot> memoryBefore;
  Option<memory::ProcessMemorySnapshot> memoryAfter;
  /// Root seed selected for the whole RunOptions invocation.
  u64 runSeed{};
  /// Per-case seed exposed through Context::seed.
  u64 seed{};
  /// Zero-based repeat index for this execution.
  usize iteration{};
  AttemptIndex attempt{};
  bool warmup{};
  TraceMode traceMode{TraceMode::Annotations};
  /// Is set when the parent reconstructs a terminal native fault from an isolated worker.
  Option<NativeFault> fault;

  [[nodiscard]] auto passed() const noexcept -> bool;

  [[nodiscard]] auto failed() const noexcept -> bool;
};

/// Compact result emitted by one physical attempt before run-level aggregation.
/// Successful attempts retain counters and timing only; failures may retain the full execution.
struct AttemptOutcome final {
  TestDescriptor descriptor{};
  StringView identifier;
  AttemptIndex attempt{};
  std::chrono::steady_clock::duration duration{};
  std::chrono::steady_clock::duration wallDuration{};
  std::chrono::steady_clock::duration minimumDuration{};
  std::chrono::steady_clock::duration maximumDuration{};
  long double meanDuration{};
  long double variableAccumulator{};
  usize timingSamples{};
  usize assertions{};
  usize failedAssertions{};
  usize errors{};
  u64 runSeed{};
  u64 seed{};
  usize iteration{};
  bool warmup{};
  bool passed{};
  bool timeout{};
  Option<TestExecution> failure;
};

/// Stack-local aggregate produced by one type-aware throughput invocation.
struct BatchExecutionContext final {
  usize completed{};
  usize passed{};
  usize assertions{};
  usize failedAssertions{};
  usize errors{};
  std::chrono::steady_clock::duration duration{};
  std::chrono::steady_clock::duration wallDuration{};
  std::chrono::steady_clock::duration minimumDuration{};
  std::chrono::steady_clock::duration maximumDuration{};
  long double meanDuration{};
  long double variableAccumulator{};
  usize timingSamples{};
  std::chrono::steady_clock::duration firstQuartile{};
  std::chrono::steady_clock::duration median{};
  std::chrono::steady_clock::duration thirdQuartile{};
  bool quantilesAvailable{};
  bool quantilesApproximate{};
  Vec<std::chrono::steady_clock::duration> quantileSamples;
  Option<TestExecution> firstFailure;
  Option<AttemptIndex> firstFailureAttempt;

  [[nodiscard]] constexpr auto failed() const noexcept -> bool {
    return firstFailure.has_value();
  }
};

[[nodiscard]] auto makeAttemptOutcome(TestExecution execution, bool retainExecution = false)
    -> AttemptOutcome;

namespace detail {

using Deadline = Option<std::chrono::steady_clock::time_point>;

/// Carries the stable shared by every return-value normalization step in one attempt.
struct NormalizationContext final { // NOLINT(cppcoreguidelines-pro-type-member-init)
  Ref<TestEnvironment> environment;
  Ref<const Context> context;
  std::source_location location;
  Ref<RunLoop> runLoop;
  Ref<const Deadline> deadline;
  bool captureProfile{true};
};

/// Groups the policy inputs that are produced while an attempt is being completed.
struct PolicyApplication final { // NOLINT(cppcoreguidelines-pro-type-member-init)
  Ref<const TestPolicy> policy;
  Ref<TestEnvironment> environment;
  std::chrono::steady_clock::duration elapsed{};
  bool retry{};
  bool cancelled{};
  bool timeoutTriggered{};
  std::source_location location;
};

// NOLINTBEGIN(readability-identifier-naming)
template <class>
inline constexpr bool is_result_return_v{};

template <class Value>
inline constexpr bool is_result_return_v<Result<Value>>{true};

template <class>
inline constexpr bool is_task_return_v{};

template <class Value>
inline constexpr bool is_task_return_v<Task<Value>>{true};
// NOLINTEND(readability-identifier-naming)

template <class Function>
concept ContextInvocable = std::invocable<Function, const Context &>;

template <class Function>
concept TestInvocable = std::invocable<Function> or ContextInvocable<Function>;

[[nodiscard]] auto returnedErrorDiagnostic(const Error &error, std::source_location location) -> Diagnostic;

[[nodiscard]] auto unhandledExceptionDiagnostic(String message, std::source_location location) -> Diagnostic;

[[nodiscard]] auto panickedDiagnostic(String message, std::source_location location) -> Diagnostic;

[[nodiscard]] auto taskLifecycleDiagnostic(const TaskLifecycleError &error, std::source_location location)
    -> Diagnostic;

[[nodiscard]] auto nativeFaultDiagnostic(const NativeFault &fault, std::source_location location)
    -> Diagnostic;

auto applyPolicy(const PolicyApplication &application) -> void;

template <class Value>
auto normalizeResult(const Result<Value> &result, NormalizationContext &context) -> void;

template <class Value>
auto normalizeTask(Task<Value> &&task, NormalizationContext &context) -> void;

template <class Return>
auto normalizeReturn(Return &&returned, NormalizationContext &context) -> void {
  using Type = std::remove_cvref_t<Return>;

  if constexpr (is_task_return_v<Type>) {
    static_assert(not std::is_lvalue_reference_v<Return>,
        "Switch asynchronous test functions must return Task<T> by value.");
    normalizeTask(std::forward<Return>(returned), context);
  } else if constexpr (std::same_as<Type, Expression>) {
    static_cast<void>(check(std::forward<Return>(returned), context.location));
  } else if constexpr (is_result_return_v<Type>) {
    normalizeResult(returned, context);
  } else if constexpr (BoolTestable<Return>) {
    if (static_cast<bool>(std::forward<Return>(returned))) {
      context.environment.get().recordPass();
      return;
    }

    Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::AssertionFailed, context.location);
    diagnostic.header.descriptionOverride = "test returned false";
    diagnostic.details.spans.front().label = "test return";
    diagnostic.details.spans.front().selection = SpanSelection::Declaration;
    context.environment.get().recordFailure(std::move(diagnostic));
  } else {
    static_assert(meta::always_false_v<Type>,
        "Switch functions must return void, a bool-testable value, or Result<T>, or Task<T>.");
  }
}

/// Re-established all execution bindings before a coroutine frame resumes.
auto resumeWithBindings(TestEnvironment &environment, const Context &context, std::coroutine_handle<> handle)
    -> void {
  EnvironmentBinding environmentBinding{environment};
  ContextBinding contextBinding{context};
  handle.resume();
}

/// Callback used by the scheduler to restore task-local state before each resume.
struct ResumeCallback final {
  Ref<TestEnvironment> environment;
  Ref<const Context> context;

  auto operator()(std::coroutine_handle<> handle) const -> void {
    resumeWithBindings(environment.get(), context.get(), handle);
  }
};

/// Requests cancellation once the scheduler reaches the attempt deadline.
struct TimeoutStopCallback final {
  Ref<NormalizationContext> context;

  auto operator()() const -> void {
    NormalizationContext &ctx = context.get();
    const Deadline &deadline = ctx.deadline.get();
    TestEnvironment &environment = ctx.environment.get();
    RunLoop &runLoop = ctx.runLoop.get();

    if (deadline and not environment.stopRequested() and
        (runLoop.timeoutTriggered() or runLoop.now() > *deadline))
      environment.requestStop();
  }
};

/// Supplies the scheduler with the next timeout boundary, unless cancellation already won the race.
struct TimeoutWakeCallback final {
  Ref<const NormalizationContext> context;

  [[nodiscard]] auto operator()() const -> Deadline {
    const NormalizationContext &ctx = context.get();

    if (ctx.environment.get().stopRequested())
      return None;

    return ctx.deadline.get();
  }
};

template <class Value>
auto normalizeTask(Task<Value> &&task, NormalizationContext &context) -> void {
  const TaskDriveResult result = detail::drive(task,
      context.runLoop,
      ResumeCallback{.environment = context.environment, .context = context.context},
      TimeoutStopCallback{context},
      TimeoutWakeCallback{context});

  TestEnvironment &environment = context.environment.get();

  switch (result.status) {
    case TaskDriveStatus::Completed: break;
    case TaskDriveStatus::Cancelled: environment.requestStop(); return;
    case TaskDriveStatus::Empty:
      environment.recordError(
          detail::taskLifecycleDiagnostic(TaskLifecycleError{TaskLifecycleFailure::Empty}, context.location));
      return;
    case TaskDriveStatus::Stranded:
      environment.recordError(detail::taskLifecycleDiagnostic(
          TaskLifecycleError{TaskLifecycleFailure::Stranded}, context.location));
      return;
    case TaskDriveStatus::PendingWork:
      environment.recordError(detail::taskLifecycleDiagnostic(
          TaskLifecycleError{TaskLifecycleFailure::PendingWork, result.pendingWork}, context.location));
      return;
  }

  if constexpr (std::same_as<Value, void>) {
    std::move(task).takeResult();
  } else {
    normalizeReturn(std::move(task).takeResult(), context);
  }
}

template <class Value>
auto normalizeResult(const Result<Value> &result, NormalizationContext &context) -> void {
  if (not result) {
    context.environment.get().recordError(returnedErrorDiagnostic(result.error(), context.location));
    return;
  }

  if constexpr (std::is_same_v<Value, void>)
    return;
  else
    normalizeReturn(*result, context);
}

template <class Function>
auto invokeTest(Function &&function, const Context &context) -> decltype(auto) {
  if constexpr (ContextInvocable<Function>)
    return std::invoke(std::forward<Function>(function), context);
  else
    return std::invoke(std::forward<Function>(function));
}

/// Owns the mutable state of one physical attempt from setup through final reporting.
///
/// The object deliberately keeps the environment and scheduler together: the scheduler's stop token belongs
/// to this environment, and every Context created for the attempt refers to the same resource arena.
struct ActiveExecution final {
  TestExecution execution;
  std::chrono::steady_clock::time_point wallStarted;
  Option<memory::ProcessMemorySnapshot> memoryBefore;
  TestEnvironment environment;
  RunLoop runLoop;
  Deadline deadline;
  bool canceled{};
  bool captureProfile{true};
  std::chrono::steady_clock::duration bodyDuration{};
  std::chrono::steady_clock::duration bodyWallDuration{};

  ActiveExecution(TestDescriptor descriptor, const InvocationSettings &invocation, TimeMode timeMode)
      : execution{
            .descriptor = std::move(descriptor),
            .seed = invocation.seed,
            .iteration = invocation.iteration,
            .attempt =
                AttemptIndex{
                    .runIteration = invocation.iteration,
                    .sample = invocation.sample,
                    .retry = invocation.retry,
                },
            .warmup = invocation.warmup,
        },
  wallStarted(invocation.captureTiming == CapturePolicy::PerAttempt ? std::chrono::steady_clock::now()
                                       : std::chrono::steady_clock::time_point{}),
  memoryBefore(invocation.captureMemory ? memory::processMemory() : None),
  environment{},
  runLoop{timeMode, environment.stopToken()},
  captureProfile(invocation.captureProfile) {
  }

  auto prepare(const InvocationSettings &invocation) -> void {
    if (execution.descriptor.name.empty())
      execution.descriptor.name = execution.descriptor.identifier;

    if (execution.descriptor.policy.trace or invocation.forceTrace) {
      environment.enableTrace();
      environment.recordTrace(std::format("enabled tracing for: {} ...", execution.descriptor.name));
    }

    if (execution.descriptor.policy.timeout)
      deadline.emplace(runLoop.now() + *execution.descriptor.policy.timeout);
  }
};

[[nodiscard]] auto makeContext(ActiveExecution &active, const InvocationSettings &invocation) -> Context {
  return Context{
      .name = active.execution.descriptor.name,
      .description = active.execution.descriptor.description,
      .testCase = active.execution.descriptor.testCase,
      .resources = active.environment.resources(),
      .seed = active.execution.seed,
      .iteration = active.execution.iteration,
      .sample = invocation.sample,
      .retry = invocation.retry,
      .warmup = invocation.warmup,
      .stopToken = active.environment.stopToken(),
      .location = active.execution.descriptor.location,
  };
}

template <class Function>
auto invokeBody(ActiveExecution &active, const Context &context, Function &&function) -> void {
  using Return = decltype(invokeTest(std::forward<Function>(function), context));
  using ReturnType = std::remove_cvref_t<Return>;
  NormalizationContext normalization{
      .environment = active.environment,
      .context = context,
      .location = active.execution.descriptor.location,
      .runLoop = active.runLoop,
      .deadline = active.deadline,
      .captureProfile = active.captureProfile,
  };

  if constexpr (std::same_as<Return, void>) {
    invokeTest(std::forward<Function>(function), context);
  } else {
    if constexpr (is_task_return_v<ReturnType>)
      static_assert(not std::is_lvalue_reference_v<Return>,
          "Switch asynchronous test functions must return Task<T> by value.");
    normalizeReturn(invokeTest(std::forward<Function>(function), context), normalization);
  }
}

auto recordException(ActiveExecution &active, const TestPanic &exception) -> void {
  active.environment.recordError(panickedDiagnostic(exception.what(), exception.location()));
}

auto recordException(ActiveExecution &active, const std::exception &exception) -> void {
  const char *message = exception.what();
  active.environment.recordError(unhandledExceptionDiagnostic(
      message != nullptr ? message : "standard exception", active.execution.descriptor.location));
}

auto recordException(ActiveExecution &active) -> void {
  active.environment.recordError(
      unhandledExceptionDiagnostic("non-standard exception", active.execution.descriptor.location));
}

template <class Function>
auto invokeBodySafely(ActiveExecution &active, const Context &context, Function &&function) -> void {
  try {
    invokeBody(active, context, std::forward<Function>(function));
  } catch (const TestAbort &) { // NOLINT(bugprone-empty-catch)
    // require() has already recorded the fatal assertion and marked this attempt as aborted.
  } catch (const TestPanic &exception) {
    recordException(active, exception);
  } catch (const std::exception &exception) {
    recordException(active, exception);
  } catch (...) {
    recordException(active);
  }
}

[[nodiscard]] auto completeExecution(ActiveExecution &active, const InvocationSettings &invocation)
    -> TestExecution {
  const auto elapsed = active.runLoop.elapsed();
  active.execution.duration = invocation.captureTiming == CapturePolicy::PerAttempt
                                  ? active.bodyDuration
                                  : std::chrono::steady_clock::duration{};
  active.execution.wallDuration = invocation.captureTiming == CapturePolicy::PerAttempt
                                      ? active.bodyWallDuration
                                      : std::chrono::steady_clock::duration{};
  if (invocation.captureProfile)
    active.execution.profile = active.environment.profileSnapshot();
  detail::applyPolicy(detail::PolicyApplication{
      .policy = active.execution.descriptor.policy,
      .environment = active.environment,
      .elapsed = elapsed,
      .retry = invocation.retry != 0,
      .cancelled = active.canceled,
      .timeoutTriggered = active.runLoop.timeoutTriggered(),
      .location = active.execution.descriptor.location,
  });
  active.environment.finalize(active.execution.descriptor.location);
  active.execution.resources = active.environment.resourceSnapshot();
  active.execution.memoryBefore = active.memoryBefore;
  active.execution.memoryAfter = invocation.captureMemory ? memory::processMemory() : None;
  active.execution.state = std::move(active.environment).takeState();
  return std::move(active.execution);
}

[[nodiscard]] auto completeAttemptOutcome(ActiveExecution &active, const InvocationSettings &invocation)
    -> AttemptOutcome {
  const auto elapsed = active.runLoop.elapsed();
  active.execution.duration = invocation.captureTiming == CapturePolicy::PerAttempt
                                  ? active.bodyDuration
                                  : std::chrono::steady_clock::duration{};
  active.execution.wallDuration = invocation.captureTiming == CapturePolicy::PerAttempt
                                      ? active.bodyWallDuration
                                      : std::chrono::steady_clock::duration{};
  if (invocation.captureProfile)
    active.execution.profile = active.environment.profileSnapshot();
  detail::applyPolicy(PolicyApplication{
      .policy = active.execution.descriptor.policy,
      .environment = active.environment,
      .elapsed = elapsed,
      .retry = invocation.retry != 0,
      .cancelled = active.canceled,
      .timeoutTriggered = active.runLoop.timeoutTriggered(),
      .location = active.execution.descriptor.location,
  });
  active.environment.finalize(active.execution.descriptor.location);
  active.execution.resources = active.environment.resourceSnapshot();
  active.execution.memoryBefore = active.memoryBefore;
  active.execution.memoryAfter = invocation.captureMemory ? memory::processMemory() : None;
  active.execution.state = std::move(active.environment).takeState();

  AttemptOutcome outcome{
      .identifier = active.execution.descriptor.identifier,
      .attempt = active.execution.attempt,
      .duration = active.execution.duration,
      .wallDuration = active.execution.wallDuration,
      .assertions = active.execution.state.assertions,
      .failedAssertions = active.execution.state.failedAssertions,
      .errors = active.execution.state.errors,
      .seed = active.execution.seed,
      .iteration = active.execution.iteration,
      .warmup = active.execution.warmup,
      .passed = active.execution.passed(),
  };
  if (not outcome.passed) {
    outcome.timeout = std::ranges::any_of(
        active.execution.state.diagnostics, [](const Diagnostic &diagnostic) constexpr noexcept -> bool {
          return diagnostic.header.code == DiagnosticCode::TimeoutExceeded;
        });
    outcome.failure = std::move(active.execution);
  }
  return outcome;
}

} // namespace detail

/// Executes one test in a dynamically bound TestEnvironment.
///
/// A detail::TestAbort records a fatal requirement failure but is not itself an error.
/// Other exceptions are converted into UnhandledException diagnostics. Coroutine timeouts request
/// Context::stopToken at the next queued resume. TimeMode::Virtual advances only Switch scheduler time.
template <detail::TestInvocable Function>
[[nodiscard]]
auto run(TestDescriptor descriptor, Function &&function, TimeMode timeMode = TimeMode::Real)
    -> TestExecution {
  const detail::InvocationSettings invocation = detail::currentInvocationSettings();
  detail::ActiveExecution active{std::move(descriptor), invocation, timeMode};
  const std::source_location location = active.execution.descriptor.location;
  const auto finalizeEnvironment =
      std::scope_exit([&active, location] -> void { active.environment.finalize(location); });
  active.prepare(invocation);
  const Context context = detail::makeContext(active, invocation);

  {
    EnvironmentBinding environmentBinding{active.environment};
    ContextBinding contextBinding{context};
    const auto bodyStarted = invocation.captureTiming == CapturePolicy::PerAttempt
                                 ? active.runLoop.elapsed()
                                 : std::chrono::steady_clock::duration{};
    const auto bodyWallStarted = invocation.captureTiming == CapturePolicy::PerAttempt
                                     ? std::chrono::steady_clock::now()
                                     : std::chrono::steady_clock::time_point{};
    if (invocation.captureProfile) {
      auto testProfile = profiling::profileScope(active.environment.profileSink(),
          active.execution.descriptor.name,
          active.execution.descriptor.location);
      detail::invokeBodySafely(active, context, std::forward<Function>(function));
    } else {
      detail::invokeBodySafely(active, context, std::forward<Function>(function));
    }
    if (invocation.captureTiming == CapturePolicy::PerAttempt) {
      active.bodyDuration = active.runLoop.elapsed() - bodyStarted;
      active.bodyWallDuration = std::chrono::steady_clock::now() - bodyWallStarted;
    }
    active.canceled = active.environment.stopRequested();
  }

  return detail::completeExecution(active, invocation);
}

template <detail::TestInvocable Function>
[[nodiscard]] auto runCompact(TestDescriptor descriptor,
    Function &&function,
    TimeMode timeMode = TimeMode::Real) -> AttemptOutcome {
  const detail::InvocationSettings invocation = detail::currentInvocationSettings();
  detail::ActiveExecution active{std::move(descriptor), invocation, timeMode};
  const std::source_location location = active.execution.descriptor.location;
  const auto finalizeEnvironment =
      std::scope_exit([&active, location] -> void { active.environment.finalize(location); });
  active.prepare(invocation);
  const Context context = detail::makeContext(active, invocation);

  {
    EnvironmentBinding environmentBinding{active.environment};
    ContextBinding contextBinding{context};
    const auto bodyStarted = invocation.captureTiming == CapturePolicy::PerAttempt
                                 ? active.runLoop.elapsed()
                                 : std::chrono::steady_clock::duration{};
    const auto bodyWallStarted = invocation.captureTiming == CapturePolicy::PerAttempt
                                     ? std::chrono::steady_clock::now()
                                     : std::chrono::steady_clock::time_point{};
    if (invocation.captureProfile) {
      auto testProfile = profiling::profileScope(active.environment.profileSink(),
          active.execution.descriptor.name,
          active.execution.descriptor.location);
      detail::invokeBodySafely(active, context, std::forward<Function>(function));
    } else {
      detail::invokeBodySafely(active, context, std::forward<Function>(function));
    }
    if (invocation.captureTiming == CapturePolicy::PerAttempt) {
      active.bodyDuration = active.runLoop.elapsed() - bodyStarted;
      active.bodyWallDuration = std::chrono::steady_clock::now() - bodyWallStarted;
    }
    active.canceled = active.environment.stopRequested();
  }

  return detail::completeAttemptOutcome(active, invocation);
}

template <detail::TestInvocable Function>
auto runBatch(TestDescriptor descriptor,
    Function &&function,
    TimeMode timeMode,
    usize count,
    BatchExecutionContext &batch) -> void {
  const detail::InvocationSettings base = detail::currentInvocationSettings();
  detail::InvocationSettings batchInvocation = base;
  batchInvocation.captureTiming = CapturePolicy::None;
  detail::ActiveExecution active{std::move(descriptor), batchInvocation, timeMode};
  active.prepare(batchInvocation);
  constexpr usize quantileReservoirSize{1024};
  batch.quantileSamples.reserve(quantileReservoirSize);
  const auto started = std::chrono::steady_clock::now();
  std::ranges::for_each(std::views::indices(count), [&](usize index) -> void {
    if (batch.failed())
      return;
    detail::InvocationSettings invocation = base;
    invocation.iteration = base.iteration + index;
    invocation.sample = index;
    active.execution.iteration = invocation.iteration;
    active.execution.attempt = AttemptIndex{.runIteration = invocation.iteration, .sample = index};
    const Context context = detail::makeContext(active, invocation);
    const usize failedBefore = active.environment.state().failedAssertions;
    const usize errorsBefore = active.environment.state().errors;
    {
      EnvironmentBinding environmentBinding{active.environment};
      ContextBinding contextBinding{context};
      const auto attemptStarted = invocation.captureTiming == CapturePolicy::PerAttempt
                                      ? std::chrono::steady_clock::now()
                                      : std::chrono::steady_clock::time_point{};
      detail::invokeBodySafely(active, context, function);
      if (invocation.captureTiming == CapturePolicy::PerAttempt) {
        const auto duration = std::chrono::steady_clock::now() - attemptStarted;
        if (batch.quantileSamples.size() < quantileReservoirSize)
          batch.quantileSamples.push_back(duration);
        else
          batch.quantileSamples[batch.timingSamples % quantileReservoirSize] = duration;
        ++batch.timingSamples;
        if (batch.timingSamples == 1) {
          batch.minimumDuration = duration;
          batch.maximumDuration = duration;
        } else {
          batch.minimumDuration = std::min(batch.minimumDuration, duration);
          batch.maximumDuration = std::max(batch.maximumDuration, duration);
        }
        const long double sample = static_cast<long double>(duration.count());
        const long double delta = sample - batch.meanDuration;
        batch.meanDuration += delta / static_cast<long double>(batch.timingSamples);
        batch.variableAccumulator += delta * (sample - batch.meanDuration);
      }
    }
    ++batch.completed;
    if (active.environment.state().failedAssertions == failedBefore and
        active.environment.state().errors == errorsBefore and not active.environment.state().aborted) {
      ++batch.passed;
    } else {
      batch.firstFailureAttempt = AttemptIndex{
          .runIteration = invocation.iteration,
          .sample = index,
      };
      // A marker stops the loop; the detailed execution is materialized once after aggregate timing closes.
      batch.firstFailure.emplace();
    }
  });
  if (batch.timingSamples != 0) {
    std::ranges::sort(batch.quantileSamples);
    const auto approximateQuantile = [&batch](long double probability) {
      const long double index = probability * static_cast<long double>(batch.quantileSamples.size() - 1);
      const usize lower = static_cast<usize>(index);
      const usize upper = std::min(lower + 1, batch.quantileSamples.size() - 1);
      const long double fraction = index - static_cast<long double>(lower);
      const long double left = static_cast<long double>(batch.quantileSamples[lower].count());
      const long double right = static_cast<long double>(batch.quantileSamples[upper].count());
      return std::chrono::steady_clock::duration{
          static_cast<std::chrono::steady_clock::duration::rep>(left + fraction * (right - left))};
    };
    const auto estimatedMedian =
        std::clamp(approximateQuantile(0.50L), batch.minimumDuration, batch.maximumDuration);
    batch.firstQuartile = std::clamp(approximateQuantile(0.25L), batch.minimumDuration, estimatedMedian);
    batch.median = estimatedMedian;
    batch.thirdQuartile = std::clamp(approximateQuantile(0.75L), estimatedMedian, batch.maximumDuration);
    batch.quantilesAvailable = true;
    batch.quantilesApproximate = true;
  }
  batch.wallDuration = std::chrono::steady_clock::now() - started;
  batch.duration = batch.wallDuration;
  TestExecution completed = detail::completeExecution(active, batchInvocation);
  batch.assertions = completed.state.assertions;
  batch.failedAssertions = completed.state.failedAssertions;
  batch.errors = completed.state.errors;
  if (batch.failed() or completed.failed()) {
    if (not batch.firstFailureAttempt)
      batch.firstFailureAttempt = completed.attempt;
    completed.attempt = *batch.firstFailureAttempt;
    completed.iteration = batch.firstFailureAttempt->runIteration;
    batch.firstFailure = std::move(completed);
    batch.passed = batch.completed - 1;
  }
}

template <detail::TestInvocable Function>
[[nodiscard]] auto run(StringView identifier,
    Function &&function,
    std::source_location location = std::source_location::current()) -> TestExecution {
  return run(
      TestDescriptor{
          .identifier = String{identifier},
          .location = location,
      },
      std::forward<Function>(function));
}

template <detail::TestInvocable Function>
[[nodiscard]] auto run(StringView identifier,
    Function &&function,
    TimeMode timeMode,
    std::source_location location = std::source_location::current()) -> TestExecution {
  return run(
      TestDescriptor{
          .identifier = String{identifier},
          .location = location,
      },
      std::forward<Function>(function),
      timeMode);
}

} // namespace Switch
