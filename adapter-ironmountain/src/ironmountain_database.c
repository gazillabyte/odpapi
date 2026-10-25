#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "ironmountain_database.h"

#include <db.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int db_open(const char *path, DB **database, u_int32_t flags, int duplicates)
{
	DB *db = NULL;
	int status = db_create(&db, NULL, 0);
	if (status != 0) return status;
	if (duplicates) {
		status = db->set_flags(db, DB_DUP);
		if (status != 0) {
			db->close(db, 0);
			return status;
		}
	}
	status = db->open(db, NULL, path, NULL, DB_BTREE, flags, 0644);
	if (status != 0) {
		db->close(db, 0);
		return status;
	}
	*database = db;
	return 0;
}

static int snapshot_state_path(const char *media_path, char *path, size_t length)
{
	int written;
	if (!media_path || !path || length == 0) return 0;
	written = snprintf(path, length, "%s.state", media_path);
	return written >= 0 && (size_t)written < length;
}

static int write_snapshot_state(const char *media_path, const char *state)
{
	char path[4096];
	char temporary[4096];
	FILE *stream;
	if (!snapshot_state_path(media_path, path, sizeof(path)) ||
		snprintf(temporary, sizeof(temporary), "%s.new", path) >= (int)sizeof(temporary))
		return 0;
	stream = fopen(temporary, "w");
	if (!stream) return 0;
	if (fprintf(stream, "%s\n", state) < 0 || fflush(stream) != 0 ||
		fclose(stream) != 0 || rename(temporary, path) != 0) {
		(void)unlink(temporary);
		return 0;
	}
	return 1;
}

int irm_database_snapshot_ready(const char *media_path)
{
	char path[4096];
	char state[32];
	FILE *stream;
	if (!snapshot_state_path(media_path, path, sizeof(path))) return 0;
	stream = fopen(path, "r");
	if (!stream) return errno == ENOENT;
	if (!fgets(state, sizeof(state), stream)) {
		fclose(stream);
		return 0;
	}
	fclose(stream);
	return strncmp(state, "ready", 5) == 0 &&
		(state[5] == '\0' || state[5] == '\n' || state[5] == '\r');
}

static void make_library_key(const struct irm_record *record, unsigned char key[IRM_LIBRARY_KEY_MAX])
{
	size_t length = strlen(record->customer_id);
	if (length > IRM_CUSTOMER_MAX) length = IRM_CUSTOMER_MAX;
	memset(key, 0, IRM_CUSTOMER_MAX);
	memcpy(key, record->customer_id, length);
	key[IRM_CUSTOMER_MAX] = record->flags & IRM_FLAG_IN ?
		IRM_LIBRARY_IN_SUFFIX : IRM_LIBRARY_OUT_SUFFIX;
}

static void make_media_key(const struct irm_record *record, unsigned char key[IRM_MEDIA_KEY_SIZE])
{
	size_t customer_length = strlen(record->customer_id);
	size_t volume_length = strlen(record->volume_id);
	if (customer_length > IRM_CUSTOMER_MAX) customer_length = IRM_CUSTOMER_MAX;
	if (volume_length > IRM_ID_MAX) volume_length = IRM_ID_MAX;
	memset(key, 0, IRM_MEDIA_KEY_SIZE);
	memcpy(key, record->customer_id, customer_length);
	memcpy(key + IRM_CUSTOMER_MAX, record->volume_id, volume_length);
}

static int parse_media_key(const char *text, unsigned char key[IRM_MEDIA_KEY_SIZE])
{
	const char *separator;
	size_t customer_length;
	size_t volume_length;
	if (!text) return 0;
	separator = strchr(text, '-');
	if (!separator || separator == text || !separator[1]) return 0;
	customer_length = (size_t)(separator - text);
	volume_length = strlen(separator + 1);
	if (customer_length > IRM_CUSTOMER_MAX || volume_length > IRM_ID_MAX) return 0;
	memset(key, 0, IRM_MEDIA_KEY_SIZE);
	memcpy(key, text, customer_length);
	memcpy(key + IRM_CUSTOMER_MAX, separator + 1, volume_length);
	return 1;
}

