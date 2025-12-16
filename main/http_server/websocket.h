#ifndef WEBSOCKET_H_
#define WEBSOCKET_H_

#include "esp_err.h"

// Reduced from 128 to 32 for low memory mode - saves ~12KB of RAM
#define MESSAGE_QUEUE_SIZE (32)
#define MAX_WEBSOCKET_CLIENTS (10)

esp_err_t websocket_handler(httpd_req_t * req);
void websocket_task(void * pvParameters);
void websocket_close_fn(httpd_handle_t hd, int sockfd);

#endif /* WEBSOCKET_H_ */
