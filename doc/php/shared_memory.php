<?php
/*
 * This file is a file with dummy php function to document the functions in
 * the eAccelerator extension.
 */

/**
 * eAccelerator shared memory access.
 * eAccelerator can be used to store data in shared memory and share this 
 * between script. This can be used instead of the php shm or shmop API. 
 * These functions can be enabled with --with-eaccelerator-shared-memory
 * at compile time
 *
 * @package eAccelerator
 */

/**
 * Put key.
 * Put key in the eaccelerator shared memory. 
 * eAccelerator doesn't serialize object, so you need to do it you're self or 
 * php will segfault on object retrieval.
 *
 * @param string	The key to identify the data
 * @param mixed		The data to store in shared memory
 * @param int		Cache the key for $ttl seconds, 0 for never
 * @return boolean	Returns true if the function was succesfull otherwise it 
 * 	will return false. When false is returned, this could mean the limit of 
 *	the total cache is exceeded or the size of the data is to big for the 
 *	eaccelerator.shm_max directive.
 * @see eaccelerator_get()
 * @see eaccelerator_rm()
 */
function eaccelerator_put ($key, $value, $ttl = 0) {}

/**
 * Get data.
 * Get data from shared memory. Object need to be serialized when storing them
 * so unserializing is necessary when retrieving them.
 *
 * @param string	The key to identify the data
 * @return mixed	Returns the requested data on succes otherwise NULL if the key doesn't exist or the key was expired.
 * @see eaccelerator_put()
 */
function eaccelerator_get ($key) {}

/**
 * Remove key.
 * Remove key from shared memory
 *
 * @param string	The key to identify the data
 * @return boolean	Return true on success and false on failure
 * @see eaccelerator_put()
 */
function eaccelerator_rm ($key) {}

/**
 * Garbage collection.
 * Removes expired keys (session data and content) from shared memory
 */
function eaccelerator_gc () {}

/**
 * Lock.
 * Create a lock with the given key, this allows you to prevent concurrent 
 * access to some part of your code. Warning, you don't need this to lock the 
 * keys used with eaccelerator_get and eaccelerator_put. The lock can be 
 * released with eaccelerator_unlock or automatic at the end of the request.
 *
 * @param string	The key to identify the data to lock
 * @return boolean	Return true on success and false on failure
 * @see eaccelerator_unlock()
 * @example eaccelerator_lock.php Lock example
 */
function eaccelerator_lock ($key) {}

/**
 * Unlock.
 * Unlock the access to a key
 * 
 * @param string	The key to identify the data to lock
 * @return boolean	Return true on success and false on failure
 * @see eaccelerator_lock()
 */
function eaccelerator_unlock ($key) {}

?>
