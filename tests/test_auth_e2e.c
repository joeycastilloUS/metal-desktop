/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */
/*
 * test_auth_e2e.c — End-to-end auth flow: desktop → relay → prime
 *
 * Exercises the full FQDN auth registration flow through the relay,
 * including dual-method verify (wellknown/cloudflare), reset-totp,
 * and verify-reset. Uses the Wire protocol against the relay server.
 *
 * Build:
 *   gcc -std=c11 -O2 test_auth_e2e.c -o test_auth_e2e.exe \
 *       -I../sdk -L../build -lws2_32
 *
 * Run (requires relay_server forwarding to prime_server):
 *   ./test_auth_e2e.exe [relay-host] [relay-port]
 *   Default: 3-nous.net 9200
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wire.h"

#define TEST_FQDN "e2e-test.example.com"
#define MAX_RESP  64

static int g_pass = 0, g_fail = 0;

static void check(const char *name, int cond)
{
    if (cond) {
        printf("  PASS  %s\n", name);
        g_pass++;
    } else {
        printf("  FAIL  %s\n", name);
        g_fail++;
    }
}

/* ── Test 1: register via relay — challenge + dual-method prompt ── */
static void test_register_relay(const char *host, int port)
{
    printf("\n[1] Relay: register\n");

    WireTriple req[4];
    int nreq = 0;
    req[nreq++] = (WireTriple){"register", "action", "none"};
    req[nreq++] = (WireTriple){TEST_FQDN, "entity", "none"};

    WireTriple resp[MAX_RESP];
    char buf[WIRE_BUF_SIZE];
    int n = wire_request(host, port, req, nreq, resp, MAX_RESP, buf, sizeof(buf));

    check("got response", n > 0);
    if (n <= 0) return;

    const char *status = wire_find(resp, n, "status");
    check("status = verify", status && strcmp(status, "verify") == 0);

    const char *challenge = wire_find(resp, n, "challenge");
    check("challenge present (64 hex chars)", challenge && strlen(challenge) == 64);

    const char *method = wire_find(resp, n, "method");
    check("method = cloudflare|wellknown",
          method && strcmp(method, "cloudflare|wellknown") == 0);
}

/* ── Test 2: verify-register via relay — wellknown on unreachable domain ── */
static void test_verify_relay_wellknown_fail(const char *host, int port)
{
    printf("\n[2] Relay: verify-register (wellknown, unreachable)\n");

    /* Register first to get challenge */
    WireTriple req[4];
    int nreq = 0;
    req[nreq++] = (WireTriple){"register", "action", "none"};
    req[nreq++] = (WireTriple){TEST_FQDN, "entity", "none"};

    WireTriple resp[MAX_RESP];
    char buf[WIRE_BUF_SIZE];
    int n = wire_request(host, port, req, nreq, resp, MAX_RESP, buf, sizeof(buf));
    if (n <= 0) { check("register for challenge", 0); return; }

    /* Verify with wellknown — should fail (domain unreachable) */
    nreq = 0;
    req[nreq++] = (WireTriple){"verify-register", "action", "none"};
    req[nreq++] = (WireTriple){TEST_FQDN, "entity", "none"};
    req[nreq++] = (WireTriple){"wellknown", "method", "none"};

    n = wire_request(host, port, req, nreq, resp, MAX_RESP, buf, sizeof(buf));
    check("got response", n > 0);
    if (n <= 0) return;

    const char *status = wire_find(resp, n, "status");
    check("status = error (unreachable)", status && strcmp(status, "error") == 0);
}

/* ── Test 3: verify-register via relay — missing method ── */
static void test_verify_relay_no_method(const char *host, int port)
{
    printf("\n[3] Relay: verify-register (no method, no token)\n");

    WireTriple req[4];
    int nreq = 0;
    req[nreq++] = (WireTriple){"verify-register", "action", "none"};
    req[nreq++] = (WireTriple){TEST_FQDN, "entity", "none"};

    WireTriple resp[MAX_RESP];
    char buf[WIRE_BUF_SIZE];
    int n = wire_request(host, port, req, nreq, resp, MAX_RESP, buf, sizeof(buf));

    check("got response", n > 0);
    if (n <= 0) return;

    const char *status = wire_find(resp, n, "status");
    check("status = error", status && strcmp(status, "error") == 0);
}

