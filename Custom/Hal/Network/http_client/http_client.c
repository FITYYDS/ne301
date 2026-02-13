/**
 * @file http_client.c
 * @brief HTTP/HTTPS client implementation using ms_network
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>
#include "Log/debug.h"
#include "mem.h"
#include "http_client.h"
#include "ms_network.h"

#define DEFAULT_USER_AGENT   "HttpClient/1.0"
#define HTTP_PROTOCOL        "HTTP/1.1"
#define CRLF                 "\r\n"
#define HEADER_SEP           "\r\n\r\n"

#define HTTP_ERR_BASE         (-0x1000)
#define HTTP_ERR_INVALID_ARG  (HTTP_ERR_BASE - 1)
#define HTTP_ERR_NO_MEM       (HTTP_ERR_BASE - 2)
#define HTTP_ERR_PARSE_URL    (HTTP_ERR_BASE - 3)
#define HTTP_ERR_CONNECT      (HTTP_ERR_BASE - 4)
#define HTTP_ERR_SEND         (HTTP_ERR_BASE - 5)
#define HTTP_ERR_RECV         (HTTP_ERR_BASE - 6)
#define HTTP_ERR_PARSE        (HTTP_ERR_BASE - 7)

/** Single custom header (key + value) */
typedef struct {
    char *key;
    char *value;
} header_entry_t;

#define MAX_HEADERS 8

/** Internal client state */
struct http_client {
    /* Connection info (from URL or config) */
    char *host;
    char *path;
    int port;
    int use_https;
    http_client_method_t method;
    const char *post_data;
    int post_len;
    char *user_agent;
    char *content_type;
    int timeout_ms;
    int buffer_size;
    const http_client_tls_config_t *tls_config;

    /* Custom headers */
    header_entry_t headers[MAX_HEADERS];
    int num_headers;

    /* Transport */
    ms_network_handle_t network;

    /* Response state (after perform) */
    int status_code;
    int64_t content_length;   /* -1 = chunked or unknown */
    int is_chunked;
    char *recv_buf;           /* buffer for recv */
    int recv_buf_len;
    int recv_buf_off;         /* consumed offset in recv_buf (body start) */
    int recv_buf_used;        /* total bytes in recv_buf (including body) */
    int64_t body_read;        /* bytes of body already read */
    /* Chunked state */
    int chunk_remaining;      /* bytes left in current chunk (0 = need to read chunk size line) */
};

static const char *method_str[] = {
    "GET", "POST", "PUT", "HEAD", "DELETE"
};

/* Parse URL: "http://host[:port]/path" or "https://host[:port]/path" */
static int parse_url(http_client_handle_t client, const char *url)
{
    const char *p, *host_start, *host_end, *port_start, *path_start;
    char port_buf[8];
    int port = 0;

    if (client == NULL || url == NULL) return HTTP_ERR_INVALID_ARG;

    p = url;
    if (strncmp(p, "https://", 8) == 0) {
        client->use_https = 1;
        client->port = HTTPS_DEFAULT_PORT;
        host_start = p + 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        client->use_https = 0;
        client->port = HTTP_DEFAULT_PORT;
        host_start = p + 7;
    } else {
        return HTTP_ERR_PARSE_URL;
    }

    /* host_end = first ':' or '/' */
    host_end = host_start;
    while (*host_end && *host_end != ':' && *host_end != '/') host_end++;
    if (host_end == host_start) return HTTP_ERR_PARSE_URL;

    client->host = (char *)hal_mem_alloc(host_end - host_start + 1, MEM_LARGE);
    if (client->host == NULL) return HTTP_ERR_NO_MEM;
    memcpy(client->host, host_start, host_end - host_start);
    client->host[host_end - host_start] = '\0';

    if (*host_end == ':') {
        port_start = host_end + 1;
        path_start = port_start;
        while (*path_start && *path_start != '/') path_start++;
        if (path_start > port_start && (path_start - port_start) < (int)sizeof(port_buf)) {
            memcpy(port_buf, port_start, path_start - port_start);
            port_buf[path_start - port_start] = '\0';
            port = atoi(port_buf);
            if (port > 0 && port < 65536) client->port = port;
        }
        if (*path_start == '/') {
            path_start = path_start;
        } else {
            path_start = ""; /* no path */
        }
    } else {
        path_start = host_end;
    }

    if (*path_start == '/') {
        size_t path_len = strlen(path_start);
        client->path = (char *)hal_mem_alloc(path_len + 1, MEM_LARGE);
        if (client->path == NULL) {
            hal_mem_free(client->host);
            client->host = NULL;
            return HTTP_ERR_NO_MEM;
        }
        memcpy(client->path, path_start, path_len + 1);
    } else {
        client->path = (char *)hal_mem_alloc(2, MEM_LARGE);
        if (client->path == NULL) {
            hal_mem_free(client->host);
            client->host = NULL;
            return HTTP_ERR_NO_MEM;
        }
        client->path[0] = '/';
        client->path[1] = '\0';
    }
    return 0;
}

