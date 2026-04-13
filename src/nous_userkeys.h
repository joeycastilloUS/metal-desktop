/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */
#ifndef NOUS_USERKEYS_H
#define NOUS_USERKEYS_H

/*
 * nous_userkeys — Per-user API key store
 *
 * Reads/writes users.dat in INI-style sections:
 *   [user:joey]
 *   ANTHROPIC_API_KEY=sk-ant-...
 *   OPENAI_API_KEY=sk-...
 *
 *   [user:scotty]
 *   ANTHROPIC_API_KEY=sk-ant-...
 */

int         nous_userkeys_load(const char *path);
const char *nous_userkeys_get(const char *user, const char *key_env);
int         nous_userkeys_set(const char *path, const char *user,
                              const char *key_env, const char *value);
void        nous_userkeys_free(void);

#endif
