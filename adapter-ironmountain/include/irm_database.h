#ifndef IRM_DATABASE_H
#define IRM_DATABASE_H

#include "irm_adapter.h"

int irm_database_load(const char *path, const struct irm_record *records,
	unsigned int count, char *error, size_t error_length);
int irm_database_load_snapshot(const char *media_path, const char *library_path,
	const char *media_index_path, const struct irm_record *records,
	unsigned int count, char *error, size_t error_length);
int irm_database_snapshot_ready(const char *media_path);
int irm_database_get(const char *path, const char *key, struct irm_record *record);
int irm_database_count(const char *path, unsigned int *count);
typedef int (*irm_database_visit_fn)(const char *key, const struct irm_record *record, void *user);
int irm_database_visit(const char *path, irm_database_visit_fn visit, void *user);
int irm_database_visit_library(const char *library_path, const char *media_path,
	const char *library_key, irm_database_visit_fn visit, void *user);
int irm_database_visit_media_id(const char *media_index_path, const char *media_path,
	const char *media_id, irm_database_visit_fn visit, void *user);
typedef int (*irm_library_visit_fn)(const char *key, unsigned int count, void *user);
int irm_database_visit_libraries(const char *path, irm_library_visit_fn visit, void *user);

#endif
