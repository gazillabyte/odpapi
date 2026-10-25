#include <httpd.h>
#include <http_config.h>
#include <http_log.h>
#include <http_protocol.h>
#include <http_request.h>
#include <util_script.h>
#include <apr_tables.h>
#include <apr_strings.h>

#include "scsi_adapter.h"

#include <cJSON.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* Keep the declaration local as well as in the public adapter header.  This
 * avoids collisions with vendor Apache include trees that may provide an
 * unrelated header with the same basename. */
char *scsi_find_json(const struct scsi_library_inventory *libraries,
	size_t library_count, const char *volume_id);
char *scsi_unknown_json(const char *hint);

typedef struct {
	const char *path;
	const char *media_type;
} scsi_changer_config;
typedef struct { const apr_array_header_t *changers; } scsi_server_config;

module AP_MODULE_DECLARE_DATA odpapi_scsi_module;

static int valid_uuid(const char *value)
{
	static const unsigned int dashes[] = { 8, 13, 18, 23 };
	unsigned int i;
	if (!value || strlen(value) != 36) return 0;
	for (i = 0; i < 36; ++i) {
		if (i == dashes[0] || i == dashes[1] || i == dashes[2] || i == dashes[3]) {
			if (value[i] != '-') return 0;
		} else if (!isxdigit((unsigned char)value[i])) return 0;
	}
	return 1;
}

static void *create_config(apr_pool_t *pool, server_rec *server)
{
	scsi_server_config *config = apr_pcalloc(pool, sizeof(*config));
	(void)server;
	config->changers = apr_array_make(pool, 2, sizeof(scsi_changer_config));
	return config;
}

static const char *set_changer(cmd_parms *cmd, void *unused, const char *path,
	const char *media_type)
{
	scsi_server_config *config = ap_get_module_config(cmd->server->module_config,
		&odpapi_scsi_module);
	scsi_changer_config *entry;
	(void)unused;
	if (!path || !*path) return "SCSIChangerDevice requires a device path";
	if (media_type && strlen(media_type) >= SCSI_ADAPTER_MEDIA_TYPE_MAX)
		return "SCSIChangerDevice media type is too long";
	entry = (scsi_changer_config *)apr_array_push((apr_array_header_t *)config->changers);
	entry->path = path;
	entry->media_type = media_type && *media_type ? media_type : "tape";
	return NULL;
}

static const command_rec commands[] = {
	AP_INIT_TAKE12("SCSIChangerDevice", set_changer, NULL, RSRC_CONF,
		"Linux SCSI media changer device node and optional media type"),
	{ NULL }
};

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
	if (request->remaining > (apr_off_t)limit)
		return HTTP_REQUEST_ENTITY_TOO_LARGE;
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

