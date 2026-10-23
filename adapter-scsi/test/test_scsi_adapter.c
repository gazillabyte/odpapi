#include "scsi_adapter.h"

#include <assert.h>
#include <cJSON.h>
#include <linux/chio.h>
#include <string.h>

int main(void)
{
	struct scsi_volume volume = { "TAPE001", 1, 3, 1, 1 };
	struct scsi_library_inventory library = {
		.device_path = "/dev/changer",
		.changer_guid = "00000000-0000-5000-8000-000000000001",
		.description = "Test media changer",
		.media_type = "LTO",
		.slots = 10,
		.drives = 4,
		.portals = 1,
		.volume_count = 1,
		.volumes = &volume
	};
	char *list_json = scsi_list_json(&library, 1);
	char *json;
	cJSON *root;
	cJSON *response;
	cJSON *objects;
	cJSON *library_object;
	const char *storage_guid;
	cJSON *item;

	assert(list_json != NULL);
	root = cJSON_Parse(list_json);
	assert(root != NULL);
	response = cJSON_GetObjectItemCaseSensitive(root, "response");
	objects = cJSON_GetObjectItemCaseSensitive(response, "objects");
	assert(cJSON_IsArray(objects));
	library_object = cJSON_GetArrayItem(objects, 0);
	storage_guid = cJSON_GetObjectItem(library_object, "guid")->valuestring;
	assert(cJSON_GetObjectItem(library_object, "moving_in")->valueint == 0);
	assert(cJSON_GetObjectItem(library_object, "moving_out")->valueint == 0);
	assert(cJSON_GetObjectItem(library_object, "scratch_total")->valueint == 0);
	assert(cJSON_GetObjectItem(library_object, "scratch_minimum")->valueint == 0);
	json = scsi_inventory_json(&library, 1, storage_guid);
	cJSON_Delete(root);
	cJSON_free(list_json);
	assert(json != NULL);
	root = cJSON_Parse(json);
	assert(root != NULL);
	response = cJSON_GetObjectItemCaseSensitive(root, "response");
	objects = cJSON_GetObjectItemCaseSensitive(response, "objects");
	assert(cJSON_IsArray(objects));
	item = cJSON_GetArrayItem(objects, 0);
	assert(cJSON_GetObjectItem(item, "status")->valueint == 0);
	assert(strcmp(cJSON_GetObjectItem(item, "id")->valuestring, "TAPE001") == 0);
	assert(strcmp(cJSON_GetObjectItem(item, "type")->valuestring, "LTO") == 0);
	assert(cJSON_GetObjectItem(item, "slot")->valueint == 4);
	assert(cJSON_GetObjectItem(item, "encrypted") == NULL);
	assert(cJSON_GetObjectItem(item, "scratch") == NULL);
	cJSON_Delete(root);
	cJSON_free(json);
	return 0;
}

