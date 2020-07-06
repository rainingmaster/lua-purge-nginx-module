NAME
====

lua-purge-nginx-module - NGINX C module that extends `ngx_http_lua_module` for enhanced NGINX cache purge capabilities

Table of Contents
=================

* [NAME](#name)
* [Synopsis](#synopsis)
* [Description](#description)
* [Methods](#methods)
* [Installation](#installation)
* [Author](#author)
* [Copyright and License](#copyright-and-license)

Synopsis
========

```nginx
http {
    lua_package_path "/path/to/lua-purge-nginx-module/lualib/?.lua;;";

    lua_shared_dict my_cache 10m;
    lua_shared_dict locks 1m;

    proxy_cache_path  /tmp/ngx_cache_purge_cache keys_zone=test_cache:10m;
    proxy_temp_path   /tmp/ngx_cache_purge_temp 1 2;
    
    upstream hello_world {
        server 127.0.0.1:1990;
    }
    
    server {
        listen 1989;
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
            more_clear_headers Date;
        }
    }

    server {
        listen 1990;
        server_name "foo.com";

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
    }

    ...
}
```

Description
=======

This module is nginx module which adds ability to purge content from proxy caches. And it is ported from [ngx_cache_purge](https://github.com/FRiCKLE/ngx_cache_purge)

Methods
=======

This section documents the methods for the `resty.purge` Lua module.

get_zone
-------------------
**syntax:** *cache_zone, err = resty_purge.get_zone(name)*

Get the `cache_zone` by `name`.

purge
------------------
**syntax:** *err = resty_purge.purge(cache_zone, key)*

Purge the data by `key` in `cache_zone`.

Installation
============

This module depends on [lua-nginx-module](https://github.com/openresty/lua-nginx-module).

If you are using the official nginx distribution, then build like this:

```bash
./configure --add-module=/path/to/lua-nginx-module \
            --add-module=/path/to/lua-purge-nginx-module
make
sudo make install
```

Otherwise, if you are using the OpenResty distribution, build it as follows:

```bash
./configure --add-module=/path/to/lua-purge-nginx-module
make
sudo make install
```

[Back to TOC](#table-of-contents)

Author
======

* rainingmaster.

[Back to TOC](#table-of-contents)

Copyright and License
=====================

This module is licensed under the BSD license.

Copyright (C) 2020, by rainingmaster.

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

[Back to TOC](#table-of-contents)

