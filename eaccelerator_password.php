<?php

function main_cli() {
  echo "Changing password for eAccelerator Web Interface (eaccelerator.php)\n\n";
  $stdin = fopen("php://stdin","r");
  while (1) {
    echo "Enter admin name: ";
    $name = fgets($stdin,1024);
    if (feof($stdin)) {echo("\n"); exit;}
    $name = str_replace(array("\n","\r"),"",$name);
    if ($name == "") {
      echo "\nSorry, admin name can't be empty\n";
    } else {
      break;
    }
  }
  while (1) {
    echo "New admin password: ";
    $p1 = fgets($stdin,1024);
    if (feof($stdin)) {echo("\n"); exit;}
    $p1 = str_replace(array("\n","\r"),"",$p1);
    if ($p1 == "") {
      echo "\nSorry, new admin password can't be empty\n";
    } else {
      break;
    }
  }
  echo "Retype new admin password: ";
  $p2 = fgets($stdin,1024);
  if (feof($stdin)) {echo("\n"); exit;}
  $p2 = str_replace(array("\n","\r"),"",$p2);
  if ($p1 != $p2) {
    echo "\nSorry, passwords do not match\n";
    exit;
  }
  $password = crypt($p1);
  echo "\nAdd the following lines into your php.ini and restart HTTPD\n\n".
       "eaccelerator.admin.name=\"$name\"\n".
       "eaccelerator.admin.password=\"$password\"\n";
}

function main_web() {
  $admin_name      = "";
  $admin_password  = "";
  $admin_password2 = "";
  $error = "";
  if (isset($_POST["submit"])) {
    if (isset($_POST["admin_name"])) {
      $admin_name = $_POST["admin_name"];
    }
    if (isset($_POST["admin_password"])) {
      $admin_password = $_POST["admin_password"];
    }
    if (isset($_POST["admin_password2"])) {
      $admin_password2 = $_POST["admin_password2"];
    }
    if (empty($admin_name)) {
      $error = "Sorry, admin name can't be empty";
    } else if (empty($admin_password)) {
      $error = "Sorry, new admin password can't be empty";
    } else if ($admin_password != $admin_password2) {
      $error = "Sorry, passwords do not match";
    } else {
      $password = crypt($admin_password);
      echo "<html><head><title>Changing password for eAccelerator Web Interface (eaccelerator.php)</title></head>".
           "<body><h1 align=\"center\">Changing password for eAccelerator Web Interface (eaccelerator.php)</h1>".
           "Add the following lines into your php.ini and restart HTTPD<br><pre><b>".
           "eaccelerator.admin.name=\"$admin_name\"\n".
           "eaccelerator.admin.password=\"$password\"\n</b></pre></body></html>";
      return;
    }
  }
  echo "<html><head><title>Changing password for eAccelerator Web Interface (eaccelerator.php)</title></head>".
       "<body><h1 align=\"center\">Changing password for eAccelerator Web Interface (eaccelerator.php)</h1>".
       "<h3 align=\"center\"><font color=\"#ff0000\">$error</font></h3>".
       "<form method=\"POST\">".
       "<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"600\" bgcolor=\"#000000\" align=\"center\">".
       "<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td width=\"50%\" bgcolor=\"#ccccff\"><b>Admin name:</b></td><td width=\"50%\"><input type=\"text\" name=\"admin_name\" size=\"32\" value=\"$admin_name\" style=\"width:100%\"></td></tr>".
       "<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td width=\"50%\" bgcolor=\"#ccccff\"><b>New admin password:</b></td><td width=\"50%\"><input type=\"text\" name=\"admin_password\" size=\"32\" value=\"$admin_password\" style=\"width:100%\"></td></tr>".
       "<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td width=\"50%\" bgcolor=\"#ccccff\"><b>Retype new admin password:</b></td><td width=\"50%\"><input type=\"text\" name=\"admin_password2\" size=\"32\" value=\"$admin_password2\" style=\"width:100%\"></td></tr>".
       "<tr><td colspan=\"2\" align=\"center\" bgcolor=\"#cccccc\"><input type=\"submit\" name=\"submit\" value=\"OK\" style=\"width:100px\"></td></tr>".
       "</form></body></html>";
}

function is_cli() {
  if (php_sapi_name() == "cli" || empty($_SERVER['PHP_SELF'])) {
    return 1;
  } else {
    return 0;
  }
}

if (is_cli()) {
  if (function_exists("ob_end_flush")) {
    ob_end_flush();
  }
  if (function_exists("ob_implicit_flush")) {
    ob_implicit_flush(1);
  }
  main_cli();
} else {
  main_web();
}

?>
