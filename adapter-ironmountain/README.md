# `mod_ironmountain.so`

This directory is the self-contained open-source Iron Mountain SecureSync
adapter project. It builds both the SFTP importer CLI and the Apache module,
and contains the Podman image setup used to run them together. It uses the
ODPAPI C helper library for request inspection and response construction.

The checkout layout is:

```text
adapter-ironmountain/
  CMakeLists.txt
  include/
  src/
    ironmountain-sync.c
    mod_ironmountain.c
    irm_report.c
    irm_database.c
  test/
  testdata/
  README.md
  LICENSE
```

The repository-root `Containerfile` and `podman/` directory build the image
when this adapter is built as part of the ODPAPI checkout.

Users should be able to clone this directory, build the C targets, and build
the container from the same checkout. The container build must use the locally
built importer and module; it must not download customer-specific software or
embed credentials.

## Current scope

The module accepts GET requests with URL arguments and POST requests with an
ODPAPI JSON `request` object. For POST, `source_library` and `id` may be
members of the request object; URL arguments take precedence. It returns a
standard ODPAPI JSON response. SecureSync is a read-only, report-backed
adapter: the importer reads timestamped IRM inventory files over SFTP and the
Apache module reads the resulting local Berkeley DB snapshot. It exposes only:

- library listing;
- tape inventory/listing; and
- local tape lookup (`find`).

Move, add, update, and scan operations are unsupported because SecureSync does
not provide a write API.

## IRM inventory semantics

The inventory report observed in the IRM fixtures is CSV with this header:

```text
Cust #,Media#,Logical Vault,DRP,Rtn-date,MT,Description,CONT/OM,Slot #,Date In,In Employee,Date Out,Out Employee
```

The importer must parse the 13 CSV columns (including quoted CSV values),
including customer ID, media ID, return date, media type, description,
container/OM, slot, inbound date, inbound employee, outbound date, and
outbound employee. The logical-vault, DRP, and container/OM columns are not
retained in the database. It must use
bounded field storage and
must not depend on NUL termination in remote records. Employee columns are
used only while locating the outbound date and are discarded; employee names
must never be stored in the Berkeley DB files or returned by the adapter. The test reports also
contain legacy rows with unquoted commas in employee names; the parser locates
the outbound date by its date shape and tolerates those extra employee-field
commas. The legacy C struct is useful for identifying the fields, but is not
the wire format of this report.

The report may contain only tapes currently at Iron Mountain. Each record is
classified by comparing its inbound and outbound dates. An outbound date equal
to or later than the inbound date places the tape in the customer's
`CUSTOMERIDOUT` library; an inbound date later than the outbound date places it
in `CUSTOMERIDIN`. A missing outbound date preserves the default of
`CUSTOMERIDIN`, which supports an IN-only report.

Operational warning: keep the source inventory scope consistent across imports.
If the adapter starts with a complete inventory containing both IN and OUT
records and later switches to an IN-only inventory, OUT records will not
receive subsequent source updates and can remain stale from the consumer's
perspective. Treat that change as a data-source migration requiring an
explicit refresh or reconciliation plan.

The adapter always exposes two libraries per customer ID found in the report,
including an empty library when that customer currently has no tapes in that
state:

```text
CUSTOMERIDIN
CUSTOMERIDOUT
```

Each library has a deterministic UUID derived from the normalized IRM customer
ID and the suffix `IN` or `OUT`. The same customer and state therefore produce
the same library identifier across imports and restarts.

Each library's `targets` array contains the UUID of the customer's opposite
library: IN targets OUT, and OUT targets IN.

The ODPAPI `scope` field is application-defined metadata. For this adapter it
is the normalized IRM Client # (`Cust #`) alone, identifying the customer's
namespace. It is not the library suffix, hardware identity, routing key, or
authorization boundary. When a later report row repeats the same scoped media
identifier, the importer keeps the first row and reports the duplicate.

## ODPAPI volume mapping

The standard ODPAPI inventory object can represent only part of the IRM row:

| IRM field | ODPAPI field | Mapping |
|---|---|---|
| `Media#` | `id` | Report the IRM media number only. The importer retains `CustomerID-Media#` as its internal Berkeley DB key; `scope` is the Client # |
| `MT` | `type` | Direct media-type value |
| `Description` | `description` | Direct value; do not replace it with a generated description when present |
| `Rtn-date` | `next_move` | Optional date-only scheduled return date; omitted when blank |
| latest of `Date In` / `Date Out` | `last_move_time` | Optional date-only value; no time is available in the report |
| `Slot #` | `slot` | Report the 1-based slot when present; zero/blank is omitted by the standard helper |
| no IRM equivalent | `target` | Omit |
| derived library | `library` | Omit for normal `OK` inventory items; the request/list context identifies the library |
| no IRM equivalent | `encrypted`, `scratch` | Omit both fields by reporting `ODPAPISTATUS_UNKNOWN`; the report does not establish either property |

`Logical Vault`, `DRP`, `CONT/OM`, `In Employee`, and
`Out Employee` have no defined ODPAPI volume fields. Logical-vault and DRP
values are not imported, and employee fields are not captured at all. The
other unmapped fields may be retained in the local
Berkeley DB value for lookup/audit, but must not be silently forced into
unrelated protocol fields. The same applies to the full inbound/outbound
transaction details when only a date can be represented by ODPAPI.

## Build

From this directory, with Apache development headers and APR installed:

```sh
cmake -S . -B build -DODPAPI_SOURCE_DIR=..
cmake --build build
```

