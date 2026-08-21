module Switch;

import std;
import Miracle;

import :Policies;

using namespace Miracle;

namespace Switch {

TestPanic::TestPanic(String message, std::source_location location) noexcept
    : message_(std::move(message))
    , location_(location) {
}

auto TestPanic::message() const noexcept -> StringView {
  return message_;
}

auto TestPanic::location() const noexcept -> std::source_location {
  return location_;
}

auto TestPanic::what() const noexcept -> const char * {
  return message_.c_str();
}

auto panic(StringView message, std::source_location location) -> void {
  throw TestPanic{String{message}, location};
}

} // namespace Switch
