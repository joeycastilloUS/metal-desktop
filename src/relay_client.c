/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */
/*
 * relay_client.c — Wire protocol client for metal relay.
 *
 * Extracted from serve.c handle_relay_auth/register pattern.
 * 0xCA envelope: [0xCA | nonce(12) | ciphertext | tag(16)]
 * AES-256-GCM encryption. WireTriple request/response.
 */

#include "relay_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define closesocket close
#define SD_SEND SHUT_WR
#endif

/* ── relay_request ── */

int relay_request(const char *host, int port,
                  const uint8_t key[32],
                  const WireTriple *req, int req_count,
                  WireTriple *resp, int max_resp,
                  char *resp_parse_buf, int resp_parse_size)
{
    /* Pack request triples */
    uint8_t *wire_buf = (uint8_t *)malloc(RELAY_BUF_SIZE);
    if (!wire_buf) return -1;

    int packed = wire_pack(req, req_count, wire_buf, RELAY_BUF_SIZE);
    if (packed <= 0) {
        free(wire_buf);
        return -1;
    }

    /* Encrypt: 0xCA envelope */
    uint8_t *enc_buf = (uint8_t *)malloc(RELAY_BUF_SIZE);
    if (!enc_buf) { free(wire_buf); return -1; }

    uint8_t nonce[12], tag[16];
    crypt_fill_random(nonce, 12);

    enc_buf[0] = 0xCA;
    memcpy(enc_buf + 1, nonce, 12);
    aes256gcm_encrypt(key, nonce,
                      wire_buf, (size_t)packed,
                      enc_buf + 13, tag);
    memcpy(enc_buf + 13 + packed, tag, 16);
    int enc_len = 1 + 12 + packed + 16;

    free(wire_buf);

    /* Connect to relay */
    SOCKET rs = wire_connect(host, port);
    if (rs == INVALID_SOCKET) {
        free(enc_buf);
        return -1;
    }

    /* Socket timeout */
#ifdef _WIN32
    DWORD tv = RELAY_TIMEOUT_MS;
    setsockopt(rs, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = RELAY_TIMEOUT_MS / 1000;
    tv.tv_usec = (RELAY_TIMEOUT_MS % 1000) * 1000;
    setsockopt(rs, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    /* Send */
    wire_send(rs, enc_buf, enc_len);
    free(enc_buf);
    shutdown(rs, SD_SEND);

    /* Receive until EOF */
    uint8_t *resp_raw = (uint8_t *)malloc(RELAY_BUF_SIZE);
    if (!resp_raw) { wire_close(rs); return -1; }

    int resp_len = 0;
    for (;;) {
        int n = recv(rs, (char *)resp_raw + resp_len,
                     RELAY_BUF_SIZE - resp_len, 0);
        if (n <= 0) break;
        resp_len += n;
        if (resp_len >= RELAY_BUF_SIZE) break;
    }
    wire_close(rs);

    if (resp_len <= 0) {
        free(resp_raw);
        return -1;
    }

    /* Decrypt 0xCA envelope */
    uint8_t *dec_buf = (uint8_t *)malloc(RELAY_BUF_SIZE);
    if (!dec_buf) { free(resp_raw); return -1; }

    int dec_len;
    if (resp_raw[0] == 0xCA && resp_len > 29) {
        uint8_t *rnonce = resp_raw + 1;
        uint8_t *rtag = resp_raw + resp_len - 16;
        int ct_len = resp_len - 29;
        if (aes256gcm_decrypt(key, rnonce,
                              resp_raw + 13, (size_t)ct_len,
                              rtag, dec_buf) != 0) {
            free(resp_raw);
            free(dec_buf);
            return -1;
        }
        dec_len = ct_len;
    } else {
        memcpy(dec_buf, resp_raw, (size_t)resp_len);
        dec_len = resp_len;
    }
    free(resp_raw);

    /* Unpack response triples */
    int tc = wire_unpack(dec_buf, dec_len, resp, max_resp,
                         resp_parse_buf, resp_parse_size);
    free(dec_buf);

    return tc;
}

/* ── relay_store_query ── */

int relay_store_query(const char *host, int port,
                      const uint8_t key[32],
                      const char *source,
                      char *out_json, int max_len)
{
    WireTriple req[3];
    int rc = 0;
    req[rc++] = (WireTriple){"store", "action", "none"};
    req[rc++] = (WireTriple){"query", "signal", "none"};
    if (source && source[0])
        req[rc++] = (WireTriple){source, "entity", "none"};

    WireTriple resp[256];
    char *parse_buf = (char *)malloc(RELAY_BUF_SIZE);
    if (!parse_buf) return -1;

    int tc = relay_request(host, port, key, req, rc,
                           resp, 256, parse_buf, RELAY_BUF_SIZE);
    if (tc <= 0) {
        free(parse_buf);
        return -1;
    }

    /* Build JSON from response triples */
    int pos = 0;
    pos += snprintf(out_json + pos, (size_t)(max_len - pos),
                    "{\"type\":\"store_result\",\"source\":\"mesh\",\"stores\":[");

    /* Response triples encode store data as subject/predicate/object */
    for (int i = 0; i < tc && pos < max_len - 100; i++) {
        if (i > 0) out_json[pos++] = ',';
        pos += snprintf(out_json + pos, (size_t)(max_len - pos),
                        "{\"s\":\"%s\",\"p\":\"%s\",\"o\":\"%s\"}",
                        resp[i].subject ? resp[i].subject : "",
                        resp[i].predicate ? resp[i].predicate : "",
                        resp[i].object ? resp[i].object : "");
    }

    pos += snprintf(out_json + pos, (size_t)(max_len - pos), "]}");
    free(parse_buf);
    return pos;
}

/* ── relay_intelligence ── */

int relay_intelligence(const char *host, int port,
                       const uint8_t key[32],
                       const char *question,
                       char *out_json, int max_len)
{
    WireTriple req[2];
    req[0] = (WireTriple){"loop", "action", "none"};
    req[1] = (WireTriple){question, "entity", "none"};

    WireTriple resp[512];
    char *parse_buf = (char *)malloc(RELAY_BUF_SIZE);
    if (!parse_buf) return -1;

    int tc = relay_request(host, port, key, req, 2,
                           resp, 512, parse_buf, RELAY_BUF_SIZE);
    if (tc <= 0) {
        free(parse_buf);
        return -1;
    }

    /* Extract intelligence response fields */
    const char *understand = wire_find(resp, tc, "understand");
    const char *local_resp = wire_find(resp, tc, "local");
    const char *catalog = wire_find(resp, tc, "catalog");

    int pos = 0;
    pos += snprintf(out_json + pos, (size_t)(max_len - pos),
                    "{\"type\":\"intelligence\"");

    if (understand)
        pos += snprintf(out_json + pos, (size_t)(max_len - pos),
                        ",\"understand\":\"%s\"", understand);
    if (local_resp)
        pos += snprintf(out_json + pos, (size_t)(max_len - pos),
                        ",\"local\":\"%s\"", local_resp);
    if (catalog)
        pos += snprintf(out_json + pos, (size_t)(max_len - pos),
                        ",\"catalog\":\"%s\"", catalog);

    /* Forward all triples as raw data */
    pos += snprintf(out_json + pos, (size_t)(max_len - pos), ",\"triples\":[");
    for (int i = 0; i < tc && pos < max_len - 100; i++) {
        if (i > 0) out_json[pos++] = ',';
        pos += snprintf(out_json + pos, (size_t)(max_len - pos),
                        "{\"s\":\"%s\",\"p\":\"%s\",\"o\":\"%s\"}",
                        resp[i].subject ? resp[i].subject : "",
                        resp[i].predicate ? resp[i].predicate : "",
                        resp[i].object ? resp[i].object : "");
    }
    pos += snprintf(out_json + pos, (size_t)(max_len - pos), "]}");

    free(parse_buf);
    return pos;
}
