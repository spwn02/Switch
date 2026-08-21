export module Switch:Json;

import std;
import Miracle;

import :Execution;
import :Reporting;

using namespace Miracle;

export namespace Switch {

/// Controls the formatting of the stable Switch JSON report schema.
struct JsonReporterOptions final {
  /// Emits indentation and line breaks when enabled. Machine output is compact by default.
  bool pretty{};
  usize indentWidth{2};
  /// Writes human-readable start/finish markers to stderr while preserving valid JSON on output.
  bool showProgress{};
};

/// Emits complete test-run state as JSON without ANSI colours or human-rendering policies.
///
/// The root object uses the current schema version. The schema deliberately keeps diagnostics, source spans,
/// notes, attachments, traces, execution seeds, and fixture-independent descriptors in the report so external
/// tools do not need to scrape the human renderer.
class JsonReporter final {
public:
  explicit JsonReporter(JsonReporterOptions options = {});

  auto addRoot(Path root) -> void;

  auto report(const RunReport &report, std::ostream &output) const -> void;

  [[nodiscard]] auto render(const RunReport &report) const -> String;

  /// Emits selected descriptors using the stable Switch JSON list schema.
  auto reportList(Span<const TestDescriptor> descriptors, std::ostream &output) const -> void;

  [[nodiscard]] auto renderList(Span<const TestDescriptor> descriptors) const -> String;

private:
  JsonReporterOptions options_{};
  Vec<Path> roots_;
};

} // namespace Switch
