/**
 * @file odpapi.h
 * @brief Public C helpers for constructing and inspecting ODPAPI JSON messages.
 * @copyright Copyright 2026 GazillaByte Pty Ltd
 * @note SPDX-License-Identifier: Apache-2.0
 *
 * The helpers use the standard external cJSON library for JSON tree
 * representation. CMake supplies its include directory and link dependency.
 * Functions that add a
 * response item transfer ownership of the newly created cJSON item to the
 * supplied array or object, following cJSON's ownership rules.
 */
#ifndef ODPAPI_H
#define ODPAPI_H

#include <stddef.h>
#include <time.h>
#include <cJSON.h>

/** @brief Common result codes returned in an ODPAPI response. */
enum ODPAPI_COMMONERROR {
	ODPAPI_COMMONERROR_OK = 0,                 /**< Request completed successfully. */
	ODPAPI_COMMONERROR_PARSEFAILED = 1,        /**< Request JSON could not be parsed. */
	ODPAPI_COMMONERROR_NOTOFFERED = 2,         /**< Requested API option is unavailable. */
	ODPAPI_COMMONERROR_NOTPERMITTED = 3,       /**< Caller is not permitted to use the option. */
	ODPAPI_COMMONERROR_FAILEDAUTH = 4,         /**< Authentication failed. */
	ODPAPI_COMMONERROR_ERRORCOMEBACK = 5,       /**< Temporary service error; retry later. */
	ODPAPI_COMMONERROR_EXCEEDEDALLOWANCE = 6,  /**< API allowance was exceeded. */
	ODPAPI_COMMONERROR_INVALIDUUID = 7,        /**< A UUID could not be interpreted. */
	ODPAPI_COMMONERROR_MISSINGARGUMENT = 8,    /**< A required argument is missing. */
	ODPAPI_COMMONERROR_MISSINGDATA = 9,        /**< A required JSON payload is missing. */
	ODPAPI_COMMONERROR_ENDPOINTFAILED = 10,   /**< The endpoint could not be reached. */
	ODPAPI_COMMONERROR_SOURCELIBRARYNOTKNOWN = 100, /**< Source library is unknown. */
	ODPAPI_COMMONERROR_TARGETLIBRARYNOTKNOWN = 101, /**< Target library is unknown. */
	ODPAPI_COMMONERROR_TARGETLIBRARYSAME = 102, /**< Source and target libraries are identical. */
	ODPAPI_COMMONERROR_LIBRARYCOMPATPROBLEM = 103, /**< Libraries are incompatible. */
	ODPAPI_COMMONERROR_UNKNOWN = 1024          /**< Unclassified result. */
};

/** @brief Tri-state boolean used when a provider may not know a value. */
enum ODPAPISTATUS {
	ODPAPISTATUS_UNKNOWN = -1, /**< Value is unknown or unsupported. */
	ODPAPISTATUS_FALSE = 0,    /**< Value is explicitly false. */
	ODPAPISTATUS_TRUE = 1      /**< Value is explicitly true. */
};

/** @brief Status of an object reported by an inventory operation. */
enum ODPAPI_INVENTORYSTATUS {
	ODPAPI_INVENTORYSTATUS_OK = 0,        /**< Object is present and available. */
	ODPAPI_INVENTORYSTATUS_MOVINGIN = 1,  /**< Object is moving into the library. */
	ODPAPI_INVENTORYSTATUS_MOVINGOUT = 2, /**< Object is moving out of the library. */
	ODPAPI_INVENTORYSTATUS_NOTPRESENT = 3,/**< Object is not present. */
	ODPAPI_INVENTORYSTATUS_EXTERNAL = 4   /**< Object is reported by another library. */
};

/** @brief Per-object result codes for a move operation. */
enum ODPAPI_MOVESTATUS {
	ODPAPI_MOVESTATUS_OK = 0,              /**< Move completed successfully. */
	ODPAPI_MOVESTATUS_UNKNOWN = 1,          /**< Object is unknown. */
	ODPAPI_MOVESTATUS_NOTINLIBRARY = 2,     /**< Object is not in the source library. */
	ODPAPI_MOVESTATUS_DATAERROR = 3,        /**< Object data is invalid. */
	ODPAPI_MOVESTATUS_FAILED = 4,           /**< Move failed. */
	ODPAPI_MOVESTATUS_MOVENOTREQUIRED = 5,  /**< Object is already moving to the target. */
	ODPAPI_MOVESTATUS_INVALIDVALUE = 6      /**< Request contains an invalid value. */
};

