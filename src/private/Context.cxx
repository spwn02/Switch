module Switch;

import :Context;

import std;
import Miracle;

using namespace Miracle;

namespace Switch {

namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables, readability-identifier-naming)
thread_local const Context *currentContext_{};

thread_local const detail::InvocationSettings *currentInvocationSettings_{};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables, readability-identifier-naming)

} // namespace

ContextBinding::ContextBinding(const Context &context) noexcept
    : previous_(currentContext_) {
  currentContext_ = std::addressof(context);
}

ContextBinding::~ContextBinding() noexcept {
  currentContext_ = previous_;
}

auto currentContext() noexcept -> Option<Ref<const Context>> {
  if (currentContext_ == nullptr)
    return None;

  return std::cref(*currentContext_);
}

detail::InvocationBinding::InvocationBinding(const InvocationSettings &settings) noexcept
    : previous_(currentInvocationSettings_) {
  currentInvocationSettings_ = std::addressof(settings);
}

detail::InvocationBinding::~InvocationBinding() noexcept {
  currentInvocationSettings_ = previous_;
}

auto detail::currentInvocationSettings() noexcept -> InvocationSettings {
  if (currentInvocationSettings_ == nullptr)
    return {};

  return *currentInvocationSettings_;
}

} // namespace Switch
