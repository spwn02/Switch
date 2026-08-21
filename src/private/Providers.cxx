module Switch;

import std;
import Miracle;

import :Providers;

using namespace Miracle;

namespace Switch {

namespace {

[[nodiscard]] constexpr auto hasWildcard(StringView value) noexcept -> bool {
  return std::ranges::any_of(
      value, [](char character) constexpr noexcept -> bool { return character == '*' or character == '?'; });
}

[[nodiscard]] constexpr auto matchStar(StringView value, StringView pattern) noexcept -> bool {
  if (matchesGlob(value, pattern))
    return true;

  if (value.empty() or value.front() == '/')
    return false;

  return matchStar(value.substr(1), pattern);
}

[[nodiscard]] auto matchDoubleStar(StringView value, StringView pattern) -> bool {
  if (matchesGlob(value, pattern))
    return true;

  if (value.empty())
    return false;

  return matchDoubleStar(value.substr(1), pattern);
}

[[nodiscard]] constexpr auto hasDotComponent(const Path &path) -> bool {
  return std::ranges::any_of(path, [](const Path &component) constexpr -> bool {
    const String name = component.generic_string();
    return name.size() > 1 and name.starts_with('.') and name != "." and name != "..";
  });
}

[[nodiscard]] constexpr auto normalizedText(const Path &path) -> String {
  String result = path.lexically_normal().generic_string();
  if (result.starts_with("./"))
    result.erase(0, 2);

  return result;
}

[[nodiscard]] constexpr auto searchRoot(StringView pattern) -> Path {
  const char *const wildcard = std::ranges::find_if(pattern,
      [](char character) constexpr noexcept -> bool { return character == '*' or character == '?'; });
  const usize prefixSize = static_cast<usize>(std::ranges::distance(pattern.begin(), wildcard));
  const Path prefix{pattern.substr(0, prefixSize)};
  Path root = prefix.parent_path();

  if (root.empty())
    return Path{"."};

  return root;
}

[[nodiscard]] constexpr auto excluded(const Path &path,
    StringView value,
    const Vec<String> &patterns) noexcept -> bool {
  return std::ranges::any_of(patterns, [&](StringView pattern) constexpr noexcept -> bool {
    if (hasWildcard(pattern))
      return matchesGlob(value, pattern);

    return value.contains(pattern) or
           std::ranges::any_of(path, [pattern](const Path &component) constexpr noexcept -> bool {
             return component.generic_string().contains(pattern);
           });
  });
}

[[nodiscard]] constexpr auto accepted(const Path &path, const FileQuery &query) -> bool {
  const String text = normalizedText(path);
  if (not query.includeDotFiles and hasDotComponent(path))
    return false;

  return not excluded(path, text, query.excludes);
}

} // namespace

auto matchesGlob(StringView value, StringView pattern) -> bool {
  if (pattern.empty())
    return value.empty();

  if (pattern.starts_with("**/")) {
    if (matchesGlob(value, pattern.substr(3)))
      return true;

    const usize separator = value.find('/');
    return separator != StringView::npos and matchesGlob(value.substr(separator + 1), pattern);
  }

  if (pattern.starts_with("**"))
    return matchDoubleStar(value, pattern.substr(2));

  if (pattern.front() == '*')
    return matchStar(value, pattern.substr(1));

  if (value.empty())
    return false;

  if (pattern.front() == '?')
    return value.front() != '/' and matchesGlob(value.substr(1), pattern.substr(1));

  return value.front() == pattern.front() and matchesGlob(value.substr(1), pattern.substr(1));
}

auto findFiles(const FileQuery &query) -> Vec<Path> {
  const Path pattern{query.pattern};
  const String normalizedPattern = normalizedText(pattern);
  const Path root = searchRoot(normalizedPattern);
  std::error_code error;
  Vec<Path> result{};

  if (not std::filesystem::exists(root, error) or error)
    return result;

  if (not hasWildcard(normalizedPattern)) {
    if (std::filesystem::is_regular_file(pattern, error) and not error and accepted(pattern, query))
      result.push_back(pattern);
    return result;
  }

  const std::filesystem::directory_options options =
      std::filesystem::directory_options::skip_permission_denied;
  const std::filesystem::recursive_directory_iterator entries{root, options, error};
  if (error)
    return result;

  std::ranges::for_each(entries, [&](const std::filesystem::directory_entry &entry) -> void {
    std::error_code entryError;
    if (not entry.is_regular_file(entryError) or entryError)
      return;

    const Path &path = entry.path();
    const String text = normalizedText(path);
    if (not matchesGlob(text, normalizedPattern) or not accepted(path, query))
      return;

    result.push_back(path);
  });

  std::ranges::sort(result, [](const Path &left, const Path &right) -> bool {
    return left.generic_string() < right.generic_string();
  });

  return result;
}

} // namespace Switch
