# Purpose and goals

The original ODPAPI specification addresses the lack of a common
communications protocol among enterprise data-protection systems. A typical
environment may include software controlling automated tape libraries,
backup software managing data and media, off-site storage vendors, and
coordination software connecting these systems.

Without a shared interface, each product pairing can require a bespoke
integration. ODPAPI aims to make integrations more portable and systems more
interchangeable.

The original goals are:

- Share object information between libraries.
- Allow objects to be moved between libraries through a common interface.
- Let compatible libraries and management systems be interchanged more easily.

The protocol's initial focus is data-protection media and library operations.
The specification refers to media as objects, so implementations can exchange
identifiers, types, locations, state, and movement information without
requiring the protocol to define how each vendor stores that information.

These goals are summarized from `documents/specification/Open Data Protection
API.docx` (2013–2014). The original document credits contributors from
GazillaByte, Hewlett-Packard, Fujitsu Australia, Databank Technologies, and
others. Contributor biographies and historical organization details are
omitted here; confirm attribution and reuse rights before republishing them.
