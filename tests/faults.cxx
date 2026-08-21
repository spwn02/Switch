import std;

import Miracle;
import Switch;

using namespace Miracle;
using namespace Switch;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::faults {

namespace FaultSubjects {

[[ = test, = group("framework"), = tag("faults", "subjects"), = isolated ]] auto aborts() -> void {
  usize *invalidPointer = reinterpret_cast<usize *>(0x1);
  std::println("{}", *invalidPointer);

  std::abort();
}

[[ = test, = group("framework"), = tag("faults", "subjects") ]] auto survives() -> void {
  require(true);
}

} // namespace FaultSubjects

namespace FilteredSubjects {

[[ = test, = group("framework"), = tag("faults", "subjects", "filtered") ]] auto passes() -> void {
  require(true);
}

} // namespace FilteredSubjects

[[nodiscard]] auto executionNamed(const Vec<TestExecution> &executions, StringView name)
    -> Option<Ref<const TestExecution>> {
  const auto execution = std::ranges::find_if(executions,
      [name](const TestExecution &candidate) -> bool { return candidate.descriptor.name == name; });
  if (execution == executions.end())
    return None;

  return std::cref(*execution);
}

[[nodiscard]] auto processIsolationUnavailable(const Vec<TestExecution> &executions) -> bool {
  return std::ranges::any_of(executions, [](const TestExecution &execution) -> bool {
    return execution.fault and execution.fault->kind == NativeFaultKind::IsolationUnavailable;
  });
}

[[ = test, = trace, = group("framework"), = tag("faults", "isolation"), = parent ]] auto
continuesUnrelatedCasesAfterANativeFault() -> void {
  const Vec<TestExecution> executions = runAllDetailed<^^FaultSubjects>(RunOptions{
      .isolation = CrashIsolation::ProcessPerCase,
  });

  require(not processIsolationUnavailable(executions));

  require(executions.size() == 2_exp);
  const Option<Ref<const TestExecution>> crashed = executionNamed(executions, "aborts");
  const Option<Ref<const TestExecution>> survived = executionNamed(executions, "survives");
  require(crashed);
  require(survived);

  require(crashed->get().failed());
  require(crashed->get().fault);
  require(crashed->get().fault->kind != NativeFaultKind::IsolationUnavailable);
  require(crashed->get().fault->address != 0);
  check(crashed->get().fault->instruction != 0);
  require(crashed->get().state.errors == 1_exp);
  check(crashed->get().state.diagnostics.front().header.code == DiagnosticCode::NativeFault);
  check(survived->get().passed());
}

[[ = test, = trace, = group("framework"), = tag("faults", "isolation", "failfast"), = parent ]] auto
stopsAfterANativeFaultWhenFailFastIsEnabled() -> void {
  const Vec<TestExecution> executions = runAllDetailed<^^FaultSubjects>(RunOptions{
      .failFast = true,
      .isolation = CrashIsolation::ProcessPerCase,
  });

  require(not processIsolationUnavailable(executions));

  require(executions.size() == 1_exp);
  require(executions.front().failed());
  require(executions.front().fault);
  require(executions.front().fault->kind == NativeFaultKind::Signal);
  check(executions.front().fault->signal == NativeSignal::SegmentationFault);
  check(eq(executions.front().fault->code, 139));
  require(executions.front().fault->address != 0);
  check(executions.front().fault->instruction != 0);

  std::ostringstream output{};
  RunAccumulator accumulator{RetentionPolicy::All};
  for (const TestExecution &execution : executions)
    accumulator.append(execution);
  static_cast<void>(Reporter{}.report(std::move(accumulator).finish(), output));
  check(output.str().contains("worker terminated with signal code 139 (segmentation fault)"));
  check(executions.front().state.diagnostics.front().header.code == DiagnosticCode::NativeFault);
}

[[ = test, = group("framework"), = tag("faults", "isolation", "filtered"), = parent ]] auto
preservesFilteredPlanIdentityInAWorker() -> void {
  constexpr u64 seed{0xF17E2ED};
  const Vec<TestExecution> isolated = runAllDetailed<^^FilteredSubjects>(RunOptions{
      .seed = seed,
      .isolation = CrashIsolation::ProcessPerCase,
  });

  require(not processIsolationUnavailable(isolated));

  const Vec<TestExecution> inProcess = runAllDetailed<^^FilteredSubjects>(RunOptions{
      .seed = seed,
      .isolation = CrashIsolation::InProcess,
  });

  require(isolated.size() == 1_exp);
  require(inProcess.size() == 1_exp);
  require(isolated.front().passed());
  check(isolated.front().descriptor.identifier == inProcess.front().descriptor.identifier);
  check(isolated.front().seed == inProcess.front().seed);
}

} // namespace Tests::faults
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::faults>();
}
