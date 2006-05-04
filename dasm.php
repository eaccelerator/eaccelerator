<?php
/*
   +----------------------------------------------------------------------+
   | eAccelerator control panel                                           |
   +----------------------------------------------------------------------+
   | Copyright (c) 2004-2006 eAccelerator								  |
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
$user = "admin";
$pw = "eAccelerator";
/** /config **/

/* {{{ auth */
if (!isset($_SERVER['PHP_AUTH_USER']) || !isset($_SERVER['PHP_AUTH_USER']) ||
        $_SERVER['PHP_AUTH_USER'] != $user || $_SERVER['PHP_AUTH_PW'] != $pw) {
    header('WWW-Authenticate: Basic realm="eAccelerator control panel"');
    header('HTTP/1.0 401 Unauthorized');
    exit;
} 
/* }}} */

?>

<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en">
<head>
    <title>eAccelerator control panel</title>
    <meta http-equiv="Content-Type" content="text/html; charset=ISO-8859-1" />
    <meta http-equiv="Content-Style-Type" content="text/css" />
    <meta http-equiv="Content-Language" content="en" />

    <style type="text/css" media="all">
        body {background-color: #ffffff; color: #000000;}
        body, td, th, h1, h2 {font-family: sans-serif;}
        pre {margin: 0px; font-family: monospace;}
        a:link {color: #000099; text-decoration: none; background-color: #ffffff;}
        a:hover {text-decoration: underline;}
        table {border-collapse: collapse; width: 800px;}
        .center {text-align: center;}
        .center table { margin-left: auto; margin-right: auto; text-align: left;}
        .center th { text-align: center !important; }
        td, th { border: 1px solid #000000; font-size: 75%; vertical-align: baseline;}
		td.source { background-color: #ffffff; font-size: small;}
        h1 {font-size: 150%;}
        h2 {font-size: 125%;}
        .p {text-align: left;}
        .e {background-color: #ccccff; font-weight: bold; color: #000000;}
        .h,th {background-color: #9999cc; font-weight: bold; color: #000000;}
        .v,td {background-color: #cccccc; color: #000000;}
        .vr{background-color: #cccccc; text-align: right; color: #000000;}
        img {float: right; border: 0px;}
        hr {width: 600px; background-color: #cccccc; border: 0px; height: 1px; color: #000000;}
        input {width: 150px}
        h1 {width: 800px;  border: 1px solid #000000; margin-left: auto; margin-right: auto; background-color: #9999cc;}
        .l {border: 1px solid #000000; text-align: left; width: 800px; margin: auto; font-size: 65%;}
        .l h2 {font-size: 200%; text-align: center; }
    </style>
</head>
<body class="center">
<h1>eAccelerator disassembler</h1>
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
