#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_http_lua_api.h>

#define MAX_ERR_LEN 128


extern ngx_module_t  ngx_http_proxy_module;


typedef struct {
    ngx_http_upstream_conf_t       upstream;
} ngx_http_lua_purge_main_conf_t;


typedef struct {
    ngx_int_t          status;
    u_char             errmsg[MAX_ERR_LEN];
    size_t             errlen;
} ngx_http_lua_purge_ctx_t;


static ngx_int_t ngx_http_lua_purge_file_cache(ngx_http_request_t *r, u_char *err, size_t *errlen);


static ngx_http_module_t ngx_http_lua_purge_module_ctx = {
    NULL,                                    /* preconfiguration */
    NULL,                                    /* postconfiguration */

    NULL,                                    /* create main configuration */
    NULL,                                    /* init main configuration */

    NULL,                                    /* create server configuration */
    NULL,                                    /* merge server configuration */

    NULL,                                    /* create location configuration */
    NULL                                     /* merge location configuration */
};


ngx_module_t ngx_http_lua_purge_module = {
    NGX_MODULE_V1,
    &ngx_http_lua_purge_module_ctx,    /* module context */
    NULL,                              /* module directives */
    NGX_HTTP_MODULE,                   /* module type */
    NULL,                              /* init master */
    NULL,                              /* init module */
    NULL,                              /* init process */
    NULL,                              /* init thread */
    NULL,                              /* exit thread */
    NULL,                              /* exit process */
    NULL,                              /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_http_lua_purge_cache_init(ngx_http_request_t *r, ngx_http_file_cache_t *cache,
    const unsigned char *key_data, int key_len)
{
    ngx_int_t                         rc;
    ngx_http_cache_t                 *c;
    ngx_str_t                        *key;

    c = ngx_pcalloc(r->pool, sizeof(ngx_http_cache_t));
    if (c == NULL) {
        return NGX_ERROR;
    }

    rc = ngx_array_init(&c->keys, r->pool, 1, sizeof(ngx_str_t));
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    key = ngx_array_push(&c->keys);
    if (key == NULL) {
        return NGX_ERROR;
    }

    key->len = key_len;
    key->data = ngx_palloc(r->pool, key_len);
    if (key->data == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(key->data, key_data, key_len);

    r->cache = c;
    c->body_start = ngx_pagesize;
    c->file_cache = cache;
    c->file.log = r->connection->log;

    ngx_http_file_cache_create_key(r);

    return NGX_OK;
}


#if (NGX_HAVE_FILE_AIO)
static int
ngx_http_lua_purge_resume_hook(lua_State *co)
{
    ngx_http_lua_purge_ctx_t        *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_purge_module);

    switch (ctx->status) {
    case NGX_OK:
    case NGX_HTTP_CACHE_STALE:
    case NGX_HTTP_CACHE_UPDATING:
        lua_pushinteger(co, NGX_OK);
        lua_pushnil(co);
        break;
    default:
        lua_pushinteger(co, NGX_ERROR);
        lua_pushlstring(co, ctx->errmsg, ctx->errlen);
    }

    return 2;
}


static ngx_int_t
ngx_http_lua_purge_resume_handler(ngx_http_request_t *r)
{
    ngx_int_t                        rc;
    ngx_http_lua_purge_ctx_t        *ctx;

    if (r->aio) {
        return;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_purge_module);
    ctx->rc = ngx_http_lua_purge_file_cache(r, ctx->errmsg, &ctx->errlen);

    return ngx_http_lua_vm_resume(r, ngx_http_lua_purge_resume_hook);
}
#endif


static ngx_int_t
ngx_http_lua_purge_file_cache(ngx_http_request_t *r, u_char *err, size_t *errlen)
{
    ngx_http_file_cache_t           *cache;
    ngx_http_cache_t                *c;
#if (NGX_HAVE_FILE_AIO)
    ngx_http_lua_purge_ctx_t        *ctx;
#endif

    switch (ngx_http_file_cache_open(r)) {
    case NGX_OK:
    case NGX_HTTP_CACHE_STALE:
    case NGX_HTTP_CACHE_UPDATING:
        break;
#if (NGX_HAVE_FILE_AIO)
    case NGX_AGAIN:
        ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_proxy_ctx_t));
        if (ctx == NULL) {
            *errlen = ngx_snprintf(err, *errlen, "alloc failed") - err;
            return NGX_ERROR;
        }

        ngx_http_set_ctx(r, ctx, ngx_http_lua_purge_module);

        // need to store the lua coroutine?

        r->write_event_handler = ngx_http_lua_purge_resume_handler;
        return NGX_AGAIN;
#endif
    case NGX_DECLINED:
        *errlen = ngx_snprintf(err, *errlen, "not found") - err;
        return NGX_ERROR;
    default:
        if (r->cache->file.fd == NGX_INVALID_FILE) {
            *errlen = ngx_snprintf(err, *errlen, "open/close file failed") - err;
        } else {
            *errlen = ngx_snprintf(err, *errlen, "alloc failed") - err;
        }

        return NGX_ERROR;
    }

    c = r->cache;
    cache = c->file_cache;

    /*
     * delete file from disk but *keep* in-memory node,
     * because other requests might still point to it.
     */

    ngx_shmtx_lock(&cache->shpool->mutex);

    if (!c->node->exists) {
        /* race between concurrent purges, backoff */
        ngx_shmtx_unlock(&cache->shpool->mutex);
        *errlen = ngx_snprintf(err, *errlen, "not found") - err;

        return NGX_ERROR;
    }

    cache->sh->size -= c->node->fs_size;
    c->node->fs_size = 0;

    c->node->exists = 0;
    c->node->updating = 0;

    ngx_shmtx_unlock(&cache->shpool->mutex);

    if (ngx_delete_file(c->file.name.data) == NGX_FILE_ERROR) {
        if (c->file.name.len < 100) {
            *errlen = ngx_snprintf(err, *errlen,
                                   "delete \"%s\" failed", c->file.name.data)
                      - err;

        } else {
            *errlen = ngx_snprintf(err, *errlen, "delete file failed") - err;
        }

        return NGX_ERROR;
    }

    return NGX_OK;
}


