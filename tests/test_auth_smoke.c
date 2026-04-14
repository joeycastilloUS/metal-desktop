/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */
/*
 * test_auth_smoke.c — Smoke test: FQDN auth challenge + verify + reset
 *
 * Exercises the Wire-based auth flow against a running prime_server on
 * localhost. Tests both Cloudflare and .well-known verification method
 * prompts. Actual CF API calls and .well-known fetch are server-side;
 * this test verifies the Wire protocol round-trips and response shapes.
 *
 * Build:
 *   gcc -std=c11 -O2 test_auth_smoke.c -o test_auth_smoke.exe \
 *       -I../sdk -L../build -lws2_32
 *
 * Run (requires prime_server on localhost:9100):
 *   ./test_auth_smoke.exe [host] [port]
 *   Default: localhost 9100
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wire.h"

#define TEST_FQDN "smoke-test.example.com"
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

/* ── Test 1: register — should return challenge + dual-method prompt ── */
static void test_register(const char *host, int port)
{
    printf("\n[1] Wire: register\n");

    WireTriple req[4];
    int nreq = 0;
    req[nreq++] = (WireTriple){"register", "action", "none"};
    req[nreq++] = (WireTriple){TEST_FQDN, "entity", "none"};

    WireTriple resp[MAX_RESP];
    char buf[WIRE_BUF_SIZE];
    int n = wire_request(host, port, req, nreq, resp, MAX_RESP, buf, sizeof(buf));

    check("got response", n > 0);
    if (n <= 0) return;

    /* Find status triple */
    const char *status = wire_find(resp, n, "status");
    check("status = verify", status && strcmp(status, "verify") == 0);

    /* Should have a challenge */
    const char *challenge = wire_find(resp, n, "challenge");
    check("challenge present", challenge && strlen(challenge) == 64);

    /* Should have method options */
    const char *method = wire_find(resp, n, "method");
    check("method = cloudflare|wellknown",
          method && strcmp(method, "cloudflare|wellknown") == 0);

    /* Message should mention metal identity */
    const char *msg = wire_find(resp, n, "message");
    check("prompt mentions metal",
          msg && strstr(msg, "metal identity") != NULL);
    check("prompt mentions .well-known",
          msg && strstr(msg, ".well-known/metal-verify") != NULL);
    check("prompt mentions Cloudflare",
          msg && strstr(msg, "Cloudflare") != NULL);
}

/* ── Test 2: verify-register with bad method — should fail gracefully ── */
static void test_verify_bad(const char *host, int port)
{
    printf("\n[2] Wire: verify-register (no token, no method)\n");

    WireTriple req[4];
    int nreq = 0;
    req[nreq++] = (WireTriple){"verify-register", "action", "none"};
    req[nreq++] = (WireTriple){TEST_FQDN, "entity", "none"};
    /* Intentionally missing cf-token and method */

    WireTriple resp[MAX_RESP];
    char buf[WIRE_BUF_SIZE];
    int n = wire_request(host, port, req, nreq, resp, MAX_RESP, buf, sizeof(buf));

    check("got response", n > 0);
    if (n <= 0) return;

    const char *status = wire_find(resp, n, "status");
    check("status = error", status && strcmp(status, "error") == 0);

    const char *msg = wire_find(resp, n, "message");
    check("message mentions cf-token or wellknown",
          msg && (strstr(msg, "cf-token") != NULL ||
                  strstr(msg, "wellknown") != NULL));
}

/* ── Test 3: verify-register with wellknown on non-existent domain ── */
static void test_verify_wellknown_fail(const char *host, int port)
{
    printf("\n[3] Wire: verify-register (wellknown, unreachable domain)\n");

    /* First register to get a challenge */
    WireTriple req[4];
    int nreq = 0;
    req[nreq++] = (WireTriple){"register", "action", "none"};
    req[nreq++] = (WireTriple){TEST_FQDN, "entity", "none"};

    WireTriple resp[MAX_RESP];
    char buf[WIRE_BUF_SIZE];
    int n = wire_request(host, port, req, nreq, resp, MAX_RESP, buf, sizeof(buf));
    if (n <= 0) { check("register for challenge", 0); return; }

    /* Now try verify with wellknown on an unreachable domain */
    nreq = 0;
    req[nreq++] = (WireTriple){"verify-register", "action", "none"};
    req[nreq++] = (WireTriple){TEST_FQDN, "entity", "none"};
    req[nreq++] = (WireTriple){"wellknown", "method", "none"};

    n = wire_request(host, port, req, nreq, resp, MAX_RESP, buf, sizeof(buf));
    check("got response", n > 0);
    if (n <= 0) return;

    const char *status = wire_find(resp, n, "status");
    check("status = error (domain unreachable)",
          status && strcmp(status, "error") == 0);
}

/* ── Test 4: reset-totp on non-existent user — should fail ── */
static void test_reset_nonexistent(const char *host, int port)
{
    printf("\n[4] Wire: reset-totp (non-existent user)\n");

    WireTriple req[4];
    int nreq = 0;
    req[nreq++] = (WireTriple){"reset-totp", "action", "none"};
    req[nreq++] = (WireTriple){"nobody.example.com", "entity", "none"};

    WireTriple resp[MAX_RESP];
    char buf[WIRE_BUF_SIZE];
    int n = wire_request(host, port, req, nreq, resp, MAX_RESP, buf, sizeof(buf));

    check("got response", n > 0);
    if (n <= 0) return;

    const char *status = wire_find(resp, n, "status");
    check("status = error (user not found)",
          status && strcmp(status, "error") == 0);
}

/* ── Test 5: auth with bad TOTP — should reject ── */
static void test_auth_bad_totp(const char *host, int port)
{
    printf("\n[5] Wire: auth (bad TOTP code)\n");

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
    /* Either "not_found" (no user) or "rejected" (bad TOTP) */
    check("status = not_found or rejected",
          status && (strcmp(status, "not_found") == 0 ||
                     strcmp(status, "rejected") == 0));
}

/* ── Test 6: register with invalid FQDN — should error ── */
static void test_register_bad_fqdn(const char *host, int port)
{
    printf("\n[6] Wire: register (invalid FQDN)\n");

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

    const char *msg = wire_find(resp, n, "message");
    check("message mentions FQDN",
          msg && strstr(msg, "FQDN") != NULL);
}

int main(int argc, char **argv)
{
    const char *host = (argc > 1) ? argv[1] : "localhost";
    int port = (argc > 2) ? atoi(argv[2]) : 9100;

    printf("metal auth smoke test — %s:%d\n", host, port);
    printf("────────────────────────────────────────\n");

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    test_register(host, port);
    test_verify_bad(host, port);
    test_verify_wellknown_fail(host, port);
    test_reset_nonexistent(host, port);
    test_auth_bad_totp(host, port);
    test_register_bad_fqdn(host, port);

    printf("\n════════════════════════════════════════\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);

#ifdef _WIN32
    WSACleanup();
#endif

    return g_fail ? 1 : 0;
}