static int media_key_text(const unsigned char key[IRM_MEDIA_KEY_SIZE], char *text, size_t length)
{
	char customer[IRM_CUSTOMER_MAX + 1];
	char volume[IRM_ID_MAX + 1];
	size_t customer_length = IRM_CUSTOMER_MAX;
	size_t volume_length = IRM_ID_MAX;
	while (customer_length > 0 && key[customer_length - 1] == 0) --customer_length;
	while (volume_length > 0 && key[IRM_CUSTOMER_MAX + volume_length - 1] == 0) --volume_length;
	memcpy(customer, key, customer_length);
	customer[customer_length] = '\0';
	memcpy(volume, key + IRM_CUSTOMER_MAX, volume_length);
	volume[volume_length] = '\0';
	{
		int written = snprintf(text, length, "%s-%s", customer, volume);
		return written >= 0 && (size_t)written < length;
	}
}

static int library_key_text(const unsigned char key[IRM_LIBRARY_KEY_MAX],
	char *text, size_t length)
{
	char customer[IRM_CUSTOMER_MAX + 1];
	size_t customer_length = IRM_CUSTOMER_MAX;
	int written;
	while (customer_length > 0 && key[customer_length - 1] == 0) --customer_length;
	memcpy(customer, key, customer_length);
	customer[customer_length] = '\0';
	if (key[IRM_CUSTOMER_MAX] != IRM_LIBRARY_IN_SUFFIX &&
		key[IRM_CUSTOMER_MAX] != IRM_LIBRARY_OUT_SUFFIX) return 0;
	written = snprintf(text, length, "%s%c", customer, key[IRM_CUSTOMER_MAX]);
	return written >= 0 && (size_t)written < length;
}

