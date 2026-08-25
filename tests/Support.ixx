export module SwitchTests.Support;

import std;

export namespace Tests::support {

template <class Function>
class ScopeExit final {
public:
  constexpr explicit ScopeExit(Function function) noexcept(std::is_nothrow_move_constructible_v<Function>)
      : function_(std::move(function)) {
  }

  ~ScopeExit() noexcept(noexcept(std::invoke(function_))) {
    std::invoke(function_);
  }

  ScopeExit(const ScopeExit &) = delete;
  auto operator=(const ScopeExit &) -> ScopeExit & = delete;
  ScopeExit(ScopeExit &&) = delete;
  auto operator=(ScopeExit &&) -> ScopeExit & = delete;

private:
  [[no_unique_address]] Function function_;
};

template <class Function>
ScopeExit(Function) -> ScopeExit<Function>;

} // namespace Tests::support
