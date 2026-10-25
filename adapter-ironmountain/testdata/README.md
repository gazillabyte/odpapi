# Synthetic IRM test data

Everything in this directory is fabricated test data. Do not replace it with
provider exports or operational records, and do not add real credentials to
the fixture or its examples.

This directory is a safe, synthetic SecureSync-style SFTP fixture. The expected
remote layout is:

    <remote-root>/
    └── archive/
        ├── inventory_20261022.csv
        ├── inventory_20261023.csv
        └── inventory_20261024.csv

Each report starts with two synthetic customer accounts (`00001` and `00002`)
and 50 tapes per account (`GZB001L5` through `GZB050L5`). Later snapshots add
two new tapes per account, so the final report contains 54 tapes per account.
The 20 rotating descriptions are IT backup/archive names. Every fifth tape has
a synthetic `Rtn-date`, which is exposed as `next_move`. The reports also
exercise the date classification rules: `out > in` is OUT, `in > out` is IN,
and a missing OUT date is IN.

- 2026-10-22: 50 tapes per account are in storage with no OUT date.
- 2026-10-23: two tapes per account are added; selected tapes have IN
  `10/20/2026` and OUT `10/21/2026`.
- 2026-10-24: two more tapes per account are added; selected tapes have OUT
  `10/22/2026` after IN `10/20/2026`, while returned tapes have IN
  `10/22/2026` after OUT `10/21/2026`.

Employee names are deliberately comical and synthetic. They are parsed only to
exercise the report format; the adapter does not retain or return them.

## Run a local synthetic SFTP server

From the repository root, expose the fixture as the `archive` directory with
Podman:

    podman run --rm --name irm-sftp-fixture -p 2222:22 \
      -v "$PWD/adapter-ironmountain/testdata/archive:/home/irmtest/archive:ro" \
      docker.io/atmoz/sftp irmtest:testpassword:::archive

Configure the importer to use host `127.0.0.1`, port `2222`, user `irmtest`,
and the password from a runtime environment variable. The importer should use
`archive` as its remote root and a temporary database directory for tests.

