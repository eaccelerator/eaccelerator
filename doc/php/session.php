<?php
/*
 * This file is a file with dummy php function to document the functions in
 * the eAccelerator extension.
 */

/**
 * eAccelerator session handling.
 * If eA session support is enabled, eA can be used as session handler. The default
 * php session handler saves it's session data to hard disk. When eA is used this
 * data is saved in shared memory when it's available and then to disk. This can give
 * a performance boost, especialy on systems with a lot hard disk I/O.
 * The session handler can be disabled with --without-eaccelerator-sessions
 * at compile time.
 *
 * @package eAccelerator
 */

/**
 * Set eAccelerator session handling. 
 * When this function is called eAccelerator is registered as session handler.
 *
 * Since PHP 4.2.0 you can also install the eAccelerator session handlers in 
 * you're php.ini with: "php.ini" by "session.save_handler=eaccelerator"
 * 
 * @return boolean	Returns true on success and false on failure
 * @link http://www.php.net/manual/en/function.session-set-save-handler.php More on custom session handlers
 */
function eaccelerator_set_session_handlers () {}

?>
