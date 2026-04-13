/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */
/**
 * wire — Native BASICI transport over TCP
 *
 * Every wire message is an array of atoms — the same decomposed form
 * that understand() produces. Packed as NTRP0001 triples: {word, role, fn}.
 * Translatable to any language, storable as triples, reasoned on as
 * code or speech. From the outside: opaque bytes.
 *
 * Atom triple format:
 *   subject   = word   ("list", "catalog", "geography")
 *   predicate = role   ("entity", "action", "structure")
 *   object    = fn     ("none", "locative", "equivalence")
 *
 * Transport: NTRP0001 packed triples over raw TCP.
 * Self-framing: header contains triple_count + payload_bytes.
 */

#ifndef WIRE_H
#define WIRE_H

#include <stdint.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define closesocket close
#endif

/* Limits */
#define WIRE_MAX_TRIPLES    1024
#define WIRE_MAX_STRING     8192
#define WIRE_BUF_SIZE       (1024 * 128)  /* 128KB max frame */

/* NTRP constants */
#define WIRE_MAGIC          "NTRP0001"
#define WIRE_MAGIC_SIZE     8
#define WIRE_HEADER_SIZE    16

/* ══════════════════════════════════════════════════════════════
 * Triple — the transport unit (three strings)
 * ══════════════════════════════════════════════════════════════ */

typedef struct {
    const char *subject;
    const char *predicate;
    const char *object;
} WireTriple;

/* ══════════════════════════════════════════════════════════════
 * BASICI Atom Layer — the language on the wire
 * ══════════════════════════════════════════════════════════════ */

/* ── Role names ── */
#define WIRE_ROLE_COUNT  7

const char *wire_role_name(uint8_t role);   /* ROLE_ENTITY → "entity"     */
uint8_t     wire_role_from(const char *s);  /* "entity"    → ROLE_ENTITY  */

/* ── Fn names (structure functions + vocabulary) ── */
#define WIRE_FN_COUNT    58

const char *wire_fn_name(uint8_t fn);       /* FN_LOCATIVE → "locative"   */
uint8_t     wire_fn_from(const char *s);    /* "locative"  → FN_LOCATIVE  */

/* ── Atom construction macros ── */

#define W_ENTITY(w)        (WireTriple){(w), "entity",    "none"}
#define W_ACTION(w)        (WireTriple){(w), "action",    "none"}
#define W_PROPERTY(w)      (WireTriple){(w), "property",  "none"}
#define W_SIGNAL(w)        (WireTriple){(w), "signal",    "none"}
#define W_STRUCTURE(w, fn) (WireTriple){(w), "structure", wire_fn_name(fn)}

/* ── Atom extraction from wire triples ── */

/** Find the action word (first triple with predicate="action"). */
const char *wire_action(const WireTriple *t, int count);

/** Collect entity words. Returns count written to out. */
int wire_entities(const WireTriple *t, int count, const char **out, int max);

/**
 * Find the word AFTER a given structure function.
 * E.g., wire_constraint(t, n, FN_LOCATIVE) finds the entity after "where/in".
 */
const char *wire_constraint(const WireTriple *t, int count, uint8_t fn);

/* ══════════════════════════════════════════════════════════════
 * Pack / Unpack — NTRP0001 format
 * ══════════════════════════════════════════════════════════════ */

int wire_pack(const WireTriple *triples, int count,
              uint8_t *buf, int buf_max);

int wire_unpack(const uint8_t *buf, int buf_len,
                WireTriple *out, int max_triples,
                char *parse_buf, int parse_buf_size);

/* ══════════════════════════════════════════════════════════════
 * TCP transport
 * ══════════════════════════════════════════════════════════════ */

SOCKET wire_connect(const char *host, int port);
int    wire_send(SOCKET s, const uint8_t *data, int len);
int    wire_recv(SOCKET s, uint8_t *buf, int buf_max);
void   wire_close(SOCKET s);

int wire_request(const char *host, int port,
                 const WireTriple *req, int req_count,
                 WireTriple *resp, int max_resp,
                 char *resp_parse_buf, int resp_parse_size);

/* ══════════════════════════════════════════════════════════════
 * Helpers
 * ══════════════════════════════════════════════════════════════ */

const char *wire_find(const WireTriple *triples, int count,
                      const char *predicate);

const char *wire_find2(const WireTriple *triples, int count,
                       const char *subject, const char *predicate);

int wire_is_ntrp(const uint8_t *buf, int len);

#endif /* WIRE_H */