static void free_connection_info(http_client_handle_t client)
{
    if (client->host) { hal_mem_free(client->host); client->host = NULL; }
    if (client->path) { hal_mem_free(client->path); client->path = NULL; }
    if (client->user_agent) { hal_mem_free(client->user_agent); client->user_agent = NULL; }
    if (client->content_type) { hal_mem_free(client->content_type); client->content_type = NULL; }
    for (int i = 0; i < client->num_headers; i++) {
        if (client->headers[i].key) hal_mem_free(client->headers[i].key);
        if (client->headers[i].value) hal_mem_free(client->headers[i].value);
        client->headers[i].key = client->headers[i].value = NULL;
    }
    client->num_headers = 0;
}

/* Find end of line (\r\n or \n). Return length including terminator, or 0 if not found. */
static int find_line_len(const char *buf, int len)
{
    int i = 0;
    while (i < len) {
        if (buf[i] == '\n') return i + 1;
        if (buf[i] == '\r' && i + 1 < len && buf[i + 1] == '\n') return i + 2;
        i++;
    }
    return 0;
}

/* Read until we have a complete line in recv_buf; may read more. Return line length or negative. */
static int read_line(http_client_handle_t client, char *line_buf, int line_max, int *out_line_len)
{
    int r, line_len = 0;
    uint32_t to = (client->timeout_ms > 0) ? (uint32_t)client->timeout_ms : (uint32_t)HTTP_CLIENT_DEFAULT_TIMEOUT_MS;

    *out_line_len = 0;
    while (1) {
        if (client->recv_buf_used > client->recv_buf_off) {
            line_len = find_line_len(client->recv_buf + client->recv_buf_off, client->recv_buf_used - client->recv_buf_off);
            if (line_len > 0) {
                int copy = line_len;
                if (copy > line_max - 1) copy = line_max - 1;
                memcpy(line_buf, client->recv_buf + client->recv_buf_off, copy);
                line_buf[copy] = '\0';
                /* trim \r\n */
                if (copy >= 2 && line_buf[copy - 2] == '\r') line_buf[copy - 2] = '\0';
                else if (copy >= 1 && line_buf[copy - 1] == '\n') line_buf[copy - 1] = '\0';
                *out_line_len = line_len;
                client->recv_buf_off += line_len;
                return 0;
            }
        }
        if (client->recv_buf_off > 0) {
            memmove(client->recv_buf, client->recv_buf + client->recv_buf_off, client->recv_buf_used - client->recv_buf_off);
            client->recv_buf_used -= client->recv_buf_off;
            client->recv_buf_off = 0;
        }
        if (client->recv_buf_used >= client->recv_buf_len) return HTTP_ERR_PARSE;
        r = ms_network_recv(client->network, (uint8_t *)(client->recv_buf + client->recv_buf_used),
                            client->recv_buf_len - client->recv_buf_used, to);
        if (r <= 0) return (r < 0) ? HTTP_ERR_RECV : HTTP_ERR_PARSE;
        client->recv_buf_used += r;
    }
}

/* Parse "HTTP/1.1 200 OK" -> 200 */
static int parse_status_line(const char *line, int *code)
{
    const char *p = line;
    while (*p && *p != ' ') p++;
    if (*p != ' ') return -1;
    p++;
    if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1]) || !isdigit((unsigned char)p[2])) return -1;
    *code = ((p[0] - '0') * 100) + ((p[1] - '0') * 10) + (p[2] - '0');
    return 0;
}

