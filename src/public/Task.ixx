export module Switch:Task;

import std;
import Miracle;

using namespace Miracle;

// NOLINTBEGIN(readability-identifier-naming)
export namespace Switch {

template <class Value = void>
class Task;

/// Selects the source of time used by one Switch task run loop.
///
/// Virtual time advances only when the loop reaches its next timer or external deadline. It therefore makes
/// Switch awaitables deterministic without changing direct OS wait semantics.
enum class[[= debug::derive]] TimeMode : u8 {
  Real,
  Virtual,
};

namespace detail {

/// Describes a broken asynchronous lifecycle observed by the test scheduler.
enum class[[= debug::derive]] TaskLifecycleFailure : u8 {
  Empty,
  Stranded,
  PendingWork,
};

/// Describes the terminal state returned by one scheduler drive operation.
enum class[[= debug::derive]] TaskDriveStatus : u8 {
  Completed,
  Cancelled,
  Stranded,
  PendingWork,
  Empty,
};

/// Carries explicit scheduler status without using exceptions for framework control flow.
struct TaskDriveResult final {
  TaskDriveStatus status{TaskDriveStatus::Empty};
  usize pendingWork{};
};

/// Describes a lifecycle diagnostic after an asynchronous test can no longer make safe progress.
class TaskLifecycleError final {
public:
  constexpr explicit TaskLifecycleError(TaskLifecycleFailure failure, usize pendingWork = 0) noexcept
      : failure_(failure)
      , pendingWork_(pendingWork) {
  }

  [[nodiscard]] constexpr auto failure() const noexcept -> TaskLifecycleFailure {
    return failure_;
  }

  [[nodiscard]] constexpr auto pendingWork() const noexcept -> usize {
    return pendingWork_;
  }

private:
  TaskLifecycleFailure failure_{};
  usize pendingWork_{};
};

class RunLoop final {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  enum class[[= debug::derive]] WaitResult : u8 {
    NoWork,
    TimerReady,
    ExternalWake,
  };

  explicit RunLoop(TimeMode timeMode = TimeMode::Real, std::stop_token stopToken = {}) noexcept;

  [[nodiscard]] auto now() const noexcept -> TimePoint;

  [[nodiscard]] auto elapsed() const noexcept -> Clock::duration;

  auto enqueue(std::coroutine_handle<> handle) -> void;

  auto scheduleAt(std::coroutine_handle<> handle, TimePoint wakeTime) -> void;

  [[nodiscard]] auto dequeue() -> Option<std::coroutine_handle<>>;

  /// Waits until a timer is ready or an external deadline is reached.
  [[nodiscard]] auto waitForWork(Option<TimePoint> externalWake) -> WaitResult;

  /// Makes every locally sleeping coroutine eligible to observe cancellation.
  auto wakeAllTimers() -> void;

  /// Discards non-owning scheduler references before their coroutine owners are destroyed.
  auto discardPending() noexcept -> void;

  /// Counts live coroutine frames with an outstanding scheduler ticket.
  [[nodiscard]] auto pendingWorkCount() const noexcept -> usize;

  [[nodiscard]] auto stopRequested() const noexcept -> bool;

  [[nodiscard]] auto timeoutTriggered() const noexcept -> bool;

private:
  struct Timer final {
    TimePoint wakeTime;
    usize sequence{};
    std::coroutine_handle<> handle;
  };

  struct TimerCompare final {
    [[nodiscard]] auto operator()(const Timer &left, const Timer &right) const noexcept -> bool {
      if (left.wakeTime != right.wakeTime)
        return left.wakeTime > right.wakeTime;

      return left.sequence > right.sequence;
    }
  };

  auto advanceTo(TimePoint time) noexcept -> void;

  auto promoteDueTimers() -> void;

  [[nodiscard]] auto nextTimerWake() const -> Option<TimePoint>;

  std::deque<std::coroutine_handle<>> ready_;
  std::priority_queue<Timer, Vec<Timer>, TimerCompare> timers_;
  /// A coroutine may own at most one ready or timer ticket at a time.
  FlatSet<void *> scheduled_;
  TimeMode timeMode_{};
  TimePoint startedAt_;
  TimePoint currentTime_;
  usize nextTimerSequence_{};
  std::stop_token stopToken_;
  bool timeoutTriggered_{};
};

/// Dynamically binds the deterministic coroutine queue while a task resumes. It is analogous to
/// EnvironmentBinding, but owns scheduling state rather than test state.
class RunLoopBinding final {
public:
  explicit RunLoopBinding(RunLoop &runLoop) noexcept;
  ~RunLoopBinding() noexcept;

