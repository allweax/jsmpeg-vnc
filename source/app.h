#ifndef APP_H
#define APP_H

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "encoder.h"
#include "grabber.h"
#include "server.h"

#define APP_MOUSE_SPEED 5.0f
#define APP_FRAME_BUFFER_SIZE (1024*1024)

typedef struct {
	encoder_t *encoder;
	grabber_t *grabber;
	server_t *server;
	int allow_input;

	float mouse_speed;
    char root[1024];
} app_t;


app_t *app_create(HWND window, int port, int bit_rate, int out_width, int out_height, int allow_input, grabber_crop_area_t crop, char *root);
void app_destroy(app_t *self);
void app_run(app_t *self, int targt_fps);

int app_on_http_req(app_t *self, struct lws *wsi, char *request);
void app_on_connect(app_t *self, struct lws *wsi, client_t *client);
void app_on_close(app_t *self, struct lws *wsi);
void app_on_message(app_t *self, struct lws *wsi, void *data, size_t len);

#endif
