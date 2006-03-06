<?php
/**
 * eaccelerator_cache_page example.
 *
 * @package eAccelerator
 */
	eaccelerator_cache_page($_SERVER['PHP_SELF'].'?GET='.serialize($_GET), 30);
	echo time();
	phpinfo();
?>