  RunLoopBinding(const RunLoopBinding &) = delete (
      "RunLoopBinding owns a pointer to a previous thread-local RunLoop.");
  auto operator=(const RunLoopBinding &)
      -> RunLoopBinding & = delete ("RunLoopBinding owns a pointer to a previous thread-local RunLoop.");
  RunLoopBinding(RunLoopBinding &&) noexcept = delete (
      "RunLoopBinding owns a pointer to a previous thread-local RunLoop.");
  auto operator=(RunLoopBinding &&) noexcept
      -> RunLoopBinding & = delete ("RunLoopBinding owns a pointer to a previous thread-local RunLoop.");

private:
  RunLoop *previous_{};
};

[[nodiscard]] auto currentRunLoop() noexcept -> RunLoop *;

auto schedule(std::coroutine_handle<> handle) -> void;

auto scheduleAfter(std::coroutine_handle<> handle, RunLoop::Clock::duration duration) -> void;

[[nodiscard]] auto stopRequested() noexcept -> bool;

/// Stores the lifecycle state shared by every Switch task promise.
///
/// The promise owns the exception and continuation until the coroutine frame is destroyed. Result consumption
/// is deliberately one-shot so a completed task cannot be observed through two owners.
class TaskPromiseBase {
public:
  struct FinalAwaiter final {
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    [[nodiscard]] constexpr auto await_ready() const noexcept -> bool {
      return false;
    }

    template <class Promise>
    [[nodiscard]] auto await_suspend(std::coroutine_handle<Promise> handle) const noexcept
        -> std::coroutine_handle<> {
      const std::coroutine_handle<> continuation = handle.promise().continuation();
      return continuation ? continuation : std::noop_coroutine();
    }

    constexpr auto await_resume() const noexcept -> void {
    }
  };

  [[nodiscard]] static constexpr auto initial_suspend() noexcept -> std::suspend_always {
    return {};
  }

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  [[nodiscard]] constexpr auto final_suspend() noexcept -> FinalAwaiter {
    return {};
  }

  auto unhandled_exception() noexcept -> void {
    exception_ = std::current_exception();
  }

  [[nodiscard]] auto hasContinuation() const noexcept -> bool {
    return static_cast<bool>(continuation_);
  }

  [[nodiscard]] auto continuation() const noexcept -> std::coroutine_handle<> {
    return continuation_;
  }

  auto setContinuation(std::coroutine_handle<> continuation) noexcept -> void {
    continuation_ = continuation;
  }

protected:
  auto consumeResult(StringView alreadyConsumedMessage,
      std::source_location location = std::source_location::current()) -> void {
    if (resultTaken_)
      fatal(alreadyConsumedMessage, {}, location);

    resultTaken_ = true;

    if (exception_)
      std::rethrow_exception(exception_);
  }

private:
  std::exception_ptr exception_;
  std::coroutine_handle<> continuation_;
  bool resultTaken_{};
};

/// Owns a coroutine handle and provides the move-only lifetime policy shared by Task specialization.
template <class Handle>
class TaskHandleStorage final {
public:
  TaskHandleStorage() = default;

  explicit TaskHandleStorage(Handle handle) noexcept
      : handle_(handle) {
  }

  ~TaskHandleStorage() noexcept {
    reset();
  }

  TaskHandleStorage(const TaskHandleStorage &) = delete ("TaskHandleStorage owns a coroutine handle.");
  auto operator=(const TaskHandleStorage &)
      -> TaskHandleStorage & = delete ("TaskHandleStorage owns a coroutine handle.");

  TaskHandleStorage(TaskHandleStorage &&other) noexcept
      : handle_(std::exchange(other.handle_, {})) {
  }

  auto operator=(TaskHandleStorage &&other) noexcept -> TaskHandleStorage & {
    if (this == std::addressof(other))
      return *this;

    reset();
    handle_ = std::exchange(other.handle_, {});
    return *this;
  }

  [[nodiscard]] auto handle() const noexcept -> Handle {
    return handle_;
  }

