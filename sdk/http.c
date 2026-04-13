/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */
/*
 * http.c — Outbound HTTPS client
 *
 * Platform-native TLS. WinHTTP on Windows. No OpenSSL. No third-party.
 * Synchronous + streaming (SSE). Pure transport — knows nothing about
 * what it carries.
 */

#include "http.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>

static HINTERNET g_session = NULL;

/* ── URL parsing ── */

typedef struct {
    wchar_t host[256];
    wchar_t path[4096];
    int port;
    int secure;
} ParsedUrl;

static int parse_url(const char *url, ParsedUrl *out)
{
    memset(out, 0, sizeof(*out));

    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) {
        out->secure = 1;
        out->port = 443;
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        out->secure = 0;
        out->port = 80;
        p += 7;
    } else {
        return -1;
    }

    /* Extract host */
    const char *host_end = p;
    while (*host_end && *host_end != '/' && *host_end != ':' && *host_end != '?')
        host_end++;

    int hlen = (int)(host_end - p);
    if (hlen <= 0 || hlen >= 255) return -1;

    char host_a[256];
    memcpy(host_a, p, (size_t)hlen);
    host_a[hlen] = '\0';
    MultiByteToWideChar(CP_UTF8, 0, host_a, -1, out->host, 256);

    p = host_end;

    /* Optional port */
    if (*p == ':') {
        p++;
        out->port = atoi(p);
        while (*p >= '0' && *p <= '9') p++;
    }

    /* Path (including query string) */
    if (*p == '/') {
        char path_a[4096];
        int plen = (int)strlen(p);
        if (plen >= 4095) plen = 4094;
        memcpy(path_a, p, (size_t)plen);
        path_a[plen] = '\0';
        MultiByteToWideChar(CP_UTF8, 0, path_a, -1, out->path, 4096);
    } else {
        out->path[0] = L'/';
        out->path[1] = 0;
    }

    return 0;
}

/* ── Parse multi-line headers string into wide char for WinHTTP ── */

static wchar_t *headers_to_wide(const char *headers)
{
    if (!headers || !headers[0]) return NULL;
    int len = (int)strlen(headers);
    int wlen = MultiByteToWideChar(CP_UTF8, 0, headers, len, NULL, 0);
    wchar_t *w = (wchar_t *)malloc((size_t)(wlen + 1) * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, headers, len, w, wlen);
    w[wlen] = 0;
    return w;
}

/* ── Init / Shutdown ── */

int http_init(void)
{
    if (g_session) return 0;
    g_session = WinHttpOpen(L"NOUS/1.0",
                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    return g_session ? 0 : -3;
}

void http_shutdown(void)
{
    if (g_session) {
        WinHttpCloseHandle(g_session);
        g_session = NULL;
    }
}

/* ── Synchronous request ── */

int http_request(const HttpRequest *req, HttpResponse *resp)
{
    if (!g_session || !req || !resp) return -3;
    memset(resp, 0, sizeof(*resp));

    ParsedUrl u;
    if (parse_url(req->url, &u) < 0) return -1;

    HINTERNET conn = WinHttpConnect(g_session, u.host, (INTERNET_PORT)u.port, 0);
    if (!conn) return -1;

    const wchar_t *verb = L"GET";
    if (req->method && strcmp(req->method, "POST") == 0) verb = L"POST";

    DWORD flags = u.secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hreq = WinHttpOpenRequest(conn, verb, u.path, NULL,
                                        WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hreq) { WinHttpCloseHandle(conn); return -1; }

    /* Timeout */
    int tms = req->timeout_ms > 0 ? req->timeout_ms : 30000;
    WinHttpSetTimeouts(hreq, tms, tms, tms, tms);

    /* Custom headers */
    wchar_t *whdrs = headers_to_wide(req->headers);
    if (whdrs) {
        WinHttpAddRequestHeaders(hreq, whdrs, (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);
        free(whdrs);
    }

    /* Send */
    DWORD blen = req->body ? (DWORD)req->body_len : 0;
    if (!WinHttpSendRequest(hreq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            req->body ? (LPVOID)req->body : WINHTTP_NO_REQUEST_DATA,
                            blen, blen, 0)) {
        WinHttpCloseHandle(hreq);
        WinHttpCloseHandle(conn);
        return -1;
    }

    /* Receive */
    if (!WinHttpReceiveResponse(hreq, NULL)) {
        WinHttpCloseHandle(hreq);
        WinHttpCloseHandle(conn);
        return -2;
    }

    /* Status code */
    DWORD status_size = sizeof(DWORD);
    DWORD status_code = 0;
    WinHttpQueryHeaders(hreq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
                        WINHTTP_NO_HEADER_INDEX);
    resp->status = (int)status_code;

    /* Response headers */
    DWORD hdr_size = 0;
    WinHttpQueryHeaders(hreq, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                        WINHTTP_HEADER_NAME_BY_INDEX, NULL, &hdr_size,
                        WINHTTP_NO_HEADER_INDEX);
    if (hdr_size > 0) {
        wchar_t *whdr = (wchar_t *)malloc(hdr_size);
        if (whdr) {
            WinHttpQueryHeaders(hreq, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                WINHTTP_HEADER_NAME_BY_INDEX, whdr, &hdr_size,
                                WINHTTP_NO_HEADER_INDEX);
            int alen = WideCharToMultiByte(CP_UTF8, 0, whdr, -1, NULL, 0, NULL, NULL);
            resp->headers = (char *)malloc((size_t)alen);
            if (resp->headers)
                WideCharToMultiByte(CP_UTF8, 0, whdr, -1, resp->headers, alen, NULL, NULL);
            free(whdr);
        }
    }

    /* Response body */
    int cap = 32768;
    resp->body = (uint8_t *)malloc((size_t)cap);
    resp->body_len = 0;
    if (!resp->body) {
        WinHttpCloseHandle(hreq);
        WinHttpCloseHandle(conn);
        return -3;
    }

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hreq, &avail)) break;
        if (avail == 0) break;

        if (resp->body_len + (int)avail > cap) {
            while (resp->body_len + (int)avail > cap) cap *= 2;
            uint8_t *nb = (uint8_t *)realloc(resp->body, (size_t)cap);
            if (!nb) break;
            resp->body = nb;
        }

        DWORD read = 0;
        WinHttpReadData(hreq, resp->body + resp->body_len, avail, &read);
        resp->body_len += (int)read;
    }

    /* Null-terminate for convenience */
    if (resp->body_len + 1 > cap) {
        uint8_t *nb = (uint8_t *)realloc(resp->body, (size_t)(resp->body_len + 1));
        if (nb) resp->body = nb;
    }
    resp->body[resp->body_len] = '\0';

    WinHttpCloseHandle(hreq);
    WinHttpCloseHandle(conn);
    return 0;
}

