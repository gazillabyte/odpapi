/**
 * @file odpapi.c
 * @brief Implementation of the ODPAPI C JSON helper functions.
 * @copyright Copyright 2026 GazillaByte Pty Ltd
 * @note SPDX-License-Identifier: Apache-2.0
 */
#include <odpapi.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>

#define ODPAPI_VERSION "1.0"

#ifdef WIN32
#define snprintf _snprintf
#define strdup _strdup
#endif

char* odpapi_format_common_result_text(char* buffer,
	                                   size_t length,
									   enum ODPAPI_COMMONERROR error)
{
	if (!buffer || length == 0) return buffer;
	memset(buffer, 0, length);

	switch(error)
	{
	default:
		snprintf(buffer, length, "Unknown error");
		break;
	case ODPAPI_COMMONERROR_OK:
		snprintf(buffer, length, "OK");
		break;
	case ODPAPI_COMMONERROR_PARSEFAILED:
		snprintf(buffer, length, "JSON parse error");
		break;
	case ODPAPI_COMMONERROR_NOTOFFERED:
		snprintf(buffer, length, "API option not offered");
		break;
	case ODPAPI_COMMONERROR_NOTPERMITTED:
		snprintf(buffer, length, "API option not permitted");
		break;
	case ODPAPI_COMMONERROR_FAILEDAUTH:
		snprintf(buffer, length, "Failed authorization");
		break;
	case ODPAPI_COMMONERROR_ERRORCOMEBACK:
		snprintf(buffer, length, "Service temporarily unavailable. Come back later");
		break;
	case ODPAPI_COMMONERROR_EXCEEDEDALLOWANCE:
		snprintf(buffer, length, "Exceeded API allowance");
		break;
	case ODPAPI_COMMONERROR_INVALIDUUID:
		snprintf(buffer, length, "Unable to interpret UUID");
		break;
	case ODPAPI_COMMONERROR_MISSINGARGUMENT:
		snprintf(buffer, length, "Missing argument");
		break;
	case ODPAPI_COMMONERROR_SOURCELIBRARYNOTKNOWN:
		snprintf(buffer, length, "Source Library is unknown");
		break;
	case ODPAPI_COMMONERROR_TARGETLIBRARYNOTKNOWN:
		snprintf(buffer, length, "Target Library is unknown");
		break;
	case ODPAPI_COMMONERROR_TARGETLIBRARYSAME:
		snprintf(buffer, length, "Target Library is the same as the Source Library");
		break;
	case ODPAPI_COMMONERROR_MISSINGDATA:
		snprintf(buffer, length, "The selected function requires a JSON file payload");
		break;
	case ODPAPI_COMMONERROR_ENDPOINTFAILED:
		snprintf(buffer, length, "Connection failure to endpoint");
		break;
	}

	return buffer;
}

void odpapi_response_epilog(cJSON *root,
							enum ODPAPI_COMMONERROR error,
							char* hint,
							time_t start)
{
	char	work[256];
	cJSON	*response = NULL;
	time_t	end = time(NULL);
	int     totaltime = (int)difftime(end, start);
	char    timework[32];
	cJSON *item;

	if (!root) return;

	sprintf(timework, "%d", totaltime);

	odpapi_format_common_result_text(work, sizeof(work), error);

	//*
	//* Get the first child
	//*
	response = odpapi_request_get_value(root, "response");

	//*
	//* If we don't have a response section then create one
	//*
	if ( response == NULL )
	{
		response = cJSON_CreateObject();
		if (!response) return;
		cJSON_AddItemToObject(root, "response", response);
	}

	item = cJSON_GetObjectItemCaseSensitive(response, "result");
	if (item && cJSON_IsNumber(item)) item->valuedouble = item->valueint = error;
	else cJSON_AddNumberToObject(response, "result", error);
	item = cJSON_GetObjectItemCaseSensitive(response, "result_text");
	if (item && cJSON_IsString(item)) cJSON_SetValuestring(item, work);
	else cJSON_AddStringToObject(response, "result_text", work);
	item = cJSON_GetObjectItemCaseSensitive(response, "transaction_seconds");
	if (item && cJSON_IsString(item)) cJSON_SetValuestring(item, timework);
	else cJSON_AddStringToObject(response, "transaction_seconds", timework);
	if (hint && *hint)
	{
		item = cJSON_GetObjectItemCaseSensitive(response, "result_hint");
		if (item && cJSON_IsString(item)) cJSON_SetValuestring(item, hint);
		else cJSON_AddStringToObject(response, "result_hint", hint);
	}
}

