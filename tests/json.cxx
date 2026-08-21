import std;

import Miracle;
import Switch;

using namespace Miracle;
using namespace Switch;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::json {

// NOLINTNEXTLINE(readability-function-size)
[[ = test, = group("framework"), = tag("json", "reporting") ]] auto writesCompleteMachineReports() -> void {
  const auto location = std::source_location::current();
  TestExecution passing = run(
      TestDescriptor{
          .identifier = "json\"passing\n\001",
          .description = "machine report",
          .policy =
              TestPolicy{
                  .trace = true,
                  .expectedPanic = String{"never"},
                  .timeout = std::chrono::milliseconds{5},
              },
          .metadata =
              TestMetadata{
                  .group = String{"reporting"},
                  .tags = Vec<String>{"json", "machine"},
              },
      },
      [] -> void {
        traceEvent("trace\nline");
        check(true);
      });
  passing.runSeed = 1234;
  passing.seed = 5678;
  passing.iteration = 2;
  passing.traceMode = TraceMode::ForcedAll;

  TestExecution failing = run(
      TestDescriptor{
          .identifier = "json failure",
          .location = location,
      },
      [location] -> void { check(false, location); });
  require(not failing.state.diagnostics.empty());
  failing.state.diagnostics.front().addNote(Vec<DiagnosticFragment>{
      DiagnosticFragment{.text = "difference: "},
      DiagnosticFragment{.text = "3", .highlighted = true},
  });
  failing.state.diagnostics.front().addExpansion(BinaryExpansion{
      .left = "2",
      .operatorName = "!=",
      .right = "3",
  });
  failing.state.diagnostics.front().addAttachment("input", "2 != 3");
  failing.fault = NativeFault{
      .kind = NativeFaultKind::Signal,
      .code = 11,
      .address = 0x10,
      .instruction = 0x20,
  };

  const Vec<TestExecution> executions{std::move(passing), std::move(failing)};
  RunAccumulator accumulator{RetentionPolicy::All};
  for (const TestExecution &execution : executions)
    accumulator.append(execution);
  const RunReport report = std::move(accumulator).finish();
  const JsonReporter reporter{JsonReporterOptions{.pretty = true}};
  const String output = reporter.render(report);

  require(executions.size() == 2_exp);
  check(report.cases.size() == 2_exp);
  check(report.failed());
  check(output.starts_with("{\n"));
  check(output.ends_with("}\n"));
  check(output.contains(R"("schema_version": 3)"));
  check(output.contains(R"("framework": "Switch")"));
  check(output.contains(R"("kind": "test_run")"));
  check(output.contains(R"("status": "failed")"));
  check(output.contains(R"("run_seed": 1234)"));
  check(output.contains(R"("seed": 5678)"));
  check(output.contains(R"("iteration": 2)"));
  check(output.contains(R"("trace_mode": "forced_all")"));
  check(output.contains(R"("attempt": {)"));
  check(output.contains(R"("profile": {)"));
  check(output.contains(R"("resources": {)"));
  check(output.contains(R"("cases": [)"));
  check(output.contains(R"(json\"passing\n)"));
  check(output.contains(R"(json\"passing\n\u0001)"));
  check(output.contains(R"(trace\nline)"));
  check(output.contains(R"("group": "reporting")"));
  check(output.contains(R"("expected_panic": "never")"));
  check(output.contains(R"("timeout_ns": 5000000)"));
  check(output.contains(R"("code": "E001")"));
  check(output.contains(R"("description": "assertion failed")"));
  check(output.contains(R"("kind": "binary")"));
  check(output.contains(R"("operator": "!=")"));
  check(output.contains(R"("highlight": {)") or output.contains("\"highlight\": null"));
  check(output.contains(R"("highlighted": true)"));
  check(output.contains(R"("name": "input")"));
  check(output.contains(R"("content": "2 != 3")"));
  check(output.contains(R"("fault": {)"));
  check(output.contains(R"("kind": "signal")"));
  check(output.contains(R"("symbols_available": false)"));

  const String compact = JsonReporter{}.render(report);
  check(compact.find('\n') == String::npos);

  const Vec<TestDescriptor> descriptors = list<^^Tests::json>(TestSelection{
      .tagsAny = {"json"},
  });
  const String listOutput = reporter.renderList(descriptors);

  require(descriptors.size() == 1_exp);
  check(listOutput.contains(R"("kind": "test_list")"));
  check(listOutput.contains(R"("count": 1)"));
  check(listOutput.contains(R"("identifier": "Tests::json::writesCompleteMachineReports")"));
  check(listOutput.contains(R"("tags": [)"));
}

} // namespace Tests::json
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::json>();
}
