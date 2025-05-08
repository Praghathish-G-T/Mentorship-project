#ifndef API_HANDLER_H
#define API_HANDLER_H

#include <microhttpd.h>
#include "mentorship_data.h" // Includes AppData*

/**
 * @brief Main request handler function registered with MHD.
 * Routes requests to specific internal handlers based on URL and method.
 * Manages state for processing POST/PATCH request bodies.
 *
 * @param cls AppData* pointer.
 * @param connection MHD connection object.
 * @param url Requested URL.
 * @param method HTTP method.
 * @param version HTTP version.
 * @param upload_data Data chunk for POST/PATCH.
 * @param upload_data_size Size of upload_data chunk.
 * @param con_cls Per-connection context (for POST/PATCH state).
 * @return MHD_Result (MHD_YES or MHD_NO).
 */
enum MHD_Result request_handler(void *cls, struct MHD_Connection *connection,
                                const char *url, const char *method,
                                const char *version, const char *upload_data,
                                size_t *upload_data_size, void **con_cls);

#endif // API_HANDLER_H