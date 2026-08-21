import std;
import Miracle; // For type aliases. Not required
import Switch;

using namespace Miracle;
using namespace Switch;

namespace Tests {

[[= test]] auto arithmetic() -> void {
  check(2U + 2U == 4_exp);
}

auto fibonacci(u64 number) -> u64 {
  return number < 2 ? number : fibonacci(number - 1) + fibonacci(number - 2);
}

[[= test]] auto fibonacciTests() {
  check(fibonacci(1) == 1_exp);
  check(fibonacci(5) == 5_exp);
}

}

consteval {
  discover<^^Tests>();
}

auto main() -> int {
  Reporter reporter{};
  RunOptions options{
      .threads = 1,
      .isolation = CrashIsolation::InProcess,
  };

  const RunReport report = runAll(reporter, std::cout, TestSelection{}, options);
  return report.passed() ? 0 : 1;
}