int odpapi_inventory_validate_args(cJSON *root, time_t start,
	const char *source_library)
{
	size_t i;
	if (!source_library || !*source_library) {
		odpapi_response_epilog(root, ODPAPI_COMMONERROR_MISSINGARGUMENT,
			"source_library is required", start);
		return 0;
	}
	if (strlen(source_library) != 36) {
		odpapi_response_epilog(root, ODPAPI_COMMONERROR_INVALIDUUID,
			"source_library is not a valid UUID", start);
		return 0;
	}
	for (i = 0; i < 36; ++i) {
		if (i == 8 || i == 13 || i == 18 || i == 23) {
			if (source_library[i] != '-') {
				odpapi_response_epilog(root, ODPAPI_COMMONERROR_INVALIDUUID,
					"source_library is not a valid UUID", start);
				return 0;
			}
		} else if (!isxdigit((unsigned char)source_library[i])) {
			odpapi_response_epilog(root, ODPAPI_COMMONERROR_INVALIDUUID,
				"source_library is not a valid UUID", start);
			return 0;
		}
	}
	return 1;
}

int odpapi_find_validate_args(cJSON *root, time_t start, const char *id)
{
	if (!id || !*id) {
		odpapi_response_epilog(root, ODPAPI_COMMONERROR_MISSINGARGUMENT,
			"id is required", start);
		return 0;
	}
	return 1;
}

int odpapi_find_response_helper(cJSON *responsearray, const char *guid)
{
	cJSON *item;
	if (!responsearray || !cJSON_IsArray(responsearray) || !guid || !*guid)
		return 0;
	item = cJSON_CreateString(guid);
	if (!item) return 0;
	cJSON_AddItemToArray(responsearray, item);
	return 1;
}

char *odpapi_response_print(cJSON *root)
{
	if (!root) return NULL;
	return cJSON_Print(root);
}

cJSON* odpapi_response_prolog(cJSON *root,
							  const char* adaptername,
							  const char* copyright,
							  const char* builddate,
							  const char* buildtime,
							  const char* token)
{
	cJSON		*meta;
	cJSON		*response;
	time_t		tNow;
	struct tm*	pTM;
	char		buffer[256];

	if (!root) return NULL;
	meta = cJSON_CreateObject();
	response = cJSON_CreateObject();
	if (!meta || !response) {
		cJSON_Delete(meta);
		cJSON_Delete(response);
		return NULL;
	}

	//*
	//* Set the time
	//*
	strcpy(buffer, "unknown-time");

	tNow = time(NULL);
	pTM = gmtime(&tNow);

	if ( pTM )
	{
		strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S GMT", pTM);
	}

	//*
	//* Add the Meta section
	//*
	cJSON_AddItemToObject(root, "meta", meta);
	cJSON_AddStringToObject(meta, "created", buffer);
	cJSON_AddStringToObject(meta, "creator", adaptername ? adaptername : "");

	snprintf(buffer,
		sizeof(buffer),
		"ODPAPI Version: %s",
		ODPAPI_VERSION);

	cJSON_AddStringToObject(meta, "version", buffer);

	snprintf(buffer,
		sizeof(buffer),
		"%s at %s",
		builddate ? builddate : "unknown-date",
		buildtime ? buildtime : "unknown-time");

	cJSON_AddStringToObject(meta, "built", buffer);

	cJSON_AddStringToObject(meta, "author", copyright ? copyright : "");


	//*
	//* Add the response section and add some placeholder data
	//*
	cJSON_AddItemToObject(root, "response", response);
	cJSON_AddNumberToObject(response, "result", ODPAPI_COMMONERROR_UNKNOWN);
	cJSON_AddStringToObject(response, "result_text", "");

	if (token)
	{
		cJSON_AddStringToObject(meta, "token", token);
	}

	return response;
}

cJSON* odpapi_request_get_request(cJSON *root)
{
	cJSON* request = NULL;

	if ( root && root->child )
	{
		request = root->child;

		while ( request )
		{
			if ( request->string && (strcmp(request->string, "request")==0) )
			{
				break;
			}

			request = request->next;
		}
	}

	return request;
}

cJSON* odpapi_request_get_value(cJSON *json, char* key)
{
	cJSON* result = NULL;

	if ( json && json->child && key )
	{
		result = json->child;

		while ( result )
		{
			if ( result->string && (strcmp(result->string, key)==0) )
			{
				break;
			}

			result = result->next;
		}
	}

	return result;
}

