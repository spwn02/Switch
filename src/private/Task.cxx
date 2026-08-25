module Switch;

import std;
import Miracle;

using namespace Miracle;

namespace Switch {

namespace detail {

namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables, readability-identifier-naming)
thread_local RunLoop *currentRunLoop_{};

} // namespace

RunLoop::RunLoop(TimeMode timeMode, std::stop_token stopToken) noexcept
    : timeMode_(timeMode)
    , startedAt_(timeMode == TimeMode::Real ? Clock::now() : TimePoint{})
    , currentTime_(startedAt_)
    , stopToken_(std::move(stopToken)) {
}

auto RunLoop::now() const noexcept -> TimePoint {
  if (timeMode_ == TimeMode::Virtual)
    return currentTime_;

  return Clock::now();
}

auto RunLoop::elapsed() const noexcept -> Clock::duration {
  return now() - startedAt_;
}

auto RunLoop::enqueue(std::coroutine_handle<> handle) -> void {
  if (not handle)
    return;

  if (handle.done()) {
    scheduled_.erase(handle.address());
    return;
  }

  if (not scheduled_.insert(handle.address()).second)
    return;

  ready_.push_back(handle);
}

auto RunLoop::scheduleAt(std::coroutine_handle<> handle, TimePoint wakeTime) -> void {
  if (not handle)
    return;

  if (handle.done()) {
    scheduled_.erase(handle.address());
    return;
  }

  if (not scheduled_.insert(handle.address()).second)
    return;

  timers_.push(Timer{
      .wakeTime = wakeTime,
      .sequence = nextTimerSequence_++,
      .handle = handle,
  });
}

auto RunLoop::advanceTo(TimePoint time) noexcept -> void {
  if (timeMode_ == TimeMode::Virtual and time > currentTime_)
    currentTime_ = time;
}

auto RunLoop::promoteDueTimers() -> void {
  const TimePoint currentTime = now();
  while (not timers_.empty() and timers_.top().wakeTime <= currentTime) {
    const Timer timer = timers_.top();
    timers_.pop();

    if (not timer.handle or timer.handle.done()) {
      if (timer.handle)
        scheduled_.erase(timer.handle.address());

      continue;
    }

    ready_.push_back(timer.handle);
  }
}

auto RunLoop::nextTimerWake() const -> Option<TimePoint> {
  if (timers_.empty())
    return None;

  return timers_.top().wakeTime;
}

auto RunLoop::dequeue() -> Option<std::coroutine_handle<>> {
  promoteDueTimers();

  while (not ready_.empty()) {
    const std::coroutine_handle<> handle = ready_.front();
    ready_.pop_front();

    if (handle)
      scheduled_.erase(handle.address());

    if (handle and not handle.done())
      return handle;
  }

  return None;
}

auto RunLoop::waitForWork(Option<TimePoint> externalWake) -> WaitResult {
  promoteDueTimers();
  if (not ready_.empty())
    return WaitResult::TimerReady;

  const Option<TimePoint> timerWake = nextTimerWake();
  if (not timerWake and not externalWake)
    return WaitResult::NoWork;

  const bool externalFirst = externalWake and (not timerWake or *externalWake <= *timerWake);
  const TimePoint wakeTime = externalFirst ? *externalWake : *timerWake;
  if (timeMode_ == TimeMode::Virtual)
    advanceTo(wakeTime);
  else
    std::this_thread::sleep_until(wakeTime);

  promoteDueTimers();

  if (externalFirst and now() >= *externalWake) {
    timeoutTriggered_ = true;
    return WaitResult::ExternalWake;
  }

  return WaitResult::TimerReady;
}

auto RunLoop::wakeAllTimers() -> void {
  while (not timers_.empty()) {
    const Timer timer = timers_.top();
    timers_.pop();

    if (not timer.handle or timer.handle.done()) {
      if (timer.handle)
        scheduled_.erase(timer.handle.address());

      continue;
    }

    ready_.push_back(timer.handle);
  }
}

auto RunLoop::discardPending() noexcept -> void {
  ready_.clear();
  while (not timers_.empty())
    timers_.pop();

  scheduled_.clear();
}

auto RunLoop::pendingWorkCount() const noexcept -> usize {
  return scheduled_.size();
}

auto RunLoop::stopRequested() const noexcept -> bool {
  return stopToken_.stop_requested();
}

auto RunLoop::timeoutTriggered() const noexcept -> bool {
  return timeoutTriggered_;
}

RunLoopBinding::RunLoopBinding(RunLoop &runLoop) noexcept
    : previous_(currentRunLoop_) {
  currentRunLoop_ = std::addressof(runLoop);
}

RunLoopBinding::~RunLoopBinding() noexcept {
  currentRunLoop_ = previous_;
}

auto currentRunLoop() noexcept -> RunLoop * {
  return currentRunLoop_;
}

auto schedule(std::coroutine_handle<> handle) -> void {
  RunLoop *const runLoop = currentRunLoop();
  if (runLoop == nullptr)
    fatal("Switch Task suspension requires an active test run loop.");

  runLoop->enqueue(handle);
}

auto scheduleAfter(std::coroutine_handle<> handle, RunLoop::Clock::duration duration) -> void {
  RunLoop *const runLoop = currentRunLoop();
  if (runLoop == nullptr)
    fatal("Switch Task suspension requires an active test run loop.");

  runLoop->scheduleAt(handle, runLoop->now() + duration);
}

[[nodiscard]]
auto stopRequested() noexcept -> bool {
  const RunLoop *const runLoop = currentRunLoop();
  return runLoop != nullptr and runLoop->stopRequested();
}

} // namespace detail

auto YieldAwaiter::await_suspend(std::coroutine_handle<> handle) const -> bool { // NOLINT
  if (detail::stopRequested())
    return true;

  detail::schedule(handle);
  return true;
}

auto SleepAwaiter::await_suspend(std::coroutine_handle<> handle) const -> bool {
  if (detail::stopRequested())
    return true;

  detail::scheduleAfter(handle, duration_);
  return true;
}

} // namespace Switch
