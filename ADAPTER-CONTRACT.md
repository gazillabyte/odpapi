# Adapter interoperability contract

This document describes a technical integration contract for independent
ODPAPI adapters. It is not a legal agreement, certification, or guarantee of
compatibility. The Apache-2.0 license applies to the ODPAPI project materials;
it does not require an independently written adapter to be open source.

The historical protocol document is incomplete and contains conflicting
examples. Until a current, versioned normative specification resolves those
conflicts, adapter authors and providers should agree on the exact protocol
version, routes, fields, status values, authentication method, and error
handling used by their integration.

## Compatibility expectations

An adapter should:

1. State which ODPAPI version and operations it implements.
2. Exchange JSON requests and responses over stateless HTTP(S) calls, using the
   authentication mechanism agreed with the provider.
3. Preserve identifiers and request sequence information so object-level
   results can be correlated with request entries.
4. Report request-wide errors separately from per-object outcomes.
5. Document how provider objects and libraries map to ODPAPI identifiers and
   fields, including any GUID encoding and scope assumptions.
6. Define behavior for unknown objects, invalid data, duplicate objects,
   unavailable endpoints, retries, and partial success.
7. Avoid exposing credentials, tokens, or sensitive customer data in source,
   logs, public examples, or test fixtures.
8. Test against synthetic fixtures or a provider-authorized test endpoint, not
   production data.

Do not infer status-code meanings from old prose where it conflicts with the
current helper enums. Record the evidence and resolve the version-specific
behavior with the integration partner before claiming interoperability.

## Independence and project listing

Adapters may be proprietary or open source, subject to their authors' chosen
licenses and their obligations to their own dependencies and providers. Use of
ODPAPI does not imply endorsement by GazillaByte. Authors who want their
adapter listed should follow [ECOSYSTEM.md](ECOSYSTEM.md) and provide public
project details with permission to publish them.
