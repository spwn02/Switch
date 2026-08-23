import Switch;

auto main() -> int {
  Switch::RunOptions options{};
  return options.threads == 1 ? 0 : 1;
}
