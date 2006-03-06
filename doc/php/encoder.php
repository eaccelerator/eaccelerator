<?php
/*
 * This file is a file with dummy php function to document the functions in
 * the eAccelerator extension.
 */

/**
 * Encoder.
 * When eA is compiled with encoder support, the eaccelerator_encode function
 * can be used to encode php files.
 * For more information can look at encode.php in the source distribution of
 * eAccelerator. encoder.php is a command line script for encoding php files
 * with this function. The encoder can be disabled with --without-eaccelerator-encoder
 * at compile time.
 *
 * @package eAccelerator
 */

/**
 * Encode files.
 * Use this function to compile/encode php scripts. This scripts can be
 * loaded with eAccelerator or eLoader installed.
 *
 * @param mixed		The source code to encode
 * @param mixed		???? A prefix to add to the code ????
 * @param string	????
 * @param string	????
 * @return mixed	Returns the encoded script on success and false on failure.
 */
function eaccelerator_encode ($src, $prefix = '', $pre_content = '', $post_content = '') {};

?>
