#ifndef SERVER_H
#define SERVER_H

#include "libwebsockets.h"

typedef enum {
    server_type_text = LWS_WRITE_TEXT,
    server_type_binary = LWS_WRITE_BINARY
} server_data_type_t;

typedef struct server_buffer_t
{
    server_data_type_t type;
    size_t buffer_size;
    unsigned char *send_buffer;
    struct server_buffer_t *next;
} buffer_t;

buffer_t *buffer_create(server_data_type_t type, unsigned char *buf, size_t size);
void buffer_push_back(buffer_t **head, buffer_t *buf);
buffer_t *buffer_pop_front(buffer_t **head);
void buffer_free(buffer_t **p);
void buffer_clear(buffer_t **head);

typedef struct server_client_t {
	struct lws *wsi;
    buffer_t * buffers;
	struct server_client_t *next;
} client_t;


client_t *client_insert(client_t **head, struct lws *wsi);
client_t *client_find(client_t **head, struct lws *wsi);
void client_remove(client_t **head, struct lws *wsi);

#define client_foreach(HEAD, CLIENT) for(client_t *CLIENT = HEAD; CLIENT; CLIENT = CLIENT->next)

typedef struct server_server_t {
	struct lws_context *context;
	size_t buffer_size;
	unsigned char *send_buffer_with_padding;
	unsigned char *send_buffer;
	void *user;
	
	int port;
	client_t *clients;
	
	void (*on_connect)(struct server_server_t *server, struct lws *wsi, client_t *client);
	void (*on_message)(struct server_server_t *server, struct lws *wsi, void *in, size_t len);
	void (*on_close)(struct server_server_t *server, struct lws *wsi);
	int (*on_http_req)(struct server_server_t *server, struct lws *wsi, char *request);
} server_t;


server_t *server_create(int port, size_t buffer_size);
void server_destroy(server_t *self);
char *server_get_host_address(server_t *self);
char *server_get_client_address(server_t *self, struct lws *wsi);
void server_update(server_t *self);
void server_send(server_t *self, struct lws *wsi, void *data, size_t size, server_data_type_t type);
void server_send_buffer(server_t *self, struct lws *wsi, buffer_t *buffer);
void server_broadcast(server_t *self, void *data, size_t size, server_data_type_t type);

#endif
