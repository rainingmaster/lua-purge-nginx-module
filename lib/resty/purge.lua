local _M = {}


local ffi = require "ffi"
local C = ffi.C
local ffi_str = ffi.string
local base = require "resty.core.base"
local FFI_OK = base.FFI_OK
local FFI_ERROR = base.FFI_ERROR
local FFI_AGAIN = base.FFI_AGAIN
local get_request = base.get_request
local co_yield = coroutine._yield

local ERR_BUF_SIZE = 128
local errmsg = base.get_errmsg_ptr()
local errbuf = base.get_string_buf(ERR_BUF_SIZE)
local errlen = base.get_size_ptr()


ffi.cdef[[
void *ngx_http_lua_purge_ffi_get_cache_zone(const unsigned char *name_data,
    int name_len, char **err);

int ngx_http_lua_purge_ffi_purge_data(ngx_http_request_t *r, void *cache_zone,
    const unsigned char *key_data, int key_len, unsigned char *err, size_t *errlen);
]]


function _M.get_zone(name)
    if name == nil then
        return nil, "nil name"
    end

    if type(name) ~= "string" then
        name = tostring(name)
    end

    local cache_zone = C.ngx_http_lua_purge_ffi_get_cache_zone(name, #name, errmsg)
    if cache_zone == nil then
        return nil, ffi_str(errmsg[0])
    end

    return cache_zone
end


function _M.purge(cache_zone, key)
    local r = get_request()
    if not r then
        error("no request found")
    end

    if type(cache_zone) ~= "cdata" then
        error("bad \"cache_zone\" argument")
    end

    if key == nil then
        return nil, "nil key"
    end

    if type(key) ~= "string" then
        key = tostring(key)
    end

    errlen[0] = ERR_BUF_SIZE

    local rc = C.ngx_http_lua_purge_ffi_purge_data(r, cache_zone, key, #key, errbuf, errlen)
    if rc == FFI_ERROR then
        return nil, ffi_str(errbuf, errlen[0])
    end

    if rc == FFI_AGAIN then
        return co_yield()
    end

    return true -- FFI_OK
end


return _M
