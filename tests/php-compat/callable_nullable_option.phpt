--TEST--
Callable options are nullable
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

$ch = curl_cffi_init();
curl_cffi_setopt_array($ch, [
    CURLOPT_PROGRESSFUNCTION => null,
]);

?>
===DONE===
--EXPECT--
===DONE===
