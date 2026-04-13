/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */
/**
 * wire — Native BASICI transport over TCP
 *
 * Every wire message is an array of atoms packed as NTRP0001 triples.
 * Atom triple: {word, role_name, fn_name}.
 * The 28 structure functions + 29 vocabulary functions = the complete
 * instruction set on the wire. Same atoms understand() produces.
 */

#include "wire.h"
#include <string.h>
#include <stdio.h>

/* ══════════════════════════════════════════════════════════════
 * BASICI Atom Name Tables — the closed set
 * ══════════════════════════════════════════════════════════════ */

/* Role enum values: ROLE_ENTITY=0 .. ROLE_UNKNOWN=6 */
static const char *g_role_names[] = {
    "entity", "action", "property", "structure",
    "signal", "reference", "unknown"
};

const char *wire_role_name(uint8_t role)
{
    return role < WIRE_ROLE_COUNT ? g_role_names[role] : "unknown";
}

uint8_t wire_role_from(const char *s)
{
    if (!s) return 6; /* ROLE_UNKNOWN */
    for (int i = 0; i < WIRE_ROLE_COUNT; i++)
        if (strcmp(s, g_role_names[i]) == 0) return (uint8_t)i;
    return 6;
}

/* Fn enum values: FN_NONE=0 .. FN_SCOPE_CLOSE=27, FN_V_PRINT=32 .. FN_V_OUTPUT=60 */
static const struct { uint8_t val; const char *name; } g_fn_table[] = {
    { 0,  "none"},
    { 1,  "auxiliary"},    { 2,  "copula"},       { 3,  "specifier"},
    { 4,  "locative"},    { 5,  "possessive"},   { 6,  "associative"},
    { 7,  "purposive"},   { 8,  "directional"},  { 9,  "source"},
    {10,  "topical"},     {11,  "relational"},   {12,  "via"},
    {13,  "spatial"},     {14,  "agent"},        {15,  "equivalence"},
    {16,  "temporal"},    {17,  "conjunction"},  {18,  "disjunction"},
    {19,  "contrast"},    {20,  "negation"},     {21,  "conditional"},
    {22,  "consequent"},  {23,  "causal"},       {24,  "exclusion"},
    {25,  "opposition"},  {26,  "quantity"},
    {27,  "scope-open"},  {28,  "scope-close"},
    /* Vocabulary functions — action words across voices */
    {32,  "v-print"},     {33,  "v-format"},     {34,  "v-length"},
    {35,  "v-compare"},   {36,  "v-find"},       {37,  "v-allocate"},
    {38,  "v-free"},      {39,  "v-sizeof"},     {40,  "v-value"},
    {41,  "v-open"},      {42,  "v-close"},      {43,  "v-write"},
    {44,  "v-read"},      {45,  "v-env"},        {46,  "v-copy"},
    {47,  "v-fill"},      {48,  "v-nil"},        {49,  "v-local"},
    {50,  "v-proc"},      {51,  "v-type"},       {52,  "v-integer"},
    {53,  "v-char"},      {54,  "v-pipe"},       {55,  "v-delete"},
    {56,  "v-flush"},     {57,  "v-channel"},    {58,  "v-realloc"},
    {59,  "v-exit"},      {60,  "v-output"},
};

#define FN_TABLE_SIZE (sizeof(g_fn_table) / sizeof(g_fn_table[0]))

const char *wire_fn_name(uint8_t fn)
{
    for (int i = 0; i < (int)FN_TABLE_SIZE; i++)
        if (g_fn_table[i].val == fn) return g_fn_table[i].name;
    return "none";
}

uint8_t wire_fn_from(const char *s)
{
    if (!s) return 0;
    for (int i = 0; i < (int)FN_TABLE_SIZE; i++)
        if (strcmp(s, g_fn_table[i].name) == 0) return g_fn_table[i].val;
    return 0;
}

/* ── Atom extraction ── */

const char *wire_action(const WireTriple *t, int count)
{
    for (int i = 0; i < count; i++)
        if (strcmp(t[i].predicate, "action") == 0)
            return t[i].subject;
    return NULL;
}