void odpapi_move_response_helper(cJSON *responsearray,
							     enum ODPAPI_MOVESTATUS status,
								 int sequence,
								 char* id,
								 char* type,
								 char* movedate,
								 char* hint)
{
	cJSON* response;
	if (!responsearray || !cJSON_IsArray(responsearray)) return;
	response = cJSON_CreateObject();
	if (!response) return;
	cJSON_AddItemToArray(responsearray, response);

	cJSON_AddNumberToObject(response, "sequence", sequence);
	cJSON_AddNumberToObject(response, "status", status);

	if ( hint && strlen(hint) )
	{
		cJSON_AddStringToObject(response, "status_hint", hint);
	}
	else
	{
		switch (status)
		{
		case ODPAPI_MOVESTATUS_OK:
			cJSON_AddStringToObject(response, "status_hint", "OK");
			break;
		case ODPAPI_MOVESTATUS_UNKNOWN:
			cJSON_AddStringToObject(response, "status_hint", "Undefined Object");
			break;
		case ODPAPI_MOVESTATUS_NOTINLIBRARY:
			cJSON_AddStringToObject(response, "status_hint", "Object not in source Library");
			break;
		case ODPAPI_MOVESTATUS_DATAERROR:
			cJSON_AddStringToObject(response, "status_hint", "Unhandled data errror");
			break;
		case ODPAPI_MOVESTATUS_FAILED:
			cJSON_AddStringToObject(response, "status_hint", "Move failed");
			break;
		case ODPAPI_MOVESTATUS_MOVENOTREQUIRED:
			cJSON_AddStringToObject(response, "status_hint", "Object is already moving to the Target Library");
			break;
		case ODPAPI_MOVESTATUS_INVALIDVALUE:
			cJSON_AddStringToObject(response, "status_hint", "Invalid data in request");
			break;
		}
	}

	if ( id && strlen(id) )
	{
		cJSON_AddStringToObject(response, "id", id);
	}

	if ( type && strlen(type) )
	{
		cJSON_AddStringToObject(response, "type", type);
	}

	if ( movedate && strlen(movedate) )
	{
		cJSON_AddStringToObject(response, "move_date", movedate);
	}
}

void odpapi_inventory_response_helper(cJSON *responsearray,
							          enum ODPAPI_INVENTORYSTATUS status,
								      int sequence,
					      const char* id,
					      const char* type,
						  const char* description,
									  unsigned int slot,
						  const char* movedate,
						  const char* target,
							  const char* nextmove,
						  const char* library,
						  const char* lastmove,
									  enum ODPAPISTATUS encrypted,
									  enum ODPAPISTATUS scratch,
									  int automatemovedate)
{
	cJSON* response;
	if (!responsearray || !cJSON_IsArray(responsearray)) return;
	response = cJSON_CreateObject();
	if (!response) return;
	cJSON_AddItemToArray(responsearray, response);

	cJSON_AddNumberToObject(response, "sequence", sequence);
	cJSON_AddNumberToObject(response, "status", status);

	if ( id && strlen(id) )
	{
		cJSON_AddStringToObject(response, "id", id);
	}

	if ( type && strlen(type) )
	{
		cJSON_AddStringToObject(response, "type", type);
	}

	if ( description && strlen(description) )
	{
		cJSON_AddStringToObject(response, "description", description);
	}

	if (slot > 0)
		cJSON_AddNumberToObject(response, "slot", slot);

	if ( movedate && strlen(movedate) )
	{
		cJSON_AddStringToObject(response, "move_date", movedate);
	}

	if ( nextmove && strlen(nextmove) )
	{
		cJSON_AddStringToObject(response, "next_move", nextmove);
	}

	if (encrypted >= 0)
		cJSON_AddBoolToObject(response, "encrypted", encrypted != 0);
	if (scratch >= 0)
		cJSON_AddBoolToObject(response, "scratch", scratch != 0);

	if ( automatemovedate )
	{
		cJSON_AddBoolToObject(response, "automate_move_date", 1);
	}

	if (target && strlen(target))
	{
		cJSON_AddStringToObject(response, "target", target);
	}

	if (lastmove && strlen(lastmove))
	{
		cJSON_AddStringToObject(response, "last_move_time", lastmove);
	}

	switch (status)
	{
	case ODPAPI_INVENTORYSTATUS_EXTERNAL:
	case ODPAPI_INVENTORYSTATUS_MOVINGIN:
		if (library && *library)
			cJSON_AddStringToObject(response, "library", library);
		break;
	default:
		break;
	}
}

