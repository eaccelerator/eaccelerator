<?php
/*
 * This file is a file with dummy php function to document the functions in
 * the eAccelerator extension.
 */

/**
 * eAccelerator controlpanel
 * It's possible to control eAccelerator with some functions eaccelerator provides.
 * These functions can be disabled with the configuration switch 
 * --without-eaccelerator-info at compile time.
 *
 * @package eAccelerator
 */

/**
 * Enable/disable caching
 * You are only allowed to use this function in scripts that are in the 
 * eaccelerator.admin_allowed_path
 *
 * @param boolean true to enable and false to disable eAccelerator caching 
 */
function eaccelerator_caching(boolean $flag) {};

/**
 * Enable/disable the optimizer
 * You are only allowed to use this function in scripts that are in the
 * eaccelerator.admin_allowed_path
 *
 * @param boolean true to enable and false to disable eAccelerator the optimizer
 */
function eaccelerator_optimizer(boolean $flag) {};

/**
 * Clean the cache
 * Remove all expired scripts and data from shared memory and disk cache. You 
 * are only allowed to use this function in scripts that are in the 
 * eaccelerator.admin_allowed_path
 */
function eaccelerator_clean() {};

/**
 * Clear the cache
 * Remove all unused scripts and data from shared memory and disk cache, this means 
 * all data that isn't used in the current requests. You are only allowed to use 
 * this function in scripts that are in the eaccelerator.admin_allowed_path
 */
function eaccelerator_clear() {};

/**
 * Purge the cache
 * Removed all scripts that are marked for deletetion. This will happen automaticly 
 * when shared memory is needed. You are only allowed to use this function in 
 * scripts that are in the eaccelerator.admin_allowed_path
 */
function eaccelerator_purge() {};

/**
 * Get info
 * Get info about eAccelerator, this is the info that is showed in the phpinfo()
 * page. There is also some information about compile time options like the 
 * shared memory and semaphores used.
 *
 * @return array An associative array with the info => value pairs
 */
function eaccelerator_info() {};

/**
 * Get cached scripts
 * Get an array with all cached scripts. This is an indexed array with each 
 * element an associative array with information about the cached script.
 * You are only allowed to use this function in scripts that are in the 
 * eaccelerator.admin_allowed_path
 *
 * @return array An indexed array with information about cached scripts
 */
function eaccelerator_cached_scripts() {};

/**
 * Get removed cached scripts
 * Get an array with all cached scripts that are marked as removed. This is an 
 * indexed array with each element an associative array with information about 
 * the cached script. You are only allowed to use this function in scripts that 
 * are in the eaccelerator.admin_allowed_path
 *
 * @return array An indexed array with information about the removed scripts
 */
function eaccelerator_removed_scripts() {};

?>
