/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/*
 * config.h — Desktop configuration.
 *
 * Loads desktop.conf (key=value format) for relay, port, and provider settings.
 * Falls back to server.conf + triples.key for backward compatibility.
 */

#define CONFIG_MAX_PROVIDERS 20

typedef struct {
    /* Server */
    int listen_port;

    /* Relay */
    char relay_host[256];
    int  relay_port;
    uint8_t psk[32];
    uint8_t cockpit_key[32];
    int  psk_loaded;

    /* Provider count (loaded from env/keys.bat) */
    int provider_count;
} DesktopConfig;

/* Load config from desktop.conf, falling back to server.conf + triples.key.
 * Returns 0 on success, -1 on error. */
int config_load(DesktopConfig *cfg);

#endif /* CONFIG_H */
