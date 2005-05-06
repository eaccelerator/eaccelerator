<?php
$web_error = "";

function eaccelerator_encoder_usage() {
  echo "Usage:\tphp -q encoder.php [options] source_file_name\n";
  echo       "\tphp -q encoder.php [options] source_file_name...\n";
  echo       "\tphp -q encoder.php [options] source_directory_name...\n\n";
  echo "Options:\n";
  echo "\t-s suffix\n\t\tencode files only with following suffix (default is \"php\")\n";
  echo "\t-a\n\t\tencode all files (no by default)\n";
  echo "\t-l\n\t\tfollow symbolic links (no by default)\n";
  echo "\t-r\n\t\tencode directories recursively (no by default)\n";
  echo "\t-c\n\t\tcopy files those shouldn't be encoded (no by default)\n";
  echo "\t-f\n\t\toverwrite existing files (no by default)\n";
  echo "\t-w\n\t\texclude check for eaccelerator_load() and subsequent warning\n";
  echo "\t-o target\n\t\tIf you encode only one script then 'target' specifyes an output\n";
  echo               "\t\tfile name. If you encode directory or several files at once\n";
  echo               "\t\tthen 'target' specifyes an output directory name.\n";
  echo "\nExamples:\n";
  echo "\tphp -q encoder.php some_file.php\n";
  echo "\tphp -q encoder.php some_file.php -o some_encoded_file.php\n";
  echo "\tphp -q encoder.php *.php -o some_dir\n";
  echo "\tphp -q encoder.php ~/public_html/x -rcf -sphp -sinc -o ~/public_html/y\n";
  echo "\n";
  exit();
}

function eaccelerator_error($str, $web) {
  if ($web) {
    global $web_error;
    $web_error = "ERROR: $str";
  } else {
    echo "eAccelerator Encoder ERROR: $str\n";
  }
}

function eaccelerator_encode_file($src, $out, $f, $c, $w, $web) {
  if (empty($out)) {
    echo "\n// $src\n";
  }
  $prefix = "";
  $cmp = eaccelerator_encode($src, $prefix);
  if (empty($cmp)) {
    eaccelerator_error("Can't compile file \"$src\"",$web);
    if ($f) {
      if ($c && !empty($out)) {
        if ($web) {
          global $web_error;
          if (!empty($web_error)) {
            echo "<font color=\"#ff0000\">$web_error</font><br>\n"; flush();
            $web_error = "";
          }
        }
        eaccelerator_copy_file($src, $out, $f, $web);
      }
    } else {
      if (!$web) {
        exit();
      }
    }
  } else {
    if (!$w) {
        $cmp = $prefix.'<?php if (!is_callable("eaccelerator_load") && !@dl((PHP_OS=="WINNT"||PHP_OS=="WIN32")?"eloader.dll":"eloader.so")) { die("This PHP script has been encoded with eAccelerator, to run it you must install <a href=\"http://eaccelerator.sourceforge.net/\">eAccelerator or eLoader</a>");} return eaccelerator_load(\''.$cmp."');?>\n";
    }
    else
    {
    	$cmp = $prefix.'<?php return eaccelerator_load(\''.$cmp."');?>\n";
    }
    if (!empty($out)) {
      if (!$f && file_exists($out)) {
        eaccelerator_error("Can't create output file \"$out\" (already exists)",$web);
      } else {
        $file = @fopen($out,"wb");
        if (!$file) {
          eaccelerator_error("Can't open output file \"$out\"",$web);
        } else {
          fwrite($file,$cmp);
          unset($cmp);
          fclose($file);
          $stat = stat($src);
          chmod($out, $stat['mode']);
          if ($web) {
            echo "<font color=\"#00aa00\">Encoding: \"$src\" -> \"$out\"</font><br>\n";
          }
        }
      }
    } else {
      if ($web) {
        echo "<pre>".htmlspecialchars($cmp)."</pre>\n";
      } else {
        echo $cmp;
      }
      unset($cmp);
    }
  }
}

function eaccelerator_mkdir($dir, $f, $web) {
  if (!empty($dir)) {
    if (!@mkdir($dir,0777)) {
      if (!$f) {
        $error = "Can't create destination directory \"$dir\"";
        if (file_exists($dir)) {
          $error .= " (already exists)";
        }
        eaccelerator_error($error, $web);
        return 0;
      }
    }
  }
  return 1;
}

