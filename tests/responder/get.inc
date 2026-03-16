<?php
  $test = isset($_GET['test']) ? $_GET['test'] : null;
  switch($test) {
    case 'post':
      var_dump($_POST);
      break;
    case 'input':
      var_dump(file_get_contents('php://input'));
      break;
    case 'getpost':
      var_dump($_GET);
      var_dump($_POST);
      break;
    case 'get':
      echo "Hello World!\n";
      echo "Hello World!";
      break;
    case 'referer':
      echo $_SERVER['HTTP_REFERER'];
      break;
    case 'useragent':
      echo $_SERVER['HTTP_USER_AGENT'];
      break;
    case 'httpversion':
      echo $_SERVER['SERVER_PROTOCOL'];
      break;
    case 'cookie':
      echo $_COOKIE['foo'];
      break;
    case 'encoding':
      echo $_SERVER['HTTP_ACCEPT_ENCODING'] ?? '';
      break;
    case 'contenttype':
      header('Content-Type: text/plain;charset=utf-8');
      break;
    case 'file':
      if (isset($_FILES['file'])) {
          echo $_FILES['file']['name'] . '|' . $_FILES['file']['type'] . '|' . $_FILES['file']['size'];
      }
      break;
    case 'string_file':
      if (isset($_FILES['file'])) {
          if ($_FILES['file']['error'] === UPLOAD_ERR_OK) {
              echo $_FILES['file']['name'] . '|' . $_FILES['file']['type'] . '|' . md5_file($_FILES['file']['tmp_name']);
          } else {
              echo 'error:' . $_FILES['file']['error'];
          }
      }
      break;
    case 'method':
      echo $_SERVER['REQUEST_METHOD'];
      break;
    case 'auth':
      if (isset($_SERVER['PHP_AUTH_USER'])) {
          echo 'user=' . $_SERVER['PHP_AUTH_USER'] . '&pass=' . $_SERVER['PHP_AUTH_PW'];
      } else {
          header('HTTP/1.1 401 Unauthorized');
          header('WWW-Authenticate: Basic realm="Test"');
          echo '401 Unauthorized';
      }
      break;
    case 'headers_json':
      $headers = [];
      foreach ($_SERVER as $k => $v) {
          if (strpos($k, 'HTTP_') === 0) {
              $name = str_replace('_', '-', strtolower(substr($k, 5)));
              $headers[$name] = $v;
          }
      }
      header('Content-Type: application/json');
      echo json_encode($headers);
      break;
    case 'json_echo':
      header('Content-Type: application/json');
      echo json_encode([
          'method' => $_SERVER['REQUEST_METHOD'],
          'content_type' => $_SERVER['CONTENT_TYPE'] ?? '',
          'body' => file_get_contents('php://input'),
      ]);
      break;
    case 'params_json':
      header('Content-Type: application/json');
      echo json_encode($_GET);
      break;
    case 'multi_file':
      $result = [];
      foreach ($_FILES as $name => $file) {
          if (is_array($file['name'])) {
              for ($i = 0; $i < count($file['name']); $i++) {
                  $result[] = ['name' => $file['name'][$i], 'type' => $file['type'][$i], 'size' => $file['size'][$i]];
              }
          } else {
              $result[] = ['name' => $file['name'], 'type' => $file['type'], 'size' => $file['size']];
          }
      }
      header('Content-Type: application/json');
      echo json_encode($result);
      break;
    case 'delay':
      $ms = isset($_GET['ms']) ? (int)$_GET['ms'] : 2000;
      usleep($ms * 1000);
      echo 'delayed';
      break;
    case 'echo_body':
      echo file_get_contents('php://input');
      break;
    case 'echo_cookies':
      header('Content-Type: application/json');
      echo json_encode($_COOKIE);
      break;
    case 'redirect_301':
      $dest = isset($_GET['to']) ? $_GET['to'] : '/?test=get';
      header('HTTP/1.1 301 Moved Permanently');
      header('Location: ' . $dest);
      echo 'Redirecting...';
      break;
    case 'redirect_loop':
      header('HTTP/1.1 301 Moved Permanently');
      header('Location: /?test=redirect_loop');
      echo 'Redirecting...';
      break;
    case 'set_cookies':
      header('Set-Cookie: foo=bar; Path=/');
      echo 'cookies set';
      break;
    case 'set_headers':
      header('X-Test: test');
      header('X-Test: test2', false);
      echo 'headers set';
      break;
    case 'gzip':
      $data = 'This is gzip compressed content for testing';
      $compressed = gzencode($data);
      header('Content-Encoding: gzip');
      header('Content-Type: text/plain');
      echo $compressed;
      break;
    case 'brotli':
      if (function_exists('brotli_compress')) {
          $data = 'This is brotli compressed content for testing';
          $compressed = brotli_compress($data);
          header('Content-Encoding: br');
          header('Content-Type: text/plain');
          echo $compressed;
      } else {
          echo 'brotli not available';
      }
      break;
    case 'echo_all':
      header('Content-Type: application/json');
      $headers = [];
      foreach ($_SERVER as $k => $v) {
          if (strpos($k, 'HTTP_') === 0) {
              $name = str_replace('_', '-', strtolower(substr($k, 5)));
              $headers[$name] = $v;
          }
      }
      echo json_encode([
          'method' => $_SERVER['REQUEST_METHOD'],
          'uri' => $_SERVER['REQUEST_URI'],
          'query' => $_GET,
          'headers' => $headers,
          'cookies' => $_COOKIE,
          'body' => file_get_contents('php://input'),
          'files' => $_FILES,
          'content_type' => $_SERVER['CONTENT_TYPE'] ?? '',
      ]);
      break;
    case 'set_domain_cookies':
      header('Set-Cookie: shared=value1; Path=/');
      header('Set-Cookie: session=abc123; Path=/; HttpOnly', false);
      header('Set-Cookie: pref=dark; Path=/; Max-Age=3600', false);
      echo 'domain cookies set';
      break;
    case 'redirect_chain':
      $step = isset($_GET['step']) ? (int)$_GET['step'] : 0;
      if ($step < 3) {
          $next = $step + 1;
          header("HTTP/1.1 302 Found");
          header("Location: /?test=redirect_chain&step=$next");
          echo "Redirecting to step $next";
      } else {
          echo 'final destination';
      }
      break;
    case 'status':
      $code = isset($_GET['code']) ? (int)$_GET['code'] : 200;
      http_response_code($code);
      header('Content-Type: application/json');
      echo json_encode(['status' => $code, 'reason' => 'Custom']);
      break;
    case 'multi_upload':
      $result = [];
      foreach ($_FILES as $field => $file) {
          if (is_array($file['name'])) {
              for ($i = 0; $i < count($file['name']); $i++) {
                  $result[] = [
                      'field' => $field,
                      'name' => $file['name'][$i],
                      'type' => $file['type'][$i],
                      'size' => $file['size'][$i],
                      'content' => file_get_contents($file['tmp_name'][$i]),
                  ];
              }
          } else {
              $result[] = [
                  'field' => $field,
                  'name' => $file['name'],
                  'type' => $file['type'],
                  'size' => $file['size'],
                  'content' => file_get_contents($file['tmp_name']),
              ];
          }
      }
      // Also include POST fields
      $post_fields = [];
      foreach ($_POST as $k => $v) {
          $post_fields[$k] = $v;
      }
      header('Content-Type: application/json');
      echo json_encode(['files' => $result, 'fields' => $post_fields]);
      break;
    default:
      echo "Hello World!\n";
      echo "Hello World!";
      break;
  }
?>
