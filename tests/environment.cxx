import std;

import Miracle;
import Switch;

using namespace Miracle;
using namespace Switch;

namespace Tests::environment {

auto boundTo(TestEnvironment &expected) -> bool {
  const Option<Ref<TestEnvironment>> current = currentEnvironment();
  return current and std::addressof(current->get()) == std::addressof(expected);
}

[[ = test, = group("framework"), = tag("environment") ]] auto bindingRestoresPreviousEnvironment() -> void {
  TestEnvironment outer{};
  TestEnvironment inner{};
  bool innerBound{};
  bool outerRestored{};

  {
    EnvironmentBinding outerBinding{outer};
    {
      EnvironmentBinding innerBinding{inner};
      innerBound = boundTo(inner);
    }
    outerRestored = boundTo(outer);
  }

  check(innerBound);
  check(outerRestored);
  check(currentEnvironment());
}

} // namespace Tests::environment

consteval {
  discover<^^Tests::environment>();
}
