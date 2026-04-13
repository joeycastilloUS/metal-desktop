/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */
/*
 * config.c — Desktop configuration loader.
 *
 * Reads desktop.conf (key=value), or falls back to legacy server.conf + triples.key.
 * Key derivation: cockpit_key = HMAC-SHA256(psk, "nous-transport:cockpit")
 */

#include "config.h"
#include "crypt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <libgen.h>
#endif

/* ── Helpers ── */

/* Get directory containing the running executable */
static void get_exe_dir(char *buf, int maxlen)
{
    buf[0] = '\0';
#ifdef _WIN32
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        /* Strip filename, keep trailing backslash */
        char *last = strrchr(path, '\\');
        if (!last) last = strrchr(path, '/');
        if (last) { last[1] = '\0'; }
        strncpy(buf, path, maxlen - 1);
        buf[maxlen - 1] = '\0';
    }
#else
    char path[1024];
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n > 0) {
        path[n] = '\0';
        char *dir = dirname(path);
        snprintf(buf, maxlen, "%s/", dir);
    }
#endif
}

static int hex_decode(const char *hex, uint8_t *out, int maxlen)
{
    int len = 0;
    while (hex[0] && hex[1] && len < maxlen) {
        unsigned int b;
        if (sscanf(hex, "%2x", &b) != 1) break;
        out[len++] = (uint8_t)b;
        hex += 2;
    }
    return len;
}

static void trim(char *s)
{
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' || s[len-1] == ' '))
        s[--len] = '\0';
}

static int load_psk(DesktopConfig *cfg, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    uint8_t raw[128];
    size_t got = fread(raw, 1, sizeof(raw), f);
    fclose(f);

    if (got == 32) {
        /* Raw binary PSK */
        memcpy(cfg->psk, raw, 32);
    } else if (got >= 64) {
        /* Hex-encoded PSK */
        char hex[128];
        memcpy(hex, raw, got < 127 ? got : 127);
        hex[got < 127 ? got : 127] = '\0';
        trim(hex);
        if (hex_decode(hex, cfg->psk, 32) != 32) return -1;
    } else {
        return -1;
    }

    /* Derive cockpit key */
    sha256_hmac(cfg->psk, 32,
                (const uint8_t *)"nous-transport:cockpit", 22,
                cfg->cockpit_key);
    cfg->psk_loaded = 1;
    return 0;
}

/* ── desktop.conf loader ── */

static int load_desktop_conf(DesktopConfig *cfg)
{
    char exedir[512];
    get_exe_dir(exedir, sizeof(exedir));
    char path[600];
    snprintf(path, sizeof(path), "%sdesktop.conf", exedir);

    FILE *f = fopen(path, "r");
    if (!f) return -1;
    {

        char line[512];
        while (fgets(line, sizeof(line), f)) {
            trim(line);
            if (line[0] == '#' || line[0] == '\0') continue;

            char *eq = strchr(line, '=');
            if (!eq) continue;
            *eq = '\0';
            char *key = line;
            char *val = eq + 1;

            /* Strip leading spaces from value */
            while (*val == ' ') val++;

            if (strcmp(key, "relay_host") == 0) {
                strncpy(cfg->relay_host, val, sizeof(cfg->relay_host) - 1);
            } else if (strcmp(key, "relay_port") == 0) {
                cfg->relay_port = atoi(val);
            } else if (strcmp(key, "listen_port") == 0) {
                cfg->listen_port = atoi(val);
            } else if (strcmp(key, "psk_file") == 0) {
                load_psk(cfg, val);
            }
        }
        fclose(f);
        return 0;
    }
    return -1;
}

/* ── Legacy server.conf + triples.key loader ── */

static void load_legacy_config(DesktopConfig *cfg)
{
    char exedir[512];
    get_exe_dir(exedir, sizeof(exedir));

    char conf_path[600];
    snprintf(conf_path, sizeof(conf_path), "%sserver.conf", exedir);
    FILE *f = fopen(conf_path, "r");
    if (f) {
        char line[256];
        if (fgets(line, sizeof(line), f)) {
            trim(line);
            char *colon = strrchr(line, ':');
            if (colon) {
                *colon = '\0';
                strncpy(cfg->relay_host, line, sizeof(cfg->relay_host) - 1);
                cfg->relay_port = atoi(colon + 1);
            }
        }
        fclose(f);
    }

    char key_path[600];
    snprintf(key_path, sizeof(key_path), "%striples.key", exedir);
    load_psk(cfg, key_path);
}

/* ── Public API ── */

int config_load(DesktopConfig *cfg)
{
    /* Defaults — relay via DNS, no config file needed */
    memset(cfg, 0, sizeof(*cfg));
    cfg->listen_port = 5050;
    strncpy(cfg->relay_host, "relay.3-nous.net", sizeof(cfg->relay_host) - 1);
    cfg->relay_port = 8080;

    /* Optional overrides: desktop.conf or legacy server.conf */
    if (load_desktop_conf(cfg) != 0)
        load_legacy_config(cfg);

    /* PSK always loaded from exe dir regardless of config source */
    if (!cfg->psk_loaded) {
        char exedir[512];
        get_exe_dir(exedir, sizeof(exedir));
        char key_path[600];
        snprintf(key_path, sizeof(key_path), "%striples.key", exedir);
        load_psk(cfg, key_path);
    }

    return 0;
}
