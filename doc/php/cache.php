<?php
/*
 * This file is a file with dummy php function to document the functions in
 * the eAccelerator extension.
 */

/**
 * Caching.
 * These functions are enabled when eAccelerator is compiled with content 
 * caching support. These functions can be disabled with the configuration
 * switch --without-eaccelerator-content-caching at compile time.
 *
 * @package eAccelerator
 */

/**
 * Cache full page.
 * Cache the full page for $ttl seconds.
 *
 * @param string A string to identify this page, something like the url.
 * @param int The number of seconds to cache the page, 0 for never expire.
 * @example eaccelerator_cache_page.php Cache page example
 */
function eaccelerator_cache_page ($key, $ttl = 0) {}

/**
 * Remove a cached page.
 * Removes the page which was cached by eaccelerator_cache_page with the same $key from cache.
 *
 * @param string    The key that identifies the cached page
 * @see eaccelerator_cache_page
 */
function eaccelerator_rm_page ($key) {}

/**
 * Cache ouput of evaled code.
 * Caches the output of the $eval_code in shared memory for $ttl seconds. The 
 * output can be removed from cache by calling eaccelerator_rm with the same
 * key.
 *
 * @param string The key to identify the output with.
 * @param string The code to eval() and cache the output from.
 * @param int The number of seconds to cache it or 0 for never expire.
 * @see eaccelerator_rm
 * @example eaccelerator_cache_output.php Cache output example.
 */
function eaccelerator_cache_output ($key, $eval_code, $ttl = 0) {}

/**
 * Cache result of evaled code.
 * Caches the result of the $evel_code in shared memory for $ttl seconds. The 
 * result can be removed from cache by calling eaccelerator_rm with the same 
 * key.
 *
 * @param string The key to identify the result with.
 * @param string The code to eval() and cache the result from.
 * @param int The number of seconds to cache it or 0 for never expire.
 * @see eaccelerator_rm
 * @example eaccelerator_cache_result.php Cache result example.
 */
function eaccelerator_cache_result ($key, $code, $ttl = 0) {}

?>
