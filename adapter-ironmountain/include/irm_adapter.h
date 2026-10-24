#ifndef IRM_ADAPTER_H
#define IRM_ADAPTER_H

#include <stddef.h>
#include <stdint.h>

#define IRM_FLAG_IN 0x00000001U
#define IRM_ID_MAX 20
#define IRM_CUSTOMER_MAX 6
#define IRM_MEDIA_TYPE_MAX 16
#define IRM_DESCRIPTION_MAX 50
#define IRM_CUSTOMER_ID_SIZE (IRM_CUSTOMER_MAX + 1)
#define IRM_VOLUME_ID_SIZE (IRM_ID_MAX + 1)
#define IRM_MEDIA_TYPE_SIZE (IRM_MEDIA_TYPE_MAX + 1)
#define IRM_DESCRIPTION_SIZE (IRM_DESCRIPTION_MAX + 1)
#define IRM_LIBRARY_KEY_MAX (IRM_CUSTOMER_MAX + 1)
#define IRM_LIBRARY_IN_SUFFIX 'I'
#define IRM_LIBRARY_OUT_SUFFIX 'O'
#define IRM_MEDIA_KEY_SIZE (IRM_CUSTOMER_MAX + IRM_ID_MAX)

typedef struct {
	int32_t year;
	int32_t month;
	int32_t day;
} date_t;

struct irm_record {
	char customer_id[IRM_CUSTOMER_ID_SIZE];
	char volume_id[IRM_VOLUME_ID_SIZE];
	char media_type[IRM_MEDIA_TYPE_SIZE];
	char description[IRM_DESCRIPTION_SIZE];
	unsigned int slot;
	date_t return_date;
	date_t in_date;
	date_t out_date;
	uint32_t flags;
};

/* Fixed-width Berkeley DB primary key: six customer bytes followed by twenty media bytes. */
struct irm_media_key {
	char customer_id[IRM_CUSTOMER_MAX];
	char volume_id[IRM_ID_MAX];
};

int irm_parse_record(const char *line, size_t length, struct irm_record *record,
	char *error, size_t error_length);
int irm_record_key(const struct irm_record *record, char *key, size_t key_length);
int irm_date_compare(const date_t *left, const date_t *right);
int irm_date_is_set(const date_t *date);
void irm_library_uuid(const char *customer_id, int in_library, char *uuid, size_t uuid_length);

#endif
