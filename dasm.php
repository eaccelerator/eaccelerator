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
*/
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
    </style>
</head>
<body class="center">
<h1>eAccelerator disassembler</h1>
<?php
    if (!isset($_GET['file'])) {
        die('File argument not given!');
    }

    $asm = eaccelerator_dasm_file($_GET['file']);
    if ($asm == null) {
        die('File not found!');
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
        <table>
            <tr>
                <th>N</th>
                <th>Opcode</th>
                <th>Extented value</th>
                <th>Op1</th>
                <th>Op2</th>
                <th>Result</th>
            </tr>
    <?php foreach($op_array as $n => $opline) { ?>
            <tr>
                <td class="e"><?= $n; ?></td>
                <td><nobr><?= $opline['opcode']; ?></nobr></td>
                <td><nobr><?= $opline['extended_value']; ?></nobr></td>
                <td><nobr><?= convert_string($opline['op1'], 50); ?></nobr></td>
                <td><nobr><?= convert_string($opline['op2'], 50); ?></nobr></td>
                <td><nobr><?= convert_string($opline['result'], 50); ?></nobr></td>
            </tr>
    <?php } ?>
        </table>
    <?php
    }
/* }}} */

/* {{{ print_function: print the given function */
    function print_function($name, $op_array) {
        echo "<h3>Function: $name</h3>";
        print_op_array($op_array);
    }
/* }}} */

/* {{{ print_function: print the given function */
    function print_method($name, $op_array) {
        echo "<h4>Method: $name</h4>";
        print_op_array($op_array);
    }
/* }}} */

    /*** start the output ***/
    /* print op_array */
    if (is_array($asm['op_array']) && count($asm['op_array'])) {
        echo "<h2>File op_array</h2>";
        print_op_array($asm['op_array']);
    }

    /* print functions */
    if (is_array($asm['functions']) && count($asm['functions']) > 0) {
        echo "<h2>File functions</h2>";
        foreach($asm['functions'] as $name => $op_array) {
            print_function($name, $op_array);
            echo "<hr />";
        }
    }

    /* print classes */
    if (is_array($asm['classes']) && count($asm['classes']) > 0) {
        echo "<h2>File classes</h2>";
        foreach($asm['classes'] as $class_name => $methods) {
            echo "<h3>Class: $class_name</h3>";
            if (isset($methods) && is_array($methods)) {
                foreach($methods as $method_name => $method_op_array) { 
                    print_method($method_name, $method_op_array);
                }
            }
            echo "<hr />";
        }
    }
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