int irm_database_load_snapshot(const char *media_path, const char *library_path,
	const char *media_index_path, const struct irm_record *records,
	unsigned int count, char *error, size_t error_length)
{
	char media_temporary[4096];
	char library_temporary[4096];
	char media_index_temporary[4096];
	DB *media = NULL;
	DB *library = NULL;
	DB *media_index = NULL;
	DBT key;
	DBT value;
	unsigned int i;
	int status;
	if (!media_path || !library_path || !media_index_path || (!records && count != 0)) return 0;
	if (snprintf(media_temporary, sizeof(media_temporary), "%s.new", media_path) >= (int)sizeof(media_temporary) ||
		snprintf(library_temporary, sizeof(library_temporary), "%s.new", library_path) >= (int)sizeof(library_temporary) ||
		snprintf(media_index_temporary, sizeof(media_index_temporary), "%s.new", media_index_path) >= (int)sizeof(media_index_temporary)) return 0;
	(void)unlink(media_temporary);
	(void)unlink(library_temporary);
	(void)unlink(media_index_temporary);
	if (!write_snapshot_state(media_path, "publishing")) return 0;
	status = db_open(media_temporary, &media, DB_CREATE | DB_EXCL, 0);
	if (status != 0) {
		if (error && error_length) snprintf(error, error_length, "database open: %s", db_strerror(status));
		return 0;
	}
	status = db_open(library_temporary, &library, DB_CREATE | DB_EXCL, 1);
	if (status != 0) goto fail;
	status = db_open(media_index_temporary, &media_index, DB_CREATE | DB_EXCL, 1);
	if (status != 0) goto fail;
	for (i = 0; i < count; ++i) {
		char key_text[IRM_CUSTOMER_MAX + IRM_ID_MAX + 2];
		unsigned char media_key[IRM_MEDIA_KEY_SIZE];
		unsigned char library_key[IRM_LIBRARY_KEY_MAX];
		unsigned char media_id_key[IRM_ID_MAX];
		if (!irm_record_key(&records[i], key_text, sizeof(key_text))) {
			status = EINVAL;
			break;
		}
		make_media_key(&records[i], media_key);
		memset(media_id_key, 0, sizeof(media_id_key));
		memcpy(media_id_key, records[i].volume_id,
			strlen(records[i].volume_id) < sizeof(media_id_key) ?
			strlen(records[i].volume_id) : sizeof(media_id_key));
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		key.data = media_key;
		key.size = sizeof(media_key);
		value.data = (void *)&records[i];
		value.size = sizeof(records[i]);
		status = media->put(media, NULL, &key, &value, DB_NOOVERWRITE);
		if (status == DB_KEYEXIST) {
			fprintf(stderr, "duplicate CUSTOMERID-Media# key %s; keeping first occurrence\n",
				key_text);
			status = 0;
			continue;
		}
		if (status != 0) break;
		make_library_key(&records[i], library_key);
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		key.data = library_key;
		key.size = sizeof(library_key);
		value.data = media_key;
		value.size = sizeof(media_key);
		status = library->put(library, NULL, &key, &value, 0);
		if (status != 0) break;
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		key.data = media_id_key;
		key.size = sizeof(media_id_key);
		value.data = media_key;
		value.size = sizeof(media_key);
		status = media_index->put(media_index, NULL, &key, &value, 0);
	}
	if (media && media->close(media, 0) != 0 && status == 0) status = EIO;
	media = NULL;
	if (library && library->close(library, 0) != 0 && status == 0) status = EIO;
	library = NULL;
	if (media_index && media_index->close(media_index, 0) != 0 && status == 0) status = EIO;
	media_index = NULL;
	if (status == 0 && rename(media_temporary, media_path) != 0) status = errno;
	if (status == 0 && rename(library_temporary, library_path) != 0) status = errno;
	if (status == 0 && rename(media_index_temporary, media_index_path) != 0) status = errno;
	if (status == 0 && !write_snapshot_state(media_path, "ready")) status = EIO;
	if (status != 0) {
		(void)unlink(media_temporary);
		(void)unlink(library_temporary);
		(void)unlink(media_index_temporary);
		if (error && error_length) {
			snprintf(error, error_length, "database load: %s", db_strerror(status));
		}
		return 0;
	}
	return 1;

fail:
	if (media) media->close(media, 0);
	if (library) library->close(library, 0);
	if (media_index) media_index->close(media_index, 0);
	unlink(media_temporary);
	unlink(library_temporary);
	unlink(media_index_temporary);
	if (error && error_length) snprintf(error, error_length, "database open: %s", db_strerror(status));
	return 0;
}

int irm_database_load(const char *path, const struct irm_record *records,
	unsigned int count, char *error, size_t error_length)
{
	char library_path[4096];
	char media_index_path[4096];
	if (!path || snprintf(library_path, sizeof(library_path), "%s.library", path) >= (int)sizeof(library_path) ||
		snprintf(media_index_path, sizeof(media_index_path), "%s.media-index", path) >= (int)sizeof(media_index_path)) return 0;
	return irm_database_load_snapshot(path, library_path, media_index_path,
		records, count, error, error_length);
}

int irm_database_get(const char *path, const char *key_text, struct irm_record *record)
{
	DB *db = NULL;
	DBT key;
	DBT value;
	unsigned char media_key[IRM_MEDIA_KEY_SIZE];
	int status;
	if (!path || !key_text || !record) return 0;
	if (!parse_media_key(key_text, media_key)) return 0;
	status = db_open(path, &db, DB_RDONLY, 0);
	if (status != 0) return 0;
	memset(&key, 0, sizeof(key));
	memset(&value, 0, sizeof(value));
	key.data = media_key;
	key.size = sizeof(media_key);
	value.data = record;
	value.ulen = sizeof(*record);
	value.flags = DB_DBT_USERMEM;
	status = db->get(db, NULL, &key, &value, 0);
	(void)db->close(db, 0);
	return status == 0 && value.size == sizeof(*record);
}

