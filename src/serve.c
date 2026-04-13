/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */
/*
 * serve.c — Browser gate
 *
 * Pure C HTTP + WebSocket server on Winsock. Serves static HTML/CSS/JS.
 * Routes WebSocket messages to the loop engine. One binary. One process.
 * Direct function calls. No spawning.
 */

#include "serve.h"
#include "http.h"
#include "wire.h"
#include "crypt.h"
#include "relay_client.h"
#include "config.h"
#include "nous_userkeys.h"
/* store.h/scope.h removed — stub types below (local store path kept for fallback) */

/* ── Built-in free-tier API keys (compiled in, gitignored) ── */
#if defined(__has_include)
  #if __has_include("nous_keys_builtin.h")
    #include "nous_keys_builtin.h"
  #endif
#endif
#ifndef NOUS_KEY_GEMINI
  #define NOUS_KEY_GEMINI    NULL
#endif
#ifndef NOUS_KEY_DEEPSEEK
  #define NOUS_KEY_DEEPSEEK  NULL
#endif
#ifndef NOUS_KEY_MISTRAL
  #define NOUS_KEY_MISTRAL   NULL
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#define Sleep(ms) usleep((ms)*1000)
#endif

/* ══════════════════════════════════════════════════════════════
 * Constants
 * ══════════════════════════════════════════════════════════════ */

#define MAX_CLIENTS      16
#define RECV_BUF         65536
#define SEND_BUF         131072
#define MAX_PROVIDERS    32
#define MAX_PATH_LEN     260
#define MAX_QUESTION     4096
#define MAX_RESPONSE     65536
#define MAX_JSON         2097152
#define WS_GUID          "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* ══════════════════════════════════════════════════════════════
 * Data structures
 * ══════════════════════════════════════════════════════════════ */

typedef enum { CONN_HTTP, CONN_WS } ConnType;

typedef struct {
    SOCKET sock;
    ConnType type;
    int id;
    int alive;
    char recv_buf[RECV_BUF];
    int recv_len;
} Client;

typedef struct {
    char name[64];
    char endpoint[512];
    char format[32];
    char model[128];
    char key_env[64];
    double input_cost;
    double output_cost;
    char api_key[256];
    int disabled;
    char hint[1024];
} Provider;

/* ── Per-query last responses (for /judge) ── */
typedef struct {
    char provider[64];
    char response[MAX_RESPONSE];
    int tokens_in;
    int tokens_out;
    double cost;
    int elapsed_ms;
} ProviderResult;

/* ══════════════════════════════════════════════════════════════
 * Globals
 * ══════════════════════════════════════════════════════════════ */

static SOCKET g_listen = INVALID_SOCKET;
static int g_running = 0;
static Client g_clients[MAX_CLIENTS];
static int g_next_id = 1;
static char g_wwwroot[MAX_PATH_LEN];

/* Relay config */
static char g_relay_host[256];
static int g_relay_port = 8080;
static uint8_t g_psk[32];
static uint8_t g_cockpit_key[32];
static int g_psk_loaded = 0;

/* Providers */
static Provider g_providers[MAX_PROVIDERS];
static int g_provider_count = 0;
static char g_global_hint[2048];

/* Cost tracking */
static double g_daily_total = 0.0;
static int g_daily_queries = 0;
static int g_daily_tokens_in = 0;
static int g_daily_tokens_out = 0;
static char g_daily_date[16];

/* Last query state (for judge) */
static char g_last_question[MAX_QUESTION];
static ProviderResult g_last_results[MAX_PROVIDERS];
static int g_last_result_count = 0;

/* Judge rankings (for advise) */
static int g_last_ranked_order[MAX_PROVIDERS]; /* indices into g_last_results[], sorted by rank */
static int g_last_ranked_count = 0;

/* Thread safety */
#ifdef _WIN32
static CRITICAL_SECTION g_ws_lock;
#else
static pthread_mutex_t g_ws_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

/* TODO Board 33: replace with relay_client.h wire-based calls */
/* These stubs allow compilation — Board 33 replaces them with wire calls */
static void *receive_get_memory(void) { return NULL; }
static void *receive_get_intel(void)  { return NULL; }
/* receive_loop_json_buf removed — replaced by relay_intelligence() in Board 33 */

/* Stub types for TripleStore — replaces store.h/scope.h includes.
 * Board 33 removes these entirely when handle_store() goes to wire protocol. */
typedef struct TripleStore TripleStore;  /* opaque — stubs return NULL */
typedef struct { uint64_t subject, predicate, object; } Fact;
typedef struct {
    uint64_t fact_count, dict_count;
    size_t   total_mapped;
} StoreStats;
typedef struct {
    TripleStore *store;
    uint64_t     position;
    int          exhausted;
} Scope;
static StoreStats triple_store_stats(TripleStore *s) {
    (void)s; StoreStats st = {0}; return st;
}
static Scope triple_store_scope(TripleStore *s) {
    (void)s; Scope sc = {0}; sc.exhausted = 1; return sc;
}
static int scope_next(Scope *s, Fact *f) {
    (void)s; (void)f; return 0;
}
static const char *triple_store_get_string(TripleStore *s, uint64_t id) {
    (void)s; (void)id; return NULL;
}

/* ══════════════════════════════════════════════════════════════
 * Section 1: Utilities
 * ══════════════════════════════════════════════════════════════ */

static void ws_lock(void)
{
#ifdef _WIN32
    EnterCriticalSection(&g_ws_lock);
#else
    pthread_mutex_lock(&g_ws_lock);
#endif
}

static void ws_unlock(void)
{
#ifdef _WIN32
    LeaveCriticalSection(&g_ws_lock);
#else
    pthread_mutex_unlock(&g_ws_lock);
#endif
}

/* ── Minimal JSON helpers ── */

static int json_get_str(const char *json, const char *key, char *out, int maxlen)
{
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) { out[0] = '\0'; return 0; }
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') { out[0] = '\0'; return 0; }
    p++;
    int i = 0;
    while (*p && *p != '"' && i < maxlen - 1) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'n') out[i++] = '\n';
            else if (*p == 't') out[i++] = '\t';
            else if (*p == '"') out[i++] = '"';
            else if (*p == '\\') out[i++] = '\\';
            else out[i++] = *p;
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    return i;
}

static int json_get_int(const char *json, const char *key)
{
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    return atoi(p);
}

static int json_get_bool(const char *json, const char *key)
{
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    return (*p == 't') ? 1 : 0;
}

/* ── JSON string escaping ── */

static int json_escape(const char *src, char *dst, int maxlen)
{
    int j = 0;
    for (int i = 0; src[i] && j < maxlen - 6; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"') { dst[j++] = '\\'; dst[j++] = '"'; }
        else if (c == '\\') { dst[j++] = '\\'; dst[j++] = '\\'; }
        else if (c == '\n') { dst[j++] = '\\'; dst[j++] = 'n'; }
        else if (c == '\r') { dst[j++] = '\\'; dst[j++] = 'r'; }
        else if (c == '\t') { dst[j++] = '\\'; dst[j++] = 't'; }
        else if (c < 0x20) {
            j += snprintf(dst + j, (size_t)(maxlen - j), "\\u%04x", c);
        } else {
            dst[j++] = (char)c;
        }
    }
    dst[j] = '\0';
    return j;
}

/* ── Base64 encode (for WebSocket handshake) ── */

static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int base64_encode(const uint8_t *in, int len, char *out, int maxlen)
{
    int j = 0;
    for (int i = 0; i < len && j < maxlen - 4; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < len) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < len) v |= (uint32_t)in[i + 2];
        out[j++] = b64[(v >> 18) & 0x3F];
        out[j++] = b64[(v >> 12) & 0x3F];
        out[j++] = (i + 1 < len) ? b64[(v >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < len) ? b64[v & 0x3F] : '=';
    }
    out[j] = '\0';
    return j;
}

/* hex_decode moved to config.c */

/* ══════════════════════════════════════════════════════════════
 * Section 2: Configuration loading
 * ══════════════════════════════════════════════════════════════ */

static void load_relay_config(void)
{
    DesktopConfig cfg;
    config_load(&cfg);

    strncpy(g_relay_host, cfg.relay_host, sizeof(g_relay_host) - 1);
    g_relay_port = cfg.relay_port;
    memcpy(g_psk, cfg.psk, 32);
    memcpy(g_cockpit_key, cfg.cockpit_key, 32);
    g_psk_loaded = cfg.psk_loaded;
}

/* ── Key resolution: user key > env var > builtin > NULL ── */

static const char *resolve_key(const char *user, const char *env_name,
                                const char *builtin)
{
    /* 1. Per-user key from users.dat */
    if (user) {
        const char *uk = nous_userkeys_get(user, env_name);
        if (uk && uk[0]) return uk;
    }
    /* 2. Environment variable */
    const char *env = getenv(env_name);
    if (env && env[0]) return env;
    /* 3. Compiled-in builtin (free tier) */
    if (builtin && builtin[0]) return builtin;
    return NULL;
}

/* Built-in provider defaults — no triple store needed */
typedef struct {
    const char *name;
    const char *endpoint;
    const char *format;
    const char *model;
    const char *key_env;
    const char *builtin_key;   /* compiled-in free-tier key, or NULL for paid */
    double input_cost;
    double output_cost;
} ProviderDefault;

static const ProviderDefault g_defaults[] = {
    /* FREE — hardcoded, zero config */
    {"gemini",        "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-pro:generateContent", "gemini", "gemini-2.5-pro", "GEMINI_API_KEY", NOUS_KEY_GEMINI, 1.25, 10.0},
    {"gemini-flash",  "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent","gemini","gemini-2.5-flash","GEMINI_API_KEY", NOUS_KEY_GEMINI, 0.15, 0.60},
    {"gemini-preview","https://generativelanguage.googleapis.com/v1beta/models/gemini-3.1-pro-preview:generateContent","gemini","gemini-3.1-pro-preview","GEMINI_API_KEY", NOUS_KEY_GEMINI, 2.0, 12.0},
    {"gemini-flash-preview","https://generativelanguage.googleapis.com/v1beta/models/gemini-3-flash-preview:generateContent","gemini","gemini-3-flash-preview","GEMINI_API_KEY", NOUS_KEY_GEMINI, 0.50, 3.0},
    {"deepseek",      "https://api.deepseek.com/chat/completions",     "openai",    "deepseek-chat",               "DEEPSEEK_API_KEY", NOUS_KEY_DEEPSEEK, 0.28, 0.42},
    {"deepseek-r1",   "https://api.deepseek.com/chat/completions",     "openai",    "deepseek-reasoner",           "DEEPSEEK_API_KEY", NOUS_KEY_DEEPSEEK, 0.28, 0.42},
    {"mistral",       "https://api.mistral.ai/v1/chat/completions",    "openai",    "mistral-medium-latest",       "MISTRAL_API_KEY",  NOUS_KEY_MISTRAL,  2.0,  6.0},
    {"mistral-large", "https://api.mistral.ai/v1/chat/completions",    "openai",    "mistral-large-latest",        "MISTRAL_API_KEY",  NOUS_KEY_MISTRAL,  2.0,  6.0},
    /* PAID — user-configurable */
    {"claude-opus",   "https://api.anthropic.com/v1/messages",         "anthropic", "claude-opus-4-6",             "ANTHROPIC_API_KEY",    NULL,  5.0, 25.0},
    {"claude-sonnet", "https://api.anthropic.com/v1/messages",         "anthropic", "claude-sonnet-4-6",           "ANTHROPIC_API_KEY",    NULL,  3.0, 15.0},
    {"claude-haiku",  "https://api.anthropic.com/v1/messages",         "anthropic", "claude-haiku-4-5-20251001",   "ANTHROPIC_API_KEY",    NULL,  1.0,  5.0},
    {"gpt",           "https://api.openai.com/v1/chat/completions",    "openai",    "gpt-5.4",                     "OPENAI_API_KEY",       NULL,  2.5, 15.0},
    {"gpt-mini",      "https://api.openai.com/v1/chat/completions",    "openai",    "gpt-5.4-mini",                "OPENAI_API_KEY",       NULL, 0.75,  4.5},
    {"gpt-nano",      "https://api.openai.com/v1/chat/completions",    "openai",    "gpt-5.4-nano",                "OPENAI_API_KEY",       NULL, 0.20, 1.25},
    {"o3",            "https://api.openai.com/v1/chat/completions",    "openai",    "o3",                          "OPENAI_API_KEY",       NULL,  2.0,  8.0},
    {"o4-mini",       "https://api.openai.com/v1/chat/completions",    "openai",    "o4-mini",                     "OPENAI_API_KEY",       NULL,  1.1,  4.4},
    {"grok",          "https://api.x.ai/v1/chat/completions",          "openai",    "grok-4.20-0309-reasoning",    "XAI_API_KEY",          NULL,  2.0,  6.0},
    {"grok-fast",     "https://api.x.ai/v1/chat/completions",          "openai",    "grok-4-1-fast-reasoning",     "XAI_API_KEY",          NULL, 0.20, 0.50},
    {"perplexity",    "https://api.perplexity.ai/chat/completions",    "openai",    "sonar-pro",                   "PERPLEXITY_API_KEY",   NULL,  3.0, 15.0},
    {"perplexity-reason","https://api.perplexity.ai/chat/completions", "openai",    "sonar-reasoning-pro",         "PERPLEXITY_API_KEY",   NULL,  2.0,  8.0},
    {"perplexity-deep","https://api.perplexity.ai/chat/completions",  "openai",    "sonar-deep-research",         "PERPLEXITY_API_KEY",   NULL,  2.0,  8.0},
    {"magistral",     "https://api.mistral.ai/v1/chat/completions",   "openai",    "magistral-medium-latest",     "MISTRAL_API_KEY",  NOUS_KEY_MISTRAL,  2.0,  6.0},
    {NULL, NULL, NULL, NULL, NULL, NULL, 0, 0}
};

