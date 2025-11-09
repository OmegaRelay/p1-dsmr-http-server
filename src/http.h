
#ifndef _SRC_HTTP_H__
#define _SRC_HTTP_H__

#include <zephyr/kernel.h>
#include <zephyr/net/http/status.h>

typedef struct {
    char *buf;
    size_t len;
    size_t offs;
} http_encoder_ctx_t;

int http_encoder_init(http_encoder_ctx_t *ctx, char *buf, size_t len,
                      enum http_status status);
int http_encoder_set_body_marker(http_encoder_ctx_t *ctx);
int http_encoder_append(http_encoder_ctx_t *ctx, const char *data, size_t len);
int http_encoder_append_header(http_encoder_ctx_t *ctx, const char *key,
                               const char *value);
int http_encoder_append_header_content_type(http_encoder_ctx_t *ctx,
                                            const char *content_type);
int http_encoder_append_header_location(http_encoder_ctx_t *ctx,
                                        const char *location);

#endif // _SRC_HTTP_H__
