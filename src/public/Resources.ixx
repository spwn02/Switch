export module Switch:Resources;

import std;
import Miracle;

using namespace Miracle;

export namespace Switch {

struct ResourceCleanupFailure final {
  String name;
  String message;
  std::source_location location;
};

struct ResourceCleanupReport final {
  Vec<ResourceCleanupFailure> failures;
};

struct ResourceSnapshot final {
  usize activeResources{};
  usize peakResources{};
  usize cleanupCount{};
  usize allocationBytes{};
  usize activeAllocationBytes{};
  usize peakAllocationBytes{};
  usize temporaryFiles{};
  usize temporaryDirectories{};
};

class ResourceHandle final {
public:
  ResourceHandle() = default;

  [[nodiscard]] auto valid() const noexcept -> bool {
    return id_ != 0;
  }

private:
  explicit constexpr ResourceHandle(usize id) noexcept // NOLINT
      : id_(id) {
  }

  usize id_{};

  friend class TestResources;
};

class TemporaryFile final {
public:
  TemporaryFile(Path path,
      fs::File file,
      std::source_location location = std::source_location::current()) noexcept;
  ~TemporaryFile() noexcept;

  TemporaryFile(const TemporaryFile &) = delete ("TemporaryFile owns File handle.");
  auto operator=(const TemporaryFile &) -> TemporaryFile & = delete ("TemporaryFile owns File handle.");
  TemporaryFile(TemporaryFile &&) noexcept;
  auto operator=(TemporaryFile &&) noexcept -> TemporaryFile &;

  [[nodiscard]] auto path() const noexcept -> const Path &;

  [[nodiscard]] auto file() noexcept -> fs::File &;

  [[nodiscard]] auto file() const noexcept -> const fs::File &;

  [[nodiscard]] auto cleanup() noexcept -> Option<ResourceCleanupFailure>;

private:
  Path path_;
  fs::File file_;
  std::source_location location_;

  friend class TestResources;
};

class TemporaryDirectory final {
public:
  TemporaryDirectory(Path path, std::source_location location = std::source_location::current()) noexcept;
  ~TemporaryDirectory() noexcept;

  TemporaryDirectory(const TemporaryDirectory &) = delete ("TemporaryDirectory owns File handle.");
  auto operator=(const TemporaryDirectory &)
      -> TemporaryDirectory & = delete ("TemporaryDirectory owns File handle.");
  TemporaryDirectory(TemporaryDirectory &&) noexcept;
  auto operator=(TemporaryDirectory &&) noexcept -> TemporaryDirectory &;

  [[nodiscard]] auto path() const noexcept -> const Path &;

  [[nodiscard]] auto cleanup() noexcept -> Option<ResourceCleanupFailure>;

private:
  Path path_;
  std::source_location location_;

  friend class TestResources;
};

class TestResources final {
private:
  class Entry {
  public:
    virtual ~Entry() noexcept = default;

    Entry(const Entry &) = delete;
    auto operator=(const Entry &) -> Entry & = delete;
    Entry(Entry &&) noexcept = delete;
    auto operator=(Entry &&) noexcept -> Entry & = delete;

    [[nodiscard]] virtual auto cleanup() noexcept -> Option<ResourceCleanupFailure> = 0;
    [[nodiscard]] virtual auto bytes() const noexcept -> usize = 0;
    virtual auto reset() noexcept -> void = 0;

  protected:
    Entry() = default;
  };

  template <class Type>
  class ValueEntry : public Entry {
  public:
    template <class... Args>
    explicit ValueEntry(Args &&...args)
        : value_(std::make_unique<Type>(std::forward<Args>(args)...)) {
    }

    [[nodiscard]] auto value() noexcept -> Type & {
      return *value_;
    }

    [[nodiscard]] auto cleanup() noexcept -> Option<ResourceCleanupFailure> override {
      if constexpr (requires(Type &value) {
                      { value.cleanup() } -> std::same_as<Option<ResourceCleanupFailure>>;
                    }) {
        return value_->cleanup();
      } else if constexpr (requires(Type &value) { value.cleanup(); }) {
        value_->cleanup();
        return None;
      } else {
        return None;
      }
    }

    [[nodiscard]] auto bytes() const noexcept -> usize override {
      return sizeof(Type);
    }

    auto reset() noexcept -> void override {
      value_.reset();
    }

  private:
    UPtr<Type> value_;
  };

  class DeferredEntry final : public Entry {
  public:
    DeferredEntry(std::move_only_function<void()> cleanup, std::source_location location)
        : cleanup_(std::move(cleanup))
        , location_(location) {
    }

    [[nodiscard]] auto cleanup() noexcept -> Option<ResourceCleanupFailure> override;

    [[nodiscard]] auto bytes() const noexcept -> usize override {
      return 0;
    }

    auto reset() noexcept -> void override {
      cleanup_ = {};
    }

  private:
    std::move_only_function<void()> cleanup_;
    std::source_location location_;
  };

public:
  TestResources() = default;
  ~TestResources() noexcept;

  TestResources(const TestResources &) = delete ("TestResources own and control heap allocations.");
  auto operator=(const TestResources &)
      -> TestResources & = delete ("TestResources own and control heap allocations.");
  TestResources(TestResources &&) noexcept = delete ("TestResources own and control heap allocations.");
  auto operator=(TestResources &&) noexcept
      -> TestResources & = delete ("TestResources own and control heap allocations.");

  template <class Type, class... Args>
  [[nodiscard]] auto allocate(Args &&...args) -> Type & {
    static_assert(
        std::is_nothrow_destructible_v<Type>, "TestResources values must have noexcept destructors.");
    auto entry = std::make_unique<ValueEntry<Type>>(std::forward<Args>(args)...);
    Type &value = entry->value();
    appendEntry(std::move(entry));
    return value;
  }

  [[nodiscard]] auto defer(std::move_only_function<void()> cleanup,
      std::source_location location = std::source_location::current()) -> ResourceHandle;

  [[nodiscard]] auto temporaryDirectory(StringView label = "directory",
      std::source_location location = std::source_location::current()) -> TemporaryDirectory &;

  [[nodiscard]] auto temporaryFile(StringView suffix = ".tmp",
      std::source_location location = std::source_location::current()) -> TemporaryFile &;

  [[nodiscard]] auto snapshot() const noexcept -> ResourceSnapshot;

  [[nodiscard]] auto cleanup() noexcept -> ResourceCleanupReport;

private:
  auto appendEntry(UPtr<Entry> entry) -> ResourceHandle;

  auto recordFailure(String name, String message, std::source_location location) -> void;

  [[nodiscard]] auto root() -> const Path &;

  Hive<UPtr<Entry>> entries_;
  Vec<Entry *> constructionOrder_;
  Vec<ResourceCleanupFailure> failures_;
  Path root_;
  bool rootAttempted_{};
  usize nextId_{1};
  usize cleanupCount_{};
  usize allocationBytes_{};
  usize activeAllocationBytes_{};
  usize peakAllocationBytes_{};
  usize activeResources_{};
  usize peakResources_{};
  usize temporaryFiles_{};
  usize temporaryDirectories_{};
  bool cleaned_{};
};

} // namespace Switch
