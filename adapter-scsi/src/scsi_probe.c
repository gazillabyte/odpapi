#define _XOPEN_SOURCE 700

#include "scsi_adapter.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/chio.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static void trim_copy(char *destination, size_t destination_length,
	const char *source)
{
	size_t length = strlen(source);
	while (length > 0 && (source[length - 1] == '\n' ||
		source[length - 1] == '\r' || source[length - 1] == ' ' ||
		source[length - 1] == '\t')) --length;
	if (length >= destination_length) length = destination_length - 1;
	memcpy(destination, source, length);
	destination[length] = '\0';
}

static int read_sysfs_value(const char *device_name, const char *attribute,
	char *value, size_t value_length)
{
	char path[256];
	char buffer[128];
	FILE *file;
	int count;

	count = snprintf(path, sizeof(path),
		"/sys/class/scsi_changer/%s/device/%s", device_name, attribute);
	if (count < 0 || (size_t)count >= sizeof(path)) return -1;
	file = fopen(path, "r");
	if (!file) return -1;
	if (!fgets(buffer, sizeof(buffer), file)) {
		fclose(file);
		return -1;
	}
	fclose(file);
	trim_copy(value, value_length, buffer);
	return value[0] ? 0 : -1;
}

static void discover_description(const char *path, char *description,
	size_t description_length)
{
	const char *device_name = strrchr(path, '/');
	char vendor[32];
	char model[64];
	int count;

	device_name = device_name ? device_name + 1 : path;
	if (read_sysfs_value(device_name, "vendor", vendor, sizeof(vendor)) == 0 &&
		read_sysfs_value(device_name, "model", model, sizeof(model)) == 0) {
		count = snprintf(description, description_length,
			"%s %s Media Changer", vendor, model);
		if (count >= 0 && (size_t)count < description_length) return;
	}
	strncpy(description, "Linux SCSI media changer", description_length - 1);
	description[description_length - 1] = '\0';
}

static int read_tape_value(int drive, const char *attribute, char *value,
	size_t value_length)
{
	char path[256];
	char buffer[128];
	FILE *file;

	if (snprintf(path, sizeof(path), "/sys/class/scsi_tape/st%d/device/%s",
		drive, attribute) >= (int)sizeof(path)) return -1;
	file = fopen(path, "r");
	if (!file) return -1;
	if (!fgets(buffer, sizeof(buffer), file)) {
		fclose(file);
		return -1;
	}
	fclose(file);
	trim_copy(value, value_length, buffer);
	return value[0] ? 0 : -1;
}

static int read_tape_address(int drive, int *target, int *lun)
{
	char path[PATH_MAX];
	char resolved[PATH_MAX];
	char *address;

	if (snprintf(path, sizeof(path), "/sys/class/scsi_tape/st%d/device", drive) >=
		(int)sizeof(path) || !realpath(path, resolved)) return -1;
	/* The symlink target ends in host:channel:target:lun. */
	address = strrchr(resolved, '/');
	if (!address || sscanf(address + 1, "%*d:%*d:%d:%d", target, lun) != 2)
		return -1;
	return 0;
}

static void discover_drive_description(int changer_fd, int element, int *device_number,
	char *description, size_t description_length)
{
	char vendor[32] = "";
	char model[64] = "";
	struct changer_get_element changer_element;
	int tape_target;
	int tape_lun;
	int drive;
	int have_vendor;
	int have_model;

	*device_number = -1;
	memset(&changer_element, 0, sizeof(changer_element));
	changer_element.cge_type = CHET_DT;
	changer_element.cge_unit = element;
	if (ioctl(changer_fd, CHIOGELEM, &changer_element) == 0 &&
		(changer_element.cge_flags & CGE_IDLUN) != 0) {
		for (drive = 0; drive < 256; ++drive) {
			if (read_tape_address(drive, &tape_target, &tape_lun) != 0 ||
				tape_target != changer_element.cge_id ||
				tape_lun != changer_element.cge_lun) continue;
			*device_number = drive;
			break;
		}
	}
	/* Some Linux changer drivers do not report ID/LUN for data-transfer
	 * elements. In that case retain the traditional element-order fallback,
	 * but only when the corresponding tape sysfs entry exists. */
	if (*device_number < 0 && element < 256) {
		char device_path[PATH_MAX];
		if (snprintf(device_path, sizeof(device_path),
			"/sys/class/scsi_tape/st%d/device", element) < (int)sizeof(device_path) &&
			access(device_path, F_OK) == 0)
			*device_number = element;
	}
	if (*device_number < 0) {
		strncpy(description, "Unknown tape drive", description_length - 1);
		description[description_length - 1] = '\0';
		return;
	}
	have_vendor = read_tape_value(*device_number, "vendor", vendor, sizeof(vendor)) == 0;
	have_model = read_tape_value(*device_number, "model", model, sizeof(model)) == 0;
	if (have_vendor && have_model)
		snprintf(description, description_length, "%s %s", vendor, model);
	else if (have_model)
		strncpy(description, model, description_length - 1);
	else if (have_vendor)
		strncpy(description, vendor, description_length - 1);
	else
		strncpy(description, "Unknown tape drive", description_length - 1);
	description[description_length - 1] = '\0';
}

