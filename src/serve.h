/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */
#ifndef SERVE_H
#define SERVE_H

/* Initialize server on port. Binds socket, creates listener. */
int serve_init(int port);

/* Run event loop. Blocks until serve_stop() called. */
int serve_run(void);

/* Stop event loop. Closes all connections, unbinds. */
void serve_stop(void);

/* Send JSON message to a specific WebSocket client. */
int serve_ws_send(int conn_id, const char *json, int len);

/* Broadcast JSON to all connected WebSocket clients. */
int serve_ws_broadcast(const char *json, int len);

#endif /* SERVE_H */
