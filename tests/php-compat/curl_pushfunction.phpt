--TEST--
Test CURLMOPT_PUSHFUNCTION
--CREDITS--
Davey Shafik
Kévin Dunglas
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
$callback = function($parent_ch, $pushed_ch, array $headers) {
	return CURL_PUSH_OK;
};

$mh = curl_cffi_multi_init();

curl_cffi_multi_setopt($mh, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
curl_cffi_multi_setopt($mh, CURLMOPT_PUSHFUNCTION, $callback);

$ch = curl_cffi_init();
curl_cffi_setopt($ch, CURLOPT_URL, "https://localhost/serverpush");
curl_cffi_setopt($ch, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1);

curl_cffi_multi_add_handle($mh, $ch);

$responses = [];
$active = null;
do {
    $status = curl_cffi_multi_exec($mh, $active);

    do {
        $info = curl_cffi_multi_info_read($mh);
        if (false !== $info && $info['msg'] == CURLMSG_DONE) {
            $handle = $info['handle'];
            if ($handle !== null) {
		        $responses[] = curl_cffi_multi_getcontent($info['handle']);
                curl_cffi_multi_remove_handle($mh, $handle);
                curl_cffi_close($handle);
            }
        }
    } while ($info);
} while (count($responses) !== 2);

curl_cffi_multi_close($mh);

sort($responses);
print_r($responses);
?>
--EXPECT--
Array
(
    [0] => main response
    [1] => pushed response
)
