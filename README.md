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




