import std;
import Miracle;
import Switch;

using namespace Miracle;
using namespace Switch;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::example {

[[ = test, = group("example") ]] auto foo(const Context[[= context]] & ctx) -> void {
  Array<usize, 10> array = ctx.resources.allocate<Array<usize, 10>>();
  std::ranges::iota(array, 1);
  check(contains(array, 10));
  const f32 average = std::ranges::fold_left(array, 0.0F, std::plus<>{}) / std::ranges::distance(array);
  check(near(average, 5, 0.5));
}

[[nodiscard]] auto divide(i32 num, i32 den) noexcept -> Result<i32> {
  if (den == 0)
    return bail(Error{"Cannot divide by zero!"});

  return num / den;
}

[[ = test, = group("example"), = tag("math"), = Case{5, 2} ]] auto dividePass(i32 num, i32 den) noexcept
    -> Result<i32> {
  return divide(num, den);
}

[[ = test,
  = group("example"),
  = tag("math"),
  = Case{5, 0},
  = shouldPanic("Cannot divide by zero!") ]] auto divideFail(i32 num, i32 den) noexcept -> Result<i32> {
  return divide(num, den);
}

} // namespace Tests::example
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::example>();
}