int irm_database_count(const char *path, unsigned int *count)
{
	DB *db = NULL;
	DBT key;
	DBT value;
	DBC *cursor = NULL;
	int status;
	if (!count) return 0;
	*count = 0;
	status = db_open(path, &db, DB_RDONLY, 0);
	if (status != 0) return 0;
	status = db->cursor(db, NULL, &cursor, 0);
	if (status == 0) {
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		while ((status = cursor->get(cursor, &key, &value, DB_NEXT)) == 0) ++*count;
		if (status == DB_NOTFOUND) status = 0;
		(void)cursor->close(cursor);
	}
	(void)db->close(db, 0);
	return status == 0;
}

int irm_database_visit(const char *path, irm_database_visit_fn visit, void *user)
{
	DB *db = NULL;
	DBT key;
	DBT value;
	DBC *cursor = NULL;
	struct irm_record record;
	char key_text[256];
	int status;
	if (!visit) return 0;
	status = db_open(path, &db, DB_RDONLY, 0);
	if (status != 0) return 0;
	status = db->cursor(db, NULL, &cursor, 0);
	memset(&key, 0, sizeof(key));
	memset(&value, 0, sizeof(value));
	value.data = &record;
	value.ulen = sizeof(record);
	value.flags = DB_DBT_USERMEM;
	while (status == 0 && (status = cursor->get(cursor, &key, &value, DB_NEXT)) == 0) {
		if (key.size >= sizeof(key_text)) { status = EOVERFLOW; break; }
		if (key.size != IRM_MEDIA_KEY_SIZE || !media_key_text(key.data, key_text, sizeof(key_text))) {
			status = EOVERFLOW;
			break;
		}
		if (value.size != sizeof(record) || !visit(key_text, &record, user)) { status = EINVAL; break; }
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		value.data = &record;
		value.ulen = sizeof(record);
		value.flags = DB_DBT_USERMEM;
	}
	if (status == DB_NOTFOUND) status = 0;
	if (cursor) (void)cursor->close(cursor);
	(void)db->close(db, 0);
	return status == 0;
}

int irm_database_visit_library(const char *library_path, const char *media_path,
	const char *library_key, irm_database_visit_fn visit, void *user)
{
	DB *library = NULL;
	DB *media = NULL;
	DBC *cursor = NULL;
	DBT key;
	DBT value;
	DBT media_value;
	unsigned char aix_key[IRM_LIBRARY_KEY_MAX];
	struct irm_record record;
	char key_text[IRM_MEDIA_KEY_SIZE + 2];
	int status;
	if (!library_path || !media_path || !library_key || !visit)
		return 0;
	memset(aix_key, 0, sizeof(aix_key));
	memcpy(aix_key, library_key, IRM_LIBRARY_KEY_MAX);
	status = db_open(library_path, &library, DB_RDONLY, 1);
	if (status != 0) return 0;
	status = db_open(media_path, &media, DB_RDONLY, 0);
	if (status != 0) {
		(void)library->close(library, 0);
		return 0;
	}
	status = library->cursor(library, NULL, &cursor, 0);
	if (status != 0) goto done;
	memset(&key, 0, sizeof(key));
	memset(&value, 0, sizeof(value));
	key.data = aix_key;
	key.size = sizeof(aix_key);
	status = cursor->get(cursor, &key, &value, DB_SET);
	while (status == 0) {
		if (value.size != IRM_MEDIA_KEY_SIZE) {
			status = EOVERFLOW;
			break;
		}
		memset(&media_value, 0, sizeof(media_value));
		media_value.data = &record;
		media_value.ulen = sizeof(record);
		media_value.flags = DB_DBT_USERMEM;
		status = media->get(media, NULL, &value, &media_value, 0);
		if (status != 0 || media_value.size != sizeof(record)) break;
		if (!media_key_text(value.data, (char *)key_text, sizeof(key_text))) {
			status = EOVERFLOW;
			break;
		}
		if (!visit(key_text, &record, user)) {
			status = EINVAL;
			break;
		}
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		status = cursor->get(cursor, &key, &value, DB_NEXT_DUP);
	}
	if (status == DB_NOTFOUND) status = 0;
done:
	if (cursor) (void)cursor->close(cursor);
	if (media) (void)media->close(media, 0);
	if (library) (void)library->close(library, 0);
	return status == 0;
}

