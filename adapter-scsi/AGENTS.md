# SCSI adapter agent instructions

These instructions supplement the repository-level `AGENTS.md`.

## Contract and implementation

- Read `README.md`, `ADAPTER-CONTRACT.md`, and `include/odpapi.h` before
  changing protocol behavior.
- Use the ODPAPI helper library for response envelopes and standard fields.
- Keep the canonical routes under `/dpapi-v1/`: `query.json`, `list.json`,
  `find.json`, and `inventory.json`.
- `/dpapi-v1` and `/dpapi-v1/` must return the unknown-route response with a
  hint to `/dpapi-v1/query.json`.
- Query must work with no query arguments or request body and must report the
  offered function names under `response.functions`.
- Document any provider-specific behavior and do not silently invent status
  meanings or required fields.
- Keep the adapter read-only. It must not reserve or move media.

## SCSI and identity rules

- Probe only `SCSIChangerDevice` paths explicitly present in Apache config.
- Keep the host iSCSI initiator, rescans, device-node creation, and device
  ownership outside the container.
- Derive stable GUID namespaces from discovered SCSI serial/VPD identity, with
  the configured `/dev` path as the documented fallback; do not add a static
  UUID value to the configuration.
- Keep generated GUIDs resolvable through the runtime GUID-to-device-element
  map so future move operations can translate source and target GUIDs back to
  Linux media-changer elements.
- Treat Storage, CAP/IO (SCSI import/export), each Drive, and Transport as
  separate logical libraries. CAP/IO is not an ODPAPI Pass-through library.
  Storage capacity is the number of storage slots. Each drive has capacity one.
- Drive descriptions may include vendor/model metadata, but that metadata is
  display text only and must never affect the stable drive GUID.
- All logical libraries for one changer share its application-defined scope,
  currently the discovered vendor/model description.
- Do not add credentials, production inventory, device captures, or binaries to
  the repository.

## Build and test

From the repository root:

```sh
cmake -S adapter-scsi -B build/scsi \
  -DODPAPI_SOURCE_DIR="$PWD" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON
cmake --build build/scsi --parallel
ctest --test-dir build/scsi --output-on-failure
podman build --file adapter-scsi/Containerfile \
  --tag localhost/scsi-adapter:test .
```

Native tests must not require a live changer. Use synthetic fixtures for core
logic; use an explicitly authorized host for live `/dev/sch*` validation.

## Deployment review

- Pass only approved changer nodes with read-only `--device` mappings.
- Use `--userns=keep-id` and `--group-add=keep-groups` for rootless testing
  when supplementary device groups are required.
- Never use `--privileged`.
- Verify Apache authentication and authorization before exposing the service.
- Test query, list, inventory, find, malformed requests, and unknown IDs.
- Run CTest after C changes and rebuild the Podman image after Containerfile or
  Apache configuration changes.
