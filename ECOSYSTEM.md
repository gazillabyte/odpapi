# ODPAPI adapter ecosystem

We welcome independent adapters built against ODPAPI, including commercial and
closed-source implementations. The Apache-2.0 license for this project does
not require adapters to publish their source code.

If you maintain an adapter and would like GazillaByte to list it here, open a
GitHub issue using the **Adapter project listing** template. Include only
information that is already public and that you approve us to republish:

- Project and maintainer name.
- Public repository or product page.
- ODPAPI protocol version and operations implemented.
- Supported platforms or integration targets.
- Adapter license (or state that it is proprietary).
- Optional public contact or support link.

GazillaByte may add the project to this page or the project wiki. A listing is
informational and does not imply testing, certification, endorsement, or a
support commitment. Maintainers can request corrections or removal at any
time.

## Listed adapters

The following open-source adapters are maintained in this repository under
the project's Apache-2.0 license. Both implement ODPAPI v1 and use the shared
`libodpapi` helpers.

| Adapter | Integration target | Supported operations | Documentation |
| --- | --- | --- | --- |
| Iron Mountain SecureSync | Read-only, report-backed inventory imported over SFTP into a local Berkeley DB snapshot and served through Apache | Query, List, Find, Inventory | [Build, configuration, and limitations](adapter-ironmountain/README.md) |
| Linux SCSI media changer | Read-only inventory of configured Linux SCSI media changers, served through Apache with Podman deployment support | Query, List, Find, Inventory | [Build, configuration, and limitations](adapter-scsi/README.md) |

Neither adapter supports Move, Add, Update, or Scan.

The Iron Mountain SecureSync adapter is an independent project and is not
maintained or endorsed by Iron Mountain.
