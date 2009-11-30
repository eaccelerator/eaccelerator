<?php
/*
   +----------------------------------------------------------------------+
   | eAccelerator control panel                                           |
   +----------------------------------------------------------------------+
   | Copyright (c) 2004-2007 eAccelerator								  |
   | http://eaccelerator.net											  |
   +----------------------------------------------------------------------+
   | This source file is subject to version 2.00 of the Zend license,     |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.zend.com/license/2_00.txt.                                |
   | If you did not receive a copy of the Zend license and are unable to  |
   | obtain it through the world-wide-web, please send a note to          |
   | license@zend.com so we can mail you a copy immediately.              |
   +----------------------------------------------------------------------+

   $ Id: $

/** config **/
$auth = true;		// Set to false to disable authentication
$user = "admin";
$pw = "eAccelerator";
/** /config **/

/* {{{ auth */
if ($auth && !isset($_SERVER['PHP_AUTH_USER']) || !isset($_SERVER['PHP_AUTH_USER']) ||
        $_SERVER['PHP_AUTH_USER'] != $user || $_SERVER['PHP_AUTH_PW'] != $pw) {
    header('WWW-Authenticate: Basic realm="eAccelerator control panel"');
    header('HTTP/1.0 401 Unauthorized');
    exit;
} 
/* }}} */

/* section = script cache */
$sec = 1;

if (!function_exists('eaccelerator_info')) {
    die('eAccelerator isn\'t installed or isn\'t compiled with info support!');
}

// Global info array
$info = eaccelerator_info();

