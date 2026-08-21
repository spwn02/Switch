export module Switch:Render;

import std;
import Miracle;

import :Diagnostics;

using namespace Miracle;

export namespace Switch {

struct SourceLine final {
  Path file;
  usize number{};
  String text;
};

/// Owns one report-local source document and its line index.
struct SourceDocument final {
  Path path;
  String contents;
  Vec<usize> lineOffsets;

  [[nodiscard]] auto line(usize number) const noexcept -> StringView;

  [[nodiscard]] auto lineCount() const noexcept -> usize;
};

/// Couples a resolved source range with the cached document that contains it.
struct SourceResolution final { // NOLINT(cppcoreguidelines-pro-type-member-init)
  Ref<const SourceDocument> document;
  /// Full source context shown around the diagnostic.
  SourceRange range;
  /// The meaningful expression or declaration portion underlined inside that context.
  SourceRange highlight;
};

/// Resolves source locations and caches complete source documents per report.
///
/// The manager owns no process-wide cache. That keeps rendering deterministic and independent renderer
/// instances safe to use on different threads.
class SourceManager final {
public:
  SourceManager();
  explicit SourceManager(Vec<Path> roots);

  auto addRoot(Path root) -> void;

  [[nodiscard]] auto resolve(const SourceSpan &span) const -> Option<SourceResolution>;

  [[nodiscard]] auto sourceLine(const SourceSpan &span) const -> Option<SourceLine>;

private:
  [[nodiscard]] static auto exists(const Path &path) -> bool;

  [[nodiscard]] auto resolvePath(const Path &requested) const -> Option<Path>;

  [[nodiscard]] auto load(const Path &path) const -> Option<Ref<const SourceDocument>>;

  Vec<Path> roots_;
  mutable FlatMap<Path, UPtr<SourceDocument>> documents_;
};

enum class[[= debug::derive]] ColorMode : u8 {
  Automatic,
  Always,
  Never,
};

struct RendererOptions final {
  ColorMode color{ColorMode::Automatic};
  bool terminal{};
  bool showSource{true};
  /// An engaged value is authoritative, including DiagnosticSection::None. An empty value selects the legacy
  /// DetailMode mapping.
  Option<DiagnosticSections> sections;
  DetailMode details{DetailMode::Failures};
  usize tabWidth{4};

  [[nodiscard]] constexpr auto effectiveSections() const noexcept -> DiagnosticSections;
};

class AnsiRenderer final {
public:
  explicit AnsiRenderer(RendererOptions options = {});

  auto render(const Diagnostic &diagnostic, const SourceManager &sources, std::ostream &output) const -> void;

private:
  [[nodiscard]] auto colorEnabled() const noexcept -> bool;

  RendererOptions options_{};
};

auto render(const Diagnostic &diagnostic,
    const SourceManager &sources,
    std::ostream &output,
    RendererOptions options = {}) -> void;

[[nodiscard]] auto renderToString(const Diagnostic &diagnostic,
    const SourceManager &sources,
    RendererOptions options = {}) -> String;

} // namespace Switch
