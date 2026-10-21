# Open Data Protection API

ODPAPI is an open protocol for exchanging object and library information
between information owners and custodians. It was designed to reduce the
number of one-off integrations between tape library management systems,
backup software, off-site storage providers, and systems that coordinate them.

This project maintains the protocol and a small C helper library. It does not
include the TapeTrack adapter, which is a separate closed-source implementation.

## Start here

- [Purpose and goals](Purpose-and-goals)
- [Protocol overview](Protocol-overview)
- [Integration guidance](Integration-guidance)
- [API operations](API-operations)
- [Community adapter projects](Adapter-projects)

The current checked-in specification is a historical 2013–2014 document. It
is useful background, but some sections are incomplete and examples conflict
with one another or with the current C helper enums. Treat these pages as
maintained orientation, not a substitute for a versioned normative protocol
specification. The project needs to reconcile those differences before making
the legacy details normative.
