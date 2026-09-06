# Changelog

All notable user-visible changes to Switch are documented here.

This project is currently pre-1.0 and follows semantic versioning for release numbering.

## Unreleased

### Changed

- Promoted the immutable reference-toolchain baseline to `cxx26-2026.09.05` (`6c7ef6afbfd8456c964c7a2625b3ea2aaa7d613f`).
- Reference CI now validates Switch against Miracle `bf1b47514cc022fe0e9786baef16043f311cf3e7` instead of the historical Miracle `v0.1.0-rc.1` source revision.
- clangd now consumes the generated compilation database without reconstructing reference-toolchain falgs in `.clangd`.

## 0.1.0-rc.1 - 2026-08-26

### Added

- Standalone C++26 testing-framework identity: `import Switch;` / `Switch::Switch`.
- Public Miracle dependency with target-first source/package resolution.
- Reflection-driven macro-free test discovery and parameterized cases.
- Fixtures, explicit member subjects, deterministic scheduling, virtual time, native fault isolation, measurements, bounded retention, human reporting, and JSON output.
- Source, FetchContent, and installed-package consumption.
- Standalone CI, documentation, examples, and release scaffolding.
- Release provenance metadata recording the exact source, reference toolchain, and Miracle release revision.
