# ADR-0001: Python binding mechanism — ctypes

Status: accepted (Phase 1) · Issue: #5 · Affects: Kuantor/kuantorwave_cli#1

## Context

The Python layer in `kuantorwave_cli` must call the compiled KuantorWave core.
Issue #1 lists three candidates: ctypes, cffi, or a thin CPython extension
module.

## Decision

Use **ctypes** against the stable C ABI defined in `include/kuantorwave.h`.

## Rationale

- **Zero build step on the Python side.** `kuantorwave_cli` stays a pure-Python
  package; installing it never needs a compiler. Only the core repo ships
  per-platform binaries.
- **Stdlib only.** No runtime dependency (cffi) and no CPython version coupling
  (extension modules must be rebuilt per Python minor version; ctypes binds at
  runtime to any Python).
- **The API shape suits it.** Eleven functions, flat structs, out-parameters,
  integer error codes — exactly the subset ctypes handles well. We designed the
  header for this (no structs by value, no callbacks, no variadic functions).
- **Cost we accept:** per-call overhead of ctypes marshalling. Irrelevant here —
  calls are file-granular (one `kw_convert` per file), not per-sample.

## Alternatives

- **cffi (ABI mode):** marginally nicer struct handling, but adds a third-party
  dependency for no capability we need.
- **CPython extension:** fastest calls and best error integration, but couples
  builds to each Python version and platform, and puts a compiler into the CLI
  install path. Wrong trade-off for a file-granular API.

## Consequences

- The core must keep `kuantorwave.h` ctypes-friendly (C ABI rules listed in the
  header preamble); `KW_ABI_VERSION` guards against wrapper/binary mismatch.
- If profiling ever shows marshalling overhead matters (e.g. future streaming
  API), revisit with cffi ABI mode as the first step — the C header stays the
  contract either way.
