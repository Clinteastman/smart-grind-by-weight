#pragma once

#include <esp_http_server.h>

#include <functional>
#include <string>

//==============================================================================
// HTTP SERVER SUPPORT
//==============================================================================
// esp_http_server takes plain C handlers with a void* context. These helpers add
// the small amount of glue the device page, JSON API and setup portal need:
// capturing lambdas as route handlers, plus request parsing and response
// shorthands.

namespace http {

/** A route handler. Returning anything but ESP_OK closes the connection. */
using Handler = std::function<esp_err_t(httpd_req_t*)>;

/**
 * Register a route.
 *
 * The handler is copied and kept alive for the lifetime of the process; routes
 * are registered once during start-up and never removed.
 */
bool route(httpd_handle_t server, const char* uri, httpd_method_t method, Handler handler);

/**
 * Register a WebSocket endpoint.
 *
 * The handler is called once with `req->method == HTTP_GET` after a successful
 * handshake, through the server's post-handshake callback, then once per
 * received frame.
 */
bool websocket_route(httpd_handle_t server, const char* uri, Handler handler);

/** Register the handler invoked for any URI without a matching route. */
bool not_found(httpd_handle_t server, Handler handler);

//------------------------------------------------------------------------------
// Responses
//------------------------------------------------------------------------------

/** Send a complete response with an explicit status and content type. */
esp_err_t send(httpd_req_t* request, int status, const char* content_type,
               const std::string& body);

/** Send a JSON body. */
esp_err_t send_json(httpd_req_t* request, int status, const std::string& body);

/** Send a JSON `{"error": "..."}` body. */
esp_err_t send_error(httpd_req_t* request, int status, const char* message);

/** Send a 302 to `location`, used by the captive portal. */
esp_err_t send_redirect(httpd_req_t* request, const char* location);

/**
 * Stream a file from LittleFS.
 *
 * @param download When true, ask the browser to save rather than render it.
 */
esp_err_t send_file(httpd_req_t* request, const char* path, const char* content_type,
                    bool download);

//------------------------------------------------------------------------------
// Requests
//------------------------------------------------------------------------------

/** Read the whole request body, refusing anything larger than `max_bytes`. */
bool read_body(httpd_req_t* request, std::string& body, size_t max_bytes);

/**
 * Pump the request body through `sink` in chunks, for payloads far larger than
 * free memory.
 *
 * Gives up when the peer sends nothing for BODY_IDLE_TIMEOUT_MS: a client that
 * declares a Content-Length and then stalls would otherwise hold the HTTP task
 * forever, and an abandoned firmware upload leaves the grind and load-cell tasks
 * suspended until it returns.
 *
 * @return false when the socket fails, the peer stalls, or `sink` aborts.
 */
using BodySink = std::function<bool(const uint8_t* data, size_t length)>;

/** How long a body transfer may stall before it is abandoned. */
constexpr uint32_t BODY_IDLE_TIMEOUT_MS = 15000;
bool stream_body(httpd_req_t* request, const BodySink& sink);

/** Look up a URL query parameter. */
bool query_param(httpd_req_t* request, const char* key, std::string& value);
bool has_query_param(httpd_req_t* request, const char* key);

/** Look up a field in an application/x-www-form-urlencoded body. */
bool form_field(const std::string& body, const char* key, std::string& value);
bool has_form_field(const std::string& body, const char* key);

/** Request header value, or an empty string when absent. */
std::string header(httpd_req_t* request, const char* name);

/**
 * Reject cross-origin writes.
 *
 * A request without an Origin header is allowed (curl, the Python tools); one
 * with an Origin must match the Host it was sent to.
 */
bool origin_allowed(httpd_req_t* request);

/**
 * Wrap the handler of a route that changes device state: a cross-origin
 * request is answered 403 before the handler runs. Form and multipart POSTs
 * need no CORS preflight, so an unwrapped route could be submitted by a page on
 * another site that the user happens to have open.
 */
Handler same_origin(Handler handler);

/** Escape a value for embedding in a JSON string literal. */
std::string json_escape(const std::string& value);

}  // namespace http
