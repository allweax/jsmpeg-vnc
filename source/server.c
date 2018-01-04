#include <stdlib.h>
#include <stdio.h>
#include "server.h"

#pragma comment(lib, "websockets_static.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "Ws2_32.lib")

buffer_t *buffer_create(server_data_type_t type, unsigned char *buf, size_t size)
{
    buffer_t *buffer = (buffer_t *)malloc(sizeof(buffer_t));
    buffer->send_buffer = (unsigned char *)malloc(size);
    memcpy(buffer->send_buffer, buf, size);
    buffer->buffer_size = size;
    buffer->type = type;
    buffer->next = NULL;
    return buffer;
}

void buffer_push_back(buffer_t **head, buffer_t *buf)
{
    buffer_t *cur = NULL;
    if (NULL == *head)
    {
        *head = buf;
    }
    else
    {
        cur = *head;
        while (cur && (NULL != cur->next))
        {
            cur = cur->next;
        }

        cur->next = buf;
    }
}

buffer_t *buffer_pop_front(buffer_t **head)
{
    buffer_t *buffer = *head;
    if (buffer)
    {
        if (buffer->next)
        {
            *head = buffer->next;
        }
        else
        {
            *head = NULL;
        }
        buffer->next = NULL;
    }

    return buffer;
}

void buffer_free(buffer_t **p)
{
    if (*p)
    {
        if ((*p)->send_buffer)
        {
            free((*p)->send_buffer);
        }

        free(*p);
        *p = NULL;
    }
}

void buffer_clear(buffer_t **head)
{
    buffer_t *cur = *head;
    while (cur)
    {
        *head = (*head)->next;
        buffer_free(&cur);
        cur = *head;
    }
}

client_t *client_insert(client_t **head, struct lws *wsi) {
	client_t *client = (struct server_client_t *)malloc(sizeof(struct server_client_t));
	client->wsi = wsi;
    client->buffers = NULL;
	client->next = *head;
	*head = client;
	return client;
}


client_t *client_find(client_t **head, struct lws *wsi) {
    for (client_t **current = head; *current; current = &(*current)->next) {
        if ((*current)->wsi == wsi) {
            return (*current);
        }
    }

    return NULL;
}

void client_remove(client_t **head, struct lws *wsi) {
	for( client_t **current = head; *current; current = &(*current)->next ) {
		if( (*current)->wsi == wsi ) {
			client_t* next = (*current)->next;
            buffer_clear(&(*current)->buffers);
			free(*current);
			*current = next;
			break;
		}
	}
}

static int callback_http(struct lws *, enum lws_callback_reasons, void *, void *, size_t);
static int callback_websockets_jsmpeg_vnc(struct lws *, enum lws_callback_reasons,void *, void *, size_t);

static struct lws_protocols server_protocols[] = {
	{ "http-only", callback_http, 0 },
	{ "ws", callback_websockets_jsmpeg_vnc, sizeof(int), 1024*1024 },
	{ NULL, NULL, 0 /* End of list */ }
};

server_t *server_create(int port, size_t buffer_size) {
	server_t *self = (server_t *)malloc(sizeof(server_t));
	memset(self, 0, sizeof(server_t));
	
	self->buffer_size = buffer_size;
	size_t full_buffer_size = LWS_SEND_BUFFER_PRE_PADDING + buffer_size + LWS_SEND_BUFFER_POST_PADDING;
	self->send_buffer_with_padding = (unsigned char *)malloc(full_buffer_size);
	self->send_buffer = &self->send_buffer_with_padding[LWS_SEND_BUFFER_PRE_PADDING];

	self->port = port;
	self->clients = NULL;

	lws_set_log_level(LLL_ERR | LLL_WARN, NULL);

	struct lws_context_creation_info info = {0};
	info.port = port;
	info.gid = -1;
	info.uid = -1;
	info.user = (void *)self;
	info.protocols = server_protocols;
	self->context = lws_create_context(&info);

	if( !self->context ) {
		server_destroy(self);
		return NULL;
	}

	return self;
}

void server_destroy(server_t *self) {
	if( self == NULL ) { return; }

	if( self->context ) {
		lws_context_destroy(self->context);
	}
	
	free(self->send_buffer_with_padding);
	free(self);
}

