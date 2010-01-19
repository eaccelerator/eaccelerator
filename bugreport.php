<?php
    /*
     * Let's dump some information about this php install
     * 
     * eAccelerator, (c) 2010 - http://www.eaccelerator.net
     */

    $sapi = php_sapi_name();
    switch ($sapi) {
        case 'apache':
        case 'apache2handler':
        case 'apache2filter':
        case 'cgi-fcgi':
            break;
        default:
            die("eAccelerator doesn't work with the SAPI you are using!\n");
    }

    if (!extension_loaded('eaccelerator') && !extension_loaded('eAccelerator') ) {
        die("eAccelerator isn't loaded, why do you want to report a bug?\n");
    }

    if (!function_exists('eaccelerator_info')) {
        echo "WARNING: Please compile eAccelerator with --with-eacelerator-info ". 
            "to provide us more information about your configuration.\n";
    }

    header('Content-type: text/plain');
    header('Content-Disposition: attachment; filename="bugreport.txt"');
?>
eAccelerator bug report
=======================

PHP Information
---------------
<?php
    printf("PHP Version: %s, Zend version: %s (SAPI: %s)\n", phpversion(), zend_version(), $sapi);
    printf("uname -a: %s\n", php_uname());
    
    $first = True;
    foreach (get_loaded_extensions() as $extension) {
        if ($first) {
            $first = False;
            echo "Extensions:\t $extension\n";
        } else {
            echo "\t\t $extension\n";
        }
    }

    if (strstr($sapi, 'apache')) {
?>

Apache information
------------------
<?php
        printf("Apache version: %s\n", apache_get_version());
        
        $first = True;
        foreach (apache_get_modules() as $module) {
            if ($first) {
                $first = False;
                echo "Module:\t\t $module\n";
            } else {
                echo "\t\t $module\n";
            }
        }
    }
?>

eAccelerator information
------------------------
<?php
    if (!function_exists('eaccelerator_info')) {
        echo "eAccelerator isn't compiled with info support :(";
        return;
    }
    $info = eaccelerator_info();
    foreach ($info as $key => $line) {
        echo "$key: $line\n";
    }

    echo "\neAccelerator options:\n";
    $ini = ini_get_all();
    foreach ($ini as $key => $value) {
        if (strstr($key, "eaccelerator")) {
            printf("\t$key: %s\n", $value['global_value']);
        }
    }

    $cachedir = $ini['eaccelerator.cache_dir']['global_value'];
    if (!is_dir($cachedir)) {
        echo "$cachedir doesn't exist!\n";
    } elseif (is_writeable($cachedir)) {
            echo "$cachedir isn't writable!";
    } else {
        echo "Cachedir seems ok!";
    }
?>
