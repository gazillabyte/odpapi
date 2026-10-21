# Guidance for AI coding agents

These instructions apply when an AI coding agent is asked to work on ODPAPI or
to help scaffold an independent adapter using this project as reference. Copy
this file into a separate adapter repository if you want these instructions to
guide work there. It is project guidance, not a license or legal agreement.

## Establish the protocol contract first

- Read `README.md`, `ADAPTER-CONTRACT.md`, and the relevant `wiki/` pages.
- The DOCX specification is historical (2013–2014), incomplete, and sometimes
  inconsistent with itself and with `include/odpapi.h`. Do not silently invent
  routes, required fields, or status-code meanings. Record discrepancies and
  request a provider/version decision before encoding ambiguous behavior.
- Keep an adapter in its own repository and architecture. The TapeTrack
  adapter is closed source and is not included here; do not seek, recreate, or
  copy its implementation or customer-specific configuration.
- Treat Apache-2.0 as the license for ODPAPI project materials only. An
  independently authored adapter can use a different license.

## Secure and testable implementation

- Never add credentials, tokens, production URLs with credentials, customer
  names, customer records, or captured production payloads to source or tests.
- Prefer HTTPS. Keep authentication in runtime configuration and redact it
  from logs and error output.
- Use synthetic fixtures and a fake/local endpoint for automated tests. Call a
  provider test endpoint only when the user has explicitly requested it and
  supplied a safe test configuration.
- Test request-wide results and per-object outcomes separately, including
  malformed input, partial success, unknown identifiers, and retryable
  endpoint failures where applicable.
- Document identifier mapping, GUID scope, supported operations, protocol
  version, and unsupported behaviors.

## Changes in this repository

- Build with CMake: `cmake -S . -B build` and `cmake --build build`.
- Generate the C API reference with `cmake --build build --target odpapi-docs`
  when Doxygen is installed.
- Add or update Doxygen comments when changing public declarations.
- Use the standard upstream cJSON library rather than vendoring its source or
  header. Preserve its MIT license when distributing it; review third-party
  licenses before adding dependencies.
- Do not commit generated build output, old captured data, binaries, secrets,
  or changes from an unrelated working tree.
- When adding an adapter to the project ecosystem list, use only public
  information approved by the adapter maintainer.
