#include "scsi_adapter.h"

#include <cJSON.h>
#include <linux/chio.h>
#include <odpapi.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

static uint32_t library_hash(const char *value, uint32_t seed)
{
	uint32_t hash = seed;
	while (*value) {
		hash ^= (unsigned char)*value++;
		hash *= 16777619U;
	}
	return hash;
}

static void library_guid(const char *identity, const char *element_class,
	char *guid, size_t length)
{
	char key[512];
	uint32_t a, b, c, d;
	snprintf(key, sizeof(key), "%s:%s", identity, element_class);
	a = library_hash(key, 2166136261U);
	b = library_hash(key, 0x9e3779b9U);
	c = library_hash(key, 0x85ebca6bU);
	d = library_hash(key, 0xc2b2ae35U);
	snprintf(guid, length, "%08x-%04x-%04x-%04x-%08x%04x",
		a, (unsigned)(b >> 16), ((unsigned)(c >> 16) & 0x0fffU) | 0x5000U,
		((unsigned)(d >> 16) & 0x3fffU) | 0x8000U, b, (unsigned)c & 0xffffU);
}

static unsigned int element_used(const struct scsi_library_inventory *library,
	int element_type)
{
	size_t i;
	unsigned int used = 0;
	for (i = 0; i < library->volume_count; ++i)
		if (library->volumes[i].element_type == element_type) ++used;
	return used;
}

static unsigned int element_present(const struct scsi_library_inventory *library,
	int element_type, int element)
{
	size_t i;
	for (i = 0; i < library->volume_count; ++i)
		if (library->volumes[i].element_type == element_type &&
			library->volumes[i].element == element) return 1;
	return 0;
}

static void volume_guid(const struct scsi_library_inventory *library,
	const struct scsi_volume *volume, char *guid, size_t guid_length)
{
	char element_class[32];
	if (volume->element_type == CHET_ST)
		strcpy(element_class, "storage");
	else if (volume->element_type == CHET_IE)
		strcpy(element_class, "capio");
	else if (volume->element_type == CHET_MT)
		strcpy(element_class, "transport");
	else {
		snprintf(element_class, sizeof(element_class), "drive:%d", volume->element);
	}
	if (volume->element_type == CHET_DT &&
		volume->element >= 0 && volume->element < SCSI_ADAPTER_MAX_DRIVES &&
		library->drive_identities[volume->element][0] != '\0')
		library_guid(library->identity, library->drive_identities[volume->element],
			guid, guid_length);
	else
		library_guid(library->identity, element_class, guid, guid_length);
}