cJSON* odpapi_list_response_helper(cJSON *responsearray,
							       int sequence,
								   char* description,
								   char* guid,
								   char* scope,
								   unsigned int capacity,
								   unsigned int capacityused,
								   unsigned int movingin,
								   unsigned int movingout,
								   unsigned int scratch,
								   unsigned int minscratch,
								   int automatemovedate,
								   double latitude,
								   double longitude,
								   char **targetguids)
{
	cJSON*				response;
	cJSON*				coords;
	cJSON*				targets;
	cJSON*				target;
	if (!responsearray || !cJSON_IsArray(responsearray)) return NULL;
	response = cJSON_CreateObject();
	if (!response) return NULL;

	cJSON_AddItemToArray(responsearray, response);
	cJSON_AddNumberToObject(response, "sequence", sequence);

	if ( automatemovedate )
	{
		cJSON_AddBoolToObject(response, "automate_move_date", 1);
	}

	if (scope)
	{
		cJSON_AddStringToObject(response, "scope", scope);
	}

	if ( guid && strlen(guid) )
	{
		cJSON_AddStringToObject(response, "guid", guid);
	}

	if ( description && strlen(description) )
	{
		cJSON_AddStringToObject(response, "description", description);
	}

	//*
	//* Now add each of the target GUIDs
	//*
	targets = cJSON_CreateArray();
	if (!targets) {
		cJSON_DeleteItemFromArray(responsearray,
			cJSON_GetArraySize(responsearray) - 1);
		return NULL;
	}
	cJSON_AddItemToObject(response, "targets", targets);

	for(; targetguids && *targetguids; targetguids++)
	{
		target = cJSON_CreateString(*targetguids);
		if (target) cJSON_AddItemToArray(targets, target);
	}

	if (capacity != UINT_MAX)
		cJSON_AddNumberToObject(response, "capacity_total", capacity);
	cJSON_AddNumberToObject(response, "capacity_used", capacityused);
	if (movingin != UINT_MAX)
		cJSON_AddNumberToObject(response, "moving_in", movingin);
	if (movingout != UINT_MAX)
		cJSON_AddNumberToObject(response, "moving_out", movingout);
	if (scratch != UINT_MAX)
		cJSON_AddNumberToObject(response, "scratch_total", scratch);
	if (minscratch != UINT_MAX)
		cJSON_AddNumberToObject(response, "scratch_minimum", minscratch);

	if (latitude || longitude)
	{
		coords = cJSON_CreateObject();
		if (!coords) return response;
		cJSON_AddItemToObject(response, "coordinates", coords);
		cJSON_AddNumberToObject(coords, "latitude", latitude);
		cJSON_AddNumberToObject(coords, "longitude", longitude);
	}

	return response;
}

void odpapi_scan_response_helper(cJSON *responsearray,
							    enum ODPAPI_SCANSTATUS status,
							    int sequence,
							    char* id,
							    char* type,
							    char* hint)
{
	cJSON* response;
	if (!responsearray || !cJSON_IsArray(responsearray)) return;
	response = cJSON_CreateObject();
	if (!response) return;
	cJSON_AddItemToArray(responsearray, response);

	cJSON_AddNumberToObject(response, "sequence", sequence);
	cJSON_AddNumberToObject(response, "status", status);

	if ( hint && strlen(hint) )
	{
		cJSON_AddStringToObject(response, "status_hint", hint);
	}

	if ( id && strlen(id) )
	{
		cJSON_AddStringToObject(response, "id", id);
	}

	if ( type && strlen(type) )
	{
		cJSON_AddStringToObject(response, "type", type);
	}
}

void odpapi_add_response_helper(cJSON *responsearray,
							    enum ODPAPI_ADDSTATUS status,
							    int sequence,
					            char* id,
							    char* hint)
{
	cJSON* response;
	if (!responsearray || !cJSON_IsArray(responsearray)) return;
	response = cJSON_CreateObject();
	if (!response) return;
	cJSON_AddItemToArray(responsearray, response);

	cJSON_AddNumberToObject(response, "sequence", sequence);
	cJSON_AddNumberToObject(response, "status", status);

	if ( hint && strlen(hint) )
	{
		cJSON_AddStringToObject(response, "status_hint", hint);
	}

	if ( id && strlen(id) )
	{
		cJSON_AddStringToObject(response, "id", id);
	}
}