/* Parse "Content-Length: 123" or "Transfer-Encoding: chunked" (compare key only, before ':') */
static void parse_header_line(http_client_handle_t client, const char *line)
{
    const char *colon = strchr(line, ':');
    if (colon == NULL) return;
    if ((size_t)(colon - line) == 14 && strncasecmp(line, "Content-Length", 14) == 0 && colon[1] == ' ') {
        client->content_length = (int64_t)atoll(colon + 2);
    } else if ((size_t)(colon - line) == 17 && strncasecmp(line, "Transfer-Encoding", 17) == 0) {
        const char *v = colon + 1;
        while (*v == ' ') v++;
        if (strncasecmp(v, "chunked", 7) == 0) client->is_chunked = 1;
    }
}

http_client_handle_t http_client_init(const http_client_config_t *config)
{
    http_client_handle_t client;
    client = (http_client_handle_t)hal_mem_alloc_large(sizeof(struct http_client));
    if (client == NULL) return NULL;
    memset(client, 0, sizeof(struct http_client));

    client->port = 0;
    client->content_length = -1;
    client->timeout_ms = HTTP_CLIENT_DEFAULT_TIMEOUT_MS;
    client->buffer_size = HTTP_CLIENT_DEFAULT_BUF_SIZE;
    client->chunk_remaining = -1;

    if (config != NULL) {
        if (config->url) {
            if (parse_url(client, config->url) != 0) {
                hal_mem_free(client);
                return NULL;
            }
        } else if (config->host) {
            size_t hl = strlen(config->host) + 1;
            size_t pl = (config->path && config->path[0]) ? (strlen(config->path) + 1) : 2;
            client->host = (char *)hal_mem_alloc(hl, MEM_LARGE);
            client->path = (char *)hal_mem_alloc(pl, MEM_LARGE);
            if (client->host) memcpy(client->host, config->host, hl);
            if (client->path) memcpy(client->path, (config->path && config->path[0]) ? config->path : "/", pl);
            client->use_https = (config->tls_config != NULL);
            client->port = config->port > 0 ? config->port : (client->use_https ? HTTPS_DEFAULT_PORT : HTTP_DEFAULT_PORT);
            if (client->host == NULL || client->path == NULL) {
                free_connection_info(client);
                hal_mem_free(client);
                return NULL;
            }
        }
        client->method = config->method;
        client->post_data = config->post_data;
        client->post_len = config->post_len;
        if (config->user_agent) {
            size_t ul = strlen(config->user_agent) + 1;
            client->user_agent = (char *)hal_mem_alloc(ul, MEM_LARGE);
            if (client->user_agent) memcpy(client->user_agent, config->user_agent, ul);
        }
        if (config->content_type) {
            size_t cl = strlen(config->content_type) + 1;
            client->content_type = (char *)hal_mem_alloc(cl, MEM_LARGE);
            if (client->content_type) memcpy(client->content_type, config->content_type, cl);
        }
        if (config->timeout_ms > 0) client->timeout_ms = config->timeout_ms;
        if (config->buffer_size > 0) client->buffer_size = config->buffer_size;
        client->tls_config = config->tls_config;
    }

    if (client->user_agent == NULL) {
        client->user_agent = (char *)hal_mem_alloc(sizeof(DEFAULT_USER_AGENT), MEM_LARGE);
        if (client->user_agent) memcpy(client->user_agent, DEFAULT_USER_AGENT, sizeof(DEFAULT_USER_AGENT));
    }
    if (client->user_agent == NULL) {
        free_connection_info(client);
        hal_mem_free(client);
        return NULL;
    }

    client->recv_buf = (char *)hal_mem_alloc_large((size_t)client->buffer_size);
    if (client->recv_buf == NULL) {
        free_connection_info(client);
        hal_mem_free(client);
        return NULL;
    }
    client->recv_buf_len = client->buffer_size;
    return client;
}

int http_client_set_url(http_client_handle_t client, const char *url)
{
    if (client == NULL || url == NULL) return HTTP_ERR_INVALID_ARG;
    free_connection_info(client);
    return parse_url(client, url);
}

