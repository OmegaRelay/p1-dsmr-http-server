/**
 * @file server.c
 * @author Theis <theismejnertsen@gmail.com>
 * @date 2025-11-02
 */

/******************************************************************************
 * Includes
 *****************************************************************************/

#include "server.h"
#include "zephyr/net/http/status.h"
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/http/method.h>
#include <zephyr/net/http/parser.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/wifi_credentials.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/errno_private.h>
#include <zephyr/sys/hash_map.h>
#include <zephyr/toolchain.h>

#include <zephyr/logging/log.h>

/******************************************************************************
 * Constants
 *****************************************************************************/

#define STATUS_STR_LEN 3
#define HTTP_DELIM "\r\n";

static const char http_protocol[] = "HTTP/1.1";
static const char http_delim[] = HTTP_DELIM;
// Just incase response cannot be serialized, use this string
static const char http_insufficient_storage[] =
    "HTTP/1.1 507 Insufficient Storage\r\n\r\n";

/******************************************************************************
 * Local Function Prototypes
 *****************************************************************************/

static int setup_server_socket(void);
static void server_thread(void);
static void handle_client(int fd, const struct sockaddr_in addr,
                          const socklen_t addrlen);

static int handle_url_cb(struct http_parser *, const char *at, size_t length);
static int handle_body_cb(struct http_parser *, const char *at, size_t length);
static void route_request(const struct server_request *req,
                          struct server_response *res);
static int serialize_response(const struct server_response *res, uint8_t *buf,
                              size_t len);
static enum http_status errno_to_http_status(int err);

/******************************************************************************
 * Private Variables
 *****************************************************************************/

LOG_MODULE_REGISTER(server, CONFIG_LOG_MAX_LEVEL);

static int server_fd = -1;
static uint8_t rx_buf[1024] = {0};
static uint8_t tx_buf[1024] = {0};

BUILD_ASSERT(
    sizeof(tx_buf) > sizeof(http_insufficient_storage),
    "tx_buf must be larger than the insufficient storage backup message");

K_SEM_DEFINE(server_run_sem, 0, 1);
K_THREAD_DEFINE(http_server, 8192, server_thread, NULL, NULL, NULL, 2, 0, 0);
struct server_request request;

SYS_HASHMAP_DEFINE_STATIC(resource_map);

/******************************************************************************
 * Public Functions
 *****************************************************************************/

void server_start(void) { k_sem_give(&server_run_sem); }

void server_stop(void) {
    k_sem_reset(&server_run_sem);
    if (server_fd >= 0) {
        (void)zsock_close(server_fd);
    }
}

int server_add_resource(char *uri, server_resource_cb_t cb) {
    int ret;
    uint64_t key = (uint64_t)sys_hash32(uri, strnlen(uri, 128));
    LOG_DBG("resource: uri: %s, key: %llu", uri, key);
    if (sys_hashmap_contains_key(&resource_map, key)) {
        return -EALREADY;
    }

    ret = sys_hashmap_insert(&resource_map, key, (uint64_t)cb, NULL);
    if (ret < 0) {
        return ret;
    }
    return 0;
}

int server_remove_resource(char *uri) {
    uint64_t key = (uint64_t)sys_hash32(uri, 128);

    // For idempotency, dont care if map was deleted or not found
    (void)sys_hashmap_remove(&resource_map, key, NULL);
    return 0;
}

/******************************************************************************
 * Local Function Implementation
 *****************************************************************************/

