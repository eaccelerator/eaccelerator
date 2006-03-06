<?php
/**
 * eaccelerator_lock example
 * Example how to use the lock functions.
 *
 * @package eAccelerator
 */
	eaccelerator_lock('count');
    /* this piece of code can only be executed by one thread at a time */
    for ($i = 0; $i < 40000; ++$i) {
        print $i;
    }
    eaccelerator_unlock('count');
?>
