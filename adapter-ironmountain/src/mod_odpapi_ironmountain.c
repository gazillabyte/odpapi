/**
 * Read-only Apache adapter for the local Iron Mountain snapshot.
 * SFTP and credentials belong to ironmountain-sync.
 */
#include <httpd.h>
#include <http_config.h>
#include <http_core.h>
#include <http_protocol.h>
#include <http_request.h>
#include <util_script.h>
#include <apr_tables.h>
#include <apr_strings.h>

#include <cJSON.h>
#include <odpapi.h>
#include "ironmountain_report.h"
#include "ironmountain_database.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>

typedef struct {
	const char *database_dir;
} ironmountain_server_config;
module AP_MODULE_DECLARE_DATA odpapi_ironmountain_module;

static void *create_config(apr_pool_t *pool, server_rec *server)
{
	ironmountain_server_config *config = apr_pcalloc(pool, sizeof(*config));
	(void)server;
	config->database_dir = "/var/lib/ironmountain";
	return config;
}

static const char *set_database_dir(cmd_parms *cmd, void *unused, const char *value)
{
	ironmountain_server_config *config = ap_get_module_config(cmd->server->module_config,
		&odpapi_ironmountain_module);
	(void)unused;
	config->database_dir = value;
	return NULL;
}

static const command_rec commands[] = {
	AP_INIT_TAKE1("IronMountainDatabaseDir", set_database_dir, NULL, RSRC_CONF,
		"Directory containing the read-only Berkeley DB files"),
	{ NULL }
};

static cJSON *new_response(enum ODPAPI_COMMONERROR result, const char *hint)
{
	cJSON *root = cJSON_CreateObject();
	if (!root || !odpapi_response_prolog(root, "ODPAPI Iron Mountain adapter",
		"Copyright 2026 GazillaByte Pty Ltd", __DATE__, __TIME__, NULL)) {
		cJSON_Delete(root);
		return NULL;
	}
	odpapi_response_epilog(root, result, (char *)hint, time(NULL));
	return root;
}

