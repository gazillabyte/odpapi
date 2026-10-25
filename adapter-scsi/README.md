# ODPAPI Linux SCSI media-changer adapter

This adapter is a read-only Apache module for Linux SCSI media changers. It
probes explicitly configured `/dev/sch*` devices using the Linux changer API
(`CHIOGPARAMS`, `CHIOGSTATUS`, and `CHIOGELEM`) and exposes ODPAPI v1 JSON
endpoints from a Podman image.

It does not log in to iSCSI, rescan the host, create device nodes, reserve the
changer, or move media. Host administration and the backup/library-management
software remain responsible for those operations.

All identifiers, device paths, credentials, and payloads shown here are
examples only. Keep operational inventory, device captures, and secrets out of
the repository; provide them through runtime configuration or local test
fixtures.

## Deployment security requirements

**Do not expose this adapter to the public internet. Always deploy it behind
a secure corporate firewall, with access restricted to approved internal
clients.** This applies to both the Apache endpoint and any reverse proxy
in front of it. HTTPS and authentication do not make public exposure an
approved deployment.

Protect all ODPAPI endpoints, including Query, with the deployment's approved
authentication and authorization policy. Use HTTPS for client connections
carrying credentials or inventory data.

## ODPAPI v1 endpoints

The routes use the ODPAPI v1 format:

| Route | Purpose |
| --- | --- |
| `GET /dpapi-v1/query[.json]` | With no arguments, reports the functions `query`, `list`, `find`, and `inventory`. |
| `GET /dpapi-v1/list[.json]` | Lists logical libraries derived from each configured changer. |
| `GET /dpapi-v1/find[.json]?id=<volume-id>` | Returns the logical library GUID containing a tape. |
| `GET /dpapi-v1/inventory[.json]?source_library=<library-guid>` | Reports the inventory for one logical library. |

`GET /dpapi-v1` and `GET /dpapi-v1/` return an ODPAPI unknown-route response
with a hint directing the client to `/dpapi-v1/query.json`. The Query
capability response is returned only by
`query.json`.

POST is also accepted. Its JSON `request` object may provide `id` or
`source_library`; URL arguments take precedence. The canonical format is the
GET-based `/dpapi-v1/*.json` format above. `source_library` is required. A missing value returns result `8`; an invalid
UUID returns result `7`; an unknown configured library returns result `100`.

Responses use the ODPAPI helper library for the response envelope and List,
Find, and inventory result fields. Unknown tape IDs return an empty
`response.objects` array. Omitting `id` returns a missing-argument response.
Query intentionally requires no arguments or request body and returns the
supported function names under `response.functions`.

## Library mapping

Each changer is represented as separate logical libraries:

- `Storage`: storage slots; capacity is the number of slots.
- `CAP/IO`: import/export portals, when present; this is not an ODPAPI
  Pass-through library.
- `Drive-N: <vendor> <model> (/dev/stN)`: one object per data-transfer
  element; capacity is `1` and usage is `1` when occupied. Model text is read
  from the matching Linux `/sys/class/scsi_tape/stN` device when available.
  The adapter matches the changer element's SCSI target/LUN to sysfs when the
  changer driver reports it; drivers that do not report target/LUN use the
  element-order mapping as a documented best-effort fallback.
- `Transport`: the changer transport/picker elements.

The `targets` field contains the stable GUIDs of the other logical libraries
that can be destinations in the changer model. The transport targets storage,
CAP/IO, and drives. A changer without portals has no CAP/IO object.

Descriptions are `Storage`, `CAP/IO`, `Drive-N: <vendor> <model> (/dev/stN)`,
and `Transport`. Inventory objects include the 1-based SCSI element address in
the standard `slot` field. The
scope for all objects belonging to one changer is the discovered vendor/model
description, for example `Example Media Changer`. ODPAPI scope is
application-defined metadata; it has no universal hardware, routing, or
authorization meaning.

When a tape is resident in a drive library, its inventory object also includes
the drive description (vendor/model and `/dev/stN` path). Storage-slot and
CAP/IO inventory objects do not include a description.