int http_client_set_method(http_client_handle_t client, http_client_method_t method)
{
    if (client == NULL) return HTTP_ERR_INVALID_ARG;
    if (method >= HTTP_METHOD_MAX) return HTTP_ERR_INVALID_ARG;
    client->method = method;
    return 0;
}

int http_client_set_post_data(http_client_handle_t client, const char *data, int len)
{
    if (client == NULL) return HTTP_ERR_INVALID_ARG;
    client->post_data = data;
    client->post_len = (len < 0 && data) ? (int)strlen(data) : len;
    return 0;
}

int http_client_set_header(http_client_handle_t client, const char *key, const char *value)
{
    size_t kl, vl;
    if (client == NULL || key == NULL || value == NULL) return HTTP_ERR_INVALID_ARG;
    if (client->num_headers >= MAX_HEADERS) return HTTP_ERR_INVALID_ARG;
    kl = strlen(key) + 1;
    vl = strlen(value) + 1;
    client->headers[client->num_headers].key = (char *)hal_mem_alloc(kl, MEM_LARGE);
    client->headers[client->num_headers].value = (char *)hal_mem_alloc(vl, MEM_LARGE);
    if (client->headers[client->num_headers].key) memcpy(client->headers[client->num_headers].key, key, kl);
    if (client->headers[client->num_headers].value) memcpy(client->headers[client->num_headers].value, value, vl);
    if (client->headers[client->num_headers].key == NULL || client->headers[client->num_headers].value == NULL) {
        if (client->headers[client->num_headers].key) hal_mem_free(client->headers[client->num_headers].key);
        if (client->headers[client->num_headers].value) hal_mem_free(client->headers[client->num_headers].value);
        return HTTP_ERR_NO_MEM;
    }
    client->num_headers++;
    return 0;
}

static int build_request(http_client_handle_t client, char *buf, int buf_size)
{
    int n = 0;
    const char *meth = method_str[client->method];
    int content_len = 0;
    if (client->post_data && (client->method == HTTP_METHOD_POST || client->method == HTTP_METHOD_PUT))
        content_len = client->post_len;

    n += snprintf(buf + n, buf_size - n, "%s %s " HTTP_PROTOCOL CRLF, meth, client->path);
    n += snprintf(buf + n, buf_size - n, "Host: %s" CRLF, client->host);
    n += snprintf(buf + n, buf_size - n, "User-Agent: %s" CRLF, client->user_agent);
    if (content_len > 0) {
        n += snprintf(buf + n, buf_size - n, "Content-Length: %d" CRLF, content_len);
        n += snprintf(buf + n, buf_size - n, "Content-Type: %s" CRLF,
                      client->content_type ? client->content_type : "application/octet-stream");
        n += snprintf(buf + n, buf_size - n, "Connection: close" CRLF);  /* ask server to close after response, avoid RST/errno 128 */
    }
    for (int i = 0; i < client->num_headers; i++)
        n += snprintf(buf + n, buf_size - n, "%s: %s" CRLF, client->headers[i].key, client->headers[i].value);
    n += snprintf(buf + n, buf_size - n, CRLF);
    if (n >= buf_size) return -1;
    return n;
}

