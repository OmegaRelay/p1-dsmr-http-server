
#include "encoder.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define HTTP_PROTOCOL "HTTP/1.1"
#define HTTP_DELIM "\r\n"

static const char http_protocol[] = HTTP_PROTOCOL;
static const char http_delim[] = HTTP_DELIM;

static int append_delim(http_encoder_ctx_t *ctx) {
    int ret =
        snprintf(&ctx->buf[ctx->offs], ctx->len - ctx->offs, "%s", http_delim);
    if (ret < 0) {
        return -ENOMEM;
    }
    ctx->offs += ret;
    return 0;
}

int http_encoder_init(http_encoder_ctx_t *ctx, char *buf, size_t len,
                      enum http_status status) {
    if (ctx == NULL || buf == NULL || len == 0) {
        return -EINVAL;
    }
    if (len < (sizeof(http_protocol) - 1) + 4 + (sizeof(http_delim) - 1)) {
        return -ENOMEM;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->buf = buf;
    ctx->len = len;
    memset(ctx->buf, 0, len);
    int ret = snprintf(ctx->buf, ctx->len, "%s %d%s", http_protocol, status,
                       http_delim);
    if (ret < 0) {
        return -ENOMEM;
    }
    ctx->offs = ret;
    return 0;
}

int http_encoder_set_body_marker(http_encoder_ctx_t *ctx) {
    return append_delim(ctx);
}

int http_encoder_append(http_encoder_ctx_t *ctx, const char *data, size_t len) {
    if ((ctx->len - ctx->offs) < len) {
        return -ENOMEM;
    }
    memcpy(&ctx->buf[ctx->offs], data, len);
    ctx->offs += len;
    return 0;
}

int http_encoder_append_header(http_encoder_ctx_t *ctx, const char *key,
                                            const char *value) {
    int ret =
        snprintf(&ctx->buf[ctx->offs], ctx->len - ctx->offs, "%s:%s", key, value);
    if (ret < 0) {
        return -ENOMEM;
    }
    ctx->offs += ret;
    return append_delim(ctx);
}

int http_encoder_append_header_content_type(http_encoder_ctx_t *ctx,
                                            const char *content_type) {
    return http_encoder_append_header(ctx, "ContentType", content_type);
}

int http_encoder_append_header_location(http_encoder_ctx_t *ctx,
                                        const char *location) {
    return http_encoder_append_header(ctx, "Location", location);
}
