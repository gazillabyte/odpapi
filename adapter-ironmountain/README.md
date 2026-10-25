# `mod_odpapi_ironmountain.so`

This directory is the self-contained open-source Iron Mountain SecureSync
adapter project. It builds both the SFTP importer CLI and the Apache module,
and contains the Podman image setup used to run them together. It uses the
ODPAPI C helper library for request inspection and response construction.

This is an independent adapter. It is not maintained or endorsed by Iron
Mountain.

The checkout layout is:

```text
adapter-ironmountain/
  CMakeLists.txt
  include/
    ironmountain_report.h
    ironmountain_database.h
  src/
    ironmountain_sync.c
    mod_odpapi_ironmountain.c
    ironmountain_report.c
    ironmountain_database.c
  test/
  testdata/
  README.md
  LICENSE
```

The repository-root `Containerfile` and `podman/` directory build the image
when this adapter is built as part of the ODPAPI checkout. The Apache
configuration is `podman/apache/odpapi-ironmountain-adapter.conf`, installed as
`/etc/apache2/conf-available/odpapi-ironmountain-adapter.conf` and enabled with
`a2enconf odpapi-ironmountain-adapter`.

Users should be able to clone this directory, build the C targets, and build
the container from the same checkout. The container build must use the locally
built importer and module; it must not download customer-specific software or
embed credentials.

All examples and test fixtures in this document are fabricated. Do not commit
provider exports, customer records, copied report rows, credentials, or other
operational data; supply any endpoint details and secrets through runtime
configuration only.

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

The synthetic IRM fixture report is CSV with this header:

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
must never be stored in the Berkeley DB files or returned by the adapter. The synthetic reports also
contain compatibility rows with unquoted commas in employee names; the parser locates
the outbound date by its date shape and tolerates those extra employee-field
commas. The compatibility C struct is useful for identifying the fields, but is not
the wire format of this report.

The fixture report models tapes held by Iron Mountain. Each record is
classified by comparing its inbound and outbound dates. An outbound date equal
to or later than the inbound date places the tape in the synthetic account's
`CUSTOMERIDOUT` library; an inbound date later than the outbound date places it
in `CUSTOMERIDIN`. A missing outbound date preserves the default of
`CUSTOMERIDIN`, which supports an IN-only report.

Fixture consistency warning: keep the source inventory scope consistent across imports.
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
`build/mod_odpapi_ironmountain.so` when Apache development files are installed.
From the ODPAPI project root, the same checkout provides a Podman build:

```sh
podman build -f Containerfile -t localhost/odpapi-ironmountain-adapter:latest .
```

