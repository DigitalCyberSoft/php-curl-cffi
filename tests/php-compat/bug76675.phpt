--TEST--
Bug #76675 (Segfault with H2 server push write/writeheader handlers)
--EXTENSIONS--
curl_impersonate
--SKIPIF--
<?php
include 'skipif-nocaddy.inc';

$curl_cffi_version = curl_cffi_version();
if ($curl_cffi_version['version_number'] < 0x080100) {
    exit("skip: test may crash with curl < 8.1.0");
}
?>
--FILE--
<?php
$transfers = 1;
$callback = function($parent, $passed) use (&$transfers) {
    curl_cffi_setopt($passed, CURLOPT_WRITEFUNCTION, function ($ch, $data) {
        echo "Received ".strlen($data);
        return strlen($data);
    });
    $transfers++;
    return CURL_PUSH_OK;
};
$mh = curl_cffi_multi_init();
curl_cffi_multi_setopt($mh, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
curl_cffi_multi_setopt($mh, CURLMOPT_PUSHFUNCTION, $callback);
$ch = curl_cffi_init();
curl_cffi_setopt($ch, CURLOPT_URL, 'https://localhost/serverpush');
curl_cffi_setopt($ch, CURLOPT_HTTP_VERSION, 3);
curl_cffi_setopt($ch, CURLOPT_SSL_VERIFYHOST, 0);
curl_cffi_setopt($ch, CURLOPT_SSL_VERIFYPEER, 0);
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
curl_cffi_multi_add_handle($mh, $ch);
$active = null;
do {
    $status = curl_cffi_multi_exec($mh, $active);
    do {
        $info = curl_cffi_multi_info_read($mh);
        if (false !== $info && $info['msg'] == CURLMSG_DONE) {
            $handle = $info['handle'];
            if ($handle !== null) {
                $transfers--;
                curl_cffi_multi_remove_handle($mh, $handle);
                curl_cffi_close($handle);
            }
        }
    } while ($info);
} while ($transfers);
curl_cffi_multi_close($mh);
?>
--EXPECTREGEX--
(Received \d+)+
