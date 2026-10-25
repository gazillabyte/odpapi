#ifndef IRONMOUNTAIN_DATABASE_H
#define IRONMOUNTAIN_DATABASE_H

#include "ironmountain_report.h"

/** Write a media database and its secondary indexes from parsed records. */
int irm_database_load(const char *path, const struct irm_record *records,
	unsigned int count, char *error, size_t error_length);

/** Atomically publish media, library, and media-ID database snapshots. */
int irm_database_load_snapshot(const char *media_path, const char *library_path,
	const char *media_index_path, const struct irm_record *records,
	unsigned int count, char *error, size_t error_length);

/** Return non-zero when the snapshot state marker is ready for reads. */
int irm_database_snapshot_ready(const char *media_path);

/** Read one record by its composite customer-media key. */
int irm_database_get(const char *path, const char *key, struct irm_record *record);

/** Count all records in a media database. */
int irm_database_count(const char *path, unsigned int *count);

/** Callback used while visiting media records. Return zero to stop. */
typedef int (*irm_database_visit_fn)(const char *key, const struct irm_record *record, void *user);

/** Visit every media record in key order. */
int irm_database_visit(const char *path, irm_database_visit_fn visit, void *user);

/** Visit records assigned to one IN or OUT library key. */
int irm_database_visit_library(const char *library_path, const char *media_path,
	const char *library_key, irm_database_visit_fn visit, void *user);

/** Visit records matching one literal media identifier. */
int irm_database_visit_media_id(const char *media_index_path, const char *media_path,
	const char *media_id, irm_database_visit_fn visit, void *user);

/** Callback used while visiting library keys and their record counts. */
typedef int (*irm_library_visit_fn)(const char *key, unsigned int count, void *user);

/** Visit each indexed library and its current record count. */
int irm_database_visit_libraries(const char *path, irm_library_visit_fn visit, void *user);

#endif
