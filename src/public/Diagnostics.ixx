export module Switch:Diagnostics;

import std;
import Miracle;

import :Annotations;

export namespace Switch {

enum class[[= debug::derive]] DiagnosticLevel : u8 {
  Error[[= debug::rename("error")]],
  Warning[[= debug::rename("warning")]],
  Note[[= debug::rename("note")]],
  Help[[= debug::rename("help")]],
  Marker[[= debug::rename("marker")]],
};

enum class[[= debug::derive]] SpanKind : u8 {
  Primary[[= debug::rename("primary")]],
  Secondary[[= debug::rename("secondary")]],
};

enum class[[ = debug::derive, = diagnostics::prefix("E") ]] DiagnosticCode : u8 {
  /// Deliberately has no message: the renderer falls back to its debug-derived name.
  Unknown = 0,

  AssertionFailed[[= diagnostics::message("assertion failed")]] = 1,
  UnhandledException[[= diagnostics::message("unhandled exception")]] = 10,
  TestReturnedError[[= diagnostics::message("test returned an error")]] = 11,
  TestPanicked[[= diagnostics::message("test panicked")]] = 12,
  TimeoutExceeded[[= diagnostics::message("test exceeded its timeout")]] = 13,
  ExpectedPanicNotObserved[[= diagnostics::message("expected panic was not observed")]] = 14,
  ProviderProducedNoValues[[= diagnostics::message("provider produced no values")]] = 15,
  TaskStranded[[= diagnostics::message("asynchronous task was stranded")]] = 16,
  ResourceCleanupFailed[[= diagnostics::message("test resource cleanup failed")]] = 17,
  NativeFault[[= diagnostics::message("test worker terminated by a native fault")]] = 18,
  WorkerLaunchFailed[[= diagnostics::message("test worker could not be started")]] = 19,
};

/// Legacy rendering shorthand retained for source compatibility.
/// TODO: Completely switch to the new API
enum class[[= debug::derive]] DetailMode : u8 {
  None,
  Failures,
  Trace,
};

/// Identifies the portion of a diagnostic that a renderer may display.
enum class[[= bitflags]] DiagnosticSection : u8 {
  None = 0,
  Header = 1 << 0,
  Source = 1 << 1,
  PrimarySpan = 1 << 2,
  SecondarySpans = 1 << 3,
  Notes = 1 << 4,
  Attachments = 1 << 5,
  Trace = 1 << 6,
  Profile = 1 << 7,
};

using DiagnosticSections = DiagnosticSection;

/// A one-based source position used by resolved diagnostic ranges.
struct SourcePosition final {
  usize line{};
  usize column{};
};

/// A serializable source-location snapshot used when an isolated worker returns diagnostics to its parent.
struct SourceLocationData final {
  String file;
  String function;
  usize line{};
  usize column{};
};

/// A source range resolved from a source plan by a report-local SourceManager.
struct SourceRange final {
  Path file;
  SourcePosition begin;
  SourcePosition end; // exclusive
};

/// Describes how a source span should be expanded by the source resolver.
enum class[[= debug::derive]] SpanSelection : u8 {
  Point[[= debug::rename("point")]],
  Invocation[[= debug::rename("invocation")]],
  EnclosingExpression[[= debug::rename("enclosing_expression")]],
  EnclosingStatement[[= debug::rename("enclosing_statement")]],
  Declaration[[= debug::rename("declaration")]],
};

struct SourceSpan final {
  std::source_location location;
  SpanSelection selection{SpanSelection::Point};
  SpanKind kind{SpanKind::Primary};
  String label;
  /// Set only for diagnostics reconstructed from an isolated worker.
  Option<SourceLocationData> remoteLocation;
};

[[nodiscard]] auto makeSpan(String label = {},
    SpanKind = SpanKind::Primary,
    std::source_location location = std::source_location::current(),
    SpanSelection selection = SpanSelection::Point) -> SourceSpan;

[[nodiscard]] auto isPrimarySpan(const SourceSpan &span) noexcept -> bool;

struct DiagnosticFragment final {
  String text;
  bool highlighted{};
};

struct DiagnosticNote final {
  DiagnosticLevel level{DiagnosticLevel::Note};
  String message;
  Option<SourceSpan> span;
  Vec<DiagnosticFragment> fragments;
};

struct DiagnosticAttachment final {
  String name;
  String content;
};

/// Structured data describing a failed assertion expansion.
struct BinaryExpansion final {
  String left;
  String operatorName;
  String right;
};

/// Structured data describing a failed membership assertion.
struct ContainsExpansion final {
  String needle;
  String container;
};

/// Structure data describing a failed approximate comparison.
struct NearExpansion final {
  String left;
  String right;
  String tolerance;
  String difference;
};

using DiagnosticExpansion = std::variant<std::monostate, BinaryExpansion, ContainsExpansion, NearExpansion>;

struct DiagnosticHeader final {
  DiagnosticCode code{};
  Option<String> descriptionOverride;
};

struct DiagnosticDetails final {
  Vec<SourceSpan> spans;
  Vec<DiagnosticNote> notes;
  Vec<DiagnosticAttachment> attachments;
  DiagnosticExpansion expansion;
};

struct Diagnostic final {
  DiagnosticLevel level{DiagnosticLevel::Error};
  DiagnosticHeader header{};
  DiagnosticDetails details{};

  [[nodiscard]] auto code() const -> String;

  [[nodiscard]] auto description() const -> String;

  [[nodiscard]] auto primarySpan() const noexcept -> Option<Ref<const SourceSpan>>;

  auto addSpan(SourceSpan span) -> Diagnostic &;

  auto addNote(String note,
      DiagnosticLevel noteLevel = DiagnosticLevel::Note,
      Option<SourceSpan> noteSpan = None) -> Diagnostic &;

  auto addNote(Vec<DiagnosticFragment> fragments,
      DiagnosticLevel noteLevel = DiagnosticLevel::Note,
      Option<SourceSpan> noteSpan = None) -> Diagnostic &;

  auto addAttachment(String name, String content) -> Diagnostic &;

  auto addExpansion(DiagnosticExpansion expansion) -> Diagnostic &;
};

[[nodiscard]] constexpr auto diagnosticCode(DiagnosticCode code) -> String {
  StringView prefix = diagnostics::annotationPrefix<^^DiagnosticCode>();
  return std::format("{}{:03}", prefix, std::to_underlying(code));
}

[[nodiscard]] constexpr auto diagnosticDescription(DiagnosticCode code) -> String {
  // NOLINTNEXTLINE(bugprone-reserved-identifier)
  template for (constexpr std::meta::info item : meta::enumerators<^^DiagnosticCode>) {
    if ([:item:] == code)
      return String{diagnostics::annotationMessage<item>()};
  }

  return String{debug::enumName(code)};
}

[[nodiscard]] auto makeDiagnostic(DiagnosticCode code,
    std::source_location location = std::source_location::current()) -> Diagnostic;

[[nodiscard]] auto makeDiagnostic(DiagnosticLevel level,
    DiagnosticCode code,
    std::source_location location = std::source_location::current()) -> Diagnostic;

} // namespace Switch