The resulting artifacts will be `build/ironmountain-sync` and
`build/mod_ironmountain.so` when Apache development files are installed.
From the ODPAPI project root, the same checkout provides a Podman build:

```sh
podman build -f Containerfile -t mod-ironmountain .
```

Configure Apache with:

```apache
LoadModule ironmountain_module /absolute/path/to/mod_ironmountain.so
IronMountainDatabaseDir /var/lib/ironmountain
```

The configured Apache handler is read-only and serves the standard `/dpapi-v1`
base URL, including `/dpapi-v1/query`, `/dpapi-v1/list`,
`/dpapi-v1/inventory`, and `/dpapi-v1/find`. SFTP credentials are used only by
the importer.

Protect the Apache location with the deployment's approved authentication
policy. The checked-in Podman configuration uses Basic authentication and
expects `/etc/apache2/odpapi-users` to be created before Apache starts. LDAP,
mTLS, or an authenticated reverse proxy may be used instead.

Inventory requests require `source_library=<library-guid>` and return only
records assigned to that library. An unscoped inventory request is rejected.
Find requests require `id=<Media#>` and return the GUIDs of the libraries
containing that media number. An unknown media number returns `result: 0` with
an empty `objects` array. The media number is matched literally, not as a
regular expression or `fnmatch` pattern.

If the Berkeley DB snapshot is unavailable, locked, or marked as incomplete,
the adapter returns a request-wide endpoint-failure result and no inventory is
claimed to be current; callers should retry after the importer completes. The
importer keeps the first duplicate `CustomerID-Media#` record and reports later
duplicates to its diagnostic output.

IRM does not report live movement or scratch totals. The adapter passes
`UINT_MAX` for `capacity_total`, `moving_in`, `moving_out`, `scratch_total`,
and `scratch_minimum`; the shared `libodpapi` List helper omits those fields.
`capacity_used` remains the number of records in the library.

## Importer runtime contract

The Podman image will contain a command-line importer linked with `libssh`.
The SFTP host, port, account, credential material, remote root, and local
database path are supplied to Podman at runtime; none are compiled into the
image.

The importer follows the existing IRM CLI convention:

```text
remote root/archive/inventory_*
```

It lists the remote directory, changes to `archive`, selects the newest
matching inventory file, and compares it with the last successfully imported
file. The checkpoint should be persisted under the mounted application state
directory (filename, remote timestamp, size, and preferably a hash) rather
than relying solely on an inherited environment variable. A lock prevents two
importers from publishing simultaneously.

The importer accepts one `--db-dir` option. It creates/updates
`IRMMEDIA.db`, `IRMLIBRARY.db`, and the import lock/download files within that
directory. The Apache module uses the same directory through the
`IronMountainDatabaseDir` directive.

Use `--verbose` (or `-v`) for high-level SFTP diagnostics, including connection
and host-key verification, archive scanning, candidate selection, checkpoint
decisions, download, and snapshot publication. Individual records and
credentials are not logged.

The Berkeley DB media primary key is:

```text
CUSTOMERID-VOLUMEID
```

`CustomerID` and `Media#` identify the first record retained for an IRM
lookup, but the reports can contain later rows with the same pair. The loader
keeps the first occurrence, reports every later occurrence to stderr, and
continues loading the snapshot. This preserves the historical first-row
behavior while making duplicate data visible to operators.

The value contains the parsed inbound, outbound, and optional return-date
structures and an integer flags field. The `IN` flag is set when the record is in the
`CUSTOMERIDIN` library; when the flag is clear, the record is in
`CUSTOMERIDOUT`. The legacy date rule determines the flag: `outdate < indate`
sets `IN`; `outdate >= indate` clears it; a missing outbound date preserves
`IN`. This also supports reports that contain only IN tapes.

IRM supplies date-only values. If a date must be converted to `time_t`, use
validated noon UTC rather than midnight. Timezone changes are expected to be
uncommon and are assumed not to exceed 11 hours; this does not affect the
direct `date_t` comparisons used for the IN/OUT rule.

The importer publishes three database files together:

- `IRMMEDIA.db` — authoritative media records keyed by `CustomerID-Media#`;
- `IRMLIBRARY.db` — Berkeley DB duplicate-key secondary index (AIX), keyed by
  exactly six customer bytes followed by one state byte (`I` for IN or `O`
  for OUT), whose duplicate values are the primary `CustomerID-Media#` keys.
  Counts are calculated with a cursor at read time.
- `IRMMEDIAID.db` — Berkeley DB duplicate-key secondary index (AIX), keyed by
  the IRM `Media#`, whose duplicate values are the primary
  `CustomerID-Media#` keys. The `find` operation reads this index rather than
  scanning `IRMMEDIA.db`.

The AIX contains one duplicate entry per loaded volume. Its duplicate values
are followed back into `IRMMEDIA.db` when volume records for a library are
listed; the AIX never stores a second copy of the volume struct. Empty
counterpart libraries are represented by the adapter's library enumeration,

The importer and Apache adapter coordinate through `IRMMEDIA.db.lock`: the
importer takes an exclusive lock while publishing a snapshot and the adapter
takes a shared lock while reading it. Database files are readable by the
Apache worker; protect the database directory with filesystem permissions and
the deployment's HTTP authentication. `IRMMEDIA.db.state` is published as
`publishing` during a multi-file update and changed to `ready` only after all
three databases are renamed. The adapter refuses to serve a snapshot whose
state is not ready, preventing a partial publication from being exposed after
an interrupted import. A subsequent successful import restores the ready
state. Empty counterpart libraries are not fabricated as AIX records.
