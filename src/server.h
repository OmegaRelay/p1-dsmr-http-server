/**
 * @file server.h
 * @author Theis <theismejnertsen@gmail.com>
 * @date 2025-11-02
 */

#ifndef __SERVER_H__
#define __SERVER_H__

/******************************************************************************
 * Includes
 *****************************************************************************/

#include <stddef.h>
#include <zephyr/net/http/method.h>
#include <zephyr/net/http/status.h>

/******************************************************************************
 * Constants
 *****************************************************************************/

#define SERVER_URL_MAX_LEN 128

/******************************************************************************
 * Types
 *****************************************************************************/

struct server_request {
    char url[SERVER_URL_MAX_LEN];
    enum http_method method;
    const char *body;
    size_t body_len;
};

struct server_response {
    int status;
    char *body;
    size_t body_len;
    void (*on_done)(int err);
};

typedef int (*server_resource_cb_t)(const struct server_request *req,
                                    struct server_response *res);

/******************************************************************************
 * Functions
 *****************************************************************************/

/**
 * Start the HTTP server
 */
void server_start(void);

/**
 * Stop the HTTP server
 */
void server_stop(void);

int server_add_resource(char *uri, server_resource_cb_t cb);

int server_remove_resource(char *uri);

#endif // __SERVER_H__
