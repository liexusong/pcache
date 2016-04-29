<pre>
                                   __
            ____  _________ ______/ /_  ___
           / __ \/ ___/ __ `/ ___/ __ \/ _ \
          / /_/ / /__/ /_/ / /__/ / / /  __/
         / .___/\___/\__,_/\___/_/ /_/\___/
        /_/
</pre>

Extension for PHP local cache (support PHP version 7)
-----------------------------------------------------
Thanks to @leandre https://github.com/nly

functions list:
---------------
* 1) pcache_set($key, $value, $expire = 0);
* 2) pcache_get($key);
* 3) pcache_del($key);

Example:
```php
<?php

pcache_set("myname", "liexusong", 60); // 1 min expire

var_dump(pcache_get("myname"));

pcache_del("myname");

var_dump(pcache_get("myname"));

?>
```

php.ini
```ini
pcache.buckets_size = 1000       ; set the HashTable buckets size
pcache.cache_size = 1048576      ; set memory caches size
pcache.enable = 1                ; enable pcache extension
```