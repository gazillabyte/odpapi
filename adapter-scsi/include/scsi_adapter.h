#ifndef ODPAPI_SCSI_ADAPTER_H
#define ODPAPI_SCSI_ADAPTER_H

#include <stddef.h>
#include <odpapi.h>

/** Maximum number of drive elements supported by one changer inventory. */
#define SCSI_ADAPTER_MAX_DRIVES 100
/** Maximum length, excluding the terminator, of a media type. */
#define SCSI_ADAPTER_MEDIA_TYPE_MAX 32

/** A volume tag and location reported by a SCSI changer. */
struct scsi_volume {
	/** Volume tag, or a generated element identifier when untagged. */
	char id[36];
	/** Linux changer element type, such as storage, drive, or portal. */
	int element_type;
	/** Element address within the changer. */
	int element;
	/** Non-zero when the element contains media. */
	int full;
	/** Non-zero when the volume has a physical volume tag. */
	int tagged;
};

/** A complete read-only snapshot of one configured SCSI changer. */
struct scsi_library_inventory {
	/** Configured changer device path. */
	char device_path[256];
	/** Stable identity used to derive ODPAPI library GUIDs. */
	char identity[256];
	/** Human-readable vendor/model description. */
	char description[128];
	/** Configured media type used in inventory responses. */
	char media_type[SCSI_ADAPTER_MEDIA_TYPE_MAX];
	/** Human-readable descriptions for drive elements. */
	char drive_descriptions[SCSI_ADAPTER_MAX_DRIVES][128];
	/** Stable identities used to derive drive library GUIDs. */
	char drive_identities[SCSI_ADAPTER_MAX_DRIVES][256];
	/** Linux tape-device numbers corresponding to drive elements. */
	int drive_device_numbers[SCSI_ADAPTER_MAX_DRIVES];
	/** Number of storage slots. */
	int slots;
	/** Number of data-transfer drives. */
	int drives;
	/** Number of import/export portals. */
	int portals;
	/** Number of transport/picker elements. */
	int transports;
	/** Number of occupied elements in @ref volumes. */
	size_t volume_count;
	/** Heap-allocated occupied-volume array; the caller owns it. */
	struct scsi_volume *volumes;
};

/**
 * Probe a configured Linux SCSI changer.
 *
 * On success, @p result owns a newly allocated @c volumes array. The caller
 * must release that array with @c free().
 *
 * @param path changer device path, such as @c /dev/sch0
 * @param result destination inventory structure
 * @param error buffer for a human-readable failure message
 * @param error_length size of @p error in bytes
 * @return 0 on success, or -1 when the device cannot be probed
 */
int scsi_probe_library(const char *path, struct scsi_library_inventory *result,
	char *error, size_t error_length);

/** Build the ODPAPI Query response for this adapter. */
char *scsi_capabilities_json(void);

/** Build the standard response for an unknown route. */
char *scsi_unknown_json(const char *hint);

/** Build a standard ODPAPI error response with an optional hint. */
char *scsi_error_json(enum ODPAPI_COMMONERROR error, const char *hint);

/** Build the ODPAPI List response for probed changers. */
char *scsi_list_json(const struct scsi_library_inventory *libraries,
	size_t library_count);

/** Build the ODPAPI Find response for a literal volume identifier. */
char *scsi_find_json(const struct scsi_library_inventory *libraries,
	size_t library_count, const char *volume_id);

/** Build the ODPAPI Inventory response for one configured library GUID. */
char *scsi_inventory_json(const struct scsi_library_inventory *libraries,
	size_t library_count, const char *source_library);

#endif