char *scsi_list_json(const struct scsi_library_inventory *libraries,
	size_t library_count)
{
	cJSON *root = cJSON_CreateObject();
	cJSON *response;
	cJSON *objects;
	char *serialized;
	size_t i;
	int sequence = 0;

	if (!root || !odpapi_response_prolog(root, "SCSI Open Data Protection API Adapter",
		"Copyright 2026 GazillaByte Pty Ltd", __DATE__, __TIME__, NULL)) {
		cJSON_Delete(root);
		return NULL;
	}
	response = odpapi_request_get_value(root, "response");
	objects = cJSON_CreateArray();
	if (!objects) { cJSON_Delete(root); return NULL; }
	cJSON_AddItemToObject(response, "objects", objects);
	for (i = 0; i < library_count; ++i) {
		char storage_guid[37];
		char capio_guid[37];
		char transport_guid[37];
		char drive_guids[SCSI_ADAPTER_MAX_DRIVES][37];
		char *targets[SCSI_ADAPTER_MAX_DRIVES + 3];
		char *scope = (char *)libraries[i].description;
		int drive;
		int target_count;

		library_guid(libraries[i].identity, "storage", storage_guid,
			sizeof(storage_guid));
		library_guid(libraries[i].identity, "capio", capio_guid,
			sizeof(capio_guid));
		library_guid(libraries[i].identity, "transport", transport_guid,
			sizeof(transport_guid));
		for (drive = 0; drive < libraries[i].drives; ++drive) {
			char drive_class[32];
			snprintf(drive_class, sizeof(drive_class), "drive:%d", drive);
			library_guid(libraries[i].identity, libraries[i].drive_identities[drive][0] ?
				libraries[i].drive_identities[drive] : drive_class, drive_guids[drive],
				sizeof(drive_guids[drive]));
		}

		/* Storage can move media to CAP/IO or any drive. */
		target_count = 0;
		if (libraries[i].portals > 0) targets[target_count++] = capio_guid;
		for (drive = 0; drive < libraries[i].drives; ++drive)
			targets[target_count++] = drive_guids[drive];
		if (libraries[i].transports > 0) targets[target_count++] = transport_guid;
		targets[target_count] = NULL;
		if (!odpapi_list_response_helper(objects, sequence++, "Storage", storage_guid,
			scope, (unsigned int)libraries[i].slots,
			element_used(&libraries[i], CHET_ST), 0, 0, 0, 0, 0,
			0.0, 0.0, targets)) { cJSON_Delete(root); return NULL; }

		if (libraries[i].portals > 0) {
			/* CAP/IO can move media to storage or any drive. */
			target_count = 0;
			targets[target_count++] = storage_guid;
			for (drive = 0; drive < libraries[i].drives; ++drive)
				targets[target_count++] = drive_guids[drive];
			if (libraries[i].transports > 0) targets[target_count++] = transport_guid;
			targets[target_count] = NULL;
			if (!odpapi_list_response_helper(objects, sequence++, "CAP/IO",
				capio_guid, scope, (unsigned int)libraries[i].portals,
				element_used(&libraries[i], CHET_IE), 0, 0, 0, 0, 0,
				0.0, 0.0, targets)) { cJSON_Delete(root); return NULL; }
		}

		for (drive = 0; drive < libraries[i].drives; ++drive) {
			char description[160];
			target_count = 0;
			targets[target_count++] = storage_guid;
			if (libraries[i].portals > 0) targets[target_count++] = capio_guid;
			for (int other = 0; other < libraries[i].drives; ++other)
				if (other != drive) targets[target_count++] = drive_guids[other];
			if (libraries[i].transports > 0) targets[target_count++] = transport_guid;
			targets[target_count] = NULL;
			if (libraries[i].drive_device_numbers[drive] >= 0)
				snprintf(description, sizeof(description), "Drive-%d: %s (/dev/st%d)",
					drive + 1, libraries[i].drive_descriptions[drive],
					libraries[i].drive_device_numbers[drive]);
			else
				snprintf(description, sizeof(description), "Drive-%d: %s",
					drive + 1, libraries[i].drive_descriptions[drive]);
			if (!odpapi_list_response_helper(objects, sequence++, description,
				drive_guids[drive], scope, 1,
				element_present(&libraries[i], CHET_DT, drive), 0, 0, 0, 0, 0,
				0.0, 0.0, targets)) { cJSON_Delete(root); return NULL; }
		}

		if (libraries[i].transports > 0) {
			/* CHIO_MOVE permits the transport element as a source or destination. */
			target_count = 0;
			targets[target_count++] = storage_guid;
			if (libraries[i].portals > 0) targets[target_count++] = capio_guid;
			for (drive = 0; drive < libraries[i].drives; ++drive)
				targets[target_count++] = drive_guids[drive];
			targets[target_count] = NULL;
			if (!odpapi_list_response_helper(objects, sequence++,
				"Transport", transport_guid, scope,
				(unsigned int)libraries[i].transports, 0, 0, 0, 0, 0, 0,
				0.0, 0.0, targets)) { cJSON_Delete(root); return NULL; }
		}
	}
	odpapi_response_epilog(root, ODPAPI_COMMONERROR_OK, NULL, time(NULL));
	serialized = odpapi_response_print(root);
	cJSON_Delete(root);
	return serialized;
}

