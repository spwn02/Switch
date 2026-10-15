export module SwitchTests.ScopeExit;

import std;

export namespace SwitchTests {

template <class Function>
class ScopeExit final {
public:
  explicit ScopeExit(Function function) noexcept(std::is_nothrow_move_constructible_v<Function>)
      : function_(std::move(function)) {
  }

  ScopeExit(const ScopeExit &) = delete;
  auto operator=(const ScopeExit &) -> ScopeExit & = delete;
  ScopeExit(ScopeExit &&other) noexcept(std::is_nothrow_move_constructible_v<Function>)
      : function_(std::move(other.function_))
      , active_(std::exchange(other.active_, false)) {
  }
  auto operator=(ScopeExit &&) -> ScopeExit & = delete;

  constexpr ~ScopeExit() noexcept(noexcept(std::invoke(function_))) {
    if (active_)
      std::invoke(function_);
  }

  constexpr auto release() noexcept -> void {
    active_ = false;
  }

private:
  [[no_unique_address]] Function function_;
  bool active_{true};
};

template <class Function>
ScopeExit(Function) -> ScopeExit<Function>;

} // namespace SwitchTests