?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en">
<head>
    <title>eAccelerator control panel</title>
    <meta http-equiv="Content-Type" content="text/html; charset=ISO-8859-1" />
    <meta http-equiv="Content-Style-Type" content="text/css" />
    <meta http-equiv="Content-Language" content="en" />

    <style type="text/css" media="all">
        body {background-color: #ffffff; color: #000000;margin: 0px;}
        body, td {font-family: Tahoma, sans-serif;font-size: 12pt;}

        a:link {color: #ff0000; text-decoration: none;}
        a:visited {color: #ff0000; text-decoration: none;}
        a:active {color: #aa0000; text-decoration: none;}
        a:hover {color: #aa0000; text-decoration: none;}
        
        .head1 {background-color: #A9CFDD; width: 100%; font-size: 32px; color: #ffffff;padding-top: 20px;font-family: Tahoma, sans-serif;}
        .head1_item {padding-left: 15px;}
        .head2 {background-color: #62ADC2; width: 100%; font-size: 18px; color: #ffffff;text-align: right; font-family: Tahoma, sans-serif;border-top: #ffffff 2px solid;}
        .head2 a:link {color: #ffffff;}
        .head2 a:visited {color: #ffffff;}
        .head2 a:active {color: #ffffff;}
        .head2 a:hover {color: #000000;}

        .menuitem {padding-left: 15px;padding-right: 15px;}
        .menuitem_sel {padding-left: 15px;padding-right: 15px;background-color: #ffffff; color: #000000;}
        .menuitem_hov {padding-left: 15px;padding-right: 15px;cursor:pointer;color: #000000;}

        pre {margin: 0px; font-family: monospace;}

        table {border-collapse: collapse; width: 800px;}
        .center {text-align: center;}
        .center table { margin-left: auto; margin-right: auto; text-align: left;}
        .center th { text-align: center !important; }
        td, th { border: 1px solid #000000; font-size: 75%; vertical-align: baseline;}
		td.source { background-color: #ffffff; font-size: small;}
        h1 {font-size: 150%;}
        h2 {font-size: 125%;}
        .p {text-align: left;}
        .e {background-color: #A9CFDD; font-weight: bold; color: #000000;}
        .h,th {background-color: #62ADC2; font-weight: bold; color: #000000;}
        .v,td {background-color: #cccccc; color: #000000;}
        .vr{background-color: #cccccc; text-align: right; color: #000000;}
        img {float: right; border: 0px;}
        hr {width: 600px; background-color: #cccccc; border: 0px; height: 1px; color: #000000;}
        input {width: 150px}
        h1 {width: 800px;  border: 1px solid #000000; margin-left: auto; margin-right: auto; background-color: #9999cc;}
        .l {border: 1px solid #000000; text-align: left; width: 800px; margin: auto; font-size: 65%;}
        .l h2 {font-size: 200%; text-align: center; }

        .footer {width: 100%;text-align: center;font-size: 9pt;color: #ababab;}
        .footer a:link {color: #ababab;}
        .footer a:visited {color: #ababab;}
        .footer a:active {color: #000000;}
        .footer a:hover {color: #000000;}
        
        small {font-size: 10pt;}
        .s {color: #676767;}

    </style>

    <script type="text/javascript">
      function menusel(i) {
        if (i.className == "menuitem_hov") i.className = "menuitem";
        else if (i.className == "menuitem") i.className = "menuitem_hov";
      }
      function gosec(i) {
        document.location = "control.php?sec="+i;
      }
    </script>

</head>
<body>
	<div class="head1"><span class="head1_item">eAccelerator control panel</span></div>
	<div class="head2">
	<?php
	$items = array(0 => 'Status', 1 => 'Script Cache');

	foreach ($items as $i => $item) {
	    echo '<span class="menuitem'.(($sec == $i)?'_sel':'').'" onmouseover="menusel(this)" onmouseout="menusel(this)" onclick="gosec('.$i.')">'.(($sec != $i)?'<a href="control.php?sec='.$i.'">'.$item.'</a>':$item).'</span>';
	}  
	?>
	</div>
	<div class="center">
<?php
    if (!isset($_GET['file']) || !is_file($_GET['file'])) {
        die('File argument not given!');
    }
	$file = $_GET['file'];

    $asm = eaccelerator_dasm_file($file);
    if ($asm == null) {
        die('File not found!');
	}

	require_once('PHP_Highlight.php');
	$h = new PHP_Highlight;
	$h->loadFile($_GET['file']);
	$source = $h->toArray();

	/* what do we need to do ? */
	if (!isset($_GET['show'])) {
		$show = '';
	} else {
		$show = $_GET['show'];
	}

	print_layout();
	switch ($show) {
		case 'main':
			print_op_array($asm['op_array']);
			break;
		case 'functions':
			if (is_array($asm['functions'][$_GET['name']])) {
	            print_function($_GET['name'], $asm['functions'][$_GET['name']]);
			}
			break;
		case 'methods':
			if (isset($_GET['method']) && is_array($asm['classes'][$_GET['name']][$_GET['method']])) {
				print_method($_GET['method'], $asm['classes'][$_GET['name']][$_GET['method']]);
			}
			break;
	}

/* {{{ convert_string */
    function convert_string($string, $length) {
        if (strlen($string) > $length) {
            $string = substr($string, 0, $length -4).' ...';
        }
        return htmlspecialchars($string);
    }
/* }}} */

/* {{{ print_op_array */
    function print_op_array($op_array) { ?>
        <h2>Global file op_array</h2>
        <table>
            <tr>
                <th>N</th>
                <th>Line</th>
                <th>Opcode</th>
                <th>Extented value</th>
                <th>Op1</th>
                <th>Op2</th>
                <th>Result</th>
            </tr>
    <?php
		$count = count($op_array);
		$line = 0;
		global $source;
		for ($i = 0; $i < $count; ++$i) {
			/* if the lineno is greater, than the last line displayed, then show the
			   code until that line above the opcode */
			if ($line < $op_array[$i]['lineno'])
			{
				$print = $op_array[$i]['lineno'];
			}
			$code = '';
			while($line < $print) {
				$code .= sprintf("%03d: %s\n", ($line + 1), $source[$line]);
				++$line;
			}
			if ($code != '') {
				echo "<tr>\n";
				echo '<td  class="source" colspan="7"><pre>' . $code . "</pre></td>\n";
				echo "</tr>\n";
			}
	?>
            <tr>
                <td class="e"><?php echo $i; ?></td>
                <td><nobr><?php echo $op_array[$i]['lineno']; ?></nobr></td>
                <td><nobr><?php echo $op_array[$i]['opcode']; ?></nobr></td>
                <td><nobr><?php echo $op_array[$i]['extended_value']; ?></nobr></td>
                <td><nobr><?php echo convert_string($op_array[$i]['op1'], 50); ?></nobr></td>
                <td><nobr><?php echo convert_string($op_array[$i]['op2'], 50); ?></nobr></td>
                <td><nobr><?php echo convert_string($op_array[$i]['result'], 50); ?></nobr></td>
            </tr>
    <?php 
		} 
		$count = count($source);
		if ($line < $count) {
			$code = '';
			while($line < $count) {
				$code .= sprintf("%03d: %s\n", ($line + 1), $source[$line]);
				++$line;
			}
			if ($code != '') {
				echo "<tr>\n";
				echo '<td  class="source" colspan="7"><pre>' . $code . "</pre></td>\n";
				echo "</tr>\n";
			}

		}
	?>
        </table>
    <?php
    }
/* }}} */

/* {{{ print_function: print the given function */
    function print_function($name, $op_array) {
        echo "<h2>Function: $name</h2>";
        print_op_array($op_array);
    }
/* }}} */

/* {{{ print_method: print the given method */
    function print_method($name, $op_array) {
        echo "<h2>Method: $name</h2>";
        print_op_array($op_array);
    }
/* }}} */

/* {{{ print_layout: print the layout of this script */
	function print_layout() {
		global $asm, $file;
		echo "<h2>Script layout</h2>\n";
		echo "<div class=\"l\">\n";
		echo "<ul>\n";
		if (isset($asm['op_array'])) {
			echo "<li><a href=\"?file=$file&show=main\">Global file op_array</a></li>";
		}
		if (isset($asm['functions']) && count($asm['functions']) > 0) {
			echo "<li>Functions<ul>\n";
			foreach ($asm['functions'] as $name => $data) {
				echo "<li><a href=\"?file=$file&show=functions&name=$name\">$name</a></li>";
			}
			echo "</ul></li>\n";
		}
		if (isset($asm['classes']) && count($asm['classes']) > 0) {
			echo "<li>Classes<ul>\n";
			foreach ($asm['classes'] as $name => $class) {
				echo "<li>$name<ul><li style=\"list-style-type: none\">Methods</li>\n";
				foreach (array_keys($class) as $method) {
					echo "<li><a href=\"?file=$file&amp;show=methods&amp;name=$name&amp;method=$method\">$method</a></li>\n";
				}
				echo "</ul></li>\n";
			}
			echo "</ul></li>\n";

		}
		echo "</ul>\n";
		echo "</div>\n";
	}
/* }}} */
?>
</div>

<div class="footer">
<?php
if (is_array($info)) {
?>
<br/><br/>
    <hr style="width:500px; color: #cdcdcd" noshade="noshade" size="1" />
    <strong>Created by the eAccelerator team &ndash; <a href="http://eaccelerator.net">http://eaccelerator.net</a></strong><br /><br />
    eAccelerator <?php echo $info['version']; ?> [shm:<?php echo $info['shm_type']?> sem:<?php echo $info['sem_type']; ?>]<br />
    PHP <?php echo phpversion();?> [ZE <?php echo zend_version(); ?>]<br />
    Using <?php echo php_sapi_name();?> on <?php echo php_uname(); ?><br />
<?php
}
?>
</div>
</body>
</html>

<?php

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */

?>
