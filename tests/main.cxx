import std;
import Switch;

using namespace Switch;

inline constexpr RunOptions selfTestOptions{
    .executionMode = ExecutionMode::Diagnostic,
    .threads = 1,
    .retention = RetentionPolicy::Failures,
    .timeMode = TimeMode::Real,
    .traceMode = Switch::TraceMode::Annotations,
    .captureTiming = CapturePolicy::PerAttempt,
    .order = ExecutionOrder::Declaration,
    .isolation = CrashIsolation::InProcess,
};

auto main() -> int {
  Reporter reporter{
      ReporterOptions{
          .renderer =
              {
                  .color = Switch::ColorMode::Automatic,
                  .terminal = true,
                  .showSource = true,
                  .details = Switch::DetailMode::Trace,
              },
          .showPassedTests = false,
          .showAttempts = false,
          .showSummary = true,
      },
  };

  const RunReport report = runAll(reporter, std::cout, TestSelection{}, selfTestOptions);
  return report.passed() ? 0 : 1;
}
