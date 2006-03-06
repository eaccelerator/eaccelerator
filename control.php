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

if (!function_exists('eaccelerator_info')) {
    die('eAccelerator isn\'t installed or isn\'t compiled with info support!');
}

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

/* {{{ process any commands */
$info = eaccelerator_info();
if (isset($_POST['caching'])) {
    if ($info['cache']) {
        eaccelerator_caching(false);
    } else {
        eaccelerator_caching(true);
    }
} else if (isset($_POST['optimizer']) && function_exists('eaccelerator_optimizer')) {
    if ($info['optimizer']) {
        eaccelerator_optimizer(false);
    } else {
        eaccelerator_optimizer(true);
    }
} else if (isset($_POST['clear'])) {
    eaccelerator_clear();
} else if (isset($_POST['clean'])) {
    eaccelerator_clean();
} else if (isset($_POST['purge'])) {
    eaccelerator_purge();
}
$info = eaccelerator_info();
/* }}} */

/* {{{ create_script_table */
function create_script_table($list) { ?>
    <table class="scripts">
        <tr>
            <th>Filename</th>
            <th>MTime</th>
            <th>Size</th>
            <th>Reloads</th>
            <th>Hits</th>
        </tr>
    <?php foreach($list as $script) { ?>
        <tr>
    <?php   if (function_exists('eaccelerator_dasm_file')) { ?>
            <td class="e"><a href="dasm.php?file=<?= $script['file']; ?>"><?= $script['file']; ?></a></td>
    <?php   } else { ?>
            <td class="e"><?= $script['file']; ?></td>
    <?php   } ?>
            <td class="vr"><?= date('Y-m-d H:i', $script['mtime']); ?></td>
            <td class="vr"><?= number_format($script['size'] / 1024, 2); ?>KB</td>
            <td class="vr"><?= $script['reloads']; ?> (<?= $script['usecount']; ?>)</td>
            <td class="vr"><?= $script['hits']; ?></td>
        </tr>
    <?php } ?>
    </table>
<?php 
}
/* }}} */

/* {{{ create_key_table */
function create_key_table($list) {
?>
    <table class="key">
        <tr>
            <th>Name</th>
            <th>Created</th>
            <th>Size</th>
            <th>ttl</th>
        </tr>
<?php
    foreach($list as $key) {
?>
        <tr>
            <td class="e"><?= $key['name']; ?></td>
            <td class="vr"><?= date('Y-m-d H:i', $key['created']); ?></td>
            <td class="vr"><?= number_format($key['size']/1024, 3); ?>KB</td>
            <td class="vr"><?php 
                if ($key['ttl'] == -1) {
                    echo 'expired';
                } elseif ($key['ttl'] == 0) {
                    echo 'none';
                } else {
                    echo date('Y-m-d H:i', $key['ttl']);
                }
            ?></td>
        </tr>
<?php
    }
?>
    </table>
<?php
}
/* }}} */

/* {{{ print_header */
function print_header() { ?>
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
        a:link {color: #000099; text-decoration: none}
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
<?php 
} 
/* }}} */
?>

<?php print_header(); ?>
<body class="center">
<h1>eAccelerator <?= $info['version']; ?> control panel</h1>

<!-- {{{ information -->
<h2>Information</h2>
<table>
<tr>
    <td class="e">Caching enabled</td> 
    <td><?= $info['cache'] ? 'yes':'no' ?></td>
</tr>
<tr>
    <td class="e">Optimizer enabled</td>
    <td><?= $info['optimizer'] ? 'yes':'no' ?></td>
</tr>
<tr>
    <td class="e">Memory usage</td>
    <td><?= number_format(100 * $info['memoryAllocated'] / $info['memorySize'], 2); ?>% 
        (<?= number_format($info['memoryAllocated'] / (1024*1024), 2); ?>MB/
        <?= number_format($info['memorySize'] / (1024*1024), 2); ?>MB)</td>
</tr>
<tr>
    <td class="e">Free memory</td>
    <td><?= number_format($info['memoryAvailable'] / (1024*1024), 2); ?>MB</td>
</tr>
<tr>
    <td class="e">Cached scripts</td>
    <td><?= $info['cachedScripts']; ?></td>
</tr>
<tr>
    <td class="e">Removed scripts</td> 
    <td><?= $info['removedScripts']; ?></td>
</tr>
<tr>
    <td class="e">Cached keys</td>
    <td><?= $info['cachedKeys']; ?></td>
</tr>
</table>
<!-- }}} -->

<!-- {{{ control -->
<h2>Actions</h2>
<form name="ea_control" method="post">
    <table>
        <tr>
            <td class="e">Caching</td>
            <td><input type="submit" name="caching" value="<?= $info['cache']?'disable':'enable'; ?>" /></td>
        </tr>
        <tr>
            <td class="e">Optimizer</td>
            <td><input type="submit" name="optimizer" value="<?= $info['optimizer']?'disable':'enable'; ?>" /></td>
        </tr>
        <tr>
            <td class="e">Clear cache</td>
            <td><input type="submit" name="clear" value="clear" title="remove all unused scripts and data from shared memory and disk cache" /></td>
        </tr>
        <tr>
            <td class="e">Clean cache</td>
            <td><input type="submit" name="clean" value="clean" title=" remove all expired scripts and data from shared memory and disk cache" /></td>
        </tr>
        <tr>
            <td class="e">Purge cache</td>
            <td><input type="submit" name="purge" value="purge" title="remove all 'removed' scripts from shared memory" /></td>
        </tr>
    </table>
</form>
<!-- }}} -->

<h2>Cached scripts</h2>
<?php create_script_table(eaccelerator_cached_scripts()); ?>

<h2>Removed scripts</h2>
<?php create_script_table(eaccelerator_removed_scripts()); ?>

<?php
if (function_exists('eaccelerator_get')) {
    echo "<h2>Cached keys</h2>";
    create_key_table(eaccelerator_list_keys());
}
?>

<!-- {{{ footer -->
<br /><br />
<table>
    <tr><td class="center">
    <a href="http://eaccelerator.net"><img src="?=<?= $info['logo']; ?>" alt="eA logo" /></a>
    <strong>Created by the eAccelerator team, <a href="http://eaccelerator.net">http://eaccelerator.net</a></strong><br /><br />
    <nobr>eAccelerator <?= $info['version']; ?> [shm:<?= $info['shm_type']?> sem:<?= $info['sem_type']; ?>]</nobr><br />
    <nobr>PHP <?= phpversion();?> [ZE <?= zend_version(); ?>]</nobr><br />
    <nobr>Using <?= php_sapi_name();?> on <?= php_uname(); ?></nobr><br />
    </td></tr>
</table>
<!-- }}} -->
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
