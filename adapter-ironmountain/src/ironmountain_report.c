#include "ironmountain_report.h"

#include <csv.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct csv_record_context {
	char fields[32][128];
	unsigned int count;
	int truncated;
};

static void copy_field(char *destination, size_t destination_length, const char *source)
{
	size_t length = strlen(source);
	if (length >= destination_length) length = destination_length - 1;
	memcpy(destination, source, length);
	destination[length] = '\0';
}

static void csv_field_callback(void *data, size_t length, void *user)
{
	struct csv_record_context *context = user;
	if (context->count >= 32) {
		context->truncated = 1;
		return;
	}
	if (length >= sizeof(context->fields[0])) {
		context->truncated = 1;
		length = sizeof(context->fields[0]) - 1;
	}
	memcpy(context->fields[context->count], data, length);
	context->fields[context->count][length] = '\0';
	++context->count;
}

static int csv_fields(const char *line, size_t length, char fields[][128], unsigned int *count)
{
	struct csv_record_context context = { {{0}}, 0, 0 };
	struct csv_parser parser;
	/* IRM reports contain legacy unescaped quotes in description fields. */
	if (csv_init(&parser, CSV_APPEND_NULL) != CSV_SUCCESS) return 0;
	if (csv_parse(&parser, line, length, csv_field_callback, NULL, &context) != length ||
		csv_fini(&parser, NULL, NULL, &context) != CSV_SUCCESS ||
		context.count < 13 || context.truncated) {
		csv_free(&parser);
		return 0;
	}
	csv_free(&parser);
	memcpy(fields, context.fields, sizeof(context.fields));
	*count = context.count;
	return 1;
}

static int parse_date(const char *text, date_t *date)
{
	int first, second, year, consumed;
	static const int month_days[] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
	};
	if (!text || !*text) { memset(date, 0, sizeof(*date)); return 0; }
	if (sscanf(text, "%d/%d/%d%n", &first, &second, &year, &consumed) == 3 &&
		text[consumed] == '\0') {
		date->month = first;
		date->day = second;
		date->year = year;
	} else if (strlen(text) == 8 && sscanf(text, "%2d%2d%4d%n", &first, &second,
		 &year, &consumed) == 3 && consumed == 8) {
		date->month = first;
		date->day = second;
		date->year = year;
	} else {
		return -1;
	}
	if (date->year < 1 || date->month < 1 || date->month > 12 || date->day < 1)
		return -1;
	if (date->day > month_days[date->month - 1] +
		(date->month == 2 && ((date->year % 4 == 0 && date->year % 100 != 0) ||
		(date->year % 400 == 0)))) return -1;
	return 1;
}

static int parse_slot(const char *text, unsigned int *slot)
{
	char *end;
	unsigned long value;

	if (!text || !*text) {
		*slot = 0;
		return 1;
	}
	errno = 0;
	value = strtoul(text, &end, 10);
	if (end == text || *end != '\0' || errno == ERANGE || value > UINT_MAX)
		return 0;
	*slot = (unsigned int)value;
	return 1;
}

int irm_date_is_set(const date_t *date)
{
	return date && date->year > 0;
}

int irm_date_compare(const date_t *left, const date_t *right)
{
	if (left->year != right->year) return left->year < right->year ? -1 : 1;
	if (left->month != right->month) return left->month < right->month ? -1 : 1;
	if (left->day != right->day) return left->day < right->day ? -1 : 1;
	return 0;
}

int irm_parse_record(const char *line, size_t length, struct irm_record *record,
	char *error, size_t error_length)
{
	char fields[32][128];
	unsigned int count;
	int return_status;
	int in_status;
	int slot_status;
	int out_status = 0;
	unsigned int i;
	if (!line || !record || !csv_fields(line, length, fields, &count)) {
		if (error && error_length) snprintf(error, error_length, "expected 13 CSV fields");
		return 0;
	}
	memset(record, 0, sizeof(*record));
	copy_field(record->customer_id, sizeof(record->customer_id), fields[0]);
	copy_field(record->volume_id, sizeof(record->volume_id), fields[1]);
	copy_field(record->media_type, sizeof(record->media_type), fields[5]);
	copy_field(record->description, sizeof(record->description), fields[6]);
	slot_status = parse_slot(fields[8], &record->slot);
	return_status = parse_date(fields[4], &record->return_date);
	in_status = parse_date(fields[9], &record->in_date);
	for (i = 10; i < count; ++i) {
		date_t candidate;
		/* Employee columns are intentionally never copied or retained. */
		if (parse_date(fields[i], &candidate) > 0) {
			record->out_date = candidate;
			out_status = 1;
			break;
		}
	}
	if (!out_status) memset(&record->out_date, 0, sizeof(record->out_date));
	if (return_status < 0 || in_status < 0 || out_status < 0 || !slot_status) {
		if (error && error_length) snprintf(error, error_length, "invalid date field");
		return 0;
	}
	if (!record->customer_id[0] || !record->volume_id[0]) {
		if (error && error_length) snprintf(error, error_length, "customer or volume is empty");
		return 0;
	}
	/* The legacy IRM rule: only an earlier out date means IN. */
	if (!irm_date_is_set(&record->out_date) ||
		(irm_date_is_set(&record->in_date) && irm_date_compare(&record->out_date, &record->in_date) < 0))
		record->flags |= IRM_FLAG_IN;
	return 1;
}

int irm_record_key(const struct irm_record *record, char *key, size_t key_length)
{
	int written;
	if (!record || !key || key_length == 0) return 0;
	written = snprintf(key, key_length, "%s-%s", record->customer_id, record->volume_id);
	return written >= 0 && (size_t)written < key_length;
}

void irm_library_uuid(const char *customer_id, int in_library, char *uuid, size_t uuid_length)
{
	uint32_t hash[4] = {2166136261U, 2246822519U, 3266489917U, 668265263U};
	char name[128];
	unsigned int i;
	if (!uuid || uuid_length == 0) return;
	snprintf(name, sizeof(name), "%s:%s", customer_id ? customer_id : "",
		in_library ? "IN" : "OUT");
	for (i = 0; name[i]; ++i) {
		hash[0] = (hash[0] ^ (unsigned char)name[i]) * 16777619U;
		hash[1] = (hash[1] ^ (unsigned char)name[i]) * 2246822519U + hash[0];
		hash[2] = (hash[2] ^ (unsigned char)name[i]) * 3266489917U + hash[1];
		hash[3] = (hash[3] ^ (unsigned char)name[i]) * 668265263U + hash[2];
	}
	hash[2] = (hash[2] & 0x0fffffffU) | 0x50000000U;
	hash[3] = (hash[3] & 0x3fffffffU) | 0x80000000U;
	snprintf(uuid, uuid_length, "%08x-%04x-%04x-%04x-%08x%04x",
		hash[0], hash[1] >> 16, hash[2] >> 16, hash[3] >> 16,
		hash[0] & 0xffffU, hash[1] & 0xffffU);
}