See [Run with Podman](#run-with-podman) below for startup arguments,
authentication, persistent storage, and the polling/cache lifecycle.

Configure Apache with:

```apache
LoadModule odpapi_ironmountain_module /absolute/path/to/mod_odpapi_ironmountain.so
IronMountainDatabaseDir /var/lib/ironmountain

<LocationMatch "^/dpapi-v1(?:/.*)?$">
    SetHandler odpapi-ironmountain-adapter
    AuthType Basic
    AuthName "ODPAPI Iron Mountain"
    AuthBasicProvider file
    AuthUserFile /etc/apache2/odpapi-users
    Require valid-user
</LocationMatch>
```

The configured Apache handler is read-only and serves the standard `/dpapi-v1`
base URL, including `/dpapi-v1/query`, `/dpapi-v1/list`,
`/dpapi-v1/inventory`, and `/dpapi-v1/find`. SFTP credentials are used only by
the importer.

Always protect the Apache location with the deployment's approved Apache
authentication policy. The checked-in Podman configuration uses Basic
authentication and expects `/etc/apache2/odpapi-users` to be created before
Apache starts. Another Apache authentication mechanism, such as LDAP or
client-certificate authentication, may be used instead. A reverse proxy does
not remove the requirement for authentication at Apache.

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

## Deployment security requirements

**Do not expose this adapter to the public internet. Always deploy it behind
a secure corporate firewall, with access restricted to approved internal
clients.** This applies to both the Apache endpoint and any reverse proxy
in front of it. HTTPS and authentication do not make public exposure an
approved deployment.

**Apache authentication is required for all ODPAPI endpoints**, including
Query. Keep the supplied Basic authentication configuration or deploy another
approved Apache authentication mechanism. Firewall restrictions and Apache
authentication are both required. Use HTTPS for client connections carrying
credentials or inventory data.

## Run with Podman

The image runs two components together: Apache serves ODPAPI requests from a
local Berkeley DB snapshot, and the entrypoint repeatedly runs the
`ironmountain-sync` CLI to refresh that snapshot over SFTP. API requests read
the local database; they do not connect to IRM or download inventory files.

### Prepare runtime configuration

Build the image from the repository root using the command above. Keep all
runtime configuration outside the checkout and container build context. For
example, create a private directory on the host:

```sh
mkdir -p "$HOME/.config/odpapi-irm"
chmod 700 "$HOME/.config/odpapi-irm"
```

Create `irm.env` in that directory with the following settings. Replace the
fabricated host, account, remote root, and password placeholder with your
runtime configuration. The password is the literal value after `=`; this file
is read by Podman, not sourced as a shell script.

```text
IRM_SFTP_HOST=sftp.example.invalid
IRM_SFTP_PORT=22
IRM_SFTP_USER=example-importer
IRM_SFTP_PASSWORD=REPLACE_AT_RUNTIME
IRM_SFTP_KNOWN_HOSTS=/run/irm/known_hosts
IRM_SFTP_ROOT=/reports
IRM_POLL_SECONDS=3600
```

```sh
chmod 600 "$HOME/.config/odpapi-irm/irm.env"
```

| Setting | Required/default | Meaning |
|---|---|---|
| `IRM_SFTP_HOST` | Required | SFTP hostname |
| `IRM_SFTP_USER` | Required | SFTP account name |
| `IRM_SFTP_PASSWORD` | Required | SFTP password; passed to the CLI through its environment |
| `IRM_SFTP_KNOWN_HOSTS` | Required | Path **inside the container** to a mounted OpenSSH-format `known_hosts` file |
| `IRM_SFTP_PORT` | `22` | SFTP port |
| `IRM_SFTP_ROOT` | Empty | Directory containing `archive`; empty means `archive` relative to the SFTP login directory |
| `IRM_POLL_SECONDS` | `3600` | Sleep in seconds after each CLI run; use a positive integer |

With `/reports` as the remote root, the importer scans
`/reports/archive/inventory_*`. Do not include `/archive` in `IRM_SFTP_ROOT`;
the importer appends it. The current image uses SFTP password authentication.

Place the provider's verified SSH host-key entry in
`$HOME/.config/odpapi-irm/known_hosts`. Verify its fingerprint with the provider
through a trusted channel before use. For a non-default port, use the
OpenSSH `[hostname]:port` entry format. Unknown or changed host keys cause the
import to fail.

Create a separate HTTP Basic authentication file for API clients. With the
host's `htpasswd` utility installed (for example, from `apache2-utils` on
Debian/Ubuntu), run:

```sh
htpasswd -cB "$HOME/.config/odpapi-irm/odpapi-users" example-api-user
chmod 644 "$HOME/.config/odpapi-irm/odpapi-users"
```

`htpasswd` prompts for the API password. Use `-c` only when creating a new
file; omit it when adding users to an existing file. The file contains password
hashes and must be readable by Apache's worker inside the container. The
private parent directory restricts host access. SFTP credentials and API
credentials serve separate purposes.

### Start the container

```sh
podman volume create ironmountain-state
podman run -d \
  --name odpapi-ironmountain-adapter \
  --restart=unless-stopped \
  --env-file "$HOME/.config/odpapi-irm/irm.env" \
  -p 127.0.0.1:8080:80 \
  -v ironmountain-state:/var/lib/ironmountain \
  -v "$HOME/.config/odpapi-irm/known_hosts:/run/irm/known_hosts:ro,Z" \
  -v "$HOME/.config/odpapi-irm/odpapi-users:/etc/apache2/odpapi-users:ro,Z" \
  localhost/odpapi-ironmountain-adapter:latest
```

| Argument | Purpose |
|---|---|
| `--name odpapi-ironmountain-adapter` | Names the container for logs, exec, stop, and start commands |
| `--restart=unless-stopped` | Restarts the container after it exits unless explicitly stopped |
| `--env-file ...` | Supplies the importer settings and SFTP password at runtime |
| `-p 127.0.0.1:8080:80` | Publishes container HTTP port 80 on host loopback port 8080 |
| `-v ironmountain-state:/var/lib/ironmountain` | Persists the database, download, and checkpoint across container replacement |
| `-v ...:ro,Z` | Mounts each configuration file read-only and applies a private SELinux label where supported |

The image's entrypoint takes configuration from these environment variables;
it does not forward arguments after the image name to the importer. To change
the polling interval, set `IRM_POLL_SECONDS`, rather than adding a CLI flag to
`podman run`.

The example exposes HTTP only on the host's loopback interface. For approved
remote corporate clients, put an internal HTTPS reverse proxy in front of it,
behind the corporate firewall, and retain Apache authentication. Do not
publish the adapter or proxy on a public interface accessible from the
internet. The image itself does not configure TLS. Treat the env file and
container administration access as sensitive because the container
environment contains the SFTP password.

Follow startup and import results, then query the authenticated endpoint:

```sh
podman logs -f odpapi-ironmountain-adapter
curl --user example-api-user http://127.0.0.1:8080/dpapi-v1/query
curl --user example-api-user http://127.0.0.1:8080/dpapi-v1/list
```