function eaccelerator_copy_dir($src, $dir, $f, $web) {
  $stat = stat($src);
  $old = umask(0);
  $ret = eaccelerator_mkdir($dir, $f, $web);
  umask($old);
  return $ret;
}

function eaccelerator_copy_file($src, $out, $f, $web) {
  $i = @fopen($src, "rb");
  if (!$i) {
    eaccelerator_error("Can't open file \"$src\" for copying",$web);
    return;
  }
  if (!$f && file_exists($out)) {
    eaccelerator_error("Can't create output file \"$out\" (already exists)");
  } else {
    $o = @fopen($out, "wb");
    if (!$o) {
      eaccelerator_error("Can't copy file into \"$out\"");
      return;
    }
    while ($tmp = fread($i, 1024*32)) {
      fwrite($o, $tmp);
    }
    fclose($i);
    fclose($o);
    $stat = stat($src);
    chmod($out, $stat['mode']);
    if ($web) {
      echo "<font color=\"#00aa00\">Copying: \"$src\" -> \"$out\"</font><br>\n";
    }
  }
}

function eaccelerator_copy_link($src, $out, $f, $web) {
  $link = readlink($src);
  if (!@symlink($link,$out)) {
    if ($f && file_exists($out)) {
      unlink($out);
      eaccelerator_copy_link($src, $out, false, $web);
    } else if ($f && is_array(lstat($out))) {
      unlink($out);
      eaccelerator_copy_link($src, $out, false, $web);
    } else {
      eaccelerator_error("Can't create symlink \"$out\" -> \"$link\"");
      return;
    }
  }
}

function eaccelerator_encode_dir($src, $out, $s, $r, $l, $c, $f, $w, $web) {
  if ($dir = @opendir($src)) {
    while (($file = readdir($dir)) !== false) {
      if ($file == "." || $file == "..") continue;
      $i = "$src/$file";
      $o = empty($out)?$out:"$out/$file";
      if (is_link($i)) {
        if ($c && !empty($o)) {
          eaccelerator_copy_link($i, $o, $f, $web);
          if ($web) {
            global $web_error;
            if (!empty($web_error)) {
              echo "<font color=\"#ff0000\">$web_error</font><br>\n"; flush();
              $web_error = "";
            }
          }
          continue;
        } else if (!$l) {
          continue;
        }
      }
      if (is_dir($i)) {
        if ($r) {
          if (eaccelerator_copy_dir($i, $o, $f, $web)) {
            eaccelerator_encode_dir($i, $o, $s, $r, $l, $c, $f, $w, $web);
          }
        }
      } else if (is_file($i)) {
        if (empty($s)) {
          eaccelerator_encode_file($i, $o, $f, $c, $w, $web);
        } else if (is_string($s)) {
          if (preg_match("/".preg_quote(".$s")."\$/i", $file)) {
            eaccelerator_encode_file($i, $o, $f, $c, $w, $web);
          } else if (!empty($o) && $c) {
            eaccelerator_copy_file($i, $o, $f, $web);
          }
        } else if (is_array($s)) {
          $encoded = false;
          foreach($s as $z) {
            if (preg_match("/".preg_quote(".$z")."\$/i", $file)) {
              eaccelerator_encode_file($i, $o, $f, $c, $w, $web);
              $encoded = true;
              break;
            }
          }
          if (!$encoded && !empty($o) && $c) {
            eaccelerator_copy_file($i, $o, $f, $web);
          }
        }
      }
      if ($web) {
        global $web_error;
        if (!empty($web_error)) {
          echo "<font color=\"#ff0000\">$web_error</font><br>\n"; flush();
          $web_error = "";
        }
      }
    }
    closedir($dir);
  } else {
    eaccelerator_error("Can't open source directory \"$src\"", $web);
  }
}

