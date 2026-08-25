module Switch;

import std;
import Miracle;

using namespace Miracle;

namespace Switch {

namespace {

// NOLINTNEXTLINE
thread_local TestEnvironment *currentEnvironment_{};

} // namespace

auto TestState::passed() const noexcept -> bool {
  return not aborted and failedAssertions == 0 and errors == 0;
}

auto TestState::failed() const noexcept -> bool {
  return not passed();
}

auto TestEnvironment::state() const noexcept -> const TestState & {
  return state_;
}

auto TestEnvironment::recordPass() noexcept -> void {
  ++state_.assertions;
}

auto TestEnvironment::recordFailure(Diagnostic diagnostic) -> void {
  state_.diagnostics.push_back(std::move(diagnostic));
  ++state_.assertions;
  ++state_.failedAssertions;
}

auto TestEnvironment::recordError(Diagnostic diagnostic) -> void {
  state_.diagnostics.push_back(std::move(diagnostic));
  ++state_.errors;
}

auto TestEnvironment::enableTrace() noexcept -> void {
  traceEnabled_ = true;
}

auto TestEnvironment::recordTrace(String message, std::source_location location) -> void {
  if (not traceEnabled_)
    return;

  state_.traces.push_back(TraceEvent{
      .message = std::move(message),
      .location = location,
  });
}

auto TestEnvironment::acceptExpectedPanic(usize diagnosticIndex) noexcept -> void {
  if (diagnosticIndex >= state_.diagnostics.size())
    return;

  const auto offset = static_cast<Vec<Diagnostic>::difference_type>(diagnosticIndex);
  state_.diagnostics.erase(state_.diagnostics.begin() + offset);
  if (state_.errors != 0)
    --state_.errors;
}

auto TestEnvironment::abort() noexcept -> void {
  state_.aborted = true;
}

auto TestEnvironment::stopToken() const noexcept -> std::stop_token {
  return stopSource_.get_token();
}

auto TestEnvironment::requestStop() noexcept -> void {
  static_cast<void>(stopSource_.request_stop());
}

auto TestEnvironment::stopRequested() const noexcept -> bool {
  return stopSource_.stop_requested();
}

auto TestEnvironment::resources() noexcept -> TestResources & {
  return resources_;
}

auto TestEnvironment::profileSink() noexcept -> profiling::ProfileSink & {
  return profile_;
}

auto TestEnvironment::finalize(std::source_location location) -> void {
  if (finalized_)
    return;

  const ResourceCleanupReport cleanup = resources_.cleanup();
  std::ranges::for_each(cleanup.failures, [this, location](const ResourceCleanupFailure &failure) -> void {
    Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::ResourceCleanupFailed, failure.location);
    diagnostic.addNote(std::format("{}: {}", failure.name, failure.message));
    diagnostic.addNote(std::format("test cleanup boundary: {}: {}", location.file_name(), location.line()),
        DiagnosticLevel::Help);
    recordError(std::move(diagnostic));
  });
  finalized_ = true;
}

auto TestEnvironment::resourceSnapshot() const noexcept -> ResourceSnapshot {
  return resources_.snapshot();
}

auto TestEnvironment::profileSnapshot() const noexcept -> profiling::ProfileSnapshot {
  return profile_.snapshot();
}

auto TestEnvironment::takeState() && noexcept -> TestState {
  return std::move(state_);
}

EnvironmentBinding::EnvironmentBinding(TestEnvironment &environment) noexcept
    : previous_(currentEnvironment_) {
  currentEnvironment_ = std::addressof(environment);
}

EnvironmentBinding::~EnvironmentBinding() noexcept {
  currentEnvironment_ = previous_;
}

auto currentEnvironment() noexcept -> Option<Ref<TestEnvironment>> {
  if (currentEnvironment_ == nullptr)
    return None;

  return std::ref(*currentEnvironment_);
}

auto currentResources() noexcept -> Option<Ref<TestResources>> {
  if (currentEnvironment_ == nullptr)
    return None;

  return std::ref(currentEnvironment_->resources());
}

auto traceEvent(StringView message, std::source_location location) -> void {
  const Option<Ref<TestEnvironment>> environment = currentEnvironment();
  if (not environment)
    return;

  environment->get().recordTrace(String{message}, location);
}

} // namespace Switch