int http_client_perform(http_client_handle_t client)
{
    char *req_buf = NULL;
    int req_len, r, line_len;
    char line[512];
    uint32_t to_ms;
    const network_tls_config_t *tls = NULL;

    if (client == NULL || client->host == NULL || client->path == NULL)
        return HTTP_ERR_INVALID_ARG;

    http_client_close(client);
    client->status_code = 0;
    client->content_length = -1;
    client->is_chunked = 0;
    client->recv_buf_off = 0;
    client->recv_buf_used = 0;
    client->body_read = 0;
    client->chunk_remaining = -1;

    to_ms = (client->timeout_ms > 0) ? (uint32_t)client->timeout_ms : (uint32_t)HTTP_CLIENT_DEFAULT_TIMEOUT_MS;
    tls = client->use_https ? client->tls_config : NULL;
    client->network = ms_network_init(tls);
    if (client->network == NULL) {
        LOG_DRV_ERROR("HTTP client: ms_network_init failed");
        return HTTP_ERR_CONNECT;
    }

    r = ms_network_connect(client->network, client->host, (uint16_t)client->port, to_ms);
    if (r != 0) {
        LOG_DRV_ERROR("HTTP client: connect %s:%d failed (%d)", client->host, client->port, r);
        ms_network_deinit(client->network);
        client->network = NULL;
        return HTTP_ERR_CONNECT;
    }

#define HTTP_REQUEST_BUF_SIZE 8192
    req_buf = (char *)hal_mem_alloc_large(HTTP_REQUEST_BUF_SIZE);
    if (req_buf == NULL) {
        ms_network_close(client->network);
        ms_network_deinit(client->network);
        client->network = NULL;
        return HTTP_ERR_NO_MEM;
    }
    req_len = build_request(client, req_buf, HTTP_REQUEST_BUF_SIZE);
    if (req_len <= 0) {
        hal_mem_free(req_buf);
        ms_network_close(client->network);
        ms_network_deinit(client->network);
        client->network = NULL;
        LOG_DRV_ERROR("HTTP client: request too long or build failed (path/url length)");
        return HTTP_ERR_PARSE;
    }

    r = ms_network_send(client->network, (uint8_t *)req_buf, (uint32_t)req_len, to_ms);
    hal_mem_free(req_buf);
    LOG_DRV_DEBUG("HTTP client: headers sent %d/%d", r, req_len);
    if (r != req_len) {
        LOG_DRV_ERROR("HTTP client: send headers failed (%d)", r);
        ms_network_close(client->network);
        ms_network_deinit(client->network);
        client->network = NULL;
        return HTTP_ERR_SEND;
    }

    /* Send body in chunks to avoid blocking/timeout on large PUT/POST */
    if (client->post_data && client->post_len > 0 && (client->method == HTTP_METHOD_POST || client->method == HTTP_METHOD_PUT)) {
        const size_t chunk = 8192;
        size_t sent = 0;
        while (sent < (size_t)client->post_len) {
            size_t to_send = (size_t)client->post_len - sent;
            if (to_send > chunk) to_send = chunk;
            r = ms_network_send(client->network, (uint8_t *)(client->post_data + sent), (uint32_t)to_send, to_ms);
            if (r <= 0 || (size_t)r != to_send) {
                ms_network_close(client->network);
                ms_network_deinit(client->network);
                client->network = NULL;
                LOG_DRV_ERROR("HTTP client: send body failed (sent %u, ret %d)", (unsigned)sent, r);
                return HTTP_ERR_SEND;
            }
            sent += (size_t)r;
        }
        LOG_DRV_DEBUG("HTTP client: body sent %d bytes", client->post_len);
    }

    /* Read status line */
    r = read_line(client, line, sizeof(line), &line_len);
    if (r != 0) {
        ms_network_close(client->network);
        ms_network_deinit(client->network);
        client->network = NULL;
        return (r == HTTP_ERR_RECV) ? HTTP_ERR_RECV : HTTP_ERR_PARSE;
    }
    if (parse_status_line(line, &client->status_code) != 0) {
        ms_network_close(client->network);
        ms_network_deinit(client->network);
        client->network = NULL;
        return HTTP_ERR_PARSE;
    }

    /* Read headers until empty line */
    while (1) {
        r = read_line(client, line, sizeof(line), &line_len);
        if (r != 0) {
            ms_network_close(client->network);
            ms_network_deinit(client->network);
            client->network = NULL;
            return (r == HTTP_ERR_RECV) ? HTTP_ERR_RECV : HTTP_ERR_PARSE;
        }
        if (line_len == 2 && line[0] == '\r' && line[1] == '\n') break; /* \r\n */
        if (line[0] == '\0') break;
        parse_header_line(client, line);
    }

    /* recv_buf_off now points to start of body in recv_buf; recv_buf_used - recv_buf_off = body bytes already in buffer */
    return 0;
}

int http_client_get_status_code(http_client_handle_t client)
{
    return client ? client->status_code : -1;
}

int64_t http_client_get_content_length(http_client_handle_t client)
{
    return client ? client->content_length : -1;
}