Each `SCSIChangerDevice` directive supplies a changer device path and optional
media type. The adapter discovers a stable changer serial number from SCSI VPD
data and uses it as the identity namespace. Logical library GUIDs are derived
from that identity and the element class. Drive serial numbers are preferred
for drive identities. If serial data is unavailable, the relevant `/dev` path
is used as a fallback and the resulting stability limitation is logged.
The current probe data is sufficient for a future move operation to build an
in-memory GUID-to-device-element mapping, allowing source and target GUIDs to
be resolved back to Linux media-changer elements. That mapping would be
rebuilt from the current SCSI probe results after a restart.

## Build and test

Native prerequisites are CMake 3.21+, a C99 compiler, Apache and APR
development headers, `pkg-config`, and Linux SCSI changer headers.

Run from the repository root:

```sh
cmake -S adapter-scsi -B build/scsi \
  -DODPAPI_SOURCE_DIR="$PWD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON
cmake --build build/scsi --parallel
ctest --test-dir build/scsi --output-on-failure
```

The built module is `build/scsi/mod_odpapi_scsi.so`; the Apache source is
`src/mod_odpapi_scsi.c`. Unit tests do not require SCSI hardware.

## Apache configuration

The image installs `podman/apache/odpapi-scsi-adapter.conf` from this adapter
directory as `/etc/httpd/conf.d/odpapi-scsi-adapter.conf`.

`SCSIChangerDevice` takes a device path and an optional media type. If the
media type is omitted, inventory uses `"tape"`. Add one line per changer:

```apache
LoadModule odpapi_scsi_module /etc/httpd/modules/mod_odpapi_scsi.so
SCSIChangerDevice /dev/sch0 LTO

<Location /dpapi-v1>
    SetHandler odpapi-scsi-adapter
    AuthType Basic
    AuthName "SCSI inventory"
    AuthBasicProvider file
    AuthUserFile /etc/httpd/scsi.htpasswd
    Require valid-user
</Location>
```

The module does not implement authentication or read credentials. Apache must
use the configured Basic authentication file, or an approved LDAP, mTLS, or
authenticated reverse proxy policy. Create `/etc/httpd/scsi.htpasswd` before
starting Apache. Use HTTPS for real credentials.

If more than one configured changer reports the same barcode, `find` returns
all matching logical library GUIDs. A failed changer produces a request-wide
endpoint failure; successfully read objects may remain as partial results and
callers should retry after correcting the device.

## Build the Podman image

Build from the repository root because the Containerfile copies the complete
source tree as its context:

```sh
podman build \
  --file adapter-scsi/Containerfile \
  --tag localhost/odpapi-scsi-adapter:latest \
  .
```

The multi-stage UBI build compiles the Apache module and runs CTest. Use
`--no-cache` for a clean rebuild.

## Deploy with Podman

The image listens on container port `8080`. Host port `8080` may already be in
use, so this example publishes host port `18080` on loopback only:

```sh
podman rm -f odpapi-scsi-adapter 2>/dev/null || true
podman run --detach \
  --name odpapi-scsi-adapter \
  --userns=keep-id \
  --group-add=keep-groups \
  --security-opt label=disable \
  --device=/dev/sch0:/dev/sch0:r \
  --publish 127.0.0.1:18080:8080 \
  localhost/odpapi-scsi-adapter:latest
```

The runtime user needs read access to `/dev/sch0` (often through the host
`cdrom` group). Verify with `ls -l /dev/sch0` and `id`. Pass additional changer
nodes with more `--device` options and add matching `SCSIChangerDevice` lines
to `podman/apache/odpapi-scsi-adapter.conf` in this adapter directory before
building the image. Do not use `--privileged`.

Test the endpoints with:

```sh
curl -X GET http://127.0.0.1:18080/dpapi-v1/query.json
curl -X GET http://127.0.0.1:18080/dpapi-v1/list.json
curl -X GET http://127.0.0.1:18080/dpapi-v1/inventory.json
curl -G --data-urlencode 'id=TAPE001' \
  http://127.0.0.1:18080/dpapi-v1/find.json
```

For approved remote corporate clients, use an internal HTTPS reverse proxy
behind the corporate firewall, with the approved authentication policy. If
binding the adapter directly to a private host interface instead, restrict
host firewall access to approved client addresses. Do not publish the adapter
or proxy on a public interface accessible from the internet or open the host
port to unrestricted traffic.

## Concurrency and limitations

The adapter opens the changer read-only and does not coordinate with other
media-management software. Inventory can therefore race with a move
performed by another controller and may be momentarily stale. Do not add
movement or reservation behavior without an explicit ownership and locking
design.