  [[nodiscard]] auto release() noexcept -> Handle {
    return std::exchange(handle_, {});
  }

private:
  auto reset() noexcept -> void {
    if (handle_)
      handle_.destroy();

    handle_ = {};
  }

  Handle handle_{};
};

/// Provides one awaiter implementation for value and void tasks.
template <class Handle, class Value>
class TaskAwaiter final {
public:
  TaskAwaiter(Handle handle, bool ownsHandle) noexcept
      : handle_(handle)
      , ownsHandle_(ownsHandle) {
  }

  ~TaskAwaiter() noexcept {
    if (ownsHandle_ and handle_)
      handle_.destroy();
  }

  TaskAwaiter(const TaskAwaiter &) = delete (
      "TaskAwaiter may own a value, which is destroyed in its destructor.");
  auto operator=(const TaskAwaiter &)
      -> TaskAwaiter & = delete ("TaskAwaiter may own a value, which is destroyed in its destructor.");

  TaskAwaiter(TaskAwaiter &&other) noexcept
      : handle_(std::exchange(other.handle_, {}))
      , ownsHandle_(std::exchange(other.ownsHandle_, false)) {
  }

  auto operator=(TaskAwaiter &&other) noexcept -> TaskAwaiter & {
    if (this == std::addressof(other))
      return *this;

    if (ownsHandle_ and handle_)
      handle_.destroy();

    handle_ = std::exchange(other.handle_, {});
    ownsHandle_ = std::exchange(other.ownsHandle_, false);
    return *this;
  }

  [[nodiscard]] auto await_ready() const noexcept -> bool {
    return handle_ and handle_.done();
  }

  auto await_suspend(std::coroutine_handle<> continuation) -> bool {
    if (not handle_)
      fatal("Switch cannot await an empty Task<T>.");

    if (handle_.promise().hasContinuation())
      fatal("Switch Task<T> may be awaited only once.");

    handle_.promise().setContinuation(continuation);
    detail::schedule(handle_);
    return true;
  }

  [[nodiscard]] auto await_resume() -> Value {
    if constexpr (std::is_void_v<Value>) {
      handle_.promise().takeResult();
    } else {
      return handle_.promise().takeResult();
    }
  }

private:
  Handle handle_{};
  bool ownsHandle_{};
};

/// Drives one task until completion, cancellation, or a broken lifecycle is observed.
template <class Value, class Resume>
auto drive(Task<Value> &task, Resume &&resume) -> TaskDriveResult;

template <class Value, class Resume, class BeforeResume>
auto drive(Task<Value> &task, Resume &&resume, BeforeResume &&beforeResume, std::stop_token stopToken = {})
    -> TaskDriveResult;

template <class Value, class Resume, class BeforeResume, class NextWakeUp>
auto drive(Task<Value> &task,
    RunLoop &runLoop,
    Resume &&resume,
    BeforeResume &&beforeResume,
    NextWakeUp &&nextWakeUp) -> TaskDriveResult;

template <class Value, class Resume, class BeforeResume, class NextWakeUp>
auto drive(Task<Value> &task,
    Resume &&resume,
    BeforeResume &&beforeResume,
    NextWakeUp &&nextWakeUp,
    std::stop_token stopToken = {}) -> TaskDriveResult;

} // namespace detail

/// Cooperative suspension point for a Switch Task.
///
/// The active deterministic run loop re-enqueues the current coroutine. A future executor can replace the
/// queue without changing Task<T> call sites. After a timeout requests cancellation, a later yield() stops a
/// coroutine that did not finish during its cancellation-aware resume.
struct YieldAwaiter final {
  [[nodiscard]] constexpr auto await_ready() const noexcept -> bool { // NOLINT
    return false;
  }

  [[nodiscard]] auto await_suspend(std::coroutine_handle<> handle) const -> bool;

  constexpr auto await_resume() const noexcept -> void {
  }
};

[[nodiscard]] constexpr auto yield() noexcept -> YieldAwaiter {
  return {};
}

/// Cooperative timer awaiter for Switch Task.
///
/// It suspends only the current coroutine. The local test run loop waits for its deadline, and timeout
/// cancellation resumes it early so cleanup can observe Context::stopToken.
class SleepAwaiter final {
public:
  constexpr explicit SleepAwaiter(std::chrono::steady_clock::duration duration) noexcept
      : duration_(duration) {
  }

  [[nodiscard]] constexpr auto await_ready() const noexcept -> bool {
    return duration_ <= std::chrono::steady_clock::duration::zero();
  }