char *scsi_capabilities_json(void)
{
	cJSON *root = cJSON_CreateObject();
	cJSON *response;
	cJSON *functions;
	char *serialized;

	if (!root || !odpapi_response_prolog(root, "SCSI Open Data Protection API Adapter",
		"Copyright 2026 GazillaByte Pty Ltd", __DATE__, __TIME__, NULL)) {
		cJSON_Delete(root);
		return NULL;
	}
	response = odpapi_request_get_value(root, "response");
	functions = cJSON_CreateArray();
	if (!functions) { cJSON_Delete(root); return NULL; }
	cJSON_AddItemToArray(functions, cJSON_CreateString("query"));
	cJSON_AddItemToArray(functions, cJSON_CreateString("list"));
	cJSON_AddItemToArray(functions, cJSON_CreateString("find"));
	cJSON_AddItemToArray(functions, cJSON_CreateString("inventory"));
	cJSON_AddItemToObject(response, "functions", functions);
	odpapi_response_epilog(root, ODPAPI_COMMONERROR_OK, NULL, time(NULL));
	serialized = odpapi_response_print(root);
	cJSON_Delete(root);
	return serialized;
}

char *scsi_error_json(enum ODPAPI_COMMONERROR error, const char *hint)
{
	cJSON *root = cJSON_CreateObject();
	char *serialized;

	if (!root || !odpapi_response_prolog(root, "SCSI Open Data Protection API Adapter",
		"Copyright 2026 GazillaByte Pty Ltd", __DATE__, __TIME__, NULL)) {
		cJSON_Delete(root);
		return NULL;
	}
	odpapi_response_epilog(root, error, (char *)hint, time(NULL));
	serialized = odpapi_response_print(root);
	cJSON_Delete(root);
	return serialized;
}

char *scsi_unknown_json(const char *hint)
{
	return scsi_error_json(ODPAPI_COMMONERROR_UNKNOWN,
		hint ? hint : "Try URL /dpapi-v1/query.json");
}

char *scsi_find_json(const struct scsi_library_inventory *libraries,
	size_t library_count, const char *volume_id)
{
	cJSON *root = cJSON_CreateObject();
	cJSON *response;
	cJSON *objects;
	char *serialized;
	size_t i;

	if (!root || !odpapi_response_prolog(root, "SCSI Open Data Protection API Adapter",
		"Copyright 2026 GazillaByte Pty Ltd", __DATE__, __TIME__, NULL)) {
		cJSON_Delete(root);
		return NULL;
	}
	response = odpapi_request_get_value(root, "response");
	objects = cJSON_CreateArray();
	if (!objects) { cJSON_Delete(root); return NULL; }
	cJSON_AddItemToObject(response, "objects", objects);
	if (!odpapi_find_validate_args(root, time(NULL), volume_id)) {
		serialized = odpapi_response_print(root);
		cJSON_Delete(root);
		return serialized;
	} else {
		for (i = 0; i < library_count; ++i) {
			size_t j;
			for (j = 0; j < libraries[i].volume_count; ++j) {
				char guid[37];
				if (strcmp(libraries[i].volumes[j].id, volume_id) != 0) continue;
				volume_guid(&libraries[i], &libraries[i].volumes[j], guid, sizeof(guid));
				odpapi_find_response_helper(objects, guid);
			}
		}
		odpapi_response_epilog(root, ODPAPI_COMMONERROR_OK, NULL, time(NULL));
	}
	serialized = odpapi_response_print(root);
	cJSON_Delete(root);
	return serialized;
}

static int volume_in_library(const struct scsi_library_inventory *library,
	const struct scsi_volume *volume, const char *source_library)
{
	char guid[37];
	volume_guid(library, volume, guid, sizeof(guid));
	return strcmp(guid, source_library) == 0;
}