Each `curl` command prompts for the API password. Query reports capabilities;
List verifies access to the imported libraries after a successful import.
Before the first successful import, database-backed requests cannot return
inventory. Use `podman stop odpapi-ironmountain-adapter` and
`podman start odpapi-ironmountain-adapter` for
routine shutdown and startup. Changes to the env file require recreating the
container with the updated `podman run` command; reuse the named state volume.

### Polling, downloads, and the local cache

1. On startup, the entrypoint starts Apache and immediately invokes
   `ironmountain-sync`. It does not wait one interval before the first check.
2. The CLI connects to SFTP, verifies the host key, and lists the remote
   `archive` directory. It selects the `inventory_*` file with the newest
   remote modification time; ties are resolved by the lexically greatest
   filename.
3. It compares that file's **name, modification time, and size** with
   `/var/lib/ironmountain/last-import`. If all three match, it prints
   `no new inventory file` and skips both the download and database rebuild.
   Each poll still connects to SFTP and reads directory metadata. There is no
   content-hash comparison; a content change preserving all three values will
   not be detected.
4. When the checkpoint is absent or differs, the CLI downloads the selected
   report, parses it, and publishes the local Berkeley DB snapshot. It prints
   `loaded N records` and writes the checkpoint after a successful import.
   Failed imports do not advance the checkpoint, so a later poll can retry.
5. The entrypoint sleeps for `IRM_POLL_SECONDS` after the CLI exits, then runs
   it again. The time between check starts is therefore the import/check
   duration plus that interval. A failed CLI run is logged and the loop
   continues while Apache is running.

Apache reads the shared database directory for every inventory/lookup request.
The importer takes an exclusive lock during its run, including SFTP discovery
and download; the adapter uses a shared lock when reading. A locked,
unavailable, or incomplete snapshot produces an endpoint-failure result and
clients should retry. If SFTP fails before a new publication starts, an
existing ready snapshot remains available after the lock is released. It can
be stale: a successful API read alone does not prove the latest SFTP poll
succeeded. Monitor importer logs and the checkpoint for refresh health.

The persistent volume contains:

| Path under `/var/lib/ironmountain` | Purpose |
|---|---|
| `IRMMEDIA.db` | Imported media records |
| `IRMLIBRARY.db` | Library index |
| `IRMMEDIAID.db` | Media-number index for Find |
| `last-import` | Last successfully imported remote filename, modification time, and size |
| `IRMMEDIA.db.lock` | Importer/adapter coordination lock |
| `IRMMEDIA.db.state` | Snapshot publication status (`publishing` or `ready`) |
| `IRMMEDIA.db.download` | Most recently downloaded report, retained locally; Apache serves the databases |

Preserve the databases and checkpoint together when moving or restoring state.
An empty state volume triggers a download on the next check. A restart with
the existing volume normally skips downloading an unchanged remote file.
The checkpoint comparison does not validate the database files: retaining
`last-import` while deleting the databases can cause an unchanged report to
be skipped even though the adapter has no usable snapshot. For an intentional
full rebuild, stop the container and move `last-import` aside in the state
volume before restarting; keep the previous database files until the importer
replaces them successfully. Protect the volume as inventory data, including
the retained report download.

## Importer runtime contract

The Podman image contains a command-line importer linked with `libssh`.
The SFTP host, port, account, credential material, remote root, and local
database path are supplied to Podman at runtime; none are compiled into the
image.

The importer follows the documented fixture convention:

```text
remote root/archive/inventory_*
```

It lists `archive` under the remote root, selects the newest matching
inventory file by remote modification time, and compares its filename,
modification time, and size with the last successfully imported file. The
container persists that checkpoint at `/var/lib/ironmountain/last-import` in
the mounted application state directory. No hash is currently stored or
compared. A lock prevents two importers from running simultaneously.

The importer accepts one `--db-dir` option. It creates/updates
`IRMMEDIA.db`, `IRMLIBRARY.db`, `IRMMEDIAID.db`, and the import lock/download
files within that directory. The Apache module uses the same directory through the
`IronMountainDatabaseDir` directive.

Use `--verbose` (or `-v`) for high-level SFTP diagnostics, including connection
and host-key verification, archive scanning, candidate selection, checkpoint
decisions, download, and snapshot publication. Individual records and
credentials are not logged.
The standard container polling loop does not enable `--verbose`; its default
logs report unchanged files, successful record counts, and errors.

The Berkeley DB media primary key is:

```text
CUSTOMERID-VOLUMEID
```

`CustomerID` and `Media#` identify the first record retained for an IRM
lookup, but the reports can contain later rows with the same pair. The loader
keeps the first occurrence, reports every later occurrence to stderr, and
continues loading the snapshot. This preserves the documented first-row
behavior while making duplicate fixture data visible to operators.

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
