<pre>
                           __       
    ____  _________ ______/ /_  ___ 
   / __ \/ ___/ __ `/ ___/ __ \/ _ \
  / /_/ / /__/ /_/ / /__/ / / /  __/
 / .___/\___/\__,_/\___/_/ /_/\___/ 
/_/                                 
</pre>

Extension for PHP local cache 
-----------------------------

* 1) used slab memory algorithm


Example:
```php
<?php

pcache_set("myname", "liexusong");

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
