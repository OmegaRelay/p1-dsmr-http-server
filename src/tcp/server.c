/**
 * @file server.c
 * @author Theis <theismejnertsen@gmail.com>
 * @date 2025-11-02
 */

/******************************************************************************
 * Includes
 *****************************************************************************/

#include "server.h"
#include "zephyr/arch/arch_interface.h"
#include "zephyr/kernel/thread_stack.h"
#include "zephyr/toolchain.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/errno_private.h>

#include <zephyr/logging/log.h>

/******************************************************************************
 * Constants
 *****************************************************************************/

struct config {
    bool is_in_use;
    k_tid_t tid;
    struct k_thread thread;
    k_thread_stack_t *stack;
    int port;
    tcp_server_on_request_cb_t on_request_cb;
};

/******************************************************************************
 * Local Function Prototypes
 *****************************************************************************/

static void server_thread(void *p1, void *p2, void *p3);
static int setup_server_socket(struct config *config);
static void handle_client(struct config *config, int fd,
                          const struct sockaddr_in addr,
                          const socklen_t addrlen);

/******************************************************************************
 * Private Variables
 *****************************************************************************/

LOG_MODULE_REGISTER(tcp_server, CONFIG_APP_LOG_LEVEL);

static struct config configs[10] = {};

static int server_fd = -1;
static uint8_t rx_buf[16384] = {0};

/******************************************************************************
 * Public Functions
 *****************************************************************************/

int tcp_server_start(int port, tcp_server_on_request_cb_t on_request_cb) {
    struct config *config = NULL;
    size_t i;
    int ret = 0;
    for (i = 0; i < ARRAY_SIZE(configs); i++) {
        config = &configs[i];
        if (!config->is_in_use) {
            ret = i;
            break;
        }
    }
    if (config == NULL) {
        return -ENOBUFS;
    }
    config->port = port;
    config->on_request_cb = on_request_cb;

    config->stack = k_thread_stack_alloc(2048, 0);
    if (config->stack == NULL) {
        return -ENOMEM;
    }
    config->tid =
        k_thread_create(&config->thread, config->stack, 8192, server_thread, config,
                        NULL, NULL, 1, 0, K_NO_WAIT);

#ifdef CONFIG_THREAD_NAME
    char name[24];
    snprintf(name, sizeof(name), "tcp_server_%u", ret);
    k_thread_name_set(&config->thread, name);
#endif // CONFIG_THREAD_NAME
    return ret;
}

void tcp_server_stop(int i) {
    struct config *config = &configs[i];
    k_thread_abort(config->tid);
    (void)k_thread_stack_free(config->stack);
}

/******************************************************************************
 * Local Function Implementation
 *****************************************************************************/

static void server_thread(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);


    struct config *config = p1;
    int ret;
    LOG_INF("starting tcp server on port %u", config->port);
    server_fd = setup_server_socket(config);
    if (server_fd < 0) {
        return;
    }

    int client_fd = -1;
    struct sockaddr_in client_addr = {};
    socklen_t client_addrlen = sizeof(client_addr);
    while (true) {
        LOG_INF("waiting for client");
        client_fd = zsock_accept(server_fd, (struct sockaddr *)&client_addr,
                                 &client_addrlen);
        if (client_fd < 0) {
            ret = -*z_errno();
            LOG_WRN("could not accept client: %d", ret);
            break;
        }

        handle_client(config, client_fd, client_addr, client_addrlen);

        (void)zsock_close(client_fd);
        LOG_INF("client closed");
    }

server_close:
    LOG_INF("server closed");
    (void)zsock_close(server_fd);
    return;
}

static int setup_server_socket(struct config *config) {
    int ret;
    struct sockaddr_in addr = {
        .sin_port = htons(config->port),
        .sin_family = AF_INET,
    };
    socklen_t addrlen = sizeof(addr);

    int fd = zsock_socket(addr.sin_family, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        ret = -*z_errno();
        LOG_ERR("could not get socket: %d", ret);
        return ret;
    }

    ret = zsock_bind(fd, (struct sockaddr *)&addr, addrlen);
    if (ret) {
        ret = -*z_errno();
        LOG_ERR("could not bind to socket: %d", ret);
        goto server_close;
    }

    ret = zsock_listen(fd, 5);
    if (ret) {
        ret = -*z_errno();
        LOG_ERR("could not listen to socket: %d", ret);
        goto server_close;
    }

    return fd;

server_close:
    (void)zsock_close(fd);
    return ret;
}

static void handle_client(struct config *config, int fd,
                          const struct sockaddr_in addr,
                          const socklen_t addrlen) {
    int ret;
    memset(rx_buf, 0, sizeof(rx_buf));
    ret = zsock_recv(fd, rx_buf, sizeof(rx_buf), 0);
    if (ret < 0) {
        ret = -*z_errno();
        LOG_ERR("could not receive from client: %d", ret);
        return;
    }
    size_t rx_len = (size_t)ret;

    static char addr_str[128] = {0};
    char *dst = net_addr_ntop(addr.sin_family, &addr.sin_addr, addr_str,
                              sizeof(addr_str));
    if (dst != NULL) {
        LOG_INF("received %dB from %s", ret, addr_str);
    }
    LOG_HEXDUMP_INF(rx_buf, rx_len, "data:");

    tcp_server_response_t response = {};
    config->on_request_cb(rx_buf, rx_len, &response);

    ret = zsock_send(fd, response.data, response.data_len, 0);
    if (response.on_done) {
        response.on_done(ret, response.user_data);
    }
}