char *server_get_host_address(server_t *self) {
    char host_name[80] = { 0 };
	struct hostent *host;
	if( 
		gethostname(host_name, sizeof(host_name)) == SOCKET_ERROR ||
		!(host = gethostbyname(host_name))
	) {
		return "127.0.0.1";
	}

	return inet_ntoa( *(IN_ADDR *)(host->h_addr_list[0]) );
}

char *server_get_client_address(server_t *self, struct lws *wsi) {
    static char ip_buffer[32] = { 0 };
    static char name_buffer[32] = { 0 };

	lws_get_peer_addresses(
		wsi, lws_get_socket_fd(wsi), 
		name_buffer, sizeof(name_buffer), 
		ip_buffer, sizeof(ip_buffer)
	);
	return ip_buffer;
}

void server_update(server_t *self) {
	lws_callback_on_writable_all_protocol(self->context, &(server_protocols[1]));
	lws_service(self->context, 0);
}

void server_send(server_t *self, struct lws *wsi, void *data, size_t size, server_data_type_t type) {
	// Caution, this may explode! The libwebsocket docs advise against ever calling libwebsocket_write()
	// outside of LWS_CALLBACK_SERVER_WRITEABLE. Honoring this advise would complicate things quite
	// a bit - and it seems to work just fine on my system as it is anyway.
	// This won't work reliably on mobile systems where network buffers are typically much smaller.
	// ¯\_(ツ)_/¯

	if( size > self->buffer_size ) {
		printf("Cant send %d bytes; exceeds buffer size (%d bytes)\n", size, self->buffer_size);
		return;
	}
	memcpy(self->send_buffer, data, size);
	lws_write(wsi, self->send_buffer, size, (enum lws_write_protocol)type);
}

void server_send_buffer(server_t *self, struct lws *wsi, buffer_t *buffer)
{
    server_send(self, wsi, buffer->send_buffer, buffer->buffer_size, buffer->type);
}

void server_broadcast(server_t *self, void *data, size_t size, server_data_type_t type)
{
    buffer_t * buffer = NULL;
    client_foreach(self->clients, client) {
        buffer = buffer_create(type, data, size);
        buffer_push_back(&client->buffers, buffer);
    }
    lws_callback_on_writable_all_protocol(self->context, &(server_protocols[1]));
}

static int callback_websockets_jsmpeg_vnc(
	struct lws *wsi,
	enum lws_callback_reasons reason,
	void *user, void *in, size_t len)
{
    struct lws_context *ctx = lws_get_context(wsi);
	server_t *self = (server_t *)lws_context_user(ctx);
    client_t * client = NULL;

	switch( reason ) {
		case LWS_CALLBACK_ESTABLISHED:
			client = client_insert(&self->clients, wsi);
            if (self->on_connect)
            {
                self->on_connect(self, wsi, client);
            }
			break;
        case LWS_CALLBACK_SERVER_WRITEABLE:
            client = client_find(&self->clients, wsi);
            buffer_t *buffer = buffer_pop_front(&client->buffers);
            if (NULL != buffer)
            {
                server_send_buffer(self, wsi, buffer);
                buffer_free(&buffer);
            }
            break;
		case LWS_CALLBACK_RECEIVE:
			if( self->on_message ) {
				self->on_message(self, wsi, in, len);
			}
			break;

		case LWS_CALLBACK_CLOSED:
			client_remove(&self->clients, wsi);		
		
			if( self->on_close ) {
				self->on_close(self, wsi);
			}
			break;
	}
	
	return 0;
}

static int callback_http(struct lws *wsi,
	enum lws_callback_reasons reason, void *user,
	void *in, size_t len)
{
    struct lws_context* ctx = lws_get_context(wsi);
	server_t *self = (server_t *)lws_context_user(ctx);
	
	if( reason == LWS_CALLBACK_HTTP ) {
        if (!self->on_http_req)
        {
            lws_return_http_status(wsi, HTTP_STATUS_NOT_IMPLEMENTED, NULL);
            return -1; // close
        }
        
        if (!self->on_http_req(self, wsi, (char *)in))
        {
            lws_return_http_status(wsi, HTTP_STATUS_NOT_FOUND, NULL);
            return -1; // close
		}
	}
    else if (reason == LWS_CALLBACK_HTTP_FILE_COMPLETION)
    {
        if (lws_http_transaction_completed(wsi))
            return -1; // close
    }
    else if (reason == LWS_CALLBACK_HTTP_WRITEABLE)
    {
        if (lws_http_transaction_completed(wsi))
            return -1; // close
    }
	
	return 0; // keep-alive
}

