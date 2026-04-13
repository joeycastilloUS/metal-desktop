/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */
#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>

typedef struct {
    const char *method;     /* "GET" or "POST" */
    const char *url;        /* full URL: https://api.openai.com/v1/... */
    const char *headers;    /* "Header: value\r\n..." NULL-terminated */
    const uint8_t *body;    /* request body (NULL for GET) */
    int body_len;
    int timeout_ms;         /* 0 = default (30s) */
} HttpRequest;

typedef struct {
    int status;             /* HTTP status code (200, 401, 429, etc.) */
    uint8_t *body;          /* response body (heap-allocated) */
    int body_len;
    char *headers;          /* response headers (heap-allocated) */
} HttpResponse;

/* Callback for streaming responses. Called per SSE data line. */
typedef void (*HttpStreamCb)(const char *data, int len, void *ctx);

/* Initialize HTTP client. Call once at boot. */
int http_init(void);

/* Synchronous request. Caller frees response with http_response_free(). */
int http_request(const HttpRequest *req, HttpResponse *resp);

/* Streaming request. Calls on_chunk per SSE data line.
   Blocks until stream ends or error. Returns 0 on success. */
int http_request_stream(const HttpRequest *req,
                        HttpStreamCb on_chunk, void *ctx);

/* Free response body and headers. */
void http_response_free(HttpResponse *resp);

/* Shutdown HTTP client. Release platform handles. */
void http_shutdown(void);

#endif /* HTTP_H */