static void copy_tag(char *destination, size_t destination_length,
	const char *source, size_t source_length)
{
	size_t length = 0;
	while (length < source_length && source[length] != '\0') ++length;
	while (length > 0 && source[length - 1] == ' ') --length;
	if (length >= destination_length) length = destination_length - 1;
	memcpy(destination, source, length);
	destination[length] = '\0';
}

static void fill_volume(int fd, int element_type, int element_number,
	const char *fallback, struct scsi_volume *volume)
{
	struct changer_get_element element;
	volume->element_type = element_type;
	volume->element = element_number;
	volume->full = 1;
	memset(&element, 0, sizeof(element));
	element.cge_type = element_type;
	element.cge_unit = element_number;
	if (ioctl(fd, CHIOGELEM, &element) == 0 &&
		(element.cge_flags & CGE_PVOLTAG) != 0) {
		copy_tag(volume->id, sizeof(volume->id), element.cge_pvoltag,
			sizeof(element.cge_pvoltag));
		volume->tagged = volume->id[0] != '\0';
	}
	if (!volume->tagged)
		snprintf(volume->id, sizeof(volume->id), "%s:%d", fallback,
			element_number);
}

int scsi_probe_library(const char *path, struct scsi_library_inventory *result,
	char *error, size_t error_length)
{
	struct changer_params params;
	unsigned char *status[4] = { NULL, NULL, NULL, NULL };
	const int types[4] = { CHET_MT, CHET_ST, CHET_IE, CHET_DT };
	int counts[4];
	int fd;
	int slot;
	size_t volume_count = 0;
	int type_index;

	if (!path || !result || !error || error_length == 0) return -1;
	fd = open(path, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		snprintf(error, error_length, "open: %s", strerror(errno));
		return -1;
	}
	memset(&params, 0, sizeof(params));
	if (ioctl(fd, CHIOGPARAMS, &params) < 0) {
		snprintf(error, error_length, "CHIOGPARAMS: %s", strerror(errno));
		close(fd);
		return -1;
	}
	counts[CHET_MT] = params.cp_npickers;
	counts[CHET_ST] = params.cp_nslots;
	counts[CHET_IE] = params.cp_nportals;
	counts[CHET_DT] = params.cp_ndrives;
	for (type_index = 0; type_index < 4; ++type_index) {
		int count = counts[types[type_index]];
		if (count < 0 || count > 100000 ||
			(types[type_index] == CHET_DT && count > SCSI_ADAPTER_MAX_DRIVES)) {
			snprintf(error, error_length, "invalid element count for type %d: %d",
				types[type_index], count);
			close(fd);
			return -1;
		}
	}
	for (type_index = 0; type_index < 4; ++type_index) {
		int type = types[type_index];
		status[type] = calloc((size_t)counts[type] ? (size_t)counts[type] : 1,
			sizeof(*status[type]));
		if (!status[type]) {
			snprintf(error, error_length, "unable to allocate element status");
			for (type_index = 0; type_index < 4; ++type_index) free(status[types[type_index]]);
			close(fd);
			return -1;
		}
		{
			struct changer_element_status request = { type, status[type] };
			if (counts[type] > 0 && ioctl(fd, CHIOGSTATUS, &request) < 0) {
				snprintf(error, error_length, "CHIOGSTATUS type %d: %s", type,
					strerror(errno));
				for (type_index = 0; type_index < 4; ++type_index) free(status[types[type_index]]);
				close(fd);
				return -1;
			}
		}
	}
	memset(result, 0, sizeof(*result));
	strncpy(result->device_path, path, sizeof(result->device_path) - 1);
	discover_description(path, result->description, sizeof(result->description));
	result->slots = params.cp_nslots;
	result->drives = params.cp_ndrives;
	result->portals = params.cp_nportals;
	result->transports = params.cp_npickers;
	for (slot = 0; slot < params.cp_ndrives; ++slot)
		discover_drive_description(fd, slot, &result->drive_device_numbers[slot],
			result->drive_descriptions[slot], sizeof(result->drive_descriptions[slot]));
	result->volume_count = 0;
	result->volumes = calloc((size_t)params.cp_nslots + (size_t)params.cp_nportals +
		(size_t)params.cp_ndrives, sizeof(*result->volumes));
	if (!result->volumes) {
		snprintf(error, error_length, "unable to allocate volume inventory");
		for (type_index = 0; type_index < 4; ++type_index) free(status[types[type_index]]);
		close(fd);
		return -1;
	}
	for (slot = 0; slot < params.cp_nslots; ++slot) {
		if (status[CHET_ST][slot] & CESTATUS_FULL)
			fill_volume(fd, CHET_ST, slot, "slot", &result->volumes[volume_count++]);
	}
	for (slot = 0; slot < params.cp_nportals; ++slot) {
		if (status[CHET_IE][slot] & CESTATUS_FULL)
			fill_volume(fd, CHET_IE, slot, "capio", &result->volumes[volume_count++]);
	}
	for (slot = 0; slot < params.cp_ndrives; ++slot) {
		if (status[CHET_DT][slot] & CESTATUS_FULL)
			fill_volume(fd, CHET_DT, slot, "drive", &result->volumes[volume_count++]);
	}
	result->volume_count = volume_count;
	for (type_index = 0; type_index < 4; ++type_index) free(status[types[type_index]]);
	close(fd);
	return 0;
}