static int setup_server_socket(void) {
    int ret;
    struct sockaddr_in addr = {
        .sin_port = htons(80),
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

static void server_thread(void) {
    int ret;
    server_fd = setup_server_socket();
    if (server_fd < 0) {
        return;
    }

    int client_fd = -1;
    struct sockaddr_in client_addr = {};
    socklen_t client_addrlen = sizeof(client_addr);
    while (true) {
        ret = k_sem_take(&server_run_sem, K_FOREVER);
        if (ret < 0) {
            LOG_ERR("could not take server run sem: %d", ret);
            goto server_close;
        }

        while (true) {
            LOG_INF("waiting for client");
            client_fd = zsock_accept(server_fd, (struct sockaddr *)&client_addr,
                                     &client_addrlen);
            if (client_fd < 0) {
                ret = -*z_errno();
                LOG_WRN("could not accept client: %d", ret);
                break;
            }

            handle_client(client_fd, client_addr, client_addrlen);

            (void)zsock_close(client_fd);
            LOG_INF("client closed");
        }
    }

server_close:
    LOG_INF("server closed");
    (void)zsock_close(server_fd);
    return;
}

static void handle_client(int fd, const struct sockaddr_in addr,
                          const socklen_t addrlen) {
    int ret;
    ret = zsock_recv(fd, rx_buf, sizeof(rx_buf), 0);
    if (ret < 0) {
        ret = -*z_errno();
        LOG_ERR("could not receive from client: %d", ret);
        return;
    }
    size_t rx_len = (size_t)ret;

    static char addr_str[128] = "";
    char *dst = net_addr_ntop(addr.sin_family, &addr.sin_addr, addr_str,
                              sizeof(addr_str));
    if (dst != NULL) {
        LOG_INF("received from %s", addr_str);
    }
    LOG_HEXDUMP_INF(rx_buf, rx_len, "data:");

    memset(&request, 0, sizeof(request));
    struct http_parser parser = {};
    struct http_parser_settings settings = {
        .on_url = handle_url_cb,
        .on_body = handle_body_cb,
    };
    http_parser_init(&parser, HTTP_REQUEST);
    http_parser_execute(&parser, &settings, rx_buf, rx_len);
    request.method = parser.method;

    LOG_INF("%d http request on %s", parser.method, request.url);
    LOG_HEXDUMP_INF(request.body, request.body_len, "request body");

    struct server_response response = {};
    route_request(&request, &response);
    size_t tx_len = 0;
    ret = serialize_response(&response, tx_buf, sizeof(tx_buf));
    if (ret <= 0) {
        LOG_ERR("could not serialize response: %d", ret);
        memset(tx_buf, 0, sizeof(tx_buf));
        memcpy(tx_buf, http_insufficient_storage,
               sizeof(http_insufficient_storage));
        tx_len = sizeof(http_insufficient_storage) - 1;
    } else {
        tx_len = ret;
    }

    LOG_HEXDUMP_INF(tx_buf, tx_len, "response:");
    ret = zsock_send(fd, tx_buf, tx_len, 0);
    if (response.on_done) {
        response.on_done(ret);
    }
}

static int handle_url_cb(struct http_parser *parser, const char *at,
                         size_t length) {
    ARG_UNUSED(parser);
    if (length >= sizeof(request.url)) {
        return -E2BIG;
    }
    memcpy(request.url, at, length);
    return 0;
}

static int handle_body_cb(struct http_parser *parser, const char *at,
                          size_t length) {
    ARG_UNUSED(parser);
    request.body = (const uint8_t *)at;
    request.body_len = length;
    return 0;
}

static void route_request(const struct server_request *req,
                          struct server_response *res) {
    uint64_t key = (uint64_t)sys_hash32(req->url, strnlen(req->url, 128));

    LOG_INF("uri: %s, key: %llu", req->url, key);
    server_resource_cb_t resource_cb;
    if (!sys_hashmap_get(&resource_map, key, (uint64_t *)&resource_cb)) {
        res->status = HTTP_404_NOT_FOUND;
        return;
    }

    int ret = resource_cb(req, res);
    if (ret < 0) {
        memset(res, 0, sizeof(*res));
        res->status = errno_to_http_status(ret);
    }
}

static int serialize_response(const struct server_response *res, uint8_t *buf,
                              size_t len) {
    size_t offset = 1; // NULL terminator
    memset(buf, 0, len);

    offset += (sizeof(http_protocol) - 1);
    if (len < offset) {
        return -ENOMEM;
    }
    strncat(buf, http_protocol, sizeof(http_protocol));

    char status_buf[1 + STATUS_STR_LEN + sizeof(http_delim)] = "";
    offset += (sizeof(status_buf) - 1);
    if (len < offset) {
        return -ENOMEM;
    }
    (void)snprintf(status_buf, sizeof(status_buf), " %d%s", res->status,
                   http_delim);
    strncat(buf, status_buf, sizeof(status_buf));

    // TODO: implement header mechanism

    offset += (sizeof(http_delim) - 1);
    if (len < offset) {
        return -ENOMEM;
    }
    strncat(buf, http_delim, sizeof(http_delim));

    if (res->body == NULL || res->body_len == 0) {
        goto exit;
    }

    if (len < offset + res->body_len) {
        return -ENOMEM;
    }
    // Overwrite NULL terminator
    memcpy(&buf[offset - 1], res->body, res->body_len);
    offset += res->body_len;

exit:
    // Leave out NULL terminator
    return offset - 1;
}

static enum http_status errno_to_http_status(int err) {
    err = abs(err);

    switch (err) {
    case -EINVAL:
        return HTTP_400_BAD_REQUEST;
    case -ENOSYS:
        return HTTP_501_NOT_IMPLEMENTED;
    default:
        return HTTP_500_INTERNAL_SERVER_ERROR;
    }
}
