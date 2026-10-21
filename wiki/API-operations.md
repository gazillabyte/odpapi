# API operations

The 2013–2014 specification names the operations below. Their descriptions
are summarized from that document; exact routes, payload schemas, validation,
and status semantics must be taken from a reconciled, versioned specification.

| Operation | Purpose in the historical specification |
| --- | --- |
| `inventory` | Return objects held by a library and their current state. |
| `move` | Request movement of objects from one library to another. |
| `scan` | Report that objects were scanned at a library, including a scan reason. |
| `add` | Add object records to a library. |
| `update` | Update object information. |
| `list` | Enumerate libraries available to the authenticated caller. |
| `query` | Report which API functions or capabilities are available. |

## Result model

The original model separates common request results from per-object results.
Common codes cover conditions such as malformed JSON, missing arguments,
authentication failure, unavailable operations, and unknown libraries.
Operation-specific status values describe an individual object's result.

The C helper exposes enums for common, inventory, move, update, scan, and add
statuses. Some historical prose tables disagree with those enum values. Use the
current header as implementation evidence, not as a substitute for resolving
the protocol specification's versioning and compatibility policy.
