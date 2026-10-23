# Adapter interoperability contract

This document describes a technical integration contract for independent
ODPAPI adapters. It is not a legal agreement, certification, or guarantee of
compatibility. The Apache-2.0 license applies to the ODPAPI project materials;
it does not require an independently written adapter to be open source.

Adapter authors and providers should agree on the exact protocol version,
routes, fields, status values, authentication method, and error handling used
by their integration. These project materials do not define every provider's
deployment details.

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

## Query capability behavior

The Query operation is the capability-discovery entry point. A client must be
able to call the adapter's query endpoint without query arguments or a request
object. The successful response must identify the functions the adapter offers
in `response.functions`, using the provider's established function names. An
adapter may include additional disclosure or configuration information, but it
must not require an object identifier, library identifier, or request body just
to discover its capabilities. Authentication and authorization may still be
required by the deployment.

Check status-code meanings against the current helper enums and the provider's
versioned contract. Record discrepancies and resolve version-specific behavior
with the integration partner before claiming interoperability.

## Inventory optional fields

The standard inventory object may include the boolean fields `encrypted` and
`scratch`. `enum ODPAPISTATUS` represents these values: `UNKNOWN` (`-1`) omits
the field, `FALSE` (`0`) emits JSON `false`, and `TRUE` (`1`) emits JSON
`true`. An adapter must not claim a value merely to populate an optional
field. The Linux SCSI changer adapter therefore uses `UNKNOWN` for both fields
because the media-changer inventory does not report encryption or scratch-media
state.

Inventory objects may also include the numeric `slot` field. It is a 1-based
library element address. The helper omits it when the adapter passes `0`.

The List fields `moving_in`, `moving_out`, `scratch_total`, and
`scratch_minimum` are unsigned counts. Adapters that do not track those
counts report zero. These fields do not use `ODPAPISTATUS`; that enum is
reserved for tri-state boolean fields such as `encrypted` and `scratch` in
inventory objects.

## Independence and project listing

Adapters may be proprietary or open source, subject to their authors' chosen
licenses and their obligations to their own dependencies and providers. Use of
ODPAPI does not imply endorsement by GazillaByte. Authors who want their
adapter listed should follow [ECOSYSTEM.md](ECOSYSTEM.md) and provide public
project details with permission to publish them.
