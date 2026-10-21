# Protocol overview

The original ODPAPI design uses stateless HTTP or HTTPS requests with JSON
request and response data. Authentication follows HTTP authentication; each
call is authenticated independently. The historical design does not define a
session-token exchange. Use HTTPS in deployments and supply credentials
through a secure runtime configuration rather than source files or examples.

Responses contain common result information for the request and may include
per-object results. A sequence number can associate each object result with
its corresponding request entry. A zero common result indicates success in
the original document; nonzero values indicate errors. Per-operation status
enumerations refine the result for individual objects.

## Libraries and GUIDs

The protocol identifies libraries with 128-bit GUIDs. The original design
requires uniqueness within a server scope, not global uniqueness. It permits
providers to encode local identifiers such as account, media type, or location
in a GUID. Those encodings are provider conventions: clients should not assume
that they can decode or compare identifiers across unrelated providers.

## Version and compatibility caution

The old specification includes different path forms (`/dpapi/v1/` and
`/dpapi-v1/`), an example with conflicting operation/query parameters, and
status descriptions that do not consistently match the C helper enums. The
historical response fixtures also include implementation-specific metadata.
These details need review against deployed implementations before they are
used as normative interoperability requirements.