static int handler(request_rec *request)
{
	scsi_server_config *config;
	scsi_changer_config *changers;
	struct scsi_library_inventory *libraries;
	apr_table_t *args = NULL;
	const char *volume_id = NULL;
	const char *source_library = NULL;
	char error[256];
	char hint[1024];
	const char *host_header;
	char *serialized;
	apr_size_t i;
	size_t successful = 0;
	size_t failed = 0;
	int list_request;
	int find_request;
	int base_request;
	int query_request;
	int inventory_request;
	cJSON *input = NULL;
	cJSON *request_object = NULL;
	char *body = NULL;
	apr_size_t body_length = 0;
	int body_status;

	if (!request->handler || strcmp(request->handler, "odpapi-scsi-adapter") != 0)
		return DECLINED;
	if (request->args && strlen(request->args) > 16384)
		return HTTP_REQUEST_URI_TOO_LARGE;
	find_request = request->uri &&
		(strcmp(request->uri, "/dpapi-v1/find.json") == 0 ||
		 strcmp(request->uri, "/dpapi-v1/find") == 0);
	query_request = request->uri &&
		(strcmp(request->uri, "/dpapi-v1/query.json") == 0 ||
		 strcmp(request->uri, "/dpapi-v1/query") == 0);
	list_request = request->uri &&
		(strcmp(request->uri, "/dpapi-v1/list.json") == 0 ||
		 strcmp(request->uri, "/dpapi-v1/list") == 0);
	inventory_request = request->uri &&
		(strcmp(request->uri, "/dpapi-v1/inventory.json") == 0 ||
		 strcmp(request->uri, "/dpapi-v1/inventory") == 0);
	base_request = request->uri &&
		(strcmp(request->uri, "/dpapi-v1") == 0 ||
		 strcmp(request->uri, "/dpapi-v1/") == 0);
	/* ODPAPI v1 read-only routes are normally queried with GET.  Keep POST
	 * accepted for compatibility with the adapter's original endpoints. */
	if (strcmp(request->method, "GET") != 0 &&
		strcmp(request->method, "POST") != 0)
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
		serialized = scsi_unknown_json(hint);
		if (!serialized) return HTTP_INTERNAL_SERVER_ERROR;
		ap_set_content_type(request, "application/json");
		ap_rputs(serialized, request);
		ap_rputc('\n', request);
		cJSON_free(serialized);
		return OK;
	}
	if (query_request) {
		serialized = scsi_capabilities_json();
		if (!serialized) return HTTP_INTERNAL_SERVER_ERROR;
		ap_set_content_type(request, "application/json");
		ap_rputs(serialized, request);
		ap_rputc('\n', request);
		cJSON_free(serialized);
		return OK;
	}
	if (!query_request && !list_request && !find_request && !inventory_request) {
		serialized = scsi_unknown_json("Try URL /dpapi-v1/query.json");
		if (!serialized) return HTTP_INTERNAL_SERVER_ERROR;
		ap_set_content_type(request, "application/json");
		ap_rputs(serialized, request);
		ap_rputc('\n', request);
		cJSON_free(serialized);
		return OK;
	}
	if (find_request || inventory_request) {
		ap_args_to_table(request, &args);
		volume_id = args ? apr_table_get(args, "id") : NULL;
		source_library = args ? apr_table_get(args, "source_library") : NULL;
	}
	body_status = read_body(request, &body, &body_length);
	if (body_status != OK) return body_status;
	if (body_length != 0) {
		input = cJSON_ParseWithLength(body, body_length);
		if (!input || !cJSON_IsObject(input)) {
			serialized = scsi_error_json(ODPAPI_COMMONERROR_PARSEFAILED,
				"Request body is not a JSON object");
			if (!serialized) { cJSON_Delete(input); return HTTP_INTERNAL_SERVER_ERROR; }
			ap_set_content_type(request, "application/json");
			ap_rputs(serialized, request);
			ap_rputc('\n', request);
			cJSON_free(serialized);
			cJSON_Delete(input);
			return OK;
		}
		request_object = odpapi_request_get_request(input);
		if (!request_object || !cJSON_IsObject(request_object)) {
			serialized = scsi_error_json(ODPAPI_COMMONERROR_MISSINGDATA,
				"ODPAPI request object is missing");
			if (!serialized) { cJSON_Delete(input); return HTTP_INTERNAL_SERVER_ERROR; }
			ap_set_content_type(request, "application/json");
			ap_rputs(serialized, request);
			ap_rputc('\n', request);
			cJSON_free(serialized);
			cJSON_Delete(input);
			return OK;
		}
		if (!source_library)
			source_library = request_string(request_object, "source_library");
		if (!volume_id)
			volume_id = request_string(request_object, "id");
	}
	/* Validate required arguments before opening or probing any changer. */
	if (find_request && (!volume_id || !*volume_id)) {
		serialized = scsi_find_json(NULL, 0, volume_id);
		if (!serialized) return HTTP_INTERNAL_SERVER_ERROR;
		ap_set_content_type(request, "application/json");
		ap_rputs(serialized, request);
		ap_rputc('\n', request);
		cJSON_free(serialized);
		return OK;
	}
	if (inventory_request && (!source_library || !*source_library ||
		!valid_uuid(source_library))) {
		serialized = scsi_inventory_json(NULL, 0, source_library);
		if (!serialized) return HTTP_INTERNAL_SERVER_ERROR;
		ap_set_content_type(request, "application/json");
		ap_rputs(serialized, request);
		ap_rputc('\n', request);
		cJSON_free(serialized);
		return OK;
	}
	config = ap_get_module_config(request->server->module_config,
		&odpapi_scsi_module);
	if (!config->changers || config->changers->nelts == 0) {
		serialized = scsi_error_json(ODPAPI_COMMONERROR_ENDPOINTFAILED,
			"No SCSI changers are configured");
		if (!serialized) return HTTP_INTERNAL_SERVER_ERROR;
		ap_set_content_type(request, "application/json");
		ap_rputs(serialized, request);
		ap_rputc('\n', request);
		cJSON_free(serialized);
		return OK;
	}
	changers = (scsi_changer_config *)config->changers->elts;
	libraries = apr_pcalloc(request->pool,
		(size_t)config->changers->nelts * sizeof(*libraries));
	for (i = 0; i < (apr_size_t)config->changers->nelts; ++i) {
		error[0] = '\0';
		if (scsi_probe_library(changers[i].path, &libraries[successful], error,
			sizeof(error)) != 0)
		{
			++failed;
			ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, request,
				"SCSI changer probe failed for %s: %s", changers[i].path, error);
		}
		else {
			strncpy(libraries[successful].media_type, changers[i].media_type,
				sizeof(libraries[successful].media_type) - 1);
			++successful;
		}
	}
	serialized = find_request ? scsi_find_json(libraries, successful, volume_id) :
		list_request ? scsi_list_json(libraries, successful) :
		scsi_inventory_json(libraries, successful, source_library);
	if (serialized && failed > 0) {
		cJSON *partial = cJSON_Parse(serialized);
		if (partial) {
			odpapi_response_epilog(partial, ODPAPI_COMMONERROR_ENDPOINTFAILED,
				"One or more configured SCSI changers could not be probed", time(NULL));
			cJSON_free(serialized);
			serialized = odpapi_response_print(partial);
			cJSON_Delete(partial);
		}
	}
	for (i = 0; i < (apr_size_t)successful; ++i) free(libraries[i].volumes);
	if (!serialized) return HTTP_INTERNAL_SERVER_ERROR;
	ap_set_content_type(request, "application/json");
	ap_rputs(serialized, request);
	ap_rputc('\n', request);
	cJSON_free(serialized);
	cJSON_Delete(input);
	return OK;
}

static void register_hooks(apr_pool_t *pool)
{
	(void)pool;
	ap_hook_handler(handler, NULL, NULL, APR_HOOK_MIDDLE);
}

module AP_MODULE_DECLARE_DATA odpapi_scsi_module = {
	STANDARD20_MODULE_STUFF, NULL, NULL, create_config, NULL, commands,
	register_hooks, 0
};

