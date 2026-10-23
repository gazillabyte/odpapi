#ifndef ODPAPI_SCSI_ADAPTER_H
#define ODPAPI_SCSI_ADAPTER_H

#include <stddef.h>
#include <odpapi.h>

#define SCSI_ADAPTER_MAX_DRIVES 100

struct scsi_volume {
	char id[36];
	int element_type;
	int element;
	int full;
	int tagged;
};

struct scsi_library_inventory {
	char device_path[256];
	char changer_guid[37];
	char description[128];
	char media_type[32];
	char drive_descriptions[SCSI_ADAPTER_MAX_DRIVES][128];
	int drive_device_numbers[SCSI_ADAPTER_MAX_DRIVES];
	int slots;
	int drives;
	int portals;
	int transports;
	size_t volume_count;
	struct scsi_volume *volumes;
};

int scsi_probe_library(const char *path, struct scsi_library_inventory *result,
	char *error, size_t error_length);
char *scsi_capabilities_json(void);
char *scsi_unknown_json(const char *hint);
char *scsi_error_json(enum ODPAPI_COMMONERROR error, const char *hint);
char *scsi_list_json(const struct scsi_library_inventory *libraries,
	size_t library_count);
char *scsi_find_json(const struct scsi_library_inventory *libraries,
	size_t library_count, const char *volume_id);
char *scsi_inventory_json(const struct scsi_library_inventory *libraries,
	size_t library_count, const char *source_library);

#endif

