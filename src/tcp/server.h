/**
 * @file server.h
 * @author Theis <theismejnertsen@gmail.com>
 * @date 2025-11-02
 */

#ifndef _TCP_SERVER_H__
#define _TCP_SERVER_H__

/******************************************************************************
 * Includes
 *****************************************************************************/

#include <stddef.h>
#include <stdint.h>

/******************************************************************************
 * Constants
 *****************************************************************************/
/******************************************************************************
 * Types
 *****************************************************************************/

typedef int (*tcp_server_on_response_done_cb_t)(int err, void *user_data);

typedef struct {
    uint8_t *data;
    size_t data_len;
    void *user_data;
    tcp_server_on_response_done_cb_t on_done;
} tcp_server_response_t;

typedef int (*tcp_server_on_request_cb_t)(uint8_t *data, size_t len,
                                          tcp_server_response_t *response);

/******************************************************************************
 * Functions
 *****************************************************************************/

/**
 * Start the TCP server
 */
int tcp_server_start(int port, tcp_server_on_request_cb_t on_request_cb);

/**
 * Stop the TCP server
 */
void tcp_server_stop(int i);

#endif // _TCP_SERVER_H__
