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

#define ODPAPI_VERSION "1.0"

#ifdef WIN32
#define snprintf _snprintf
#define strdup _strdup
#endif

char* odpapi_format_common_result_text(char* buffer,                  
	                                   size_t length,                    
									   enum ODPAPI_COMMONERROR error)    
{
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
	cJSON	*tmp;
	time_t	end = time(NULL);
	int     totaltime = (int)difftime(end, start);
	char    timework[32];

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
		cJSON_AddItemToObject(root, "response", response);
		cJSON_AddNumberToObject(response, "result", error);
		cJSON_AddStringToObject(response, "result_text", work);
		cJSON_AddStringToObject(response, "transaction_seconds", timework);

		if ( hint )
		{
			cJSON_AddStringToObject(response, "result_hint", hint);
		}
	}
	else
	{
		if ( hint && strlen(hint) )
		{
			cJSON_AddStringToObject(response, "result_hint", hint);
		}

		cJSON_AddStringToObject(response, "transaction_seconds", timework);

		//*
		//* If we do have a response section then reset the result and result_text
		//*
		tmp = response->child;

		while ( tmp )
		{
			if ( tmp->string && (strcmp(tmp->string, "result")==0) )
			{
				tmp->valuedouble = tmp->valueint = error;
			}
			else
			{
				if ( tmp->string && (strcmp(tmp->string, "result_text")==0) )
				{
					if ( tmp->valuestring )
						free(tmp->valuestring);

					tmp->valuestring = strdup(work);
				}
			}

			tmp = tmp->next;
		}
	}
}

cJSON* odpapi_response_prolog(cJSON *root,
							  const char* adaptername,
							  const char* copyright,
							  const char* builddate,
							  const char* buildtime,
							  const char* token)
{
	cJSON		*meta = cJSON_CreateObject();
	cJSON		*response = cJSON_CreateObject();
	time_t		tNow;
	struct tm*	pTM;
	char		buffer[256];

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
	cJSON_AddStringToObject(meta, "creator", adaptername); 

	snprintf(buffer, 
		sizeof(buffer), 
		"ODPAPI Version: %s", 
		ODPAPI_VERSION);
	
	cJSON_AddStringToObject(meta, "version", buffer); 
	
	snprintf(buffer,
		sizeof(buffer),
		"%s at %s",
		builddate,
		buildtime);

	cJSON_AddStringToObject(meta, "built", buffer);

	cJSON_AddStringToObject(meta, "author", copyright);


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
		request = root->child->next;

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

	if ( json && json->child )
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
	cJSON* response = cJSON_CreateObject();
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
								      char* id,
								      char* type,
									  char* description,
									  unsigned int slot,
									  char* movedate,
									  char* target,
									  char* library,
									  char* lastmove,
									  enum ODPAPISTATUS encrypted,
									  enum ODPAPISTATUS scratch,
									  int automatemovedate)
{
	cJSON* response = cJSON_CreateObject();
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

	if (encrypted >= 0)
		cJSON_AddBoolToObject(response, "encrypted", encrypted != 0);
	if (scratch >= 0)
		cJSON_AddBoolToObject(response, "scratch", scratch != 0);

	if ( automatemovedate )
	{
		cJSON_AddBoolToObject(response, "automate_move_date", 1);
	}

	if (strlen(target))
	{
		cJSON_AddStringToObject(response, "target", target);
	}
	
	if (strlen(lastmove))
	{
		cJSON_AddStringToObject(response, "last_move_time", lastmove);
	}

	switch (status)
	{
	case ODPAPI_INVENTORYSTATUS_EXTERNAL:
	case ODPAPI_INVENTORYSTATUS_MOVINGIN:
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
	unsigned int	    i;
	cJSON*				response = cJSON_CreateObject();
	cJSON*				coords;
	cJSON*				targets;
	cJSON*				target;
	
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
	cJSON_AddItemToObject(response, "targets", targets);

	for(i=0; *targetguids; targetguids++, i++)
	{
		target = cJSON_CreateString(*targetguids);
		cJSON_AddItemToArray(targets, target);
	}

	cJSON_AddNumberToObject(response, "capacity_total", capacity);
	cJSON_AddNumberToObject(response, "capacity_used", capacityused);
	cJSON_AddNumberToObject(response, "moving_in", movingin);
	cJSON_AddNumberToObject(response, "moving_out", movingout);
	cJSON_AddNumberToObject(response, "scratch_total", scratch);
	cJSON_AddNumberToObject(response, "scratch_minimum", minscratch);

	if (latitude || longitude)
	{
		coords = cJSON_CreateObject();
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
	cJSON* response = cJSON_CreateObject();
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
	cJSON* response = cJSON_CreateObject();
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
