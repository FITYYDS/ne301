/**
 * @file http_client.h
 * @brief HTTP/HTTPS client using ms_network (TCP/TLS)
 * @details Reference: ESP-IDF esp_http_client, transport via ms_network_port
 */

#ifndef __HTTP_CLIENT_H__
#define __HTTP_CLIENT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "ms_network.h"

/** Default HTTP/HTTPS port */
#define HTTP_DEFAULT_PORT  80
#define HTTPS_DEFAULT_PORT 443

/** Default buffer size and timeout */
#define HTTP_CLIENT_DEFAULT_BUF_SIZE   (1024)
#define HTTP_CLIENT_DEFAULT_TIMEOUT_MS (10000)

/** HTTP method */
typedef enum {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_DELETE,
    HTTP_METHOD_MAX
} http_client_method_t;

/** HTTP client handle (opaque) */
typedef struct http_client http_client_t;
typedef http_client_t *http_client_handle_t;

/** TLS config for HTTPS (same as ms_network) */
typedef network_tls_config_t http_client_tls_config_t;

/** HTTP client configuration */
typedef struct {
    const char *url;                     /**< Full URL (e.g. https://example.com/path). Overrides host/path when set */
    const char *host;                    /**< Host when url is not used */
    const char *path;                    /**< Path (default "/") when url is not used */
    int port;                            /**< Port (0 = 80 for HTTP, 443 for HTTPS) */
    http_client_method_t method;         /**< HTTP method */
    const char *post_data;               /**< POST body (for POST/PUT), may be NULL */
    int post_len;                        /**< POST body length (-1 = strlen(post_data)) */
    const char *user_agent;              /**< User-Agent header (NULL = default) */
    const char *content_type;            /**< Content-Type for POST (NULL = "application/octet-stream") */
    int timeout_ms;                     /**< Connection/recv timeout (0 = default) */
    int buffer_size;                     /**< Recv buffer size (0 = default) */
    const http_client_tls_config_t *tls_config; /**< TLS config: non-NULL = use HTTPS; NULL = HTTP (no TLS). When url is set, scheme from URL applies; NULL still means HTTPS without cert verify for https:// URLs */
} http_client_config_t;

/**
 * @brief Create HTTP client
 * @param config Configuration (can be NULL for defaults; url or host must be set before perform)
 * @return Client handle or NULL on failure
 */
http_client_handle_t http_client_init(const http_client_config_t *config);

/**
 * @brief Set URL (host, port, path, scheme from URL)
 * @param client Client handle
 * @param url Full URL (e.g. http://host/path or https://host:443/path)
 * @return 0 on success, negative on error
 */
int http_client_set_url(http_client_handle_t client, const char *url);

/**
 * @brief Set HTTP method
 */
int http_client_set_method(http_client_handle_t client, http_client_method_t method);

/**
 * @brief Set POST body (for POST/PUT)
 * @param data Body data (not copied; must remain valid until perform/cleanup)
 * @param len Length (-1 = strlen(data))
 */
int http_client_set_post_data(http_client_handle_t client, const char *data, int len);

/**
 * @brief Set request header (key: value). Call before perform.
 */
int http_client_set_header(http_client_handle_t client, const char *key, const char *value);

/**
 * @brief Perform HTTP request (blocking): connect, send request, receive headers.
 *        After success, use http_client_get_status_code(), http_client_get_content_length(),
 *        http_client_read() to read body, then http_client_close() or http_client_cleanup().
 * @param client Client handle
 * @return 0 on success, negative on error (e.g. connection failed, invalid URL)
 */
int http_client_perform(http_client_handle_t client);

/**
 * @brief Get response status code (e.g. 200). Valid after perform().
 */
int http_client_get_status_code(http_client_handle_t client);

/**
 * @brief Get Content-Length from response (-1 if chunked or absent).
 */
int64_t http_client_get_content_length(http_client_handle_t client);

/**
 * @brief Whether response is chunked encoding.
 */
bool http_client_is_chunked(http_client_handle_t client);

/**
 * @brief Read response body (after perform).
 * @param client Client handle
 * @param buffer Output buffer
 * @param len Buffer length
 * @return Bytes read, 0 on EOF, negative on error
 */
int http_client_read(http_client_handle_t client, char *buffer, int len);

/**
 * @brief Close connection; keep client for another request (e.g. new URL).
 */
void http_client_close(http_client_handle_t client);

/**
 * @brief Close and free client.
 */
void http_client_cleanup(http_client_handle_t client);

#ifdef __cplusplus
}
#endif

#endif /* __HTTP_CLIENT_H__ */