function eaccelerator_encoder_main() {
  $argc = $_SERVER['argc'];
  $argv = $_SERVER['argv'];

  $src = array();
  $out = null;
  unset($s);
  $r = false;
  $l = false;
  $a = false;
  $c = false;
  $f = false;
  $w = false;

  for ($i = 1; $i < $argc; $i++) {
    $arg = $argv[$i];
    if (!empty($arg)) {
      if ($arg[0] == '-') {
        if ($arg[1] == "o") {
          if (!empty($out)) {
            eaccelerator_encoder_usage();
          }
          if (strlen($arg) == 2) {
            if ($argc <= $i || empty($argv[$i+1]) || $argv[$i+1][0] == "-") {
              eaccelerator_encoder_usage();
            }
            $out = $argv[++$i];
          } else {
            $out = substr($arg,2);
          }

        } else if ($arg[1] == "s") {
          if (strlen($arg) == 2) {
            $s[] = $argv[++$i];
          } else {
            $s[] = substr($arg,2);
          }
        } else if (strlen($arg) == 2 && $arg[1] == "r") {
          $r = true;
        } else if (strlen($arg) == 2 && $arg[1] == "l") {
          $l = true;
        } else if (strlen($arg) == 2 && $arg[1] == "a") {
          $a = true;
        } else if (strlen($arg) == 2 && $arg[1] == "c") {
          $c = true;
        } else if (strlen($arg) == 2 && $arg[1] == "f") {
          $f = true;
        } else if (strlen($arg) == 2 && $arg[1] == "w") {
          $w = true;
        } else {
          $len = strlen($arg);
          if ($len > 1) {
            $n   = 1;
            while ($n < $len) {
              if ($arg[$n] == "r") {
                $r = true;
              } else if ($arg[$n] == "l") {
                $l = true;
              } else if ($arg[$n] == "a") {
                $a = true;
              } else if ($arg[$n] == "c") {
                $c = true;
              } else if ($arg[$n] == "f") {
                $f = true;
              } else if ($arg[$n] == "w") {
                $w = true;
              } else {
                if ($arg[$n] != "o" && $arg[$n] != "s") {
                  echo("eAccelerator Encoder ERROR: Unknown option \"-".$arg[$n]."\"\n\n");
                }
                eaccelerator_encoder_usage();
              }
              ++$n;
            }
          } else {
            echo("eAccelerator Encoder ERROR: Unknown option \"$arg\"\n\n");
            eaccelerator_encoder_usage();
          }
        }
      } else {
        $src[] = $arg;
      }
    }
  }
  if (isset($src) && is_array($src) && count($src) > 0) {
    $cnt = count($src);
    if ($a) {
      $s = "";
    } else if (!isset($s)) {
      $s = "php";
    }
    if ($cnt > 1) {
      if (!eaccelerator_mkdir($out, $f, 0)) {
        return;
      }
    }
    foreach($src as $file) {
      if (!file_exists($file)) {
        echo("eAccelerator Encoder ERROR: Source file \"$file\" doesn't exist.\n");
      } else {
        if (is_dir($file)) {
          if ($cnt == 1) {
            if (eaccelerator_mkdir($out, $f, 0)) {
              eaccelerator_encode_dir($file, $out, $s, $r, $l, $c, $f, $w, 0);
            }
          } else {
            if (eaccelerator_copy_dir($file, $out."/".basename($file), $f, 0)) {
              eaccelerator_encode_dir($file, $out."/".basename($file), $s, $r, $l, $c, $f, $w, 0);
            }
          }
        } else {
          if ($cnt == 1) {
            eaccelerator_encode_file($file, $out, $f, $c, $w, 0);
          } else {
            if (empty($out)) {
              eaccelerator_encode_file($file, $out, $f, $c, $w, 0);
            } else {
              eaccelerator_encode_file($file, $out."/".basename($file), $f, $c, $w, 0);
            }
          }
        }
      }
    }
  } else {
    eaccelerator_encoder_usage();
  }
}

