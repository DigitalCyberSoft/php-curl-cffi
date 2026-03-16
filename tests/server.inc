<?php declare(strict_types=1);

function curl_cli_server_start() {
    $php_executable = getenv('TEST_PHP_EXECUTABLE') ?: PHP_BINARY;
    $doc_root = __DIR__;
    $router = "responder/get.inc";
    $cmd = [$php_executable, '-t', $doc_root, '-S', 'localhost:0', $router];
    $descriptorspec = array(
        0 => STDIN,
        1 => STDOUT,
        2 => ['pipe', 'w'],
    );
    $handle = proc_open($cmd, $descriptorspec, $pipes, $doc_root, null, array("suppress_errors" => true));

    $bound = null;
    stream_set_blocking($pipes[2], false);
    for ($i = 0; $i < 60; $i++) {
        usleep(50000);
        $status = proc_get_status($handle);
        if (empty($status['running'])) {
            echo "Server is not running\n";
            proc_terminate($handle);
            exit(1);
        }
        while (($line = fgets($pipes[2])) !== false) {
            if (preg_match('@PHP \S* Development Server \(https?://(.*?:\d+)\) started@', $line, $matches)) {
                $bound = $matches[1];
                fclose($pipes[2]);
                break 2;
            }
        }
    }
    if ($bound === null) {
        echo "Server did not output startup message";
        proc_terminate($handle);
        exit(1);
    }

    $error = "Unable to connect to server\n";
    for ($i=0; $i < 60; $i++) {
        usleep(50000);
        $status = proc_get_status($handle);
        $fp = @fsockopen("tcp://$bound");
        if (!($status && $status['running'])) {
            $error = "Server is not running\n";
            break;
        }
        if ($fp) {
            $error = '';
            break;
        }
    }
    if ($fp) {
        fclose($fp);
    }
    if ($error) {
        echo $error;
        proc_terminate($handle);
        exit(1);
    }

    register_shutdown_function(
        function($handle) {
            proc_terminate($handle);
            for ($i = 0; $i < 60; $i++) {
                $status = proc_get_status($handle);
                if (!($status && $status['running'])) {
                    break;
                }
                usleep(50000);
            }
        },
        $handle
    );

    return "http://$bound";
}
