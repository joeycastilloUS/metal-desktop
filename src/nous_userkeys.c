/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */
/*
 * nous_userkeys.c — Per-user API key store
 *
 * INI-style file: [user:<name>] sections, KEY=VALUE lines.
 * Pure C, no dependencies. ~100 lines.
 */

#include "nous_userkeys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_USERS    32
#define MAX_KEYS     16
#define MAX_NAME     64
#define MAX_VAL      256

typedef struct {
    char key[MAX_NAME];
    char val[MAX_VAL];
} UserKey;

typedef struct {
    char    name[MAX_NAME];
    UserKey keys[MAX_KEYS];
    int     key_count;
} UserEntry;

static UserEntry g_users[MAX_USERS];
static int       g_user_count = 0;

static UserEntry *find_user(const char *name)
{
    for (int i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].name, name) == 0)
            return &g_users[i];
    }
    return NULL;
}

int nous_userkeys_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    g_user_count = 0;
    UserEntry *cur = NULL;
    char line[512];

    while (fgets(line, sizeof(line), f)) {
        /* Trim trailing whitespace */
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' '))
            line[--len] = '\0';

        /* Skip empty lines and comments */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#') continue;

        /* Section header: [user:name] */
        if (*p == '[' && g_user_count < MAX_USERS) {
            char *colon = strchr(p, ':');
            char *end   = strchr(p, ']');
            if (colon && end && end > colon) {
                cur = &g_users[g_user_count++];
                memset(cur, 0, sizeof(*cur));
                int nlen = (int)(end - colon - 1);
                if (nlen > 0 && nlen < MAX_NAME) {
                    memcpy(cur->name, colon + 1, (size_t)nlen);
                    cur->name[nlen] = '\0';
                }
            }
            continue;
        }

        /* KEY=VALUE */
        if (cur && cur->key_count < MAX_KEYS) {
            char *eq = strchr(p, '=');
            if (!eq) continue;
            int klen = (int)(eq - p);
            if (klen <= 0 || klen >= MAX_NAME) continue;
            char *val = eq + 1;
            int vlen = (int)strlen(val);
            if (vlen <= 0 || vlen >= MAX_VAL) continue;

            UserKey *uk = &cur->keys[cur->key_count++];
            memcpy(uk->key, p, (size_t)klen);
            uk->key[klen] = '\0';
            memcpy(uk->val, val, (size_t)vlen);
            uk->val[vlen] = '\0';
        }
    }

    fclose(f);
    fprintf(stderr, "[nous] loaded %d user key profiles from %s\n", g_user_count, path);
    return 0;
}

const char *nous_userkeys_get(const char *user, const char *key_env)
{
    if (!user || !key_env) return NULL;
    UserEntry *u = find_user(user);
    if (!u) return NULL;
    for (int i = 0; i < u->key_count; i++) {
        if (strcmp(u->keys[i].key, key_env) == 0)
            return u->keys[i].val;
    }
    return NULL;
}

int nous_userkeys_set(const char *path, const char *user,
                      const char *key_env, const char *value)
{
    if (!user || !key_env || !value) return -1;

    /* Find or create user */
    UserEntry *u = find_user(user);
    if (!u) {
        if (g_user_count >= MAX_USERS) return -1;
        u = &g_users[g_user_count++];
        memset(u, 0, sizeof(*u));
        strncpy(u->name, user, MAX_NAME - 1);
    }

    /* Find or create key slot */
    UserKey *slot = NULL;
    for (int i = 0; i < u->key_count; i++) {
        if (strcmp(u->keys[i].key, key_env) == 0) {
            slot = &u->keys[i];
            break;
        }
    }
    if (!slot) {
        if (u->key_count >= MAX_KEYS) return -1;
        slot = &u->keys[u->key_count++];
        strncpy(slot->key, key_env, MAX_NAME - 1);
    }
    strncpy(slot->val, value, MAX_VAL - 1);

    /* Write entire file back */
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (int i = 0; i < g_user_count; i++) {
        UserEntry *ue = &g_users[i];
        fprintf(f, "[user:%s]\n", ue->name);
        for (int j = 0; j < ue->key_count; j++)
            fprintf(f, "%s=%s\n", ue->keys[j].key, ue->keys[j].val);
        fprintf(f, "\n");
    }
    fclose(f);
    return 0;
}

void nous_userkeys_free(void)
{
    g_user_count = 0;
    memset(g_users, 0, sizeof(g_users));
}