function eaccelerator_encoder_web() {
  echo "<html><head><title>eAccelerator Encoder</title></head>".
       "<body><h1 align=\"center\">eAccelerator Encoder ".EACCELERATOR_VERSION."</h1>";
  $error = "";
  $source = "";
  $target = "";
  $s = "php";
  $suffixies = "php";
  if (isset($_POST["submit"])) {
    if (isset($_POST["source"])) {
      $source = $_POST["source"];
    }
    if (isset($_POST["target"])) {
      $target = $_POST["target"];
    }
    if (isset($_POST["suffixies"])) {
      $suffixies = $_POST["suffixies"];
      if (strpos($suffixies,",") !== false) {
        $s = explode(",",$suffixies);
      } else {
        $s = $suffixies;
      }
    }
    $all = isset($_POST["all"])?$_POST["all"]:false;
    $links = isset($_POST["links"])?$_POST["links"]:false;
    $recursive = isset($_POST["recursive"])?$_POST["recursive"]:false;
    $copy = isset($_POST["copy"])?$_POST["copy"]:false;
    $force = isset($_POST["force"])?$_POST["force"]:false;
    $check = isset($_POST["check"])?$_POST["check"]:false;
    if (empty($source)) {
      $error = "ERROR: Source is not specified!";
    } else if (!file_exists($source)) {
      $error = "ERROR: Source file \"$source\" doesn't exist.\n";
    } else {
      if (is_dir($source)) {
        if (eaccelerator_mkdir($target, $force, 1)) {
          if ($all) {
            $s = "";
          }
          eaccelerator_encode_dir($source, $target, $s, $recursive, $links, $copy, $force, $check, 1);
        }
      } else {
        eaccelerator_encode_file($source, $target, $force, $copy, $check, 1);
      }
      global $web_error;
      if (empty($web_error)) {
        echo "<br><b>DONE</b></html>";
        return;
      } else {
        $error = $web_error;
      }
    }
  }
  echo "<h3 align=\"center\"><font color=\"#ff0000\">$error</font></h3>".
       "<form method=\"POST\">".
       "<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"600\" bgcolor=\"#000000\" align=\"center\">".
       "<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td width=\"50%\" bgcolor=\"#ccccff\"><b>Souce file or directory name:</b></td><td width=\"50%\"><input type=\"text\" name=\"source\" size=\"32\" value=\"$source\" style=\"width:100%\"></td></tr>".
       "<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td width=\"50%\" bgcolor=\"#ccccff\"><b>Target file or directory name:</b></td><td width=\"50%\"><input type=\"text\" name=\"target\" size=\"32\" value=\"$target\" style=\"width:100%\"></td></tr>".
       "<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td width=\"50%\" bgcolor=\"#ccccff\"><b>PHP suffixies <small>(comma separated list)</small>:</b></td><td width=\"50%\"><input type=\"text\" name=\"suffixies\" size=\"32\" value=\"$suffixies\" style=\"width:100%\"></td></tr>".

       "<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td width=\"50%\" bgcolor=\"#ccccff\"><b>Options:</b></td><td width=\"50%\">".
       "<input type=\"checkbox\" id=\"all\" name=\"all\"".(empty($all)?"":" checked")."> - <label for=\"all\">encode all files</label><br>".
       "<input type=\"checkbox\" id=\"links\" name=\"links\"".(empty($links)?"":" checked")."> - <label for=\"links\">follow symbolic links</label><br>".
       "<input type=\"checkbox\" id=\"recursive\" name=\"recursive\"".(empty($recursive)?"":" checked")."> - <label for=\"recursive\">encode directories recursively</label><br>".
       "<input type=\"checkbox\" id=\"copy\" name=\"copy\"".(empty($copy)?"":" checked")."> - <label for=\"copy\">copy files those shouldn't be encoded</label><br>".
       "<input type=\"checkbox\" id=\"force\" name=\"force\"".(empty($force)?"":" checked")."> - <label for=\"force\">overwrite existing files</label><br>".
       "<input type=\"checkbox\" id=\"check\" name=\"check\"".(empty($check)?"":" checked")."> - <label for=\"check\">exclude eaccelerator_load() check</label><br>".
       "</td></tr>".
       "<tr><td colspan=\"2\" align=\"center\" bgcolor=\"#cccccc\"><input type=\"submit\" name=\"submit\" value=\"OK\" style=\"width:100px\"></td></tr>".
       "</form></body></html>";
}

set_time_limit(0);

function is_cli() {
  if (php_sapi_name() == "cli" || empty($_SERVER['PHP_SELF'])) {
    return 1;
  } else {
    return 0;
  }
}

if (is_cli()) {
  if (!is_callable("eaccelerator_encode") && !(!extension_loaded("eAccelerator") && @dl((PHP_OS=="WINNT"||PHP_OS=="WIN32")?"eaccelerator.dll":"eaccelerator.so") && is_callable("eaccelerator_encode"))) {
    die("ERROR: eAccelerator Encoder is not installed\n");
  }
  if (!isset($_SERVER['argc'])) {
    die("ERROR: Set \"register_argc_argv = On\" in your php.ini or use a CLI version of PHP to run encoder.\n");
  }
  eaccelerator_encoder_main();
} else {
  if (!is_callable("eaccelerator_encode") && !(!extension_loaded("eAccelerator") && @dl((PHP_OS=="WINNT"||PHP_OS=="WIN32")?"eaccelerator.dll":"eaccelerator.so") && is_callable("eaccelerator_encode"))) {
    die("<html><head><title>eAccelerator Encoder</title></head><body><h1 align=\"center\">eAccelerator Encoder is not installed</h1></body></html>");
  }
  eaccelerator_encoder_web();
}
?>
