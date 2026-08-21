module Switch;

import std;
import Miracle;

import :Resources;

using namespace Miracle;

namespace Switch {

namespace {

[[nodiscard]] auto pathText(const Path &path) -> String {
  return path.generic_string();
}

} // namespace

TemporaryFile::TemporaryFile(Path path, fs::File file, std::source_location location) noexcept
    : path_(std::move(path))
    , file_(std::move(file))
    , location_(location) {
}

TemporaryFile::~TemporaryFile() noexcept {
  static_cast<void>(cleanup());
}

TemporaryFile::TemporaryFile(TemporaryFile &&other) noexcept
    : path_(std::exchange(other.path_, {}))
    , file_(std::move(other.file_))
    , location_(other.location_) {
}

auto TemporaryFile::operator=(TemporaryFile &&other) noexcept -> TemporaryFile & {
  if (this == std::addressof(other))
    return *this;

  static_cast<void>(cleanup());
  path_ = std::exchange(other.path_, {});
  file_ = std::move(other.file_);
  location_ = other.location_;
  return *this;
}

auto TemporaryFile::path() const noexcept -> const Path & {
  return path_;
}

auto TemporaryFile::file() noexcept -> fs::File & {
  return file_;
}

auto TemporaryFile::file() const noexcept -> const fs::File & {
  return file_;
}

auto TemporaryFile::cleanup() noexcept -> Option<ResourceCleanupFailure> {
  if (path_.empty())
    return None;

  file_.close();
  std::error_code error{};
  const bool removed = std::filesystem::remove(path_, error);
  const Path path = std::exchange(path_, {});
  if (removed and not error)
    return None;

  if (not error and not removed)
    return None;

  return ResourceCleanupFailure{
      .name = "temporary file",
      .message = std::format("failed to remove {}: {}", pathText(path), error.message()),
      .location = location_,
  };
}

TemporaryDirectory::TemporaryDirectory(Path path, std::source_location location) noexcept
    : path_(std::move(path))
    , location_(location) {
}

TemporaryDirectory::~TemporaryDirectory() noexcept {
  static_cast<void>(cleanup());
}

TemporaryDirectory::TemporaryDirectory(TemporaryDirectory &&other) noexcept
    : path_(std::exchange(other.path_, {}))
    , location_(other.location_) {
}

auto TemporaryDirectory::operator=(TemporaryDirectory &&other) noexcept -> TemporaryDirectory & {
  if (this == std::addressof(other))
    return *this;

  static_cast<void>(cleanup());
  path_ = std::exchange(other.path_, {});
  location_ = other.location_;
  return *this;
}

auto TemporaryDirectory::path() const noexcept -> const Path & {
  return path_;
}

auto TemporaryDirectory::cleanup() noexcept -> Option<ResourceCleanupFailure> {
  if (path_.empty())
    return None;

  std::error_code error{};
  const Path path = std::exchange(path_, {});
  static_cast<void>(std::filesystem::remove_all(path, error));
  if (not error)
    return None;

  return ResourceCleanupFailure{
      .name = "temporary directory",
      .message = std::format("failed to remove {}: {}", pathText(path), error.message()),
      .location = location_,
  };
}

auto TestResources::DeferredEntry::cleanup() noexcept -> Option<ResourceCleanupFailure> {
  if (not cleanup_)
    return None;

  try {
    cleanup_();
    cleanup_ = {};
    return None;
  } catch (const std::exception &exception) {
    const char *message = exception.what();
    cleanup_ = {};
    return ResourceCleanupFailure{
        .name = "deferred cleanup",
        .message = message == nullptr ? String{"resource cleanup threw"} : String{message},
        .location = location_,
    };
  } catch (...) {
    cleanup_ = {};
    return ResourceCleanupFailure{
        .name = "deferred cleanup",
        .message = String{"resource cleanup threw a non-standard exception"},
        .location = location_,
    };
  }
}

TestResources::~TestResources() noexcept {
  static_cast<void>(cleanup());
}

auto TestResources::appendEntry(UPtr<Entry> entry) -> ResourceHandle {
  Entry *const address = entry.get();
  entries_.emplace(std::move(entry));
  constructionOrder_.push_back(address);
  const usize bytes = address->bytes();
  allocationBytes_ += bytes;
  activeAllocationBytes_ += bytes;
  peakAllocationBytes_ = std::max(peakAllocationBytes_, activeAllocationBytes_);
  ++activeResources_;
  peakResources_ = std::max(peakResources_, activeResources_);
  return ResourceHandle{nextId_++};
}

auto TestResources::recordFailure(String name, String message, std::source_location location) -> void {
  failures_.push_back(ResourceCleanupFailure{
      .name = std::move(name),
      .message = std::move(message),
      .location = location,
  });
}

auto TestResources::root() -> const Path & {
  if (not rootAttempted_) {
    rootAttempted_ = true;
    Result<Path> created = fs::temporaryDirectory("switch");
    if (created) {
      root_ = std::move(*created);
    } else {
      recordFailure("resource creation", created.error().display(), std::source_location::current());
    }
  }

  return root_;
}

auto TestResources::defer(std::move_only_function<void()> cleanup, std::source_location location)
    -> ResourceHandle {
  if (not cleanup)
    std::terminate();

  return appendEntry(std::make_unique<DeferredEntry>(std::move(cleanup), location));
}

auto TestResources::temporaryDirectory(StringView label, std::source_location location)
    -> TemporaryDirectory & {
  const Path parent = root();
  if (parent.empty())
    return allocate<TemporaryDirectory>(Path{}, location);

  const Result<Path> created = fs::createDirectory(parent, std::format("{}-{}", label, nextId_));
  if (not created) {
    recordFailure("resource creation", created.error().display(), location);
    return allocate<TemporaryDirectory>(Path{}, location);
  }

  auto &directory = allocate<TemporaryDirectory>(*created, location);
  ++temporaryDirectories_;
  return directory;
}

auto TestResources::temporaryFile(StringView suffix, std::source_location location) -> TemporaryFile & {
  const Path parent = root();
  if (parent.empty())
    return allocate<TemporaryFile>(Path{}, fs::File{Path{}, std::ios::in}, location);

  const Result<Path> path = fs::temporaryPath(parent, "file", suffix);
  if (not path) {
    recordFailure("resource creation", path.error().display(), location);
    return allocate<TemporaryFile>(Path{}, fs::File{Path{}, std::ios::in}, location);
  }

  const fs::OpenOptions options{
      .create_new = true,
      .read = true,
      .write = true,
  };
  Result<fs::File> opened = options.open(*path);
  if (not opened) {
    recordFailure("resource creation", opened.error().display(), location);
    return allocate<TemporaryFile>(Path{}, fs::File{Path{}, std::ios::in}, location);
  }

  auto &file = allocate<TemporaryFile>(*path, std::move(*opened), location);
  ++temporaryFiles_;
  return file;
}

auto TestResources::snapshot() const noexcept -> ResourceSnapshot {
  return ResourceSnapshot{
      .activeResources = activeResources_,
      .peakResources = peakResources_,
      .cleanupCount = cleanupCount_,
      .allocationBytes = allocationBytes_,
      .activeAllocationBytes = activeAllocationBytes_,
      .peakAllocationBytes = peakAllocationBytes_,
      .temporaryFiles = temporaryFiles_,
      .temporaryDirectories = temporaryDirectories_,
  };
}

// NOLINTNEXTLINE(bugprone-exception-escape)
auto TestResources::cleanup() noexcept -> ResourceCleanupReport {
  if (cleaned_)
    return {};

  ResourceCleanupReport result{
      .failures = std::move(failures_),
  };
  std::ranges::for_each(std::views::reverse(constructionOrder_), [this, &result](Entry *entry) -> void {
    if (const Option<ResourceCleanupFailure> failure = entry->cleanup())
      result.failures.push_back(*failure);

    activeAllocationBytes_ -= entry->bytes();
    if (activeResources_ != 0)
      --activeResources_;
    ++cleanupCount_;
    entry->reset();
  });
  constructionOrder_.clear();
  entries_.clear();

  if (not root_.empty()) {
    std::error_code error{};
    const Path path = std::exchange(root_, {});
    static_cast<void>(std::filesystem::remove_all(path, error));
    if (error)
      result.failures.push_back(ResourceCleanupFailure{
          .name = "temporary root",
          .message = std::format("failed to remove {}: {}", pathText(path), error.message()),
          .location = std::source_location::current(),
      });
  }

  cleaned_ = true;
  return result;
}

} // namespace Switch