  [[nodiscard]] auto await_suspend(std::coroutine_handle<> handle) const -> bool;

  constexpr auto await_resume() const noexcept -> void {
  }

private:
  std::chrono::steady_clock::duration duration_;
};

template <class Rep, class Period>
[[nodiscard]] constexpr auto sleepFor(std::chrono::duration<Rep, Period> duration) noexcept -> SleepAwaiter {
  return SleepAwaiter{std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration)};
}

template <class Value>
class [[nodiscard]] Task final {
  static_assert(not std::is_reference_v<Value>, "Switch Task<T> cannot store a reference.");

public:
  class promise_type;
  using Handle = std::coroutine_handle<promise_type>;

  class promise_type final : public detail::TaskPromiseBase {
  public:
    using FinalAwaiter = detail::TaskPromiseBase::FinalAwaiter;

    [[nodiscard]] auto get_return_object() noexcept -> Task {
      return Task{Handle::from_promise(*this)};
    }

    template <class Return>
      requires std::constructible_from<Value, Return>
    auto return_value(Return &&value) -> void {
      value_.emplace(std::forward<Return>(value));
    }

    [[nodiscard]] auto takeResult() -> Value {
      consumeResult("Switch Task<T> result may be read only once.");

      if (not value_)
        fatal("Switch Task<T> completed without a value.");

      return std::move(*value_);
    }

  private:
    Option<Value> value_{};
  };

  using Awaiter = detail::TaskAwaiter<Handle, Value>;

  Task() = default;

  ~Task() noexcept = default;

  Task(const Task &) = delete ("Task<T> owns coroutine handle");
  auto operator=(const Task &) -> Task & = delete ("Task<T> owns coroutine handle");

  Task(Task &&) noexcept = default;
  auto operator=(Task &&) noexcept -> Task & = default;

  [[nodiscard]] auto valid() const noexcept -> bool {
    return static_cast<bool>(storage_.handle());
  }

  [[nodiscard]] auto done() const noexcept -> bool {
    const Handle handle = storage_.handle();
    return not handle or handle.done();
  }

  [[nodiscard]] auto takeResult() && -> Value {
    const Handle handle = storage_.handle();
    if (not handle or not handle.done())
      fatal("Switch cannot read an incomplete Task<T>.");

    return handle.promise().takeResult();
  }

  [[nodiscard]] auto operator co_await() & noexcept -> Awaiter {
    return Awaiter{storage_.handle(), false};
  }

  [[nodiscard]] auto operator co_await() && noexcept -> Awaiter {
    return Awaiter{storage_.release(), true};
  }

private:
  template <class OtherValue, class Resume, class BeforeResume, class NextWakeUp>
  friend auto detail::drive(Task<OtherValue> &task,
      detail::RunLoop &runLoop,
      Resume &&resume,
      BeforeResume &&beforeResume,
      NextWakeUp &&nextWakeUp) -> detail::TaskDriveResult;

  explicit Task(Handle handle) noexcept
      : storage_(handle) {
  }

  [[nodiscard]] auto handle() const noexcept -> Handle {
    return storage_.handle();
  }

  detail::TaskHandleStorage<Handle> storage_{};
};

template <>
class [[nodiscard]] Task<void> final {
public:
  class promise_type;
  using Handle = std::coroutine_handle<promise_type>;

  class promise_type final : public detail::TaskPromiseBase {
  public:
    using FinalAwaiter = detail::TaskPromiseBase::FinalAwaiter;

    [[nodiscard]] auto get_return_object() noexcept -> Task {
      return Task{Handle::from_promise(*this)};
    }

    auto return_void() -> void {
    }

    auto takeResult() -> void {
      consumeResult("Switch Task<T> result may be read only once.");
    }
  };

  using Awaiter = detail::TaskAwaiter<Handle, void>;

  Task() = default;

  ~Task() noexcept = default;

  Task(const Task &) = delete ("Task<T> owns coroutine handle");
  auto operator=(const Task &) -> Task & = delete ("Task<T> owns coroutine handle");

  Task(Task &&other) noexcept = default;
  auto operator=(Task &&other) noexcept -> Task & = default;

  [[nodiscard]] auto valid() const noexcept -> bool {
    return static_cast<bool>(storage_.handle());
  }