int
ngx_http_lua_purge_ffi_purge_data(ngx_http_request_t *r, ngx_shm_zone_t *cache_zone,
    const unsigned char *key_data, int key_len, u_char *err, size_t *errlen)
{
    ngx_int_t   rc;

    rc = ngx_http_lua_purge_cache_init(r, cache_zone->data, key_data, key_len);
    if (rc != NGX_OK) {
        *errlen = ngx_snprintf(err, *errlen, "alloc failed") - err;
        return NGX_ERROR;
    }

#if (NGX_HAVE_FILE_AIO)
    if (r->aio) {
        return NGX_OK;
    }
#endif

    rc = ngx_http_lua_purge_file_cache(r, err, errlen);

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http file cache purge: %i, \"%s\"",
                   rc, r->cache->file.name.data);

    switch (rc) {
    case NGX_OK:
#if (NGX_HAVE_FILE_AIO)
    case NGX_AGAIN:
#endif
        return rc;
    default:
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_shm_zone_t*
ngx_http_lua_purge_ffi_get_cache_zone(const unsigned char *name_data,
    int name_len, char **err)
{
    ngx_list_part_t  *part;
    ngx_shm_zone_t   *shm_zone;
    ngx_uint_t        i;

    part = (ngx_list_part_t *) &ngx_cycle->shared_memory.part;
    shm_zone = part->elts;

    for (i = 0; /* void */ ; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            shm_zone = part->elts;
            i = 0;
        }

        if (name_len != (int) shm_zone[i].shm.name.len) {
            continue;
        }

        if (ngx_strncmp(name_data, shm_zone[i].shm.name.data, name_len)
            != 0)
        {
            continue;
        }

        if ((void*) &ngx_http_proxy_module != shm_zone[i].tag) {
            *err = "zone is not for http_proxy";
            return NULL;
        }

        return &shm_zone[i];
    }

    *err = "not found";
    return NULL;
}
