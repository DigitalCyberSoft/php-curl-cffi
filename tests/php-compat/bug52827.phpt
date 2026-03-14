--TEST--
Bug #52827 (curl_cffi_setopt with CURLOPT_STDERR erroneously increments the resource refcount)
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
$s = fopen('php://temp/maxmemory=1024','wb+');

/* force conversion of inner stream to STDIO.
 * This is not necessary in Windows because the
 * cast to a FILE* handle in curl_cffi_setopt already
 * forces the conversion in that platform. The
 * reason for this conversion is that the memory
 * stream has an ugly but working mechanism to
 * prevent being double freed when it's encapsulated,
 * while STDIO streams don't. */
$i = 0;
while ($i++ < 5000) {
fwrite($s, str_repeat('a',1024));
}
$handle=curl_cffi_init('http://www.example.com');
curl_cffi_setopt($handle, CURLOPT_STDERR, $s);

echo "Done.";
?>
--EXPECT--
Done.