/* ── Test 4: reset-totp via relay — non-existent user ── */
static void test_reset_relay_nonexistent(const char *host, int port)
{
    printf("\n[4] Relay: reset-totp (non-existent user)\n");

    WireTriple req[4];
    int nreq = 0;
    req[nreq++] = (WireTriple){"reset-totp", "action", "none"};
    req[nreq++] = (WireTriple){"nobody.e2e-test.example.com", "entity", "none"};

    WireTriple resp[MAX_RESP];
    char buf[WIRE_BUF_SIZE];
    int n = wire_request(host, port, req, nreq, resp, MAX_RESP, buf, sizeof(buf));

    check("got response", n > 0);
    if (n <= 0) return;

    const char *status = wire_find(resp, n, "status");
    check("status = error", status && strcmp(status, "error") == 0);
}

/* ── Test 5: verify-reset via relay — no prior reset ── */
static void test_verify_reset_relay_no_challenge(const char *host, int port)
{
    printf("\n[5] Relay: verify-reset (no prior reset-totp)\n");

    WireTriple req[4];
    int nreq = 0;
    req[nreq++] = (WireTriple){"verify-reset", "action", "none"};
    req[nreq++] = (WireTriple){TEST_FQDN, "entity", "none"};
    req[nreq++] = (WireTriple){"wellknown", "method", "none"};

    WireTriple resp[MAX_RESP];
    char buf[WIRE_BUF_SIZE];
    int n = wire_request(host, port, req, nreq, resp, MAX_RESP, buf, sizeof(buf));

    check("got response", n > 0);
    if (n <= 0) return;

    const char *status = wire_find(resp, n, "status");
    check("status = error (no challenge)", status && strcmp(status, "error") == 0);
}

/* ── Test 6: auth via relay — bad TOTP ── */
static void test_auth_relay_bad_totp(const char *host, int port)
{
    printf("\n[6] Relay: auth (bad TOTP)\n");

    WireTriple req[4];
    int nreq = 0;
    req[nreq++] = (WireTriple){"auth", "action", "none"};
    req[nreq++] = (WireTriple){TEST_FQDN, "entity", "none"};
    req[nreq++] = (WireTriple){"000000", "signal", "none"};

    WireTriple resp[MAX_RESP];
    char buf[WIRE_BUF_SIZE];
    int n = wire_request(host, port, req, nreq, resp, MAX_RESP, buf, sizeof(buf));

    check("got response", n > 0);
    if (n <= 0) return;

    const char *status = wire_find(resp, n, "status");
    check("status = not_found or rejected",
          status && (strcmp(status, "not_found") == 0 ||
                     strcmp(status, "rejected") == 0));
}

/* ── Test 7: register via relay — bad FQDN ── */
static void test_register_relay_bad_fqdn(const char *host, int port)
{
    printf("\n[7] Relay: register (invalid FQDN)\n");

    WireTriple req[4];
    int nreq = 0;
    req[nreq++] = (WireTriple){"register", "action", "none"};
    req[nreq++] = (WireTriple){"no-dot-here", "entity", "none"};

    WireTriple resp[MAX_RESP];
    char buf[WIRE_BUF_SIZE];
    int n = wire_request(host, port, req, nreq, resp, MAX_RESP, buf, sizeof(buf));

    check("got response", n > 0);
    if (n <= 0) return;

    const char *status = wire_find(resp, n, "status");
    check("status = error", status && strcmp(status, "error") == 0);
}

int main(int argc, char **argv)
{
    const char *host = (argc > 1) ? argv[1] : "3-nous.net";
    int port = (argc > 2) ? atoi(argv[2]) : 9200;

    printf("metal auth e2e test — %s:%d\n", host, port);
    printf("────────────────────────────────────────\n");

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    test_register_relay(host, port);
    test_verify_relay_wellknown_fail(host, port);
    test_verify_relay_no_method(host, port);
    test_reset_relay_nonexistent(host, port);
    test_verify_reset_relay_no_challenge(host, port);
    test_auth_relay_bad_totp(host, port);
    test_register_relay_bad_fqdn(host, port);

    printf("\n════════════════════════════════════════\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);

#ifdef _WIN32
    WSACleanup();
#endif

    return g_fail ? 1 : 0;
}