static cJSON *capabilities_response(void)
{
	cJSON *root = cJSON_CreateObject();
	cJSON *response;
	cJSON *functions;
	if (!root || !odpapi_response_prolog(root, "ODPAPI Iron Mountain adapter",
		"Copyright 2026 GazillaByte Pty Ltd", __DATE__, __TIME__, NULL)) {
		cJSON_Delete(root);
		return NULL;
	}
	response = odpapi_request_get_value(root, "response");
	functions = cJSON_CreateArray();
	if (!functions) {
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToArray(functions, cJSON_CreateString("query"));
	cJSON_AddItemToArray(functions, cJSON_CreateString("list"));
	cJSON_AddItemToArray(functions, cJSON_CreateString("find"));
	cJSON_AddItemToArray(functions, cJSON_CreateString("inventory"));
	cJSON_AddItemToObject(response, "functions", functions);
	odpapi_response_epilog(root, ODPAPI_COMMONERROR_OK, NULL, time(NULL));
	return root;
}

static int read_body(request_rec *request, char **body, apr_size_t *length)
{
	const apr_size_t limit = 1024U * 1024U;
	apr_size_t total = 0;
	long read;
	char *buffer;
	int status = ap_setup_client_block(request, REQUEST_CHUNKED_DECHUNK);
	if (status != OK) return status;
	if (!ap_should_client_block(request)) {
		*body = apr_pstrdup(request->pool, "");
		*length = 0;
		return OK;
	}
	if (request->remaining > limit) return HTTP_REQUEST_ENTITY_TOO_LARGE;
	buffer = apr_palloc(request->pool, limit + 1U);
	while ((read = ap_get_client_block(request, buffer + total, limit - total)) > 0)
		total += (apr_size_t)read;
	if (read < 0) return HTTP_BAD_REQUEST;
	buffer[total] = '\0';
	*body = buffer;
	*length = total;
	return OK;
}

static const char *request_string(cJSON *request_object, const char *name)
{
	cJSON *value;
	if (!request_object || !name) return NULL;
	value = cJSON_GetObjectItemCaseSensitive(request_object, name);
	return cJSON_IsString(value) ? value->valuestring : NULL;
}

static void add_inventory(cJSON *objects, int sequence, const char *id,
	const struct irm_record *record, enum ODPAPI_INVENTORYSTATUS status)
{
	char last_move[32] = "";
	char next_move[32] = "";
	const date_t *latest = &record->in_date;
	if (irm_date_is_set(&record->out_date) &&
		(!irm_date_is_set(latest) || irm_date_compare(&record->out_date, latest) > 0))
		latest = &record->out_date;
	if (irm_date_is_set(latest))
		snprintf(last_move, sizeof(last_move), "%04d-%02d-%02d",
			latest->year, latest->month, latest->day);
	if (irm_date_is_set(&record->return_date))
		snprintf(next_move, sizeof(next_move), "%04d-%02d-%02d",
			record->return_date.year, record->return_date.month, record->return_date.day);
	odpapi_inventory_response_helper(objects, status, sequence, (char *)id,
		record->media_type, record->description, record->slot, "",
		"", next_move, "", last_move, ODPAPISTATUS_UNKNOWN, ODPAPISTATUS_UNKNOWN, 0);
}

static void add_library(cJSON *objects, int sequence, const char *customer, int in_library,
	unsigned int count)
{
	char description[64];
	char guid[64];
	char target_guid[64];
	char *targets[2];
	snprintf(description, sizeof(description), "Iron Mountain Customer(%s) %s",
		customer, in_library ? "In" : "Out");
	irm_library_uuid(customer, in_library, guid, sizeof(guid));
	irm_library_uuid(customer, !in_library, target_guid, sizeof(target_guid));
	targets[0] = target_guid;
	targets[1] = NULL;
	odpapi_list_response_helper(objects, sequence, description, guid, (char *)customer,
		UINT_MAX, count, UINT_MAX, UINT_MAX,
		UINT_MAX, UINT_MAX, 0, 0.0, 0.0, targets);
}

struct customer_summary {
	char customer[IRM_CUSTOMER_MAX + 1];
	unsigned int count[2];
};

struct customer_context {
	apr_array_header_t *customers;
};

static int collect_customer(const char *key, const struct irm_record *record, void *user)
{
	struct customer_context *context = user;
	struct customer_summary *summary;
	int state = (record->flags & IRM_FLAG_IN) ? 1 : 0;
	int i;
	(void)key;
	for (i = 0; i < context->customers->nelts; ++i) {
		summary = &APR_ARRAY_IDX(context->customers, i, struct customer_summary);
		if (strcmp(summary->customer, record->customer_id) == 0) {
			summary->count[state]++;
			return 1;
		}
	}
	summary = apr_array_push(context->customers);
	strncpy(summary->customer, record->customer_id, sizeof(summary->customer) - 1);
	summary->customer[sizeof(summary->customer) - 1] = '\0';
	summary->count[state] = 1;
	return 1;
}

struct requested_inventory_context {
	cJSON *objects;
	const char *id;
	const char *source_library;
	int sequence;
	int found;
};

static int emit_requested_inventory(const char *key,
	const struct irm_record *record, void *user)
{
	struct requested_inventory_context *context = user;
	char library_guid[64];
	(void)key;
	if (strcmp(context->id, record->volume_id) != 0)
		return 1;
	if (context->source_library) {
		irm_library_uuid(record->customer_id, (record->flags & IRM_FLAG_IN) != 0,
			library_guid, sizeof(library_guid));
		if (strcmp(context->source_library, library_guid) != 0)
			return 1;
	}
	add_inventory(context->objects, context->sequence++, context->id, record,
		ODPAPI_INVENTORYSTATUS_OK);
	context->found = 1;
	return 1;
}

static void emit_inventory(const char *media_index_path, const char *media_path,
	const char *id, int sequence,
	cJSON *objects, const char *source_library)
{
	struct requested_inventory_context context = {
		objects, id, source_library, sequence, 0
	};
	if (!irm_database_visit_media_id(media_index_path, media_path, id,
		emit_requested_inventory, &context) ||
		!context.found) {
		odpapi_inventory_response_helper(objects, ODPAPI_INVENTORYSTATUS_NOTPRESENT,
			sequence, (char *)id, "", "", 0, "", "", "", "",
			"", ODPAPISTATUS_UNKNOWN, ODPAPISTATUS_UNKNOWN, 0);
	}
}

struct find_context {
	cJSON *objects;
	const char *id;
	const char *source_library;
};

static int emit_find_result(const char *key, const struct irm_record *record,
	void *user)
{
	struct find_context *context = user;
	char library_guid[64];
	(void)key;
	if (context->source_library) {
		irm_library_uuid(record->customer_id, (record->flags & IRM_FLAG_IN) != 0,
			library_guid, sizeof(library_guid));
		if (strcmp(context->source_library, library_guid) != 0)
			return 1;
	}
	irm_library_uuid(record->customer_id, (record->flags & IRM_FLAG_IN) != 0,
		library_guid, sizeof(library_guid));
	return odpapi_find_response_helper(context->objects, library_guid);
}

static int emit_find(const char *media_index_path, const char *media_path,
	const char *id, cJSON *objects, const char *source_library)
{
	struct find_context context = { objects, id, source_library };
	return irm_database_visit_media_id(media_index_path, media_path, id,
		emit_find_result, &context);
}

struct filtered_inventory_context {
	cJSON *objects;
	int sequence;
	const char *source_library;
};

struct library_key_context {
	const char *guid;
	char key[IRM_LIBRARY_KEY_MAX + 1];
	int found;
};

static int find_library_key(const char *key, unsigned int count, void *user)
{
	struct library_key_context *context = user;
	char customer[IRM_CUSTOMER_ID_SIZE];
	char guid[64];
	size_t length;
	(void)count;
	if (!key) return 1;
	length = strlen(key);
	if (length < 2 || length > IRM_CUSTOMER_MAX + 1 ||
		(key[length - 1] != IRM_LIBRARY_IN_SUFFIX &&
		 key[length - 1] != IRM_LIBRARY_OUT_SUFFIX))
		return 1;
	memset(customer, 0, sizeof(customer));
	memcpy(customer, key, length - 1);
	irm_library_uuid(customer, key[length - 1] == IRM_LIBRARY_IN_SUFFIX,
		guid, sizeof(guid));
	if (strcmp(context->guid, guid) == 0) {
		memset(context->key, 0, sizeof(context->key));
		memcpy(context->key, customer, length - 1);
		context->key[IRM_CUSTOMER_MAX] = key[length - 1];
		context->found = 1;
	}
	return 1;
}

static int emit_filtered(const char *key, const struct irm_record *record, void *user)
{
	struct filtered_inventory_context *context = user;
	char library_guid[64];
	irm_library_uuid(record->customer_id, (record->flags & IRM_FLAG_IN) != 0,
		library_guid, sizeof(library_guid));
	if (context->source_library &&
		strcmp(context->source_library, library_guid) != 0)
		return 1;
	add_inventory(context->objects, context->sequence++, record->volume_id, record,
		ODPAPI_INVENTORYSTATUS_OK);
	return 1;
}

static const char *operation(const char *uri)
{
	if (!uri) return NULL;
	if (strcmp(uri, "/dpapi-v1/list") == 0 ||
		strcmp(uri, "/dpapi-v1/list.json") == 0) return "list";
	if (strcmp(uri, "/dpapi-v1/find") == 0 ||
		strcmp(uri, "/dpapi-v1/find.json") == 0) return "find";
	if (strcmp(uri, "/dpapi-v1/inventory") == 0 ||
		strcmp(uri, "/dpapi-v1/inventory.json") == 0) return "inventory";
	return NULL;
}

static int handler(request_rec *request)
{
	ironmountain_server_config *config;
	cJSON *input = NULL;
	cJSON *root = NULL;
	cJSON *response;
	cJSON *objects;
	cJSON *request_object;
	cJSON *requested_objects;
	char *body = NULL;
	apr_size_t body_length = 0;
	char *serialized;
	char hint[1024];
	const char *host_header;
	const char *op;
	const char *volume_id = NULL;
	const char *media_database;
	const char *library_database;
	const char *media_index_database;
	char library_key[IRM_LIBRARY_KEY_MAX + 1] = "";
	const char *source_library = NULL;
	apr_table_t *args = NULL;
	int status;
	int query_request;
	int base_request;
	int lock_fd = -1;
	char lock_path[4096];
	if (!request->handler || strcmp(request->handler, "odpapi-ironmountain-adapter") != 0)
		return DECLINED;
	query_request = request->uri &&
		(strcmp(request->uri, "/dpapi-v1/query") == 0 ||
		 strcmp(request->uri, "/dpapi-v1/query.json") == 0);
	base_request = request->uri &&
		(strcmp(request->uri, "/dpapi-v1") == 0 ||
		 strcmp(request->uri, "/dpapi-v1/") == 0);
	if (strcmp(request->method, "GET") != 0 && strcmp(request->method, "POST") != 0)
		return HTTP_METHOD_NOT_ALLOWED;
	if (base_request) {
		host_header = apr_table_get(request->headers_in, "Host");
		if (host_header && *host_header && strchr(host_header, ':')) {
			snprintf(hint, sizeof(hint), "Try URL http://%s/dpapi-v1/query.json",
				host_header);
		} else {
			snprintf(hint, sizeof(hint), "Try URL http://%s:%u/dpapi-v1/query.json",
				host_header && *host_header ? host_header :
					(request->hostname ? request->hostname : "localhost"),
				request->server ? request->server->port : 0);
		}
		root = new_response(ODPAPI_COMMONERROR_UNKNOWN, hint);
		if (!root) return HTTP_INTERNAL_SERVER_ERROR;
		serialized = odpapi_response_print(root);
		cJSON_Delete(root);
		if (!serialized) return HTTP_INTERNAL_SERVER_ERROR;
		ap_set_content_type(request, "application/json");
		ap_rwrite(serialized, strlen(serialized), request);
		ap_rwrite("\n", 1, request);
		cJSON_free(serialized);
		return OK;
	}
	if (query_request) {
		root = capabilities_response();
		if (!root) return HTTP_INTERNAL_SERVER_ERROR;
		serialized = odpapi_response_print(root);
		cJSON_Delete(root);
		if (!serialized) return HTTP_INTERNAL_SERVER_ERROR;
		ap_set_content_type(request, "application/json");
		ap_rwrite(serialized, strlen(serialized), request);
		ap_rwrite("\n", 1, request);
		cJSON_free(serialized);
		return OK;
	}
	config = ap_get_module_config(request->server->module_config, &odpapi_ironmountain_module);
	media_database = apr_pstrcat(request->pool, config->database_dir, "/IRMMEDIA.db", NULL);
	library_database = apr_pstrcat(request->pool, config->database_dir, "/IRMLIBRARY.db", NULL);
	media_index_database = apr_pstrcat(request->pool, config->database_dir, "/IRMMEDIAID.db", NULL);
	op = operation(request->uri);
	if (!op) {
		root = new_response(ODPAPI_COMMONERROR_UNKNOWN,
			"Try URL /dpapi-v1/query.json");
		goto output;
	}
	if (strcmp(op, "inventory") == 0 || strcmp(op, "find") == 0) {
		ap_args_to_table(request, &args);
		source_library = args ? apr_table_get(args, "source_library") : NULL;
		volume_id = args ? apr_table_get(args, "id") : NULL;
	}
	status = read_body(request, &body, &body_length);
	if (status != OK) return status;
	if (body_length != 0) {
		input = cJSON_ParseWithLength(body, body_length);
		if (!input || !cJSON_IsObject(input)) {
			root = new_response(ODPAPI_COMMONERROR_PARSEFAILED, "Request body is not a JSON object");
			goto output;
		}
		request_object = odpapi_request_get_request(input);
		if (!request_object || !cJSON_IsObject(request_object)) {
			root = new_response(ODPAPI_COMMONERROR_MISSINGDATA, "ODPAPI request object is missing");
			goto output;
		}
		/* Query arguments take precedence, while JSON request members provide
		 * the equivalent body-based form for POST clients. */
		if (!source_library)
			source_library = request_string(request_object, "source_library");
		if (!volume_id)
			volume_id = request_string(request_object, "id");
	} else {
		request_object = NULL;
	}
	root = new_response(ODPAPI_COMMONERROR_OK, NULL);
	if (!root) goto output;
	if (strcmp(op, "inventory") == 0) {
		if (!odpapi_inventory_validate_args(root, time(NULL), source_library))
			goto output;
	}
	if (strcmp(op, "find") == 0) {
		if (!odpapi_find_validate_args(root, time(NULL), volume_id))
			goto output;
	}
	response = odpapi_request_get_value(root, "response");
	objects = cJSON_CreateArray();
	cJSON_AddItemToObject(response, "objects", objects);
	if (snprintf(lock_path, sizeof(lock_path), "%s/IRMMEDIA.db.lock",
		config->database_dir) >= (int)sizeof(lock_path) ||
		(lock_fd = open(lock_path, O_RDONLY)) < 0 ||
		flock(lock_fd, LOCK_SH) != 0) {
		odpapi_response_epilog(root, ODPAPI_COMMONERROR_ENDPOINTFAILED,
			"Unable to lock Berkeley DB snapshot", time(NULL));
		goto output;
	}
	if (!irm_database_snapshot_ready(media_database)) {
		odpapi_response_epilog(root, ODPAPI_COMMONERROR_ENDPOINTFAILED,
			"Berkeley DB snapshot publication is incomplete", time(NULL));
		goto output;
	}
	if (strcmp(op, "inventory") == 0) {
		struct library_key_context context = { source_library, "", 0 };
		if (!irm_database_visit_libraries(library_database, find_library_key, &context) ||
			!context.found) {
			odpapi_response_epilog(root, ODPAPI_COMMONERROR_SOURCELIBRARYNOTKNOWN,
				"source_library is not configured", time(NULL));
			goto output;
		}
		memcpy(library_key, context.key, sizeof(library_key));
	}
	if (strcmp(op, "list") == 0) {
		struct customer_context context;
		apr_array_header_t *customers = apr_array_make(request->pool, 4,
			sizeof(struct customer_summary));
		context.customers = customers;
		if (!customers || !irm_database_visit(media_database, collect_customer, &context)) {
			odpapi_response_epilog(root, ODPAPI_COMMONERROR_ENDPOINTFAILED,
				"Unable to read Berkeley DB", time(NULL));
		} else {
			int i;
			for (i = 0; i < customers->nelts; ++i) {
				struct customer_summary *summary = &APR_ARRAY_IDX(customers, i,
					struct customer_summary);
				add_library(objects, i * 2, summary->customer, 1, summary->count[1]);
				add_library(objects, i * 2 + 1, summary->customer, 0, summary->count[0]);
			}
		}
	} else if (strcmp(op, "inventory") == 0) {
		if (volume_id) {
			struct requested_inventory_context context = {
				objects, volume_id, source_library, 0, 0
			};
			if (!irm_database_visit_library(library_database, media_database,
				library_key, emit_requested_inventory, &context))
				odpapi_response_epilog(root, ODPAPI_COMMONERROR_ENDPOINTFAILED,
					"Unable to read Berkeley DB", time(NULL));
			else if (!context.found)
				odpapi_inventory_response_helper(objects, ODPAPI_INVENTORYSTATUS_NOTPRESENT,
					0, (char *)volume_id, "", "", 0, "", "", "", "",
					"", ODPAPISTATUS_UNKNOWN, ODPAPISTATUS_UNKNOWN, 0);
		} else {
			struct filtered_inventory_context context = { objects, 0, source_library };
			if (!irm_database_visit_library(library_database, media_database,
				library_key, emit_filtered, &context))
				odpapi_response_epilog(root, ODPAPI_COMMONERROR_ENDPOINTFAILED,
					"Unable to read Berkeley DB", time(NULL));
		}
	} else if (strcmp(op, "find") == 0) {
		if (!emit_find(media_index_database, media_database, volume_id, objects,
			source_library))
			odpapi_response_epilog(root, ODPAPI_COMMONERROR_ENDPOINTFAILED,
				"Unable to read Berkeley DB", time(NULL));
	} else {
		if (volume_id) {
			emit_inventory(media_index_database, media_database, volume_id, 0,
				objects, source_library);
		} else {
			requested_objects = odpapi_request_get_value(request_object, "objects");
			if (!requested_objects || !cJSON_IsArray(requested_objects)) {
			struct filtered_inventory_context context = { objects, 0, source_library };
			if (!irm_database_visit(media_database, emit_filtered, &context))
				odpapi_response_epilog(root, ODPAPI_COMMONERROR_ENDPOINTFAILED,
					"Unable to read Berkeley DB", time(NULL));
			} else {
			int sequence = 0;
			cJSON *item;
			cJSON_ArrayForEach(item, requested_objects) {
				cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
				if (cJSON_IsString(id) && id->valuestring)
					emit_inventory(media_index_database, media_database, id->valuestring,
						sequence++, objects, source_library);
			}
		}
		}
	}

output:
	if (lock_fd >= 0) {
		(void)flock(lock_fd, LOCK_UN);
		(void)close(lock_fd);
	}
	cJSON_Delete(input);
	if (!root) return HTTP_INTERNAL_SERVER_ERROR;
	serialized = odpapi_response_print(root);
	cJSON_Delete(root);
	if (!serialized) return HTTP_INTERNAL_SERVER_ERROR;
	ap_set_content_type(request, "application/json");
	ap_rwrite(serialized, strlen(serialized), request);
	ap_rwrite("\n", 1, request);
	cJSON_free(serialized);
	return OK;
}

static void register_hooks(apr_pool_t *pool)
{
	(void)pool;
	ap_hook_handler(handler, NULL, NULL, APR_HOOK_MIDDLE);
}

module AP_MODULE_DECLARE_DATA odpapi_ironmountain_module = {
	STANDARD20_MODULE_STUFF, NULL, NULL, create_config, NULL, commands, register_hooks
};