/** @brief Per-object result codes for an update operation. */
enum ODPAPI_UPDATESTATUS {
	ODPAPI_UPDATESTATUS_OK = 0,            /**< Update completed successfully. */
	ODPAPI_UPDATESTATUS_UNKNOWN = 1,        /**< Object is unknown. */
	ODPAPI_UPDATESTATUS_NOTINLIBRARY = 2,   /**< Object is not in the specified library. */
	ODPAPI_UPDATESTATUS_DATAERROR = 3,      /**< Object data is invalid. */
	ODPAPI_UPDATESTATUS_FAILED = 4,         /**< Update failed. */
	ODPAPI_UPDATESTATUS_INVALIDVALUE = 5    /**< Request contains an invalid value. */
};

/** @brief Per-object result codes for a scan operation. */
enum ODPAPI_SCANSTATUS {
	ODPAPI_SCANSTATUS_OK = 0,            /**< Scan completed successfully. */
	ODPAPI_SCANSTATUS_DATAERROR = 1,     /**< Object data is invalid. */
	ODPAPI_SCANSTATUS_UNKNOWN = 2,       /**< Object is unknown. */
	ODPAPI_SCANSTATUS_FAILED = 3,        /**< Scan failed. */
	ODPAPI_SCANSTATUS_INVALIDVALUE = 4   /**< Request contains an invalid value. */
};

/** @brief Per-object result codes for an add operation. */
enum ODPAPI_ADDSTATUS {
	ODPAPI_ADDSTATUS_OK = 0,                  /**< Object was added successfully. */
	ODPAPI_ADDSTATUS_DATAERROR = 1,           /**< Object data is invalid. */
	ODPAPI_ADDSTATUS_EXISTS = 2,              /**< Object already exists in this library. */
	ODPAPI_ADDSTATUS_EXISTSOTHERLIBRARY = 3,  /**< Object exists in another library. */
	ODPAPI_ADDSTATUS_FAILED = 4,               /**< Add operation failed. */
	ODPAPI_ADDSTATUS_INVALIDVALUE = 5          /**< Request contains an invalid value. */
};

/** @brief Scan action requested for a volume. */
enum ODPAPI_SCANTYPE {
	ODPAPI_SCANTYPE_UNDEFINED = 0, /**< No scan action specified. */
	ODPAPI_SCANTYPE_IN = 1,        /**< Record an inbound scan. */
	ODPAPI_SCANTYPE_OUT = 2,       /**< Record an outbound scan. */
	ODPAPI_SCANTYPE_AUDIT = 3,     /**< Record an audit scan. */
	ODPAPI_SCANTYPE_PICKUP = 4     /**< Record a pickup scan. */
};

/** @brief Format a common result code as human-readable text.
 * @param[out] buffer Writable output buffer.
 * @param length Size of @p buffer in bytes; zero produces an empty result.
 * @param error Common ODPAPI result code.
 * @return The same pointer as @p buffer.
 */
char *odpapi_format_common_result_text(char *buffer, size_t length,
	                                   enum ODPAPI_COMMONERROR error);

/** @brief Add standard metadata and an initial response object to a JSON root.
 * @param root Existing cJSON object. The function adds `meta` and `response` children.
 * @param adaptername Adapter name written as `meta.creator`.
 * @param copyright Author/copyright text written as `meta.author`.
 * @param builddate Build date string.
 * @param buildtime Build time string.
 * @param token Optional token written to metadata; may be NULL.
 * @return The response object owned by @p root, or NULL if allocation fails.
 */
cJSON *odpapi_response_prolog(cJSON *root, const char *adaptername,
	const char *copyright, const char *builddate, const char *buildtime,
	const char *token);

/** @brief Set the common result fields and elapsed transaction time.
 * @param root Response document to update.
 * @param error Common ODPAPI result code.
 * @param hint Optional detail string; may be NULL.
 * @param start Start time from `time()` used to calculate whole elapsed seconds.
 */
void odpapi_response_epilog(cJSON *root, enum ODPAPI_COMMONERROR error,
	char *hint, time_t start);

/** @brief Validate Inventory arguments and populate a standard error response.
 * @param root Response document to update when validation fails.
 * @param start Start time from `time()` used to calculate elapsed seconds.
 * @param source_library Required source-library UUID.
 * @return Nonzero when all arguments are present; zero when the response was updated.
 */
int odpapi_inventory_validate_args(cJSON *root, time_t start,
	const char *source_library);

/** @brief Validate Find arguments and populate a standard error response.
 * @param root Response document to update when validation fails.
 * @param start Start time from `time()` used to calculate elapsed seconds.
 * @param id Required object identifier. Find callers and adapters must treat
 * this as an exact literal value, never as a regular expression or fnmatch
 * pattern.
 * @return Nonzero when the identifier is present; zero when the response was updated.
 */
int odpapi_find_validate_args(cJSON *root, time_t start, const char *id);

/** @brief Append a library GUID to a standard Find response.
 * @param responsearray Find response `objects` array.
 * @param guid Library GUID to append as a string.
 * @return Nonzero on success; zero when allocation or input validation fails.
 */
int odpapi_find_response_helper(cJSON *responsearray, const char *guid);

