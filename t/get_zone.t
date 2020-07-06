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
        lua_shared_dict dogs 1m;

        lua_package_path "$pwd/t/servroot/html/?.lua;$pwd/lib/?.lua;;";
    
        proxy_cache_path  /tmp/ngx_cache_purge_cache keys_zone=test_cache:10m;
        proxy_temp_path   /tmp/ngx_cache_purge_temp 1 2;
    };

    $block->set_value("http_config", eval { $http_config });
});

no_long_string();
run_tests();

__DATA__

=== TEST 1: get zone failed
--- config
    location = /t {
        content_by_lua_block {
            local resty_purge = require "resty.purge"

            local cache_zone, err = resty_purge.get_zone("test_cache2")
            if not cache_zone then
                ngx.say("get zone failed: ", err)
                return
            end

            ngx.say("get zone success")
        }
    }
--- request
GET /t
--- response_body
get zone failed: not found
--- no_error_log
[error]



=== TEST 2: wrong zone type
--- config
    location = /t {
        content_by_lua_block {
            local resty_purge = require "resty.purge"

            local cache_zone, err = resty_purge.get_zone("dogs")
            if not cache_zone then
                ngx.say("get zone failed: ", err)
                return
            end

            ngx.say("get zone success")
        }
    }
--- request
GET /t
--- response_body
get zone failed: zone is not for http_proxy
--- no_error_log
[error]



=== TEST 3: get zone in init_by_lua
--- http_config
    init_by_lua_block {
        local resty_purge = require "resty.purge"

        local cache_zone, err = resty_purge.get_zone("test_cache")
        if not cache_zone then
            ngx.log(ngx.ERR, "get zone failed: ", err)
            return
        end
    }
--- config
    location = /t {
        content_by_lua_block {
            ngx.say("ok")
        }
    }
--- request
GET /t
--- response_body
ok
--- no_error_log
[error]
