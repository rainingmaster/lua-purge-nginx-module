# vim:set ft= ts=4 sw=4 et:

use Test::Nginx::Socket::Lua;
use Cwd qw(cwd);

repeat_each(2);

plan tests => repeat_each() * blocks() * 3;

my $pwd = cwd();

add_block_preprocessor(sub {
    my $block = shift;

    my $http_config = $block->http_config || '';
    $http_config .= qq{
        lua_package_path "$pwd/t/servroot/html/?.lua;$pwd/lib/?.lua;;";
    
        proxy_cache_path  /tmp/ngx_cache_purge_cache keys_zone=test_cache:10m;
        proxy_temp_path   /tmp/ngx_cache_purge_temp 1 2;
    
        upstream hello_world {
            server 127.0.0.1:1990;
        }
    
        server {
            listen 1990;
            location / {
                content_by_lua_block {
                    ngx.say("hello world")
                }
            }
        }
    
        server {
            listen 1991;
            server_name     'localhost';
            location /cache {
                proxy_pass         http://hello_world;
                proxy_cache        test_cache;
                proxy_cache_key    \$uri;
                proxy_cache_valid  3m;
                add_header         X-Cache-Status \$upstream_cache_status;
            }
        }
    };

    my $user_files = <<'_EOC_';
>>> cache.lua
local _M = {}

function _M.run()
    local sock = ngx.socket.tcp()
    local ok, err = sock:connect("127.0.0.1", 1991)
    if not ok then
        ngx.say("failed to connect: ", err)
        return
    end

    local req = [[GET /cache HTTP/1.0
        Host: localhost
        Connection: close

    ]]

    local bytes, err = sock:send(req)
    if not bytes then
        ngx.say("failed to send request: ", err)
        return
    end

    local ret, err = sock:receiveany(10 * 1024)
    if not ret then
        ngx.say("failed to receive a line: ", err)
        return

    else
        local m = ngx.re.match(ret, "hello world")
        if not m then
            ngx.say("received: ", ret)
            return
        end
    end

    sock:close()
end

return _M
_EOC_

    $block->set_value("user_files", $user_files);
    $block->set_value("http_config", eval { $http_config });
});

no_long_string();
run_tests();

__DATA__

=== TEST 1: sanity
--- config
    location = /t {
        content_by_lua_block {
            local cache = require "cache"
            cache.run()

            local resty_purge = require "resty.purge"

            local key = "/cache";
            local cache_zone, err = resty_purge.get_zone("test_cache")
            if not cache_zone then
                ngx.say("get zone failed: ", err)
                return
            end

            ngx.say("get zone success")

            local res, err = resty_purge.purge(cache_zone, key)
            if not res then
                ngx.say("purge failed: ", err)
                return
            end

            ngx.say("purge success")
        }
    }
--- request
GET /t
--- response_body
get zone success
purge success
--- no_error_log
[error]



=== TEST 2: not found
--- config
    location = /t {
        content_by_lua_block {
            local resty_purge = require "resty.purge"

            local key = "/cache";
            local cache_zone, err = resty_purge.get_zone("test_cache")
            if not cache_zone then
                ngx.say("get zone failed: ", err)
                return
            end

            ngx.say("get zone success")

            local res, err = resty_purge.purge(cache_zone, key)
            if not res then
                ngx.say("purge failed: ", err)
                return
            end

            ngx.say("purge success")
        }
    }
--- request
GET /t
--- response_body
get zone success
purge failed: not found
--- no_error_log
[error]



=== TEST 3: run in rewrite_by_lua
--- config
    location = /t {
        rewrite_by_lua_block {
            local cache = require "cache"
            cache.run()

            local resty_purge = require "resty.purge"

            local key = "/cache";
            local cache_zone, err = resty_purge.get_zone("test_cache")
            if not cache_zone then
                ngx.say("get zone failed: ", err)
                return
            end

            ngx.say("get zone success")

            local res, err = resty_purge.purge(cache_zone, key)
            if not res then
                ngx.say("purge failed: ", err)
                return
            end

            ngx.say("purge success")
        }
    }
--- request
GET /t
--- response_body
get zone success
purge success
--- no_error_log
[error]



=== TEST 4: run in access_by_lua
--- config
    location = /t {
        access_by_lua_block {
            local cache = require "cache"
            cache.run()

            local resty_purge = require "resty.purge"

            local key = "/cache";
            local cache_zone, err = resty_purge.get_zone("test_cache")
            if not cache_zone then
                ngx.say("get zone failed: ", err)
                return
            end

            ngx.say("get zone success")

            local res, err = resty_purge.purge(cache_zone, key)
            if not res then
                ngx.say("purge failed: ", err)
                return
            end

            ngx.say("purge success")
        }
    }
--- request
GET /t
--- response_body
get zone success
purge success
--- no_error_log
[error]