static int library_guid_is_configured(const struct scsi_library_inventory *library,
	const char *source_library)
{
	char guid[37];
	int drive;
	if (library->slots > 0) {
		library_guid(library->identity, "storage", guid, sizeof(guid));
		if (strcmp(guid, source_library) == 0) return 1;
	}
	if (library->portals > 0) {
		library_guid(library->identity, "capio", guid, sizeof(guid));
		if (strcmp(guid, source_library) == 0) return 1;
	}
	for (drive = 0; drive < library->drives; ++drive) {
		char element_class[32];
		snprintf(element_class, sizeof(element_class), "drive:%d", drive);
		library_guid(library->identity, library->drive_identities[drive][0] ?
			library->drive_identities[drive] : element_class, guid, sizeof(guid));
		if (strcmp(guid, source_library) == 0) return 1;
	}
	if (library->transports > 0) {
		library_guid(library->identity, "transport", guid, sizeof(guid));
		if (strcmp(guid, source_library) == 0) return 1;
	}
	return 0;
}

char *scsi_inventory_json(const struct scsi_library_inventory *libraries,
	size_t library_count, const char *source_library)
{
	cJSON *root = cJSON_CreateObject();
	cJSON *response;
	cJSON *objects;
	size_t i;
	char *serialized;
	int matched = 0;

	if (!root || !odpapi_response_prolog(root, "SCSI Open Data Protection API Adapter",
		"Copyright 2026 GazillaByte Pty Ltd", __DATE__, __TIME__, NULL)) {
		cJSON_Delete(root);
		return NULL;
	}
	response = odpapi_request_get_value(root, "response");
	objects = cJSON_CreateArray();
	if (!objects) { cJSON_Delete(root); return NULL; }
	cJSON_AddItemToObject(response, "objects", objects);
	if (!odpapi_inventory_validate_args(root, time(NULL), source_library)) {
		serialized = odpapi_response_print(root);
		cJSON_Delete(root);
		return serialized;
	}
	for (i = 0; i < library_count; ++i) {
		size_t j;
		if (library_guid_is_configured(&libraries[i], source_library)) matched = 1;
		for (j = 0; j < libraries[i].volume_count; ++j) {
			const struct scsi_volume *volume = &libraries[i].volumes[j];
			char description[192];
			char *description_value = NULL;
			if (!volume_in_library(&libraries[i], volume, source_library)) continue;
			if (!library_guid_is_configured(&libraries[i], source_library)) continue;
			if (volume->element_type == CHET_DT &&
				volume->element >= 0 && volume->element < libraries[i].drives) {
				if (libraries[i].drive_device_numbers[volume->element] >= 0)
					snprintf(description, sizeof(description), "Drive-%d: %s (/dev/st%d)",
					volume->element + 1,
					libraries[i].drive_descriptions[volume->element],
					libraries[i].drive_device_numbers[volume->element]);
				else
					snprintf(description, sizeof(description), "Drive-%d: %s",
						volume->element + 1,
						libraries[i].drive_descriptions[volume->element]);
				description_value = description;
			}
			odpapi_inventory_response_helper(objects, ODPAPI_INVENTORYSTATUS_OK,
				(int)cJSON_GetArraySize(objects), (char *)volume->id,
				(char *)(libraries[i].media_type[0] ? libraries[i].media_type : "tape"),
				description_value,
				volume->element + 1, "", "", "", "", "", ODPAPISTATUS_UNKNOWN,
				ODPAPISTATUS_UNKNOWN, 0);
		}
	}
	odpapi_response_epilog(root, matched ? ODPAPI_COMMONERROR_OK :
		ODPAPI_COMMONERROR_SOURCELIBRARYNOTKNOWN, matched ? NULL :
		"source_library is not configured", time(NULL));
	serialized = odpapi_response_print(root);
	cJSON_Delete(root);
	return serialized;
}