/** @brief Serialize an ODPAPI response as formatted JSON.
 * @param root Response document owned by the caller.
 * @return A heap-allocated JSON string, released with cJSON_free(), or NULL.
 */
char *odpapi_response_print(cJSON *root);

/** @brief Find a named child in a cJSON object (case-sensitive).
 * @param json Object or branch whose direct children are searched.
 * @param key NUL-terminated child name.
 * @return Matching child, or NULL when absent or arguments are NULL.
 */
cJSON *odpapi_request_get_value(cJSON *json, char *key);

/** @brief Find the `request` member in a parsed ODPAPI root object.
 * @param root Parsed root object.
 * @return Request member, or NULL if not found.
 */
cJSON *odpapi_request_get_request(cJSON *root);

/** @brief Append one move result object to a response array.
 * @param responsearray Destination cJSON array; takes ownership of the new item.
 * @param status Per-object move status.
 * @param sequence Request item sequence number.
 * @param id Optional object identifier.
 * @param type Optional object type.
 * @param movedate Optional move date in the protocol's date format.
 * @param hint Optional status text; when nonempty, overrides generated status text.
 */
void odpapi_move_response_helper(cJSON *responsearray, enum ODPAPI_MOVESTATUS status,
	int sequence, char *id, char *type, char *movedate, char *hint);

/** @brief Append one inventory result object to a response array.
 * @param responsearray Destination cJSON array; takes ownership of the new item.
 * @param status Inventory status.
 * @param sequence Request item sequence number.
 * @param id Optional object identifier.
 * @param type Optional object type.
 * @param description Optional object description.
	 * @param slot Optional 1-based library slot; zero omits the field.
 * @param movedate Optional move date.
	 * @param nextmove Optional predicted next movement date.
 * @param target Optional target library identifier.
 * @param library Library identifier, used for EXTERNAL and MOVINGIN statuses.
 * @param lastmove Optional last-move timestamp.
	 * @param encrypted Tri-state encryption status; UNKNOWN omits the field.
	 * @param scratch Tri-state scratch status; UNKNOWN omits the field.
 * @param automatemovedate Nonzero to report automatic move dates.
 */
void odpapi_inventory_response_helper(cJSON *responsearray,
	enum ODPAPI_INVENTORYSTATUS status, int sequence, const char *id, const char *type,
	const char *description, unsigned int slot, const char *movedate, const char *target,
	const char *nextmove, const char *library, const char *lastmove, enum ODPAPISTATUS encrypted,
	enum ODPAPISTATUS scratch,
	int automatemovedate);

/** @brief Append one scan result object to a response array.
 * @param responsearray Destination cJSON array; takes ownership of the new item.
 * @param status Per-object scan status.
 * @param sequence Request item sequence number.
 * @param id Optional object identifier.
 * @param type Optional object type.
 * @param hint Optional status text.
 */
void odpapi_scan_response_helper(cJSON *responsearray, enum ODPAPI_SCANSTATUS status,
	int sequence, char *id, char *type, char *hint);

/** @brief Append one add result object to a response array.
 * @param responsearray Destination cJSON array; takes ownership of the new item.
 * @param status Per-object add status.
 * @param sequence Request item sequence number.
 * @param id Optional object identifier.
 * @param hint Optional status text.
 */
void odpapi_add_response_helper(cJSON *responsearray, enum ODPAPI_ADDSTATUS status,
	int sequence, char *id, char *hint);

/** @brief Append and return one library description object.
 * @param responsearray Destination cJSON array; takes ownership of the new item.
 * @param sequence Request item sequence number.
 * @param description Library description.
 * @param guid Library GUID.
 * @param scope Optional library scope.
	 * @param capacity Total capacity; UINT_MAX omits the field.
 * @param capacityused Capacity currently used.
	 * @param movingin Count of objects moving in; UINT_MAX omits the field.
	 * @param movingout Count of objects moving out; UINT_MAX omits the field.
	 * @param scratch Scratch object count; UINT_MAX omits the field.
	 * @param minscratch Minimum scratch count; UINT_MAX omits the field.
 * @param automatemovedate Nonzero to enable automatic move dates.
 * @param latitude Latitude; coordinates are emitted when either coordinate is nonzero.
 * @param longitude Longitude; coordinates are emitted when either coordinate is nonzero.
 * @param targetguids NULL-terminated array of target library GUID strings.
 * @return The newly appended library object, or NULL if allocation fails.
 */
cJSON *odpapi_list_response_helper(cJSON *responsearray, int sequence,
	char *description, char *guid, char *scope, unsigned int capacity,
	unsigned int capacityused, unsigned int movingin, unsigned int movingout,
	unsigned int scratch, unsigned int minscratch, int automatemovedate,
	double latitude, double longitude, char **targetguids);

#endif /* ODPAPI_H */
