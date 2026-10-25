FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config apache2-dev libapr1-dev \
    libdb-dev libssh-dev libcsv-dev libcjson-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src
RUN cmake -S adapter-ironmountain -B build/ironmountain \
      -DODPAPI_SOURCE_DIR=/src -DODPAPI_FETCH_CJSON=OFF \
      -DODPAPI_BUILD_DOCS=OFF -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build/ironmountain --parallel \
    && cmake --install build/ironmountain --prefix /opt/ironmountain

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    apache2 libdb5.3 libssh-4 libcsv3 libcjson1 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /var/lib/ironmountain

COPY --from=build /opt/ironmountain/bin/ironmountain-sync /usr/local/bin/ironmountain-sync
COPY --from=build /opt/ironmountain/lib/mod_ironmountain.so /usr/lib/apache2/modules/mod_ironmountain.so
COPY podman/apache/odpapi-ironmountain-adapter.conf /etc/apache2/conf-available/odpapi-ironmountain-adapter.conf
COPY podman/entrypoint.sh /usr/local/bin/ironmountain-entrypoint

RUN a2enconf odpapi-ironmountain-adapter && chmod 0755 /usr/local/bin/ironmountain-entrypoint

VOLUME ["/var/lib/ironmountain"]
EXPOSE 80
ENTRYPOINT ["/usr/local/bin/ironmountain-entrypoint"]
