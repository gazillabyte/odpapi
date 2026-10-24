#!/bin/sh
set -eu

: "${IRM_SFTP_HOST:?IRM_SFTP_HOST is required}"
: "${IRM_SFTP_USER:?IRM_SFTP_USER is required}"
: "${IRM_SFTP_KNOWN_HOSTS:?IRM_SFTP_KNOWN_HOSTS is required}"
: "${IRM_SFTP_PASSWORD:?IRM_SFTP_PASSWORD is required}"

state=/var/lib/ironmountain/last-import
interval=${IRM_POLL_SECONDS:-3600}
remote_root=${IRM_SFTP_ROOT:-}
port=${IRM_SFTP_PORT:-22}

apachectl -DFOREGROUND &
apache_pid=$!

cleanup() {
    kill "$apache_pid" 2>/dev/null || true
}
trap cleanup INT TERM EXIT

while kill -0 "$apache_pid" 2>/dev/null; do
    /usr/local/bin/ironmountain-sync \
        --host "$IRM_SFTP_HOST" \
        --port "$port" \
        --user "$IRM_SFTP_USER" \
        --password-env IRM_SFTP_PASSWORD \
        --known-hosts "$IRM_SFTP_KNOWN_HOSTS" \
        --remote-root "$remote_root" \
        --db-dir /var/lib/ironmountain \
        --state "$state" || true
    sleep "$interval" &
    wait $!
done

wait "$apache_pid"