bool http_client_is_chunked(http_client_handle_t client)
{
    return client ? (client->is_chunked != 0) : false;
}

/* Read next chunk size line (chunked). On return chunk_remaining is set. */
static int read_chunk_size(http_client_handle_t client)
{
    char line[64];
    int line_len, r;
    unsigned int chunk_hex;
    r = read_line(client, line, sizeof(line), &line_len);
    if (r != 0) return r;
    chunk_hex = (unsigned int)strtoul(line, NULL, 16);
    client->chunk_remaining = (int)chunk_hex;
    return 0;
}

int http_client_read(http_client_handle_t client, char *buffer, int len)
{
    int avail, to_copy, r;
    uint32_t to_ms;

    if (client == NULL || buffer == NULL || len <= 0 || client->network == NULL)
        return HTTP_ERR_INVALID_ARG;

    to_ms = (client->timeout_ms > 0) ? (uint32_t)client->timeout_ms : (uint32_t)HTTP_CLIENT_DEFAULT_TIMEOUT_MS;

    if (client->is_chunked) {
        if (client->chunk_remaining < 0) {
            r = read_chunk_size(client);
            if (r != 0) return r;
            if (client->chunk_remaining == 0) return 0; /* end of body */
        }
        if (client->chunk_remaining == 0) {
            r = read_chunk_size(client);
            if (r != 0) return r;
            if (client->chunk_remaining == 0) return 0;
        }
        avail = client->recv_buf_used - client->recv_buf_off;
        if (avail <= 0) {
            int to_read = len;
            if (to_read > client->chunk_remaining) to_read = client->chunk_remaining;
            if (to_read > client->recv_buf_len) to_read = client->recv_buf_len;
            r = ms_network_recv(client->network, (uint8_t *)client->recv_buf, to_read, to_ms);
            if (r <= 0) return (r < 0) ? HTTP_ERR_RECV : 0;
            client->recv_buf_used = r;
            client->recv_buf_off = 0;
            avail = r;
        }
        to_copy = avail;
        if (to_copy > len) to_copy = len;
        if (to_copy > client->chunk_remaining) to_copy = client->chunk_remaining;
        memcpy(buffer, client->recv_buf + client->recv_buf_off, to_copy);
        client->recv_buf_off += to_copy;
        client->chunk_remaining -= to_copy;
        client->body_read += to_copy;
        return to_copy;
    }

    /* Content-Length or unknown */
    if (client->content_length >= 0 && client->body_read >= client->content_length)
        return 0;
    avail = client->recv_buf_used - client->recv_buf_off;
    if (avail > 0) {
        to_copy = avail;
        if (to_copy > len) to_copy = len;
        if (client->content_length >= 0 && (client->body_read + to_copy) > client->content_length)
            to_copy = (int)(client->content_length - client->body_read);
        memcpy(buffer, client->recv_buf + client->recv_buf_off, to_copy);
        client->recv_buf_off += to_copy;
        client->body_read += to_copy;
        return to_copy;
    }
    /* Need to read more */
    {
        int to_read = len;
        if (client->content_length >= 0) {
            int64_t left = client->content_length - client->body_read;
            if (left <= 0) return 0;
            if (to_read > left) to_read = (int)left;
        }
        if (to_read > client->recv_buf_len) to_read = client->recv_buf_len;
        r = ms_network_recv(client->network, (uint8_t *)buffer, to_read, to_ms);
        if (r <= 0) {
            /* r==0: connection closed (EOF); r<0: error e.g. timeout */
            return (r < 0) ? HTTP_ERR_RECV : 0;
        }
        client->body_read += r;
        return r;
    }
}

void http_client_close(http_client_handle_t client)
{
    if (client == NULL) return;
    if (client->network != NULL) {
        ms_network_close(client->network);
        ms_network_deinit(client->network);
        client->network = NULL;
    }
}

void http_client_cleanup(http_client_handle_t client)
{
    if (client == NULL) return;
    http_client_close(client);
    free_connection_info(client);
    if (client->recv_buf) {
        hal_mem_free(client->recv_buf);
        client->recv_buf = NULL;
    }
    hal_mem_free(client);
}
