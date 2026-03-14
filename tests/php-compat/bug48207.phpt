--TEST--
Test curl_cffi_setopt() CURLOPT_FILE readonly file handle
--CREDITS--
Mark van der Velden
#testfest Utrecht 2009
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
/*
 * Description       : Adds a file which stores the received data from curl_cffi_exec();
 * Source code       : ext/curl/multi.c
 * Test documentation: http://wiki.php.net/qa/temp/ext/curl
 */

// Figure out what handler to use
include 'server.inc';
$host = curl_cli_server_start();
if(!empty($host)) {

    // Use the set Environment variable
    $url = "$host/get.inc?test=1";

} else {

    // Create a temporary file for the test
    $tempname = tempnam(sys_get_temp_dir(), 'CURL_HANDLE');
    $url = 'file://'. $tempname;

    // add the test data to the file
    file_put_contents($tempname, "Hello World!\nHello World!");
}


$tempfile	= tempnam(sys_get_temp_dir(), 'CURL_FILE_HANDLE');
$fp = fopen($tempfile, "r"); // Opening 'fubar' with the incorrect readonly flag

$ch = curl_cffi_init($url);
try {
    curl_cffi_setopt($ch, CURLOPT_FILE, $fp);
} catch (ValueError $exception) {
    echo $exception->getMessage() . "\n";
}

curl_cffi_exec($ch);
curl_cffi_close($ch);
is_file($tempfile) and @unlink($tempfile);
isset($tempname) and is_file($tempname) and @unlink($tempname);
?>
--EXPECT--
curl_cffi_setopt(): The provided file handle must be writable
Hello World!
Hello World!