static void load_providers(void)
{
    /* Load per-user keys (users.dat next to binary, or configured path) */
    nous_userkeys_load("users.dat");

    g_provider_count = 0;
    int free_count = 0, paid_count = 0;
    for (int i = 0; g_defaults[i].name && g_provider_count < MAX_PROVIDERS; i++) {
        const ProviderDefault *d = &g_defaults[i];
        Provider *p = &g_providers[g_provider_count];
        memset(p, 0, sizeof(*p));

        strncpy(p->name,     d->name,     sizeof(p->name) - 1);
        strncpy(p->endpoint, d->endpoint, sizeof(p->endpoint) - 1);
        strncpy(p->format,   d->format,   sizeof(p->format) - 1);
        strncpy(p->model,    d->model,    sizeof(p->model) - 1);
        strncpy(p->key_env,  d->key_env,  sizeof(p->key_env) - 1);
        p->input_cost  = d->input_cost;
        p->output_cost = d->output_cost;

        /* 4-layer key resolution: user > env > builtin > NULL */
        const char *key = resolve_key(NULL, d->key_env, d->builtin_key);
        if (key) strncpy(p->api_key, key, sizeof(p->api_key) - 1);

        if (d->builtin_key) free_count++; else if (p->api_key[0]) paid_count++;
        g_provider_count++;
    }
    fprintf(stderr, "[serve] providers: %d free (builtin), %d paid (user key)\n",
            free_count, paid_count);
}

/* ══════════════════════════════════════════════════════════════
 * Section 3: WebSocket framing
 * ══════════════════════════════════════════════════════════════ */

/* Write a WebSocket text frame (server→client, unmasked) */
static int ws_send_frame(SOCKET sock, const char *data, int len)
{
    uint8_t hdr[10];
    int hlen = 0;

    hdr[0] = 0x81; /* FIN + text opcode */
    if (len < 126) {
        hdr[1] = (uint8_t)len;
        hlen = 2;
    } else if (len < 65536) {
        hdr[1] = 126;
        hdr[2] = (uint8_t)(len >> 8);
        hdr[3] = (uint8_t)(len & 0xFF);
        hlen = 4;
    } else {
        hdr[1] = 127;
        memset(hdr + 2, 0, 4);
        hdr[6] = (uint8_t)((len >> 24) & 0xFF);
        hdr[7] = (uint8_t)((len >> 16) & 0xFF);
        hdr[8] = (uint8_t)((len >> 8) & 0xFF);
        hdr[9] = (uint8_t)(len & 0xFF);
        hlen = 10;
    }

    if (send(sock, (char *)hdr, hlen, 0) != hlen) return -1;
    /* Send payload — loop for large frames */
    int sent = 0;
    while (sent < len) {
        int n = send(sock, data + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}

/* Read a WebSocket frame (client→server, masked). Returns payload length or -1. */
static int ws_read_frame(Client *c, char *out, int maxlen)
{
    if (c->recv_len < 2) return 0; /* need more data */

    uint8_t *buf = (uint8_t *)c->recv_buf;
    int opcode = buf[0] & 0x0F;
    int fin = (buf[0] >> 7) & 1;
    int masked = (buf[1] >> 7) & 1;
    uint64_t plen = buf[1] & 0x7F;
    int hlen = 2;

    if (plen == 126) {
        if (c->recv_len < 4) return 0;
        plen = ((uint64_t)buf[2] << 8) | buf[3];
        hlen = 4;
    } else if (plen == 127) {
        if (c->recv_len < 10) return 0;
        plen = 0;
        for (int i = 0; i < 8; i++)
            plen = (plen << 8) | buf[2 + i];
        hlen = 10;
    }

    int mask_len = masked ? 4 : 0;
    int total = hlen + mask_len + (int)plen;
    if (c->recv_len < total) return 0; /* need more data */

    uint8_t mask[4] = {0};
    if (masked) memcpy(mask, buf + hlen, 4);

    /* Unmask + copy payload */
    int copy = (int)plen < maxlen - 1 ? (int)plen : maxlen - 1;
    for (int i = 0; i < copy; i++)
        out[i] = (char)(buf[hlen + mask_len + i] ^ mask[i & 3]);
    out[copy] = '\0';

    /* Consume frame from buffer */
    memmove(c->recv_buf, c->recv_buf + total, (size_t)(c->recv_len - total));
    c->recv_len -= total;

    /* Handle control frames */
    if (opcode == 0x8) return -1;  /* close */
    if (opcode == 0x9) {           /* ping → pong */
        uint8_t pong[2] = {0x8A, 0x00};
        send(c->sock, (char *)pong, 2, 0);
        return 0;
    }

    (void)fin;
    return (opcode == 0x1 || opcode == 0x0) ? copy : 0;
}

/* ══════════════════════════════════════════════════════════════
 * Section 4: HTTP server
 * ══════════════════════════════════════════════════════════════ */

static const char *mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".js") == 0) return "text/javascript; charset=utf-8";
    if (strcmp(ext, ".css") == 0) return "text/css; charset=utf-8";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".ico") == 0) return "image/x-icon";
    return "application/octet-stream";
}

static int serve_static(SOCKET sock, const char *url_path)
{
    /* Map URL to file path */
    char fpath[MAX_PATH_LEN * 2];
    if (strcmp(url_path, "/") == 0)
        snprintf(fpath, sizeof(fpath), "%s/index.html", g_wwwroot);
    else
        snprintf(fpath, sizeof(fpath), "%s%s", g_wwwroot, url_path);

    /* Security: no .. traversal in the URL path */
    if (strstr(url_path, "..")) {
        const char *r = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
        send(sock, r, (int)strlen(r), 0);
        return -1;
    }

    FILE *f = fopen(fpath, "rb");
    if (!f) {
        const char *r = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send(sock, r, (int)strlen(r), 0);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: keep-alive\r\n"
        "\r\n", mime_type(fpath), fsize);
    send(sock, header, hlen, 0);

    char fbuf[8192];
    while (fsize > 0) {
        int chunk = fsize > (long)sizeof(fbuf) ? (int)sizeof(fbuf) : (int)fsize;
        int got = (int)fread(fbuf, 1, (size_t)chunk, f);
        if (got <= 0) break;
        send(sock, fbuf, got, 0);
        fsize -= got;
    }
    fclose(f);
    return 0;
}

static int handle_http(Client *c)
{
    /* Find end of HTTP headers */
    char *end = strstr(c->recv_buf, "\r\n\r\n");
    if (!end) return 0; /* incomplete */

    char method[16], path[1024], version[16];
    sscanf(c->recv_buf, "%15s %1023s %15s", method, path, version);

    /* WebSocket upgrade? */
    char *upgrade = strstr(c->recv_buf, "Upgrade: websocket");
    if (!upgrade) upgrade = strstr(c->recv_buf, "Upgrade: WebSocket");

    if (upgrade && strcmp(method, "GET") == 0) {
        /* Extract Sec-WebSocket-Key */
        char *keyp = strstr(c->recv_buf, "Sec-WebSocket-Key: ");
        if (!keyp) keyp = strstr(c->recv_buf, "sec-websocket-key: ");
        if (!keyp) return -1;
        keyp += 19;
        char ws_key[64];
        int ki = 0;
        while (*keyp && *keyp != '\r' && ki < 63) ws_key[ki++] = *keyp++;
        ws_key[ki] = '\0';

        /* Compute accept: SHA1(key + GUID) → base64 */
        char concat[128];
        snprintf(concat, sizeof(concat), "%s%s", ws_key, WS_GUID);
        uint8_t hash[20];
        sha1((const uint8_t *)concat, strlen(concat), hash);
        char accept[64];
        base64_encode(hash, 20, accept, sizeof(accept));

        /* Send upgrade response */
        char resp[512];
        int rlen = snprintf(resp, sizeof(resp),
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: %s\r\n"
            "\r\n", accept);
        send(c->sock, resp, rlen, 0);

        c->type = CONN_WS;

        /* Consume the HTTP request from buffer */
        int consumed = (int)(end + 4 - c->recv_buf);
        memmove(c->recv_buf, end + 4, (size_t)(c->recv_len - consumed));
        c->recv_len -= consumed;
        return 1;
    }

    /* API endpoints */
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/loop-config") == 0) {
        char *body = (char *)malloc(32768);
        if (!body) return -1;
        int pos = 0;
        pos += snprintf(body + pos, 32768 - (size_t)pos, "{\"providers\":[");
        for (int i = 0; i < g_provider_count; i++) {
            if (i > 0) body[pos++] = ',';
            char ename[128], emodel[256];
            json_escape(g_providers[i].name, ename, sizeof(ename));
            json_escape(g_providers[i].model, emodel, sizeof(emodel));
            pos += snprintf(body + pos, 32768 - (size_t)pos,
                "{\"name\":\"%s\",\"model\":\"%s\"}", ename, emodel);
        }
        pos += snprintf(body + pos, 32768 - (size_t)pos, "]}");
        char hdr[256];
        int hlen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Content-Length: %d\r\nConnection: keep-alive\r\n\r\n", pos);
        send(c->sock, hdr, hlen, 0);
        send(c->sock, body, pos, 0);
        free(body);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/pillar-config") == 0) {
        const char *body = "{}";
        char hdr[256];
        int hlen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Content-Length: 2\r\nConnection: keep-alive\r\n\r\n");
        send(c->sock, hdr, hlen, 0);
        send(c->sock, body, 2, 0);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/pinned") == 0) {
        const char *body = "[]";
        char hdr[256];
        int hlen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Content-Length: 2\r\nConnection: keep-alive\r\n\r\n");
        send(c->sock, hdr, hlen, 0);
        send(c->sock, body, 2, 0);
    } else if (strcmp(method, "GET") == 0) {
        /* Static file */
        serve_static(c->sock, path);
    } else {
        const char *r = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";
        send(c->sock, r, (int)strlen(r), 0);
    }

    /* Consume request from buffer */
    int consumed = (int)(end + 4 - c->recv_buf);
    memmove(c->recv_buf, end + 4, (size_t)(c->recv_len - consumed));
    c->recv_len -= consumed;
    return 0;
}

/* ══════════════════════════════════════════════════════════════
 * Section 5: WebSocket send helpers
 * ══════════════════════════════════════════════════════════════ */

int serve_ws_send(int conn_id, const char *json, int len)
{
    ws_lock();
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].alive && g_clients[i].type == CONN_WS &&
            g_clients[i].id == conn_id) {
            ws_send_frame(g_clients[i].sock, json, len);
            break;
        }
    }
    ws_unlock();
    return 0;
}

int serve_ws_broadcast(const char *json, int len)
{
    ws_lock();
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].alive && g_clients[i].type == CONN_WS) {
            ws_send_frame(g_clients[i].sock, json, len);
        }
    }
    ws_unlock();
    return 0;
}

/* Send a JSON event to a specific client */
static void send_event(int conn_id, const char *json)
{
    serve_ws_send(conn_id, json, (int)strlen(json));
}

/* ══════════════════════════════════════════════════════════════
 * Section 6: Relay auth/register
 * ══════════════════════════════════════════════════════════════ */

