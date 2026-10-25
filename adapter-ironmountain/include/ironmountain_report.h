#ifndef IRONMOUNTAIN_REPORT_H
#define IRONMOUNTAIN_REPORT_H

#include <stddef.h>
#include <stdint.h>

/** Record flag indicating that the volume belongs to the synthetic IN library. */
#define IRM_FLAG_IN 0x00000001U
/** Maximum volume identifier length, excluding the terminator. */
#define IRM_ID_MAX 20
/** Maximum customer identifier length, excluding the terminator. */
#define IRM_CUSTOMER_MAX 6
/** Maximum media-type length, excluding the terminator. */
#define IRM_MEDIA_TYPE_MAX 16
/** Maximum description length, excluding the terminator. */
#define IRM_DESCRIPTION_MAX 50
#define IRM_CUSTOMER_ID_SIZE (IRM_CUSTOMER_MAX + 1)
#define IRM_VOLUME_ID_SIZE (IRM_ID_MAX + 1)
#define IRM_MEDIA_TYPE_SIZE (IRM_MEDIA_TYPE_MAX + 1)
#define IRM_DESCRIPTION_SIZE (IRM_DESCRIPTION_MAX + 1)
#define IRM_LIBRARY_KEY_MAX (IRM_CUSTOMER_MAX + 1)
#define IRM_LIBRARY_IN_SUFFIX 'I'
#define IRM_LIBRARY_OUT_SUFFIX 'O'
#define IRM_MEDIA_KEY_SIZE (IRM_CUSTOMER_MAX + IRM_ID_MAX)

/** A date from the source report; a non-positive year means unset. */
typedef struct {
	/** Calendar year. */
	int32_t year;
	/** Calendar month. */
	int32_t month;
	/** Calendar day. */
	int32_t day;
} date_t;

/** Parsed and normalized record from one CSV report row. */
struct irm_record {
	/** Source customer identifier. */
	char customer_id[IRM_CUSTOMER_ID_SIZE];
	/** Source media identifier. */
	char volume_id[IRM_VOLUME_ID_SIZE];
	/** Source media type. */
	char media_type[IRM_MEDIA_TYPE_SIZE];
	/** Source description. */
	char description[IRM_DESCRIPTION_SIZE];
	/** One-based source slot, or zero when absent. */
	unsigned int slot;
	/** Scheduled return date, when present. */
	date_t return_date;
	/** Inbound date, when present. */
	date_t in_date;
	/** Outbound date, when present. */
	date_t out_date;
	/** Classification flags such as @ref IRM_FLAG_IN. */
	uint32_t flags;
};

/** Fixed-width Berkeley DB primary key: customer bytes followed by media bytes. */
struct irm_media_key {
	/** Fixed-width customer identifier bytes. */
	char customer_id[IRM_CUSTOMER_MAX];
	/** Fixed-width media identifier bytes. */
	char volume_id[IRM_ID_MAX];
};

/** Parse one synthetic IRM inventory CSV row into @p record. */
int irm_parse_record(const char *line, size_t length, struct irm_record *record,
	char *error, size_t error_length);

/** Format a record's composite customer/media key into @p key. */
int irm_record_key(const struct irm_record *record, char *key, size_t key_length);

/** Compare two calendar dates, returning -1, 0, or 1. */
int irm_date_compare(const date_t *left, const date_t *right);

/** Return non-zero when @p date contains a set year. */
int irm_date_is_set(const date_t *date);

/** Derive the stable IN or OUT library UUID for a customer identifier. */
void irm_library_uuid(const char *customer_id, int in_library, char *uuid, size_t uuid_length);

#endif