/* ── Streaming request (SSE) ── */

int http_request_stream(const HttpRequest *req, HttpStreamCb on_chunk, void *ctx)
{
    if (!g_session || !req || !on_chunk) return -3;

    ParsedUrl u;
    if (parse_url(req->url, &u) < 0) return -1;

    HINTERNET conn = WinHttpConnect(g_session, u.host, (INTERNET_PORT)u.port, 0);
    if (!conn) return -1;

    const wchar_t *verb = L"POST";
    DWORD flags = u.secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hreq = WinHttpOpenRequest(conn, verb, u.path, NULL,
                                        WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hreq) { WinHttpCloseHandle(conn); return -1; }

    int tms = req->timeout_ms > 0 ? req->timeout_ms : 60000;
    WinHttpSetTimeouts(hreq, tms, tms, tms, tms);

    wchar_t *whdrs = headers_to_wide(req->headers);
    if (whdrs) {
        WinHttpAddRequestHeaders(hreq, whdrs, (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);
        free(whdrs);
    }

    DWORD blen = req->body ? (DWORD)req->body_len : 0;
    if (!WinHttpSendRequest(hreq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            req->body ? (LPVOID)req->body : WINHTTP_NO_REQUEST_DATA,
                            blen, blen, 0)) {
        WinHttpCloseHandle(hreq);
        WinHttpCloseHandle(conn);
        return -1;
    }

    if (!WinHttpReceiveResponse(hreq, NULL)) {
        WinHttpCloseHandle(hreq);
        WinHttpCloseHandle(conn);
        return -2;
    }

    /* Read body line-by-line, callback on "data: " prefix */
    char line_buf[65536];
    int line_pos = 0;
    char chunk[8192];

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hreq, &avail)) break;
        if (avail == 0) break;

        DWORD to_read = avail > sizeof(chunk) ? sizeof(chunk) : avail;
        DWORD read = 0;
        if (!WinHttpReadData(hreq, chunk, to_read, &read)) break;
        if (read == 0) break;

        /* Process bytes — find complete lines */
        for (DWORD i = 0; i < read; i++) {
            if (chunk[i] == '\n') {
                line_buf[line_pos] = '\0';
                /* Strip trailing \r */
                if (line_pos > 0 && line_buf[line_pos - 1] == '\r')
                    line_buf[--line_pos] = '\0';

                /* SSE data line */
                if (line_pos >= 6 && strncmp(line_buf, "data: ", 6) == 0) {
                    const char *data = line_buf + 6;
                    int dlen = line_pos - 6;
                    if (dlen == 6 && memcmp(data, "[DONE]", 6) == 0) {
                        /* Stream complete */
                        goto done;
                    }
                    on_chunk(data, dlen, ctx);
                }
                line_pos = 0;
            } else {
                if (line_pos < (int)sizeof(line_buf) - 1)
                    line_buf[line_pos++] = chunk[i];
            }
        }
    }

done:
    WinHttpCloseHandle(hreq);
    WinHttpCloseHandle(conn);
    return 0;
}

/* ── Free response ── */

void http_response_free(HttpResponse *resp)
{
    if (!resp) return;
    free(resp->body);
    free(resp->headers);
    memset(resp, 0, sizeof(*resp));
}

#else
/* ── Linux stub — TODO: kTLS or minimal TLS 1.3 ── */

int http_init(void) { return 0; }
void http_shutdown(void) {}
int http_request(const HttpRequest *req, HttpResponse *resp)
{
    (void)req;
    memset(resp, 0, sizeof(*resp));
    return -1;
}
int http_request_stream(const HttpRequest *req, HttpStreamCb on_chunk, void *ctx)
{
    (void)req; (void)on_chunk; (void)ctx;
    return -1;
}
void http_response_free(HttpResponse *resp) { (void)resp; }

#endif /* _WIN32 */