int wire_entities(const WireTriple *t, int count, const char **out, int max)
{
    int n = 0;
    for (int i = 0; i < count && n < max; i++)
        if (strcmp(t[i].predicate, "entity") == 0)
            out[n++] = t[i].subject;
    return n;
}

const char *wire_constraint(const WireTriple *t, int count, uint8_t fn)
{
    const char *fn_name = wire_fn_name(fn);
    for (int i = 0; i < count - 1; i++) {
        if (strcmp(t[i].predicate, "structure") == 0 &&
            strcmp(t[i].object, fn_name) == 0) {
            /* Return next non-structure word */
            for (int j = i + 1; j < count; j++) {
                if (strcmp(t[j].predicate, "structure") != 0)
                    return t[j].subject;
            }
        }
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════
 * Little-endian encode/decode
 * ══════════════════════════════════════════════════════════════ */

static inline void w16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
}

static inline void w32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline uint16_t r16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t r32(const uint8_t *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/* ── Detection ── */

int wire_is_ntrp(const uint8_t *buf, int len)
{
    return len >= WIRE_HEADER_SIZE
        && memcmp(buf, WIRE_MAGIC, WIRE_MAGIC_SIZE) == 0;
}

/* ── Pack ── */

int wire_pack(const WireTriple *triples, int count,
              uint8_t *buf, int buf_max)
{
    if (!triples || count < 0 || !buf || buf_max < WIRE_HEADER_SIZE)
        return -1;

    uint8_t *out = buf + WIRE_HEADER_SIZE;
    int remaining = buf_max - WIRE_HEADER_SIZE;

    for (int i = 0; i < count; i++) {
        if (!triples[i].subject || !triples[i].predicate || !triples[i].object)
            return -1;

        uint16_t s_len = (uint16_t)strlen(triples[i].subject);
        uint16_t p_len = (uint16_t)strlen(triples[i].predicate);
        uint16_t o_len = (uint16_t)strlen(triples[i].object);

        int need = 2 + s_len + 2 + p_len + 2 + o_len;
        if (need > remaining)
            return -1;

        w16(out, s_len); out += 2;
        memcpy(out, triples[i].subject, s_len); out += s_len;

        w16(out, p_len); out += 2;
        memcpy(out, triples[i].predicate, p_len); out += p_len;

        w16(out, o_len); out += 2;
        memcpy(out, triples[i].object, o_len); out += o_len;

        remaining -= need;
    }

    uint32_t payload = (uint32_t)(out - buf - WIRE_HEADER_SIZE);
    memcpy(buf, WIRE_MAGIC, WIRE_MAGIC_SIZE);
    w32(buf + 8, (uint32_t)count);
    w32(buf + 12, payload);

    return (int)(out - buf);
}

/* ── Unpack ── */

int wire_unpack(const uint8_t *buf, int buf_len,
                WireTriple *out, int max_triples,
                char *parse_buf, int parse_buf_size)
{
    if (!wire_is_ntrp(buf, buf_len))
        return -1;

    uint32_t triple_count  = r32(buf + 8);
    uint32_t payload_bytes = r32(buf + 12);

    if ((int)(WIRE_HEADER_SIZE + payload_bytes) > buf_len)
        return -1;
    if ((int)triple_count > max_triples)
        return -1;

    const uint8_t *p   = buf + WIRE_HEADER_SIZE;
    const uint8_t *end = p + payload_bytes;
    char *pb = parse_buf;
    int pb_left = parse_buf_size;
    int count = 0;

    for (uint32_t i = 0; i < triple_count; i++) {
        /* Subject */
        if (p + 2 > end) return -1;
        uint16_t s_len = r16(p); p += 2;
        if (p + s_len > end || (int)(s_len + 1) > pb_left) return -1;
        out[count].subject = pb;
        memcpy(pb, p, s_len); pb[s_len] = '\0';
        pb += s_len + 1; pb_left -= s_len + 1; p += s_len;

        /* Predicate */
        if (p + 2 > end) return -1;
        uint16_t p_len = r16(p); p += 2;
        if (p + p_len > end || (int)(p_len + 1) > pb_left) return -1;
        out[count].predicate = pb;
        memcpy(pb, p, p_len); pb[p_len] = '\0';
        pb += p_len + 1; pb_left -= p_len + 1; p += p_len;

        /* Object */
        if (p + 2 > end) return -1;
        uint16_t o_len = r16(p); p += 2;
        if (p + o_len > end || (int)(o_len + 1) > pb_left) return -1;
        out[count].object = pb;
        memcpy(pb, p, o_len); pb[o_len] = '\0';
        pb += o_len + 1; pb_left -= o_len + 1; p += o_len;

        count++;
    }

    return count;
}

/* ── Helpers ── */

const char *wire_find(const WireTriple *triples, int count,
                      const char *predicate)
{
    for (int i = 0; i < count; i++)
        if (strcmp(triples[i].predicate, predicate) == 0)
            return triples[i].object;
    return NULL;
}

const char *wire_find2(const WireTriple *triples, int count,
                       const char *subject, const char *predicate)
{
    for (int i = 0; i < count; i++)
        if (strcmp(triples[i].subject, subject) == 0 &&
            strcmp(triples[i].predicate, predicate) == 0)
            return triples[i].object;
    return NULL;
}

/* ── TCP transport ── */

static int send_all(SOCKET s, const uint8_t *data, int len)
{
    int sent = 0;
    while (sent < len) {
        int n = send(s, (const char *)(data + sent), len - sent, 0);
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}

static int recv_all(SOCKET s, uint8_t *buf, int len)
{
    int got = 0;
    while (got < len) {
        int n = recv(s, (char *)(buf + got), len - got, 0);
        if (n <= 0) return -1;
        got += n;
    }
    return 0;
}

SOCKET wire_connect(const char *host, int port)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((unsigned short)port);

    /* Try numeric IP first */
    addr.sin_addr.s_addr = inet_addr(host);
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        struct hostent *he = gethostbyname(host);
        if (!he) return INVALID_SOCKET;
        memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }

    return s;
}

int wire_send(SOCKET s, const uint8_t *data, int len)
{
    return send_all(s, data, len);
}

int wire_recv(SOCKET s, uint8_t *buf, int buf_max)
{
    if (buf_max < WIRE_HEADER_SIZE) return -1;

    /* Read NTRP header (16 bytes) */
    if (recv_all(s, buf, WIRE_HEADER_SIZE) != 0) return -1;

    /* Validate magic */
    if (memcmp(buf, WIRE_MAGIC, WIRE_MAGIC_SIZE) != 0) return -1;

    /* Read payload size from header */
    uint32_t payload = r32(buf + 12);
    int total = WIRE_HEADER_SIZE + (int)payload;
    if (total > buf_max) return -1;

    /* Read remaining payload */
    if (payload > 0) {
        if (recv_all(s, buf + WIRE_HEADER_SIZE, (int)payload) != 0)
            return -1;
    }

    return total;
}

void wire_close(SOCKET s)
{
    if (s != INVALID_SOCKET)
        closesocket(s);
}

/* ── One-shot request/response ── */

int wire_request(const char *host, int port,
                 const WireTriple *req, int req_count,
                 WireTriple *resp, int max_resp,
                 char *resp_parse_buf, int resp_parse_size)
{
    /* Pack request */
    uint8_t send_buf[WIRE_BUF_SIZE];
    int packed = wire_pack(req, req_count, send_buf, WIRE_BUF_SIZE);
    if (packed < 0) return -1;

    /* Connect */
    SOCKET s = wire_connect(host, port);
    if (s == INVALID_SOCKET) return -1;

    /* Send */
    if (wire_send(s, send_buf, packed) != 0) {
        wire_close(s);
        return -1;
    }

    /* Receive response */
    uint8_t recv_buf[WIRE_BUF_SIZE];
    int recv_len = wire_recv(s, recv_buf, WIRE_BUF_SIZE);
    wire_close(s);

    if (recv_len < 0) return -1;

    /* Unpack */
    return wire_unpack(recv_buf, recv_len, resp, max_resp,
                       resp_parse_buf, resp_parse_size);
}