static void handle_relay_auth(int conn_id, const char *json)
{
    char user[128], totp[16];
    json_get_str(json, "user", user, sizeof(user));
    json_get_str(json, "totp", totp, sizeof(totp));

    if (!g_psk_loaded || !g_relay_host[0]) {
        char r[256];
        snprintf(r, sizeof(r),
            "{\"type\":\"relay_auth_result\",\"status\":\"error\","
            "\"message\":\"no relay config\"}");
        send_event(conn_id, r);
        return;
    }

    /* Build wire triples: auth request */
    WireTriple req[3];
    req[0] = (WireTriple){"auth", "action", "none"};
    req[1] = (WireTriple){user, "entity", "none"};
    req[2] = (WireTriple){totp, "signal", "none"};

    /* Pack and encrypt */
    uint8_t wire_buf[WIRE_BUF_SIZE];
    int packed = wire_pack(req, 3, wire_buf, WIRE_BUF_SIZE);
    if (packed <= 0) {
        send_event(conn_id, "{\"type\":\"relay_auth_result\",\"status\":\"error\","
                   "\"message\":\"pack failed\"}");
        return;
    }

    /* Encrypt with cockpit key */
    uint8_t enc_buf[WIRE_BUF_SIZE];
    uint8_t nonce[12];
    crypt_fill_random(nonce, 12);
    uint8_t tag[16];
    enc_buf[0] = 0xCA;
    memcpy(enc_buf + 1, nonce, 12);
    aes256gcm_encrypt(g_cockpit_key, nonce,
                      wire_buf, (size_t)packed,
                      enc_buf + 13, tag);
    memcpy(enc_buf + 13 + packed, tag, 16);
    int enc_len = 1 + 12 + packed + 16;

    /* TCP to relay */
    SOCKET rs = wire_connect(g_relay_host, g_relay_port);
    if (rs == INVALID_SOCKET) {
        send_event(conn_id, "{\"type\":\"relay_auth_result\",\"status\":\"error\","
                   "\"message\":\"relay unreachable\"}");
        return;
    }

    /* Set socket timeout for recv */
#ifdef _WIN32
    DWORD tv = 10000; /* 10 seconds */
    setsockopt(rs, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));
#endif

    wire_send(rs, enc_buf, enc_len);
    shutdown(rs, SD_SEND); /* half-close: tell relay we're done sending */

    /* Read raw response (0xCA envelope, not NTRP) — loop until EOF */
    uint8_t resp_buf[8192];
    int resp_len = 0;
    for (;;) {
        int n = recv(rs, (char *)resp_buf + resp_len,
                     (int)sizeof(resp_buf) - resp_len, 0);
        if (n <= 0) break;
        resp_len += n;
        if (resp_len >= (int)sizeof(resp_buf)) break;
    }
    wire_close(rs);

    if (resp_len <= 0) {
        char errmsg[256];
        snprintf(errmsg, sizeof(errmsg),
            "{\"type\":\"relay_auth_result\",\"status\":\"error\","
            "\"message\":\"no response (recv=%d)\"}", resp_len);
        send_event(conn_id, errmsg);
        return;
    }

    /* Decrypt response */
    uint8_t dec_buf[8192];
    int dec_len = resp_len;
    if (resp_buf[0] == 0xCA && resp_len > 29) {
        uint8_t *rnonce = resp_buf + 1;
        uint8_t *rtag = resp_buf + resp_len - 16;
        int ct_len = resp_len - 29;
        if (aes256gcm_decrypt(g_cockpit_key, rnonce,
                              resp_buf + 13, (size_t)ct_len,
                              rtag, dec_buf) == 0) {
            dec_len = ct_len;
        } else {
            send_event(conn_id, "{\"type\":\"relay_auth_result\",\"status\":\"error\","
                       "\"message\":\"decrypt failed\"}");
            return;
        }
    } else {
        memcpy(dec_buf, resp_buf, (size_t)resp_len);
        dec_len = resp_len;
    }

    /* Unpack response triples */
    WireTriple triples[32];
    char parse_buf[8192];
    int tc = wire_unpack(dec_buf, dec_len, triples, 32, parse_buf, sizeof(parse_buf));

    const char *status = wire_find(triples, tc, "status");
    const char *message = wire_find(triples, tc, "message");

    char resp_json[1024];
    char esc_msg[512];
    json_escape(message ? message : "", esc_msg, sizeof(esc_msg));
    snprintf(resp_json, sizeof(resp_json),
        "{\"type\":\"relay_auth_result\",\"status\":\"%s\","
        "\"user\":\"%s\",\"message\":\"%s\"}",
        status ? status : "error", user, esc_msg);
    send_event(conn_id, resp_json);
}

static void handle_relay_register(int conn_id, const char *json)
{
    char user[128];
    json_get_str(json, "user", user, sizeof(user));

    if (!g_psk_loaded || !g_relay_host[0]) {
        send_event(conn_id, "{\"type\":\"relay_register_result\",\"status\":\"error\","
                   "\"message\":\"no relay config\"}");
        return;
    }

    WireTriple req[2];
    req[0] = (WireTriple){"register", "action", "none"};
    req[1] = (WireTriple){user, "entity", "none"};

    uint8_t wire_buf[WIRE_BUF_SIZE];
    int packed = wire_pack(req, 2, wire_buf, WIRE_BUF_SIZE);
    if (packed <= 0) {
        send_event(conn_id, "{\"type\":\"relay_register_result\",\"status\":\"error\","
                   "\"message\":\"pack failed\"}");
        return;
    }

    uint8_t enc_buf[WIRE_BUF_SIZE];
    uint8_t nonce[12];
    crypt_fill_random(nonce, 12);
    uint8_t tag[16];
    enc_buf[0] = 0xCA;
    memcpy(enc_buf + 1, nonce, 12);
    aes256gcm_encrypt(g_cockpit_key, nonce,
                      wire_buf, (size_t)packed,
                      enc_buf + 13, tag);
    memcpy(enc_buf + 13 + packed, tag, 16);
    int enc_len = 1 + 12 + packed + 16;

    SOCKET rs = wire_connect(g_relay_host, g_relay_port);
    if (rs == INVALID_SOCKET) {
        send_event(conn_id, "{\"type\":\"relay_register_result\",\"status\":\"error\","
                   "\"message\":\"relay unreachable\"}");
        return;
    }

    /* Set socket timeout */
#ifdef _WIN32
    DWORD rtv = 10000;
    setsockopt(rs, SOL_SOCKET, SO_RCVTIMEO, (char *)&rtv, sizeof(rtv));
#endif

    wire_send(rs, enc_buf, enc_len);
    shutdown(rs, SD_SEND); /* half-close: tell relay we're done sending */

    /* Loop recv until EOF */
    uint8_t resp_buf[WIRE_BUF_SIZE];
    int resp_len = 0;
    for (;;) {
        int n = recv(rs, (char *)resp_buf + resp_len,
                     WIRE_BUF_SIZE - resp_len, 0);
        if (n <= 0) break;
        resp_len += n;
        if (resp_len >= WIRE_BUF_SIZE) break;
    }
    wire_close(rs);

    if (resp_len <= 0) {
        send_event(conn_id, "{\"type\":\"relay_register_result\",\"status\":\"error\","
                   "\"message\":\"no response\"}");
        return;
    }

    /* Decrypt */
    uint8_t dec_buf[WIRE_BUF_SIZE];
    int dec_len = resp_len;
    if (resp_buf[0] == 0xCA && resp_len > 29) {
        uint8_t *rnonce = resp_buf + 1;
        uint8_t *rtag = resp_buf + resp_len - 16;
        int ct_len = resp_len - 29;
        if (aes256gcm_decrypt(g_cockpit_key, rnonce,
                              resp_buf + 13, (size_t)ct_len,
                              rtag, dec_buf) == 0) {
            dec_len = ct_len;
        } else {
            send_event(conn_id, "{\"type\":\"relay_register_result\",\"status\":\"error\","
                       "\"message\":\"decrypt failed\"}");
            return;
        }
    } else {
        memcpy(dec_buf, resp_buf, (size_t)resp_len);
    }

    WireTriple triples[32];
    char parse_buf[WIRE_BUF_SIZE];
    int tc = wire_unpack(dec_buf, dec_len, triples, 32, parse_buf, WIRE_BUF_SIZE);

    const char *status = wire_find(triples, tc, "status");
    const char *qr_data = wire_find(triples, tc, "qr_data");
    const char *qr_size_s = wire_find(triples, tc, "qr_size");
    const char *totp_uri = wire_find(triples, tc, "totp_uri");
    const char *message = wire_find(triples, tc, "message");

    /* Build response JSON */
    char *resp_json = (char *)malloc(MAX_JSON);
    if (!resp_json) return;

    char esc_uri[4096], esc_qr[65536], esc_msg[512];
    json_escape(totp_uri ? totp_uri : "", esc_uri, sizeof(esc_uri));
    json_escape(qr_data ? qr_data : "", esc_qr, sizeof(esc_qr));
    json_escape(message ? message : "", esc_msg, sizeof(esc_msg));

    snprintf(resp_json, MAX_JSON,
        "{\"type\":\"relay_register_result\",\"status\":\"%s\","
        "\"user\":\"%s\",\"totp_uri\":\"%s\","
        "\"qr_size\":%s,\"qr_data\":\"%s\",\"message\":\"%s\"}",
        status ? status : "error", user, esc_uri,
        qr_size_s ? qr_size_s : "0", esc_qr, esc_msg);
    send_event(conn_id, resp_json);
    free(resp_json);
}

/* ══════════════════════════════════════════════════════════════
 * Section 7: Store handler
 * ══════════════════════════════════════════════════════════════ */

static void handle_store(int conn_id, const char *json)
{
    char source[16];
    json_get_str(json, "source", source, sizeof(source));

    if (strcmp(source, "mesh") == 0) {
        /* Mesh store — wire protocol through relay */
        if (!g_psk_loaded || !g_relay_host[0]) {
            send_event(conn_id, "{\"type\":\"store_result\",\"source\":\"mesh\","
                       "\"stores\":[],\"error\":\"no relay config\"}");
            return;
        }
        char *mesh_json = (char *)malloc(MAX_JSON);
        if (!mesh_json) return;
        int n = relay_store_query(g_relay_host, g_relay_port,
                                  g_cockpit_key, "mesh",
                                  mesh_json, MAX_JSON);
        if (n > 0) {
            send_event(conn_id, mesh_json);
        } else {
            send_event(conn_id, "{\"type\":\"store_result\",\"source\":\"mesh\","
                       "\"stores\":[],\"error\":\"relay query failed\"}");
        }
        free(mesh_json);
        return;
    }

    /* Local stores — direct memory access! No subprocess! */
    TripleStore *stores[4];
    const char *names[4];
    int ns = 0;

    TripleStore *mem = (TripleStore *)receive_get_memory();
    TripleStore *intel = (TripleStore *)receive_get_intel();
    if (mem)   { stores[ns] = mem;   names[ns] = "MEMORY";       ns++; }
    if (intel) { stores[ns] = intel; names[ns] = "INTELLIGENCE";  ns++; }

    /* Build response JSON */
    int cap = MAX_JSON;
    char *out = (char *)malloc((size_t)cap);
    if (!out) return;
    int pos = 0;

#define SAFE_SNPRINTF(fmt, ...) do { \
    int _n = snprintf(out + pos, (size_t)(cap - pos), fmt, ##__VA_ARGS__); \
    if (_n > 0 && pos + _n < cap) pos += _n; \
    else if (_n > 0) pos = cap - 1; \
} while(0)

    SAFE_SNPRINTF("{\"type\":\"store_result\",\"source\":\"local\",\"stores\":[");

    for (int si = 0; si < ns; si++) {
        if (si > 0 && pos < cap - 1) out[pos++] = ',';

        StoreStats st = triple_store_stats(stores[si]);
        SAFE_SNPRINTF(
            "{\"name\":\"%s\",\"facts\":%llu,\"dict\":%llu,\"bytes\":%zu,\"triples\":[",
            names[si],
            (unsigned long long)st.fact_count,
            (unsigned long long)st.dict_count,
            st.total_mapped);

        /* Iterate all facts — cap at 5000 for sanity */
        Scope sc = triple_store_scope(stores[si]);
        Fact fact;
        int fc = 0;
        while (scope_next(&sc, &fact) && fc < 5000) {
            const char *s = triple_store_get_string(stores[si], fact.subject);
            const char *p = triple_store_get_string(stores[si], fact.predicate);
            const char *o = triple_store_get_string(stores[si], fact.object);
            if (!s || !p || !o) continue;

            if (fc > 0 && pos < cap - 1) out[pos++] = ',';

            char es[512], ep[512], eo[1024];
            json_escape(s, es, sizeof(es));
            json_escape(p, ep, sizeof(ep));
            json_escape(o, eo, sizeof(eo));

            int needed = (int)strlen(es) + (int)strlen(ep) + (int)strlen(eo) + 40;
            if (pos + needed >= cap - 100) break;

            SAFE_SNPRINTF("{\"s\":\"%s\",\"p\":\"%s\",\"o\":\"%s\"}", es, ep, eo);
            fc++;
        }

        SAFE_SNPRINTF("]}");
    }

    SAFE_SNPRINTF("]}");
#undef SAFE_SNPRINTF
    send_event(conn_id, out);
    free(out);
}