  [[nodiscard]] auto done() const noexcept -> bool {
    const Handle handle = storage_.handle();
    return not handle or handle.done();
  }

  auto takeResult() && -> void {
    const Handle handle = storage_.handle();
    if (not handle or not handle.done())
      fatal("Switch cannot read an incomplete Task<T>.");

    handle.promise().takeResult();
  }

  [[nodiscard]] auto operator co_await() & noexcept -> Awaiter {
    return Awaiter{storage_.handle(), false};
  }

  [[nodiscard]] auto operator co_await() && noexcept -> Awaiter {
    return Awaiter{storage_.release(), true};
  }

private:
  template <class OtherValue, class Resume, class BeforeResume, class NextWakeUp>
  friend auto detail::drive(Task<OtherValue> &task,
      detail::RunLoop &runLoop,
      Resume &&resume,
      BeforeResume &&beforeResume,
      NextWakeUp &&nextWakeUp) -> detail::TaskDriveResult;

  explicit Task(Handle handle) noexcept
      : storage_(handle) {
  }

  [[nodiscard]] auto handle() const noexcept -> Handle {
    return storage_.handle();
  }

  detail::TaskHandleStorage<Handle> storage_;
};

namespace detail {

template <class Value, class Resume, class BeforeResume, class NextWakeUp>
auto drive(Task<Value> &task,
    RunLoop &runLoop,
    Resume &&resume,
    BeforeResume &&beforeResume,
    NextWakeUp &&nextWakeUp) -> TaskDriveResult {
  if (not task.valid())
    return TaskDriveResult{.status = TaskDriveStatus::Empty};

  const auto discardPending = std::scope_exit([&runLoop] -> void { runLoop.discardPending(); });
  runLoop.enqueue(task.handle());

  while (not task.done()) {
    if (const Option<std::coroutine_handle<>> handle = runLoop.dequeue()) {
      RunLoopBinding binding{runLoop};
      std::invoke(std::forward<BeforeResume>(beforeResume));
      std::invoke(std::forward<Resume>(resume), *handle);
      continue;
    }

    const RunLoop::WaitResult waitResult =
        runLoop.waitForWork(std::invoke(std::forward<NextWakeUp>(nextWakeUp)));
    if (waitResult == RunLoop::WaitResult::NoWork)
      break;

    if (waitResult == RunLoop::WaitResult::ExternalWake) {
      RunLoopBinding binding{runLoop};
      std::invoke(std::forward<BeforeResume>(beforeResume));
      if (runLoop.stopRequested())
        runLoop.wakeAllTimers();
    }
  }

  if (not task.done() and runLoop.stopRequested())
    return TaskDriveResult{.status = TaskDriveStatus::Cancelled};

  if (not task.done())
    return TaskDriveResult{.status = TaskDriveStatus::Stranded};

  const usize pendingWork = runLoop.pendingWorkCount();
  if (pendingWork != 0)
    return TaskDriveResult{
        .status = TaskDriveStatus::PendingWork,
        .pendingWork = pendingWork,
    };

  return TaskDriveResult{.status = TaskDriveStatus::Completed};
}

template <class Value, class Resume, class BeforeResume, class NextWakeUp>
auto drive(Task<Value> &task,
    Resume &&resume,
    BeforeResume &&beforeResume,
    NextWakeUp &&nextWakeUp,
    std::stop_token stopToken) -> TaskDriveResult {
  RunLoop runLoop{TimeMode::Real, std::move(stopToken)};
  return drive(task,
      runLoop,
      std::forward<Resume>(resume),
      std::forward<BeforeResume>(beforeResume),
      std::forward<NextWakeUp>(nextWakeUp));
}

template <class Value, class Resume, class BeforeResume>
auto drive(Task<Value> &task, Resume &&resume, BeforeResume &&beforeResume, std::stop_token stopToken)
    -> TaskDriveResult {
  return drive(
      task,
      std::forward<Resume>(resume),
      std::forward<BeforeResume>(beforeResume),
      [] noexcept -> Option<RunLoop::TimePoint> { return None; },
      std::move(stopToken));
}

template <class Value, class Resume>
auto drive(Task<Value> &task, Resume &&resume) -> TaskDriveResult {
  return drive(task, std::forward<Resume>(resume), [] noexcept -> void {});
}

} // namespace detail

} // namespace Switch
// NOLINTEND(readability-identifier-naming)
