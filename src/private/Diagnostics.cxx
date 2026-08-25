module Switch;

import std;
import Miracle;

using namespace Miracle;

namespace Switch {

auto makeSpan(String label, SpanKind kind, std::source_location location, SpanSelection selection)
    -> SourceSpan {
  return SourceSpan{
      .location = location,
      .selection = selection,
      .kind = kind,
      .label = std::move(label),
  };
}

auto isPrimarySpan(const SourceSpan &span) noexcept -> bool {
  return span.kind == SpanKind::Primary;
}

auto Diagnostic::code() const -> String {
  return diagnosticCode(header.code);
}

auto Diagnostic::description() const -> String {
  if (header.descriptionOverride)
    return *header.descriptionOverride;

  return diagnosticDescription(header.code);
}

auto Diagnostic::primarySpan() const noexcept -> Option<Ref<const SourceSpan>> {
  const auto span = std::ranges::find_if(details.spans, isPrimarySpan);
  if (span == details.spans.end())
    return None;

  return std::cref(*span);
}

auto Diagnostic::addSpan(SourceSpan span) -> Diagnostic & {
  details.spans.push_back(std::move(span));
  return *this;
}

auto Diagnostic::addNote(String note, DiagnosticLevel noteLevel, Option<SourceSpan> noteSpan)
    -> Diagnostic & {
  details.notes.push_back(DiagnosticNote{
      .level = noteLevel,
      .message = std::move(note),
      .span = std::move(noteSpan),
      .fragments = {},
  });
  return *this;
}

auto Diagnostic::addNote(Vec<DiagnosticFragment> fragments,
    DiagnosticLevel noteLevel,
    Option<SourceSpan> noteSpan) -> Diagnostic & {
  details.notes.push_back(DiagnosticNote{
      .level = noteLevel,
      .message = {},
      .span = std::move(noteSpan),
      .fragments = std::move(fragments),
  });
  return *this;
}

auto Diagnostic::addAttachment(String name, String content) -> Diagnostic & {
  details.attachments.push_back(DiagnosticAttachment{
      .name = std::move(name),
      .content = std::move(content),
  });
  return *this;
}

auto Diagnostic::addExpansion(DiagnosticExpansion expansion) -> Diagnostic & {
  details.expansion = std::move(expansion);
  return *this;
}

auto makeDiagnostic(DiagnosticCode code, std::source_location location) -> Diagnostic {
  return makeDiagnostic(DiagnosticLevel::Error, code, location);
}

auto makeDiagnostic(DiagnosticLevel level, DiagnosticCode code, std::source_location location) -> Diagnostic {
  Diagnostic result{
      .level = level,
      .header = DiagnosticHeader{.code = code},
      .details = {},
  };
  result.addSpan(makeSpan({}, SpanKind::Primary, location));
  return result;
}

} // namespace Switch