/* ══════════════════════════════════════════════════════════════
 * Section 8: AI Provider fan-out (loop engine)
 * ══════════════════════════════════════════════════════════════ */

/* ── Build request body per provider format ── */

static int build_anthropic_body(const Provider *p, const char *task,
                                const char *hint, char *out, int maxlen)
{
    char esc_task[MAX_QUESTION * 2], esc_hint[2048], esc_model[256];
    json_escape(task, esc_task, sizeof(esc_task));
    json_escape(hint ? hint : "", esc_hint, sizeof(esc_hint));
    json_escape(p->model, esc_model, sizeof(esc_model));

    return snprintf(out, (size_t)maxlen,
        "{\"model\":\"%s\",\"max_tokens\":4096,"
        "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]"
        "%s%s%s}",
        esc_model, esc_task,
        (hint && hint[0]) ? ",\"system\":\"" : "",
        (hint && hint[0]) ? esc_hint : "",
        (hint && hint[0]) ? "\"" : "");
}

static int build_openai_body(const Provider *p, const char *task,
                             const char *hint, char *out, int maxlen)
{
    char esc_task[MAX_QUESTION * 2], esc_hint[2048], esc_model[256];
    json_escape(task, esc_task, sizeof(esc_task));
    json_escape(hint ? hint : "", esc_hint, sizeof(esc_hint));
    json_escape(p->model, esc_model, sizeof(esc_model));

    if (hint && hint[0]) {
        return snprintf(out, (size_t)maxlen,
            "{\"model\":\"%s\",\"max_completion_tokens\":4096,"
            "\"messages\":["
            "{\"role\":\"system\",\"content\":\"%s\"},"
            "{\"role\":\"user\",\"content\":\"%s\"}"
            "]}", esc_model, esc_hint, esc_task);
    }
    return snprintf(out, (size_t)maxlen,
        "{\"model\":\"%s\",\"max_completion_tokens\":4096,"
        "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}",
        esc_model, esc_task);
}

static int build_gemini_body(const Provider *p, const char *task,
                             const char *hint, char *out, int maxlen)
{
    char esc_task[MAX_QUESTION * 2], esc_hint[2048];
    json_escape(task, esc_task, sizeof(esc_task));
    json_escape(hint ? hint : "", esc_hint, sizeof(esc_hint));
    (void)p;

    if (hint && hint[0]) {
        return snprintf(out, (size_t)maxlen,
            "{\"system_instruction\":{\"parts\":[{\"text\":\"%s\"}]},"
            "\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":\"%s\"}]}]}",
            esc_hint, esc_task);
    }
    return snprintf(out, (size_t)maxlen,
        "{\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":\"%s\"}]}]}",
        esc_task);
}

/* ── Build headers per format ── */

static int build_headers(const Provider *p, char *out, int maxlen)
{
    if (strcmp(p->format, "anthropic") == 0) {
        return snprintf(out, (size_t)maxlen,
            "Content-Type: application/json\r\n"
            "x-api-key: %s\r\n"
            "anthropic-version: 2023-06-01\r\n", p->api_key);
    }
    if (strcmp(p->format, "gemini") == 0) {
        /* Gemini uses query param for key, not header */
        return snprintf(out, (size_t)maxlen,
            "Content-Type: application/json\r\n");
    }
    /* OpenAI-compatible (openai, grok, deepseek, perplexity, mistral) */
    return snprintf(out, (size_t)maxlen,
        "Content-Type: application/json\r\n"
        "Authorization: Bearer %s\r\n", p->api_key);
}

/* ── Build full URL (Gemini needs key in query param) ── */

static void build_url(const Provider *p, char *out, int maxlen)
{
    if (strcmp(p->format, "gemini") == 0) {
        snprintf(out, (size_t)maxlen, "%s?key=%s", p->endpoint, p->api_key);
    } else {
        snprintf(out, (size_t)maxlen, "%s", p->endpoint);
    }
}

/* ── Extract response text per format ── */

