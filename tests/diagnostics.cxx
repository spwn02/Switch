import std;
import Miracle;
import Switch;

using namespace Miracle;
using namespace Switch;

namespace Tests::diagnostics {

auto makeDiagnosticForRendering() -> Diagnostic {
  const auto location = std::source_location::current();
  Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::AssertionFailed, location);
  diagnostic.details.spans.clear();
  diagnostic.addSpan(makeSpan("left value", SpanKind::Primary, location, SpanSelection::EnclosingExpression));
  diagnostic.addNote("left: 2");
  diagnostic.addNote("right: 3");
  diagnostic.addNote(Vec<DiagnosticFragment>{
      DiagnosticFragment{.text = "difference: "},
      DiagnosticFragment{.text = "3", .highlighted = true},
  });
  diagnostic.addExpansion(BinaryExpansion{
      .left = "2",
      .operatorName = "!=",
      .right = "3",
  });
  diagnostic.addAttachment("input", "2 != 3");

  return diagnostic;
}
[[ = test, = group("framework"), = tag("diagnostics") ]] auto diagnostics() -> void {
  const Diagnostic diagnostic = makeDiagnosticForRendering();
  const SourceSpan &primary = diagnostic.primarySpan()->get();
  const usize lineNumberWidth = std::to_string(primary.location.line()).size();
  const String locationPrefix = String(lineNumberWidth - 1, ' ') + "--> ";
  const String sourcePrefix = std::format("{:>{}} | ", primary.location.line(), lineNumberWidth);
  const String markerPrefix = String(lineNumberWidth + 1, ' ') + "| ";
  const SourceManager sources{Vec<Path>{std::filesystem::current_path()}};
  const SourceSpan multiLine = makeSpan(
      "multi-line", SpanKind::Primary, std::source_location::current(), SpanSelection::EnclosingStatement);
  const Option<SourceResolution> resolution = sources.resolve(multiLine);
  require(resolution);

  Diagnostic multiLineDiagnostic = makeDiagnostic(DiagnosticCode::AssertionFailed);
  multiLineDiagnostic.details.spans.clear();
  multiLineDiagnostic.addSpan(multiLine);

  const String multiLineOutput = renderToString(multiLineDiagnostic,
      sources,
      RendererOptions{.color = ColorMode::Never, .terminal = false, .showSource = true});

  const String plain = renderToString(diagnostic,
      sources,
      RendererOptions{.color = ColorMode::Never, .terminal = false, .showSource = true, .tabWidth = 4});

  const String compact = renderToString(diagnostic,
      sources,
      RendererOptions{
          .color = ColorMode::Never,
          .terminal = false,
          .showSource = false,
          .details = DetailMode::None,
      });

  const String coloured = renderToString(diagnostic,
      sources,
      RendererOptions{.color = ColorMode::Always, .terminal = false, .showSource = true, .tabWidth = 4});

  const String headerOnly = renderToString(diagnostic,
      sources,
      RendererOptions{
          .color = ColorMode::Never,
          .terminal = false,
          .sections = DiagnosticSection::Header,
      });

  const String hidden = renderToString(diagnostic,
      sources,
      RendererOptions{
          .color = ColorMode::Never,
          .terminal = false,
          .sections = DiagnosticSection::None,
      });

  Diagnostic custom = makeDiagnostic(DiagnosticCode::AssertionFailed);
  custom.header.descriptionOverride = "custom diagnostic description";

  check(diagnostic.code() == "E001"_exp);
  check(diagnostic.description() == "assertion failed"_exp);
  check(diagnosticDescription(DiagnosticCode::Unknown) == "Unknown"_exp);
  check(diagnosticCode(DiagnosticCode::Unknown) == "E000"_exp);
  check(custom.description() == "custom diagnostic description"_exp);
  check(plain.contains("error[E001]: assertion failed"));
  check(plain.contains("left value"));
  check(plain.contains("= note: left: 2"));
  check(plain.contains("= note: difference: 3"));
  check(plain.contains("= expanded to: 2 != 3"));
  check(plain.contains("const auto location"));
  check(plain.contains(locationPrefix));
  check(plain.contains(sourcePrefix));
  check(plain.contains(markerPrefix));
  check(plain.contains("attachment: input"));
  check(resolution->range.end.line > resolution->range.begin.line);
  check(resolution->highlight.begin.line >= resolution->range.begin.line);
  check(resolution->highlight.end.line <= resolution->range.end.line);
  check(multiLineOutput.contains("const SourceSpan multiLine = makeSpan("));
  check(multiLineOutput.contains("\"multi-line\""));
  check(not plain.contains("\x1b["));
  check(compact.contains("error[E001]: assertion failed"));
  check(not compact.contains("left: 2"));
  check(not compact.contains("attachment: input"));
  check(headerOnly.contains("error[E001]: assertion failed"));
  check(not headerOnly.contains("left: 2"));
  check(not headerOnly.contains("const auto location"));
  check(hidden.empty());
  check(coloured.contains("\x1b[1;31m"));
  check(coloured.contains("\x1b[1;31m3\x1b[0m"));
  check(coloured.contains("\x1b[0m"));
};

} // namespace Tests::diagnostics

consteval {
  discover<^^Tests::diagnostics>();
}
