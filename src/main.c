/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */
/*
 * main.c — metal Desktop entry point.
 * Initializes and runs the HTTP + WebSocket server on localhost:5050.
 */

#include "serve.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#endif

int main(int argc, char **argv)
{
    int port = 5050;

    if (argc > 1)
        port = atoi(argv[1]);

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    fprintf(stderr, "metal Desktop starting on port %d...\n", port);

    if (serve_init(port) < 0) {
        fprintf(stderr, "Cannot bind port %d\n", port);
        return 1;
    }

    serve_run();

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
