<?php
/*
 * This file is a file with dummy php function to document the functions in
 * the eAccelerator extension.
 */

/**
 * eAccelerator disassemebler
 * These functions can be disabled with the configuration switch 
 * --with-eaccelerator-disassembler at compile time.
 *
 * @package eAccelerator
 */

/**
 * Get the opcodes from a file
 * This function returns an array with opcodes for the given filename. It 
 * will only return the opcodes for a cached file. These arrays of opcodes 
 * are returned for the global file, the function in the file and all methods 
 * of all classes in that file.
 *
 * @return array On the first level this array can contain three elements:
 *          - op_array: the op_array of the file
 *          - functions: an array with key the function name and as value the op_array of that function
 *          - classes: an array with key the classname and as value an array with as key the method name and as value the op_array of that method.
 */
function eaccelerator_dasm_file(string $filename) {};

?>
