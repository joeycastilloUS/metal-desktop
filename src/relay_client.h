/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */
#ifndef RELAY_CLIENT_H
#define RELAY_CLIENT_H

#include "wire.h"
#include "crypt.h"

/*
 * relay_client — Wire protocol client for metal relay.
 *
 * One function: relay_request(). Pack triples, encrypt with 0xCA envelope,
 * send to relay, receive response, decrypt, unpack. Same pattern used by
 * handle_relay_auth() and handle_relay_register(), extracted for reuse.
 *
 * Higher-level convenience: relay_store_query() and relay_intelligence().
 */

#define RELAY_BUF_SIZE   131072
#define RELAY_TIMEOUT_MS 30000

/* One-shot wire request to relay.
 * Packs req triples, encrypts with key + 0xCA envelope, sends to relay,
 * receives + decrypts response, unpacks into resp triples.
 * Returns triple count on success, -1 on error.
 * resp_parse_buf must be at least RELAY_BUF_SIZE bytes. */
int relay_request(const char *host, int port,
                  const uint8_t key[32],
                  const WireTriple *req, int req_count,
                  WireTriple *resp, int max_resp,
                  char *resp_parse_buf, int resp_parse_size);

/* Query relay for store data.
 * Sends a "store" action with optional source filter.
 * Writes JSON response into out_json (up to max_len).
 * Returns length of JSON on success, -1 on error. */
int relay_store_query(const char *host, int port,
                      const uint8_t key[32],
                      const char *source,
                      char *out_json, int max_len);

/* Send intelligence/reasoning request to relay.
 * Sends a "loop" action with the question text.
 * Writes JSON response into out_json (up to max_len).
 * Returns length of JSON on success, -1 on error. */
int relay_intelligence(const char *host, int port,
                       const uint8_t key[32],
                       const char *question,
                       char *out_json, int max_len);

#endif /* RELAY_CLIENT_H */