int irm_database_visit_media_id(const char *media_index_path, const char *media_path,
	const char *media_id, irm_database_visit_fn visit, void *user)
{
	DB *index = NULL;
	DB *media = NULL;
	DBC *cursor = NULL;
	DBT key;
	DBT value;
	DBT media_value;
	unsigned char index_key[IRM_ID_MAX];
	struct irm_record record;
	char key_text[IRM_MEDIA_KEY_SIZE + 2];
	int status;
	size_t length;
	if (!media_index_path || !media_path || !media_id || !visit) return 0;
	length = strlen(media_id);
	if (length == 0 || length > IRM_ID_MAX) return 0;
	memset(index_key, 0, sizeof(index_key));
	memcpy(index_key, media_id, length);
	status = db_open(media_index_path, &index, DB_RDONLY, 1);
	if (status != 0) return 0;
	status = db_open(media_path, &media, DB_RDONLY, 0);
	if (status != 0) {
		(void)index->close(index, 0);
		return 0;
	}
	status = index->cursor(index, NULL, &cursor, 0);
	if (status != 0) goto done;
	memset(&key, 0, sizeof(key));
	memset(&value, 0, sizeof(value));
	key.data = index_key;
	key.size = sizeof(index_key);
	status = cursor->get(cursor, &key, &value, DB_SET);
	while (status == 0) {
		if (value.size != IRM_MEDIA_KEY_SIZE) {
			status = EOVERFLOW;
			break;
		}
		memset(&media_value, 0, sizeof(media_value));
		media_value.data = &record;
		media_value.ulen = sizeof(record);
		media_value.flags = DB_DBT_USERMEM;
		status = media->get(media, NULL, &value, &media_value, 0);
		if (status != 0 || media_value.size != sizeof(record)) break;
		if (!media_key_text(value.data, key_text, sizeof(key_text))) {
			status = EOVERFLOW;
			break;
		}
		if (!visit(key_text, &record, user)) {
			status = EINVAL;
			break;
		}
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		status = cursor->get(cursor, &key, &value, DB_NEXT_DUP);
	}
	if (status == DB_NOTFOUND) status = 0;
done:
	if (cursor) (void)cursor->close(cursor);
	if (media) (void)media->close(media, 0);
	if (index) (void)index->close(index, 0);
	return status == 0;
}

int irm_database_visit_libraries(const char *path, irm_library_visit_fn visit, void *user)
{
	DB *db = NULL;
	DBT key;
	DBT value;
	DBC *cursor = NULL;
	char key_text[IRM_LIBRARY_KEY_MAX + 1];
	db_recno_t count;
	int status;
	if (!visit) return 0;
	status = db_open(path, &db, DB_RDONLY, 1);
	if (status != 0) return 0;
	status = db->cursor(db, NULL, &cursor, 0);
	memset(&key, 0, sizeof(key));
	memset(&value, 0, sizeof(value));
	status = cursor->get(cursor, &key, &value, DB_FIRST);
	while (status == 0) {
		if (key.size != IRM_LIBRARY_KEY_MAX) { status = EOVERFLOW; break; }
		if (!library_key_text(key.data, key_text, sizeof(key_text))) {
			status = EINVAL;
			break;
		}
		if (cursor->count(cursor, &count, 0) != 0 || count > UINT_MAX ||
			!visit(key_text, (unsigned int)count, user)) { status = EINVAL; break; }
		memset(&key, 0, sizeof(key));
		memset(&value, 0, sizeof(value));
		status = cursor->get(cursor, &key, &value, DB_NEXT_NODUP);
	}
	if (cursor) (void)cursor->close(cursor);
	(void)db->close(db, 0);
	return status == 0 || status == DB_NOTFOUND;
}