static int extract_text(const Provider *p, const char *body, char *out, int maxlen)
{
    out[0] = '\0';
    const char *found = NULL;

    if (strcmp(p->format, "anthropic") == 0) {
        /* "text":"..." inside content array — skip "type":"text" value first */
        const char *c = strstr(body, "\"content\"");
        if (c) {
            const char *t = strstr(c, "\"text\"");
            if (t) {
                /* First "text" is the value of "type" — skip to the key */
                t = strstr(t + 6, "\"text\"");
                if (t) found = t;
            }
        }
    } else if (strcmp(p->format, "gemini") == 0) {
        /* candidates[0].content.parts[0].text */
        const char *c = strstr(body, "\"candidates\"");
        if (c) {
            const char *t = strstr(c, "\"text\"");
            if (t) found = t;
        }
    } else {
        /* OpenAI-style: choices[0].message.content */
        const char *c = strstr(body, "\"choices\"");
        if (c) {
            const char *m = strstr(c, "\"message\"");
            if (!m) m = c;
            const char *t = strstr(m, "\"content\"");
            if (t) found = t;
        }
    }

    if (!found) return 0;

    /* Skip to the string value */
    found += strlen(found[1] == 't' ? "\"text\"" : "\"content\"");
    while (*found == ' ' || *found == ':') found++;
    if (*found == 'n') return 0; /* null */
    if (*found != '"') return 0;
    found++;

    /* Extract the string value (handle escapes) */
    int i = 0;
    while (*found && *found != '"' && i < maxlen - 1) {
        if (*found == '\\' && found[1]) {
            found++;
            if (*found == 'n') out[i++] = '\n';
            else if (*found == 't') out[i++] = '\t';
            else if (*found == '"') out[i++] = '"';
            else if (*found == '\\') out[i++] = '\\';
            else if (*found == '/') out[i++] = '/';
            else if (*found == 'u') {
                /* Unicode escape \uXXXX */
                unsigned int cp = 0;
                for (int j = 1; j <= 4 && found[j]; j++) {
                    cp <<= 4;
                    char h = found[j];
                    if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                }
                found += 4;
                /* Simple UTF-8 encode for BMP */
                if (cp < 0x80) {
                    out[i++] = (char)cp;
                } else if (cp < 0x800) {
                    if (i + 2 < maxlen) {
                        out[i++] = (char)(0xC0 | (cp >> 6));
                        out[i++] = (char)(0x80 | (cp & 0x3F));
                    }
                } else {
                    if (i + 3 < maxlen) {
                        out[i++] = (char)(0xE0 | (cp >> 12));
                        out[i++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        out[i++] = (char)(0x80 | (cp & 0x3F));
                    }
                }
            } else {
                out[i++] = *found;
            }
        } else {
            out[i++] = *found;
        }
        found++;
    }
    out[i] = '\0';
    return i;
}

/* ── Extract token usage ── */

static void extract_usage(const Provider *p, const char *body,
                          int *tokens_in, int *tokens_out)
{
    *tokens_in = 0;
    *tokens_out = 0;

    const char *usage = strstr(body, "\"usage\"");
    if (!usage) usage = strstr(body, "\"usageMetadata\"");
    if (!usage) return;

    if (strcmp(p->format, "anthropic") == 0) {
        *tokens_in = json_get_int(usage, "input_tokens");
        *tokens_out = json_get_int(usage, "output_tokens");
    } else if (strcmp(p->format, "gemini") == 0) {
        *tokens_in = json_get_int(usage, "promptTokenCount");
        *tokens_out = json_get_int(usage, "candidatesTokenCount");
    } else {
        *tokens_in = json_get_int(usage, "prompt_tokens");
        if (*tokens_in == 0)
            *tokens_in = json_get_int(usage, "input_tokens");
        *tokens_out = json_get_int(usage, "completion_tokens");
        if (*tokens_out == 0)
            *tokens_out = json_get_int(usage, "output_tokens");
    }
}

static double calc_cost(const Provider *p, int tokens_in, int tokens_out)
{
    return (tokens_in * p->input_cost + tokens_out * p->output_cost) / 1000000.0;
}

/* ── Per-provider thread context ── */

typedef struct {
    int conn_id;
    int provider_idx;
    char task[MAX_QUESTION];
    char hint[2048];
} ProviderThreadCtx;

#ifdef _WIN32
static DWORD WINAPI provider_thread(LPVOID arg)
#else
static void *provider_thread(void *arg)
#endif
{
    ProviderThreadCtx *ctx = (ProviderThreadCtx *)arg;
    Provider *p = &g_providers[ctx->provider_idx];

    LARGE_INTEGER freq, t0, t1;
#ifdef _WIN32
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
#else
    struct timespec ts0, ts1;
    clock_gettime(CLOCK_MONOTONIC, &ts0);
#endif

    /* Build request */
    char *body = (char *)malloc(MAX_QUESTION * 3);
    char headers[1024];
    char url[1024];
    if (!body) goto done;

    build_url(p, url, sizeof(url));
    build_headers(p, headers, sizeof(headers));

    if (strcmp(p->format, "anthropic") == 0) {
        build_anthropic_body(p, ctx->task, ctx->hint, body, MAX_QUESTION * 3);
    } else if (strcmp(p->format, "gemini") == 0) {
        build_gemini_body(p, ctx->task, ctx->hint, body, MAX_QUESTION * 3);
    } else {
        build_openai_body(p, ctx->task, ctx->hint, body, MAX_QUESTION * 3);
    }

    HttpRequest req = {
        .method = "POST",
        .url = url,
        .headers = headers,
        .body = (const uint8_t *)body,
        .body_len = (int)strlen(body),
        .timeout_ms = 60000
    };
    HttpResponse resp;
    int rc = http_request(&req, &resp);

#ifdef _WIN32
    QueryPerformanceCounter(&t1);
    int elapsed_ms = (int)((t1.QuadPart - t0.QuadPart) * 1000 / freq.QuadPart);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    int elapsed_ms = (int)((ts1.tv_sec - ts0.tv_sec) * 1000 +
                           (ts1.tv_nsec - ts0.tv_nsec) / 1000000);
#endif

    if (rc != 0 || resp.status < 200 || resp.status >= 300) {
        char err_json[1024];
        char esc_body[512];
        if (resp.body)
            json_escape((char *)resp.body, esc_body, sizeof(esc_body));
        else
            esc_body[0] = '\0';
        snprintf(err_json, sizeof(err_json),
            "{\"type\":\"error\",\"provider\":\"%s\","
            "\"content\":\"HTTP %d: %.400s\",\"elapsed_ms\":%d}",
            p->name, rc != 0 ? rc : resp.status,
            esc_body, elapsed_ms);
        send_event(ctx->conn_id, err_json);
        http_response_free(&resp);
        free(body);
        goto done;
    }

    /* Extract text */
    char *text = (char *)malloc(MAX_RESPONSE);
    if (!text) {
        http_response_free(&resp);
        free(body);
        goto done;
    }
    extract_text(p, (char *)resp.body, text, MAX_RESPONSE);

    /* Extract usage */
    int tokens_in = 0, tokens_out = 0;
    extract_usage(p, (char *)resp.body, &tokens_in, &tokens_out);
    if (tokens_in == 0 && tokens_out == 0) {
        tokens_in = (int)strlen(ctx->task) / 4;
        tokens_out = (int)strlen(text) / 4;
    }
    double cost = calc_cost(p, tokens_in, tokens_out);

    /* Store for judge */
    ws_lock();
    if (g_last_result_count < MAX_PROVIDERS) {
        ProviderResult *pr = &g_last_results[g_last_result_count++];
        strncpy(pr->provider, p->name, sizeof(pr->provider) - 1);
        strncpy(pr->response, text, MAX_RESPONSE - 1);
        pr->tokens_in = tokens_in;
        pr->tokens_out = tokens_out;
        pr->cost = cost;
        pr->elapsed_ms = elapsed_ms;
    }
    g_daily_total += cost;
    g_daily_queries++;
    g_daily_tokens_in += tokens_in;
    g_daily_tokens_out += tokens_out;
    ws_unlock();

    /* Send response event — include request body + raw API response for inspection */
    {
        int body_len = (int)strlen(body);
        int raw_len = resp.body ? (int)strlen((char *)resp.body) : 0;
        int out_cap = MAX_RESPONSE * 2 + body_len * 2 + raw_len * 2 + 2048;
        char *json_out = (char *)malloc((size_t)out_cap);
        if (json_out) {
            char *esc_text = (char *)malloc(MAX_RESPONSE * 2);
            char *esc_body = (char *)malloc(body_len * 2 + 1);
            char *esc_raw = (char *)malloc(raw_len * 2 + 1);
            if (esc_text && esc_body && esc_raw) {
                json_escape(text, esc_text, MAX_RESPONSE * 2);
                json_escape(body, esc_body, body_len * 2);
                json_escape(resp.body ? (char *)resp.body : "", esc_raw, raw_len * 2);
                snprintf(json_out, out_cap,
                    "{\"type\":\"text\",\"provider\":\"%s\","
                    "\"content\":\"%s\","
                    "\"request\":\"%s\","
                    "\"raw\":\"%s\","
                    "\"tokens_in\":%d,\"tokens_out\":%d,"
                    "\"cost\":%.6f,\"elapsed_ms\":%d}",
                    p->name, esc_text, esc_body, esc_raw,
                    tokens_in, tokens_out, cost, elapsed_ms);
                send_event(ctx->conn_id, json_out);
            }
            free(esc_text);
            free(esc_body);
            free(esc_raw);
            free(json_out);
        }
    }

    http_response_free(&resp);
    free(text);
    free(body);

done:
    free(ctx);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/* ── Guided AI thread — independent expert query, runs parallel to fan-out ── */

#ifdef _WIN32
static DWORD WINAPI guided_ai_thread(LPVOID arg)
#else
static void *guided_ai_thread(void *arg)
#endif
{
    ProviderThreadCtx *ctx = (ProviderThreadCtx *)arg;
    Provider *p = &g_providers[ctx->provider_idx];

    send_event(ctx->conn_id,
        "{\"type\":\"search_step\",\"step\":\"sense\","
        "\"detail\":\"detecting knowledge domain...\"}");

    send_event(ctx->conn_id,
        "{\"type\":\"search_step\",\"step\":\"sense\","
        "\"detail\":\"pillar: GENERAL (confidence: 0.50)\"}");

    {
        char step[512];
        snprintf(step, sizeof(step),
            "{\"type\":\"search_step\",\"step\":\"select\","
            "\"detail\":\"expert: %s (%s)\"}",
            p->name, p->model);
        send_event(ctx->conn_id, step);
    }

    {
        char step[512];
        snprintf(step, sizeof(step),
            "{\"type\":\"search_step\",\"step\":\"auth\","
            "\"detail\":\"%s loaded\"}", p->key_env);
        send_event(ctx->conn_id, step);
    }

    /* Build domain-focused prompt */
    char domain_hint[4096];
    snprintf(domain_hint, sizeof(domain_hint),
        "Provide a clear, factual answer.%s%s",
        ctx->hint[0] ? "\n\n" : "", ctx->hint);

    {
        char step[512];
        snprintf(step, sizeof(step),
            "{\"type\":\"search_step\",\"step\":\"prompt\","
            "\"detail\":\"system prompt (%d chars)\"}", (int)strlen(domain_hint));
        send_event(ctx->conn_id, step);
    }

    /* Build request */
    char *body = (char *)malloc(MAX_QUESTION * 3);
    char headers[1024];
    char url[1024];
    if (!body) goto gdone;

    build_url(p, url, sizeof(url));
    build_headers(p, headers, sizeof(headers));

    if (strcmp(p->format, "anthropic") == 0) {
        build_anthropic_body(p, ctx->task, domain_hint, body, MAX_QUESTION * 3);
    } else if (strcmp(p->format, "gemini") == 0) {
        build_gemini_body(p, ctx->task, domain_hint, body, MAX_QUESTION * 3);
    } else {
        build_openai_body(p, ctx->task, domain_hint, body, MAX_QUESTION * 3);
    }

    {
        char step[512];
        snprintf(step, sizeof(step),
            "{\"type\":\"search_step\",\"step\":\"request\","
            "\"detail\":\"POST %s (model: %s)\"}", p->endpoint, p->model);
        send_event(ctx->conn_id, step);
    }

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    HttpRequest req = {
        .method = "POST",
        .url = url,
        .headers = headers,
        .body = (const uint8_t *)body,
        .body_len = (int)strlen(body),
        .timeout_ms = 60000
    };
    HttpResponse resp;
    int rc = http_request(&req, &resp);

    QueryPerformanceCounter(&t1);
    int elapsed_ms = (int)((t1.QuadPart - t0.QuadPart) * 1000 / freq.QuadPart);

    {
        char step[512];
        snprintf(step, sizeof(step),
            "{\"type\":\"search_step\",\"step\":\"response\","
            "\"detail\":\"HTTP %d — %dms — %d bytes\"}",
            rc != 0 ? rc : resp.status, elapsed_ms, resp.body_len);
        send_event(ctx->conn_id, step);
    }

    if (rc != 0 || resp.status < 200 || resp.status >= 300) {
        char step[512];
        snprintf(step, sizeof(step),
            "{\"type\":\"search_step\",\"step\":\"error\","
            "\"detail\":\"HTTP %d\"}", rc != 0 ? rc : resp.status);
        send_event(ctx->conn_id, step);
        send_event(ctx->conn_id,
            "{\"type\":\"search\",\"error\":\"expert request failed\"}");
        http_response_free(&resp);
        free(body);
        goto gdone;
    }

    /* Extract text + usage */
    {
        char *text = (char *)malloc(MAX_RESPONSE);
        if (!text) {
            http_response_free(&resp);
            free(body);
            goto gdone;
        }
        extract_text(p, (char *)resp.body, text, MAX_RESPONSE);

        int tokens_in = 0, tokens_out = 0;
        extract_usage(p, (char *)resp.body, &tokens_in, &tokens_out);
        if (tokens_in == 0 && tokens_out == 0) {
            tokens_in = (int)strlen(ctx->task) / 4;
            tokens_out = (int)strlen(text) / 4;
        }
        double cost = calc_cost(p, tokens_in, tokens_out);

        {
            char step[256];
            snprintf(step, sizeof(step),
                "{\"type\":\"search_step\",\"step\":\"extract\","
                "\"detail\":\"%d tokens in / %d tokens out — $%.6f\"}",
                tokens_in, tokens_out, cost);
            send_event(ctx->conn_id, step);
        }

        ws_lock();
        g_daily_total += cost;
        g_daily_queries++;
        g_daily_tokens_in += tokens_in;
        g_daily_tokens_out += tokens_out;
        ws_unlock();

        {
            char step[256];
            snprintf(step, sizeof(step),
                "{\"type\":\"search_step\",\"step\":\"done\","
                "\"detail\":\"%d in / %d out — $%.4f\"}",
                tokens_in, tokens_out, cost);
            send_event(ctx->conn_id, step);
        }

        /* Send the search result — populates main panel */
        char *search_json = (char *)malloc(MAX_RESPONSE + 1024);
        if (search_json) {
            char esc_text[MAX_RESPONSE];
            json_escape(text, esc_text, MAX_RESPONSE);
            snprintf(search_json, MAX_RESPONSE + 1024,
                "{\"type\":\"search\",\"content\":\"%s\","
                "\"provider\":\"%s\",\"pillar\":\"general\","
                "\"tokens_in\":%d,\"tokens_out\":%d,"
                "\"cost\":%.6f}",
                esc_text, p->name, tokens_in, tokens_out, cost);
            send_event(ctx->conn_id, search_json);
            free(search_json);
        }

        free(text);
    }

    http_response_free(&resp);
    free(body);

gdone:
    free(ctx);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/* ── Main loop engine entry point ── */

static void handle_task(int conn_id, const char *json)
{
    char task[MAX_QUESTION];
    json_get_str(json, "content", task, sizeof(task));
    if (!task[0]) return;

    /* Save for judge */
    strncpy(g_last_question, task, MAX_QUESTION - 1);
    g_last_result_count = 0;
    g_last_ranked_count = 0;

    /* Phase 1: emit query start */
    {
        char esc[MAX_QUESTION * 2];
        json_escape(task, esc, sizeof(esc));
        char evt[MAX_QUESTION * 2 + 128];
        snprintf(evt, sizeof(evt),
            "{\"type\":\"phase\",\"phase\":\"understand\","
            "\"status\":\"understand\",\"content\":\"%s\"}", esc);
        send_event(conn_id, evt);
    }

    /* Sense event — mark all enabled providers as sensed */
    {
        char sense[4096];
        int spos = 0;
        spos += snprintf(sense + spos, sizeof(sense) - (size_t)spos,
            "{\"type\":\"sense\",\"pillar\":\"general\","
            "\"confidence\":0.5,\"stage1\":null,"
            "\"providers\":[");
        int first = 1;
        for (int i = 0; i < g_provider_count; i++) {
            if (g_providers[i].disabled || !g_providers[i].api_key[0]) continue;
            if (!first) sense[spos++] = ',';
            spos += snprintf(sense + spos, sizeof(sense) - (size_t)spos,
                "\"%s\"", g_providers[i].name);
            first = 0;
        }
        spos += snprintf(sense + spos, sizeof(sense) - (size_t)spos,
            "],\"related\":[]}");
        send_event(conn_id, sense);
    }

    /* Phase 2: intelligence via relay wire protocol */
    if (g_psk_loaded && g_relay_host[0]) {
        char *relay_json = (char *)malloc(MAX_JSON);
        if (relay_json) {
            int n = relay_intelligence(g_relay_host, g_relay_port,
                                       g_cockpit_key, task,
                                       relay_json, MAX_JSON);
            if (n > 0) {
                send_event(conn_id, relay_json);
            }
            free(relay_json);
        }
    }

    /* Phase 3: fan out to AI providers + guided AI — all in parallel */
    {
#ifdef _WIN32
        HANDLE threads[MAX_PROVIDERS + 1]; /* +1 for guided AI */
#endif
        int thread_count = 0;

        /* Build combined hint */
        char combined_hint[4096];
        combined_hint[0] = '\0';
        if (g_global_hint[0]) {
            strncpy(combined_hint, g_global_hint, sizeof(combined_hint) - 1);
        }

        /* Launch guided AI thread — picks first available provider */
        for (int i = 0; i < g_provider_count; i++) {
            Provider *p = &g_providers[i];
            if (p->disabled || !p->api_key[0]) continue;
            ProviderThreadCtx *gctx = (ProviderThreadCtx *)malloc(sizeof(*gctx));
            if (!gctx) break;
            gctx->conn_id = conn_id;
            gctx->provider_idx = i;
            strncpy(gctx->task, task, MAX_QUESTION - 1);
            gctx->task[MAX_QUESTION - 1] = '\0';
            strncpy(gctx->hint, combined_hint, sizeof(gctx->hint) - 1);
            gctx->hint[sizeof(gctx->hint) - 1] = '\0';
#ifdef _WIN32
            threads[thread_count] = CreateThread(NULL, 0, guided_ai_thread,
                                                  gctx, 0, NULL);
            if (threads[thread_count])
                thread_count++;
            else
                free(gctx);
#else
            pthread_t gtid;
            if (pthread_create(&gtid, NULL, guided_ai_thread, gctx) == 0) {
                pthread_detach(gtid);
                thread_count++;
            } else {
                free(gctx);
            }
#endif
            break; /* only one guided AI thread */
        }

        for (int i = 0; i < g_provider_count && thread_count < MAX_PROVIDERS + 1; i++) {
            Provider *p = &g_providers[i];

            if (p->disabled) {
                char skip[256];
                snprintf(skip, sizeof(skip),
                    "{\"type\":\"skipped\",\"provider\":\"%s\","
                    "\"reason\":\"disabled\"}", p->name);
                send_event(conn_id, skip);
                continue;
            }

            if (!p->api_key[0]) {
                char skip[256];
                snprintf(skip, sizeof(skip),
                    "{\"type\":\"error\",\"provider\":\"%s\","
                    "\"content\":\"no %s set\"}", p->name, p->key_env);
                send_event(conn_id, skip);
                continue;
            }

            /* Send thinking event */
            {
                char think[128];
                snprintf(think, sizeof(think),
                    "{\"type\":\"thinking\",\"provider\":\"%s\"}", p->name);
                send_event(conn_id, think);
            }

            /* Build per-provider hint */
            char full_hint[4096];
            full_hint[0] = '\0';
            if (combined_hint[0] && p->hint[0]) {
                snprintf(full_hint, sizeof(full_hint), "%s\n\n%s",
                         combined_hint, p->hint);
            } else if (combined_hint[0]) {
                strncpy(full_hint, combined_hint, sizeof(full_hint) - 1);
            } else if (p->hint[0]) {
                strncpy(full_hint, p->hint, sizeof(full_hint) - 1);
            }

            ProviderThreadCtx *ctx = (ProviderThreadCtx *)malloc(sizeof(*ctx));
            if (!ctx) continue;
            ctx->conn_id = conn_id;
            ctx->provider_idx = i;
            strncpy(ctx->task, task, MAX_QUESTION - 1);
            ctx->task[MAX_QUESTION - 1] = '\0';
            strncpy(ctx->hint, full_hint, sizeof(ctx->hint) - 1);
            ctx->hint[sizeof(ctx->hint) - 1] = '\0';

#ifdef _WIN32
            threads[thread_count] = CreateThread(NULL, 0, provider_thread,
                                                  ctx, 0, NULL);
            if (threads[thread_count])
                thread_count++;
            else
                free(ctx);
#else
            pthread_t tid;
            if (pthread_create(&tid, NULL, provider_thread, ctx) == 0) {
                pthread_detach(tid);
                thread_count++;
            } else {
                free(ctx);
            }
#endif
        }

#ifdef _WIN32
        /* Wait for all provider threads to complete */
        if (thread_count > 0)
            WaitForMultipleObjects((DWORD)thread_count, threads, TRUE, 120000);
        for (int i = 0; i < thread_count; i++)
            CloseHandle(threads[i]);
#endif
    }

    /* Phase 4: emit daily cost + done */
    {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        snprintf(g_daily_date, sizeof(g_daily_date), "%04d-%02d-%02d",
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);

        char evt[256];
        snprintf(evt, sizeof(evt),
            "{\"type\":\"daily_cost\",\"total\":%.6f,"
            "\"queries\":%d,\"tokens_in\":%d,\"tokens_out\":%d,"
            "\"date\":\"%s\"}",
            g_daily_total, g_daily_queries,
            g_daily_tokens_in, g_daily_tokens_out, g_daily_date);
        send_event(conn_id, evt);

        snprintf(evt, sizeof(evt),
            "{\"type\":\"done\",\"elapsed_ms\":0,\"responded\":%d}",
            g_last_result_count);
        send_event(conn_id, evt);
    }
}

/* ══════════════════════════════════════════════════════════════
 * Section 9: Judge system
 * ══════════════════════════════════════════════════════════════ */

/* ── Judge thread context ── */
typedef struct {
    int conn_id;
    int provider_idx;
    char *prompt;       /* shared, do not free */
    /* Output: parsed rankings */
    char rankings[MAX_PROVIDERS][64];
    int rank_count;
    char raw[2048];
    double cost;
    int tokens_in;
    int tokens_out;
    int elapsed_ms;
    int success;
} JudgeThreadCtx;

/* Parse "1. name — reason\n2. name — reason" into ordered name list */
static void parse_rankings(const char *text, JudgeThreadCtx *jctx)
{
    jctx->rank_count = 0;
    const char *p = text;
    while (*p && jctx->rank_count < MAX_PROVIDERS) {
        /* Find digit followed by . or ) */
        while (*p && !(*p >= '1' && *p <= '9' && (p[1] == '.' || p[1] == ')'))) p++;
        if (!*p) break;
        p += 2; /* skip "1." */
        while (*p == ' ') p++;
        /* Extract name — until ' —', ' -', newline, or end */
        char name[64];
        int ni = 0;
        while (*p && *p != '\n' && ni < 63) {
            if ((*p == ' ' && (p[1] == '-' || (p[1] == '\xe2' && p[2] == '\x80')))) break;
            if (*p == '-' && p[1] == '-') break;
            name[ni++] = *p++;
        }
        /* Trim trailing spaces */
        while (ni > 0 && name[ni-1] == ' ') ni--;
        name[ni] = '\0';
        if (ni > 0) {
            strncpy(jctx->rankings[jctx->rank_count], name, 63);
            jctx->rank_count++;
        }
        /* Skip to next line */
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
}

#ifdef _WIN32
static DWORD WINAPI judge_thread(LPVOID arg)
#else
static void *judge_thread(void *arg)
#endif
{
    JudgeThreadCtx *jctx = (JudgeThreadCtx *)arg;
    Provider *p = &g_providers[jctx->provider_idx];
    jctx->success = 0;

    char *body = (char *)malloc(MAX_QUESTION * 3 + 65536);
    char headers[1024];
    char url[1024];
    if (!body) goto jdone;

    build_url(p, url, sizeof(url));
    build_headers(p, headers, sizeof(headers));

    char sys_hint[] = "You are a judge evaluating AI responses. "
        "Rank all responses from best to worst. Be fair and objective. "
        "Return ONLY a numbered list with the provider name and a brief reason.";

    if (strcmp(p->format, "anthropic") == 0)
        build_anthropic_body(p, jctx->prompt, sys_hint, body, MAX_QUESTION * 3 + 65536);
    else if (strcmp(p->format, "gemini") == 0)
        build_gemini_body(p, jctx->prompt, sys_hint, body, MAX_QUESTION * 3 + 65536);
    else
        build_openai_body(p, jctx->prompt, sys_hint, body, MAX_QUESTION * 3 + 65536);

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    HttpRequest req = {
        .method = "POST", .url = url, .headers = headers,
        .body = (const uint8_t *)body, .body_len = (int)strlen(body),
        .timeout_ms = 45000
    };
    HttpResponse resp;
    int rc = http_request(&req, &resp);

    QueryPerformanceCounter(&t1);
    jctx->elapsed_ms = (int)((t1.QuadPart - t0.QuadPart) * 1000 / freq.QuadPart);

    if (rc != 0 || resp.status < 200 || resp.status >= 300) {
        char err[256];
        snprintf(err, sizeof(err),
            "{\"type\":\"judge_error\",\"provider\":\"%s\","
            "\"detail\":\"HTTP %d\",\"elapsed_ms\":%d}",
            p->name, rc != 0 ? rc : resp.status, jctx->elapsed_ms);
        send_event(jctx->conn_id, err);
        http_response_free(&resp);
        free(body);
        goto jdone;
    }

    /* Extract text + usage */
    char *text = (char *)malloc(MAX_RESPONSE);
    if (text) {
        extract_text(p, (char *)resp.body, text, MAX_RESPONSE);
        extract_usage(p, (char *)resp.body, &jctx->tokens_in, &jctx->tokens_out);
        if (jctx->tokens_in == 0 && jctx->tokens_out == 0) {
            jctx->tokens_in = (int)strlen(jctx->prompt) / 4;
            jctx->tokens_out = (int)strlen(text) / 4;
        }
        jctx->cost = calc_cost(p, jctx->tokens_in, jctx->tokens_out);

        /* Parse rankings */
        parse_rankings(text, jctx);

        /* Save raw for display */
        strncpy(jctx->raw, text, 2000);
        jctx->raw[2000] = '\0';

        /* Send judge_vote event */
        {
            char *evt = (char *)malloc(8192);
            if (evt) {
                int epos = 0;
                epos += snprintf(evt + epos, 8192 - (size_t)epos,
                    "{\"type\":\"judge_vote\",\"provider\":\"%s\","
                    "\"rankings\":[", p->name);
                for (int r = 0; r < jctx->rank_count; r++) {
                    char esc_name[128];
                    json_escape(jctx->rankings[r], esc_name, sizeof(esc_name));
                    if (r > 0) evt[epos++] = ',';
                    epos += snprintf(evt + epos, 8192 - (size_t)epos,
                        "\"%s\"", esc_name);
                }
                char esc_raw[4096];
                json_escape(jctx->raw, esc_raw, sizeof(esc_raw));
                epos += snprintf(evt + epos, 8192 - (size_t)epos,
                    "],\"raw\":\"%s\",\"cost\":%.6f,"
                    "\"tokens_in\":%d,\"tokens_out\":%d,"
                    "\"elapsed_ms\":%d}",
                    esc_raw, jctx->cost,
                    jctx->tokens_in, jctx->tokens_out, jctx->elapsed_ms);
                send_event(jctx->conn_id, evt);
                free(evt);
            }
        }

        jctx->success = 1;

        ws_lock();
        g_daily_total += jctx->cost;
        g_daily_queries++;
        g_daily_tokens_in += jctx->tokens_in;
        g_daily_tokens_out += jctx->tokens_out;
        ws_unlock();

        free(text);
    }

    http_response_free(&resp);
    free(body);

jdone:
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void handle_judge(int conn_id, const char *json)
{
    (void)json;
    if (g_last_result_count < 2) {
        send_event(conn_id,
            "{\"type\":\"judge_step\",\"detail\":\"need at least 2 responses to judge\"}");
        send_event(conn_id, "{\"type\":\"judge_done\"}");
        return;
    }

    send_event(conn_id, "{\"type\":\"judge_start\"}");

    /* Build judge prompt listing all responses */
    int prompt_cap = MAX_RESPONSE * MAX_PROVIDERS + 4096;
    char *prompt = (char *)malloc((size_t)prompt_cap);
    if (!prompt) {
        send_event(conn_id, "{\"type\":\"judge_done\"}");
        return;
    }

    int pos = 0;
    pos += snprintf(prompt + pos, (size_t)(prompt_cap - pos),
        "Question: \"%s\"\n\n", g_last_question);

    /* Provider name list for the prompt */
    char name_list[2048];
    int nlpos = 0;
    for (int i = 0; i < g_last_result_count; i++) {
        if (i > 0) nlpos += snprintf(name_list + nlpos, sizeof(name_list) - (size_t)nlpos, ", ");
        nlpos += snprintf(name_list + nlpos, sizeof(name_list) - (size_t)nlpos,
            "%s", g_last_results[i].provider);
        pos += snprintf(prompt + pos, (size_t)(prompt_cap - pos),
            "=== %s ===\n%.2000s\n\n",
            g_last_results[i].provider, g_last_results[i].response);
    }

    pos += snprintf(prompt + pos, (size_t)(prompt_cap - pos),
        "Rank these responses from best to worst based on accuracy, completeness, and clarity.\n"
        "Return ONLY a numbered list using these exact names: %s\n"
        "Format: 1. name — reason\n", name_list);

    {
        char step[512];
        snprintf(step, sizeof(step),
            "{\"type\":\"judge_step\",\"detail\":\"collecting %d responses for judging\"}",
            g_last_result_count);
        send_event(conn_id, step);
    }

    /* Count and announce */
    int judge_count = 0;
    for (int i = 0; i < g_provider_count; i++) {
        if (!g_providers[i].disabled && g_providers[i].api_key[0]) judge_count++;
    }

    {
        char step[256];
        snprintf(step, sizeof(step),
            "{\"type\":\"judge_step\",\"detail\":\"sending judge prompt to %d providers (%d chars)...\"}",
            judge_count, pos);
        send_event(conn_id, step);
    }

    /* Fan out */
#ifdef _WIN32
    HANDLE threads[MAX_PROVIDERS];
#endif
    JudgeThreadCtx jctxs[MAX_PROVIDERS];
    int thread_count = 0;

    for (int i = 0; i < g_provider_count && thread_count < MAX_PROVIDERS; i++) {
        Provider *p = &g_providers[i];
        if (p->disabled || !p->api_key[0]) continue;

        {
            char think[128];
            snprintf(think, sizeof(think),
                "{\"type\":\"judge_thinking\",\"provider\":\"%s\"}", p->name);
            send_event(conn_id, think);
        }

        JudgeThreadCtx *jctx = &jctxs[thread_count];
        memset(jctx, 0, sizeof(*jctx));
        jctx->conn_id = conn_id;
        jctx->provider_idx = i;
        jctx->prompt = prompt;

#ifdef _WIN32
        threads[thread_count] = CreateThread(NULL, 0, judge_thread,
                                              jctx, 0, NULL);
        if (threads[thread_count])
            thread_count++;
#endif
    }

#ifdef _WIN32
    if (thread_count > 0)
        WaitForMultipleObjects((DWORD)thread_count, threads, TRUE, 120000);
    for (int i = 0; i < thread_count; i++)
        CloseHandle(threads[i]);
#endif

    /* Aggregate rankings: average rank across all judges */
    double rank_sums[MAX_PROVIDERS];
    int rank_counts[MAX_PROVIDERS];
    memset(rank_sums, 0, sizeof(rank_sums));
    memset(rank_counts, 0, sizeof(rank_counts));

    int total_judges = 0;
    double total_judge_cost = 0;

    for (int j = 0; j < thread_count; j++) {
        JudgeThreadCtx *jctx = &jctxs[j];
        if (!jctx->success) continue;
        total_judges++;
        total_judge_cost += jctx->cost;

        for (int r = 0; r < jctx->rank_count; r++) {
            /* Find which result index this name maps to */
            for (int ri = 0; ri < g_last_result_count; ri++) {
                /* Case-insensitive partial match */
                if (strstr(jctx->rankings[r], g_last_results[ri].provider) ||
                    strstr(g_last_results[ri].provider, jctx->rankings[r])) {
                    rank_sums[ri] += (r + 1);
                    rank_counts[ri]++;
                    break;
                }
            }
        }
    }

    /* Build judge_result event */
    {
        /* Sort by avg rank */
        typedef struct { int idx; double avg; int votes; } RankEntry;
        RankEntry entries[MAX_PROVIDERS];
        int entry_count = 0;
        for (int i = 0; i < g_last_result_count; i++) {
            if (rank_counts[i] > 0) {
                entries[entry_count].idx = i;
                entries[entry_count].avg = rank_sums[i] / rank_counts[i];
                entries[entry_count].votes = rank_counts[i];
                entry_count++;
            }
        }
        /* Simple bubble sort by avg rank */
        for (int a = 0; a < entry_count - 1; a++)
            for (int b = a + 1; b < entry_count; b++)
                if (entries[b].avg < entries[a].avg) {
                    RankEntry tmp = entries[a];
                    entries[a] = entries[b];
                    entries[b] = tmp;
                }

        /* Save ranked order for /advise */
        g_last_ranked_count = entry_count;
        for (int i = 0; i < entry_count; i++)
            g_last_ranked_order[i] = entries[i].idx;

        char *evt = (char *)malloc(8192);
        if (evt) {
            int epos = 0;
            epos += snprintf(evt + epos, 8192 - (size_t)epos,
                "{\"type\":\"judge_result\",\"judges\":%d,"
                "\"cost\":%.6f,\"final_ranks\":[",
                total_judges, total_judge_cost);

            for (int i = 0; i < entry_count; i++) {
                RankEntry *e = &entries[i];
                const char *medal = i == 0 ? "gold" : i == 1 ? "silver" : i == 2 ? "bronze" : "none";
                if (i > 0) evt[epos++] = ',';
                epos += snprintf(evt + epos, 8192 - (size_t)epos,
                    "{\"provider\":\"%s\",\"avg_rank\":%.2f,"
                    "\"votes\":%d,\"position\":%d,\"medal\":\"%s\"}",
                    g_last_results[e->idx].provider, e->avg,
                    e->votes, i + 1, medal);
            }

            epos += snprintf(evt + epos, 8192 - (size_t)epos, "]}");
            send_event(conn_id, evt);
            free(evt);
        }
    }

    /* Judge cost */
    {
        char evt[256];
        snprintf(evt, sizeof(evt),
            "{\"type\":\"judge_cost\",\"last\":%.6f,"
            "\"daily_total\":%.6f,\"daily_count\":%d}",
            total_judge_cost, g_daily_total, g_daily_queries);
        send_event(conn_id, evt);
    }

    free(prompt);
    send_event(conn_id, "{\"type\":\"judge_done\"}");
}

/* ══════════════════════════════════════════════════════════════
 * Section 9b: Analysis thread context
 * ══════════════════════════════════════════════════════════════ */

typedef struct {
    int conn_id;
    int provider_idx;
    char *prompt; /* heap, thread frees */
} AdviseCtx;

/* ══════════════════════════════════════════════════════════════
 * Section 9b: Summarize system — comparative analysis of responses
 * ══════════════════════════════════════════════════════════════ */

#ifdef _WIN32
static DWORD WINAPI summarize_thread(LPVOID arg)
#else
static void *summarize_thread(void *arg)
#endif
{
    AdviseCtx *actx = (AdviseCtx *)arg;
    Provider *p = &g_providers[actx->provider_idx];

    char sys_hint[] =
        "You are an analytical summarizer. Compare and contrast the AI responses below. "
        "Structure your output as:\n"
        "1. CONSENSUS — what most or all providers agree on (table if possible)\n"
        "2. DIFFERENCES — where providers gave similar answers but with notable "
        "variations (state who said what)\n"
        "3. OUTLIERS — any provider with a unique or contrarian perspective "
        "(name them, explain how they diverge)\n"
        "4. RANKED OVERVIEW — if rankings are provided, list providers by rank "
        "with a one-line summary of each response's key contribution.\n\n"
        "Use tables and structured formatting. Be specific about which provider "
        "said what. Do not merge — compare.";

    int prompt_len = (int)strlen(actx->prompt);

    {
        char step[256];
        snprintf(step, sizeof(step),
            "{\"type\":\"summarize_step\",\"detail\":\"building %s request (%d char prompt)\"}",
            p->format, prompt_len);
        send_event(actx->conn_id, step);
    }

    int body_cap = prompt_len * 3 + 8192;
    char *body = (char *)malloc((size_t)body_cap);
    char headers[1024];
    char url[1024];
    if (!body) goto sdone;

    build_url(p, url, sizeof(url));
    build_headers(p, headers, sizeof(headers));

    if (strcmp(p->format, "anthropic") == 0)
        build_anthropic_body(p, actx->prompt, sys_hint, body, body_cap);
    else if (strcmp(p->format, "gemini") == 0)
        build_gemini_body(p, actx->prompt, sys_hint, body, body_cap);
    else
        build_openai_body(p, actx->prompt, sys_hint, body, body_cap);

    {
        char step[512];
        snprintf(step, sizeof(step),
            "{\"type\":\"summarize_step\",\"detail\":\"POST %s (%s) \\u2014 %d bytes, timeout 120s\"}",
            p->endpoint, p->model, (int)strlen(body));
        send_event(actx->conn_id, step);
    }

    send_event(actx->conn_id,
        "{\"type\":\"summarize_step\",\"detail\":\"analyzing responses \\u2014 finding consensus, differences, outliers...\"}");

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    HttpRequest req = {
        .method = "POST", .url = url, .headers = headers,
        .body = (const uint8_t *)body, .body_len = (int)strlen(body),
        .timeout_ms = 120000
    };
    HttpResponse resp;
    int rc = http_request(&req, &resp);

    QueryPerformanceCounter(&t1);
    int elapsed_ms = (int)((t1.QuadPart - t0.QuadPart) * 1000 / freq.QuadPart);

    {
        char step[256];
        snprintf(step, sizeof(step),
            "{\"type\":\"summarize_step\",\"detail\":\"HTTP %d \\u2014 %dms \\u2014 %d bytes\"}",
            rc != 0 ? rc : resp.status, elapsed_ms, resp.body_len);
        send_event(actx->conn_id, step);
    }

    if (rc != 0 || resp.status < 200 || resp.status >= 300) {
        send_event(actx->conn_id,
            "{\"type\":\"summarize_step\",\"detail\":\"analysis failed\"}");
        send_event(actx->conn_id,
            "{\"type\":\"summarize_done\",\"content\":\"\",\"provider\":\"\"}");
        http_response_free(&resp);
        free(body);
        goto sdone;
    }

    {
        char *text = (char *)malloc(MAX_RESPONSE);
        if (text) {
            extract_text(p, (char *)resp.body, text, MAX_RESPONSE);
            int tokens_in = 0, tokens_out = 0;
            extract_usage(p, (char *)resp.body, &tokens_in, &tokens_out);
            if (tokens_in == 0 && tokens_out == 0) {
                tokens_in = prompt_len / 4;
                tokens_out = (int)strlen(text) / 4;
            }
            double cost = calc_cost(p, tokens_in, tokens_out);

            int text_len = (int)strlen(text);
            int line_count = 1;
            for (int i = 0; i < text_len; i++)
                if (text[i] == '\n') line_count++;

            {
                char step[256];
                snprintf(step, sizeof(step),
                    "{\"type\":\"summarize_step\",\"detail\":\"summary received \\u2014 %d chars, %d lines\"}",
                    text_len, line_count);
                send_event(actx->conn_id, step);
            }

            ws_lock();
            g_daily_total += cost;
            g_daily_queries++;
            g_daily_tokens_in += tokens_in;
            g_daily_tokens_out += tokens_out;
            ws_unlock();

            {
                char step[256];
                snprintf(step, sizeof(step),
                    "{\"type\":\"summarize_step\",\"detail\":\"%d tok in / %d tok out \\u2014 $%.4f \\u2014 %dms\"}",
                    tokens_in, tokens_out, cost, elapsed_ms);
                send_event(actx->conn_id, step);
            }

            /* Send summarize_done with full content */
            {
                int evt_cap = MAX_RESPONSE * 2 + 1024;
                char *evt = (char *)malloc((size_t)evt_cap);
                if (evt) {
                    char *esc_text = (char *)malloc(MAX_RESPONSE * 2);
                    if (esc_text) {
                        json_escape(text, esc_text, MAX_RESPONSE * 2);
                        snprintf(evt, evt_cap,
                            "{\"type\":\"summarize_done\",\"content\":\"%s\","
                            "\"provider\":\"%s\",\"cost\":%.6f,"
                            "\"tokens_in\":%d,\"tokens_out\":%d,"
                            "\"elapsed_ms\":%d}",
                            esc_text, p->name, cost, tokens_in, tokens_out, elapsed_ms);
                        send_event(actx->conn_id, evt);
                        free(esc_text);
                    }
                    free(evt);
                }
            }

            free(text);
        }
    }

    http_response_free(&resp);
    free(body);

sdone:
    free(actx->prompt);
    free(actx);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void handle_summarize(int conn_id, const char *json)
{
    (void)json;
    if (g_last_result_count < 2) {
        send_event(conn_id,
            "{\"type\":\"summarize_step\",\"detail\":\"need at least 2 responses to compare \\u2014 run a query first\"}");
        send_event(conn_id,
            "{\"type\":\"summarize_done\",\"content\":\"\",\"provider\":\"\"}");
        return;
    }

    send_event(conn_id, "{\"type\":\"summarize_start\"}");

    /* Step 1: Explain */
    {
        char step[256];
        snprintf(step, sizeof(step),
            "{\"type\":\"summarize_step\",\"detail\":\"%d providers answered \\u2014 comparing responses\"}",
            g_last_result_count);
        send_event(conn_id, step);
    }

    /* Step 2: Show ranking if available */
    if (g_last_ranked_count > 0) {
        char rank_list[1024];
        int rlpos = 0;
        for (int i = 0; i < g_last_ranked_count && rlpos < 900; i++) {
            int ridx = g_last_ranked_order[i];
            if (i > 0) rlpos += snprintf(rank_list + rlpos, sizeof(rank_list) - (size_t)rlpos, ", ");
            rlpos += snprintf(rank_list + rlpos, sizeof(rank_list) - (size_t)rlpos,
                "#%d %s", i + 1, g_last_results[ridx].provider);
        }
        char step[1280];
        snprintf(step, sizeof(step),
            "{\"type\":\"summarize_step\",\"detail\":\"judge ranked them: %s\"}", rank_list);
        send_event(conn_id, step);
    } else {
        send_event(conn_id,
            "{\"type\":\"summarize_step\",\"detail\":\"no /judge run \\u2014 comparing in arrival order\"}");
    }

    /* Step 3: List responses */
    if (g_last_ranked_count > 0) {
        for (int i = 0; i < g_last_ranked_count; i++) {
            int ridx = g_last_ranked_order[i];
            char step[512];
            snprintf(step, sizeof(step),
                "{\"type\":\"summarize_step\",\"detail\":\"  #%d %s \\u2014 %d chars\"}",
                i + 1, g_last_results[ridx].provider,
                (int)strlen(g_last_results[ridx].response));
            send_event(conn_id, step);
        }
    } else {
        for (int i = 0; i < g_last_result_count; i++) {
            char step[512];
            snprintf(step, sizeof(step),
                "{\"type\":\"summarize_step\",\"detail\":\"  %s \\u2014 %d chars\"}",
                g_last_results[i].provider,
                (int)strlen(g_last_results[i].response));
            send_event(conn_id, step);
        }
    }

    /* Step 4: Pick analyzer — user choice, judge winner, or first available */
    int sum_provider = -1;
    const char *pick_reason = "";

    /* Check for user-selected engine override */
    char engine[64];
    json_get_str(json, "engine", engine, sizeof(engine));
    if (engine[0]) {
        for (int i = 0; i < g_provider_count; i++) {
            if (!g_providers[i].disabled && g_providers[i].api_key[0] &&
                strcmp(g_providers[i].name, engine) == 0) {
                sum_provider = i;
                pick_reason = "user selected";
                break;
            }
        }
    }

    if (sum_provider < 0 && g_last_ranked_count > 0) {
        int ridx = g_last_ranked_order[0];
        for (int i = 0; i < g_provider_count; i++) {
            if (!g_providers[i].disabled && g_providers[i].api_key[0] &&
                strcmp(g_providers[i].name, g_last_results[ridx].provider) == 0) {
                sum_provider = i;
                pick_reason = "judge winner analyzes \\u2014 most accurate eye on the field";
                break;
            }
        }
    }
    if (sum_provider < 0) {
        for (int i = 0; i < g_provider_count; i++) {
            if (!g_providers[i].disabled && g_providers[i].api_key[0]) {
                sum_provider = i;
                pick_reason = "first available provider";
                break;
            }
        }
    }
    if (sum_provider < 0) {
        send_event(conn_id,
            "{\"type\":\"summarize_step\",\"detail\":\"no providers available\"}");
        send_event(conn_id,
            "{\"type\":\"summarize_done\",\"content\":\"\",\"provider\":\"\"}");
        return;
    }

    {
        char step[512];
        snprintf(step, sizeof(step),
            "{\"type\":\"summarize_step\",\"detail\":\"analyzer: %s (%s)\"}",
            g_providers[sum_provider].name, g_providers[sum_provider].model);
        send_event(conn_id, step);
    }
    {
        char step[512];
        snprintf(step, sizeof(step),
            "{\"type\":\"summarize_step\",\"detail\":\"  why: %s\"}", pick_reason);
        send_event(conn_id, step);
    }

    /* Step 5: Build prompt */
    int prompt_cap = MAX_RESPONSE * MAX_PROVIDERS + 8192;
    char *prompt = (char *)malloc((size_t)prompt_cap);
    if (!prompt) {
        send_event(conn_id,
            "{\"type\":\"summarize_done\",\"content\":\"\",\"provider\":\"\"}");
        return;
    }

    int pos = 0;
    pos += snprintf(prompt + pos, (size_t)(prompt_cap - pos),
        "Question: \"%s\"\n\n", g_last_question);

    if (g_last_ranked_count > 0) {
        pos += snprintf(prompt + pos, (size_t)(prompt_cap - pos),
            "Responses ranked best-to-worst by peer review:\n\n");
        for (int i = 0; i < g_last_ranked_count; i++) {
            int ridx = g_last_ranked_order[i];
            pos += snprintf(prompt + pos, (size_t)(prompt_cap - pos),
                "=== #%d %s ===\n%s\n\n",
                i + 1, g_last_results[ridx].provider, g_last_results[ridx].response);
        }
    } else {
        pos += snprintf(prompt + pos, (size_t)(prompt_cap - pos),
            "Responses (unranked):\n\n");
        for (int i = 0; i < g_last_result_count; i++) {
            pos += snprintf(prompt + pos, (size_t)(prompt_cap - pos),
                "=== %s ===\n%s\n\n",
                g_last_results[i].provider, g_last_results[i].response);
        }
    }

    pos += snprintf(prompt + pos, (size_t)(prompt_cap - pos),
        "Analyze and compare these responses. Structure your output as:\n\n"
        "1. RANKED OVERVIEW — one line per provider (by rank if available) "
        "summarizing their key contribution.\n"
        "2. OUTLIERS — any provider with a unique or contrarian take. "
        "Name them, explain how they diverge.\n"
        "3. DIFFERENCES — similar answers with notable variations. "
        "Name who said what.\n"
        "4. CONSENSUS — what most/all agree on. Use a table if possible.\n\n"
        "Do NOT merge responses. Compare them. Be specific about who said what.\n");

    {
        char step[512];
        snprintf(step, sizeof(step),
            "{\"type\":\"summarize_step\",\"detail\":\"task: find consensus, differences, outliers across %d responses\"}",
            g_last_ranked_count > 0 ? g_last_ranked_count : g_last_result_count);
        send_event(conn_id, step);
    }

    /* Send full prompt for inspection */
    {
        int evt_cap = pos * 2 + 512;
        char *evt = (char *)malloc((size_t)evt_cap);
        if (evt) {
            char *esc_prompt = (char *)malloc(pos * 2 + 1);
            if (esc_prompt) {
                json_escape(prompt, esc_prompt, pos * 2);
                snprintf(evt, evt_cap,
                    "{\"type\":\"summarize_prompt\",\"content\":\"%s\",\"length\":%d}",
                    esc_prompt, pos);
                send_event(conn_id, evt);
                free(esc_prompt);
            }
            free(evt);
        }
    }

    /* Launch thread */
    AdviseCtx *actx = (AdviseCtx *)malloc(sizeof(*actx));
    if (!actx) {
        free(prompt);
        send_event(conn_id,
            "{\"type\":\"summarize_done\",\"content\":\"\",\"provider\":\"\"}");
        return;
    }
    actx->conn_id = conn_id;
    actx->provider_idx = sum_provider;
    actx->prompt = prompt;

#ifdef _WIN32
    HANDLE ht = CreateThread(NULL, 0, summarize_thread, actx, 0, NULL);
    if (!ht) {
        free(prompt);
        free(actx);
        send_event(conn_id,
            "{\"type\":\"summarize_done\",\"content\":\"\",\"provider\":\"\"}");
    }
    if (ht) CloseHandle(ht);
#else
    pthread_t tid;
    if (pthread_create(&tid, NULL, summarize_thread, actx) == 0) {
        pthread_detach(tid);
    } else {
        free(prompt);
        free(actx);
        send_event(conn_id,
            "{\"type\":\"summarize_done\",\"content\":\"\",\"provider\":\"\"}");
    }
#endif
}

/* ══════════════════════════════════════════════════════════════
 * Section 10: Hints and provider state
 * ══════════════════════════════════════════════════════════════ */

static void handle_set_hint(int conn_id, const char *json)
{
    (void)conn_id;
    char provider[64], hint[1024];
    json_get_str(json, "provider", provider, sizeof(provider));
    json_get_str(json, "hint", hint, sizeof(hint));
    for (int i = 0; i < g_provider_count; i++) {
        if (strcmp(g_providers[i].name, provider) == 0) {
            strncpy(g_providers[i].hint, hint, sizeof(g_providers[i].hint) - 1);
            break;
        }
    }
}

static void handle_set_global_hint(int conn_id, const char *json)
{
    (void)conn_id;
    json_get_str(json, "hint", g_global_hint, sizeof(g_global_hint));
}

static void handle_set_disabled(int conn_id, const char *json)
{
    (void)conn_id;
    char provider[64];
    json_get_str(json, "provider", provider, sizeof(provider));
    int disabled = json_get_bool(json, "disabled");
    for (int i = 0; i < g_provider_count; i++) {
        if (strcmp(g_providers[i].name, provider) == 0) {
            g_providers[i].disabled = disabled;
            break;
        }
    }
}

static void handle_get_hints(int conn_id, const char *json)
{
    (void)json;
    /* Build hints response */
    char *out = (char *)malloc(MAX_JSON);
    if (!out) return;

    int pos = 0;
    pos += snprintf(out + pos, (size_t)(MAX_JSON - pos),
        "{\"type\":\"hints\",\"global_hint\":");

    char esc[2048];
    json_escape(g_global_hint, esc, sizeof(esc));
    pos += snprintf(out + pos, (size_t)(MAX_JSON - pos), "\"%s\",\"providers\":{", esc);

    int first = 1;
    for (int i = 0; i < g_provider_count; i++) {
        if (g_providers[i].hint[0]) {
            if (!first) out[pos++] = ',';
            first = 0;
            json_escape(g_providers[i].hint, esc, sizeof(esc));
            pos += snprintf(out + pos, (size_t)(MAX_JSON - pos),
                "\"%s\":\"%s\"", g_providers[i].name, esc);
        }
    }
    pos += snprintf(out + pos, (size_t)(MAX_JSON - pos), "},\"disabled\":[");

    first = 1;
    for (int i = 0; i < g_provider_count; i++) {
        if (g_providers[i].disabled) {
            if (!first) out[pos++] = ',';
            first = 0;
            pos += snprintf(out + pos, (size_t)(MAX_JSON - pos),
                "\"%s\"", g_providers[i].name);
        }
    }
    pos += snprintf(out + pos, (size_t)(MAX_JSON - pos), "]}");

    send_event(conn_id, out);
    free(out);
}

/* ══════════════════════════════════════════════════════════════
 * Section 11: Message router
 * ══════════════════════════════════════════════════════════════ */

static void route_ws_message(int conn_id, const char *msg, int len)
{
    (void)len;
    char type[64];
    json_get_str(msg, "type", type, sizeof(type));

    if (strcmp(type, "relay_auth") == 0) {
        handle_relay_auth(conn_id, msg);
    } else if (strcmp(type, "relay_register") == 0) {
        handle_relay_register(conn_id, msg);
    } else if (strcmp(type, "task") == 0) {
        handle_task(conn_id, msg);
    } else if (strcmp(type, "judge") == 0) {
        handle_judge(conn_id, msg);
    } else if (strcmp(type, "store") == 0) {
        handle_store(conn_id, msg);
    } else if (strcmp(type, "set_hint") == 0) {
        handle_set_hint(conn_id, msg);
    } else if (strcmp(type, "set_global_hint") == 0) {
        handle_set_global_hint(conn_id, msg);
    } else if (strcmp(type, "set_disabled") == 0) {
        handle_set_disabled(conn_id, msg);
    } else if (strcmp(type, "get_hints") == 0) {
        handle_get_hints(conn_id, msg);
    } else if (strcmp(type, "summarize") == 0) {
        handle_summarize(conn_id, msg);
    } else if (strcmp(type, "new") == 0) {
        /* Reset loop state */
        g_last_result_count = 0;
        g_last_ranked_count = 0;
        g_last_question[0] = '\0';
    }
    /* terminal, set_route, reconsider — TODO */
}

/* ══════════════════════════════════════════════════════════════
 * Section 12: Event loop
 * ══════════════════════════════════════════════════════════════ */

static void find_wwwroot(void)
{
    /* Search known locations relative to exe */
    const char *candidates[] = {
        "wwwroot",
        "../wwwroot",
        "../../user/interface/desktop/wwwroot",
        "../../../user/interface/desktop/wwwroot",
        /* No hardcoded paths — exe-relative only */
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        char test[MAX_PATH_LEN * 2];
        snprintf(test, sizeof(test), "%s/index.html", candidates[i]);
        FILE *f = fopen(test, "r");
        if (f) {
            fclose(f);
            strncpy(g_wwwroot, candidates[i], MAX_PATH_LEN - 1);
            return;
        }
    }
    /* Fallback */
    strncpy(g_wwwroot, "wwwroot", MAX_PATH_LEN - 1);
}

int serve_init(int port)
{
#ifdef _WIN32
    InitializeCriticalSection(&g_ws_lock);
#endif

    find_wwwroot();
    load_relay_config();

    /* Bind and listen */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)port);

    g_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen == INVALID_SOCKET) return -1;

    /* Allow address reuse */
    int opt = 1;
    setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));

    if (bind(g_listen, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
        return -1;
    }

    if (listen(g_listen, 8) < 0) {
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
        return -1;
    }

    fprintf(stderr, "[serve] listening on localhost:%d\n", port);
    fprintf(stderr, "[serve] wwwroot: %s\n", g_wwwroot);
    fprintf(stderr, "[serve] relay: %s:%d (psk: %s)\n",
            g_relay_host, g_relay_port, g_psk_loaded ? "loaded" : "missing");

    return 0;
}

int serve_run(void)
{
    g_running = 1;

    /* Load providers after stores are ready */
    load_providers();
    fprintf(stderr, "[serve] %d providers loaded\n", g_provider_count);

    /* Initialize HTTP client for outbound requests */
    http_init();

    while (g_running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(g_listen, &rfds);

        SOCKET maxfd = g_listen;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_clients[i].alive) {
                FD_SET(g_clients[i].sock, &rfds);
                if (g_clients[i].sock > maxfd)
                    maxfd = g_clients[i].sock;
            }
        }

        struct timeval tv = {0, 100000}; /* 100ms */
        int ready = select((int)(maxfd + 1), &rfds, NULL, NULL, &tv);
        if (ready < 0) continue;

        /* Accept new connections */
        if (FD_ISSET(g_listen, &rfds)) {
            struct sockaddr_in ca;
            int calen = sizeof(ca);
            SOCKET ns = accept(g_listen, (struct sockaddr *)&ca, &calen);
            if (ns != INVALID_SOCKET) {
                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (!g_clients[i].alive) { slot = i; break; }
                }
                if (slot >= 0) {
                    memset(&g_clients[slot], 0, sizeof(Client));
                    g_clients[slot].sock = ns;
                    g_clients[slot].type = CONN_HTTP;
                    g_clients[slot].id = g_next_id++;
                    g_clients[slot].alive = 1;
                } else {
                    closesocket(ns);
                }
            }
        }

        /* Process client data */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            Client *c = &g_clients[i];
            if (!c->alive || !FD_ISSET(c->sock, &rfds)) continue;

            /* Read available data */
            int space = RECV_BUF - c->recv_len - 1;
            if (space <= 0) { c->alive = 0; closesocket(c->sock); continue; }

            int got = recv(c->sock, c->recv_buf + c->recv_len, space, 0);
            if (got <= 0) {
                c->alive = 0;
                closesocket(c->sock);
                continue;
            }
            c->recv_len += got;
            c->recv_buf[c->recv_len] = '\0';

            if (c->type == CONN_HTTP) {
                handle_http(c);
            }

            if (c->type == CONN_WS) {
                char msg[RECV_BUF];
                int mlen;
                while ((mlen = ws_read_frame(c, msg, sizeof(msg))) > 0) {
                    route_ws_message(c->id, msg, mlen);
                }
                if (mlen < 0) {
                    c->alive = 0;
                    closesocket(c->sock);
                }
            }
        }
    }

    /* Cleanup */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].alive)
            closesocket(g_clients[i].sock);
    }
    closesocket(g_listen);
    g_listen = INVALID_SOCKET;
    http_shutdown();
    return 0;
}

void serve_stop(void)
{
    g_running = 0;
}
