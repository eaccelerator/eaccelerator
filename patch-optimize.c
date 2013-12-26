--- optimize.c.orig	2012-08-16 17:34:36.000000000 +0400
+++ optimize.c	2013-08-26 15:03:11.000000000 +0400
@@ -3501,6 +3501,17 @@
     }
 
 #ifdef ZEND_ENGINE_2_3
+    /* do not optimize if opcodes have "closures" */
+    zend_op* op = op_array->opcodes;
+    int len = op_array->last;
+    int line_num;
+
+    for (line_num = 0; line_num < len; op++,line_num++) {
+        if (op->opcode == ZEND_DECLARE_LAMBDA_FUNCTION) {
+            return;
+        }
+    }
+
     /* We run pass_two() here to let the Zend engine resolve ZEND_GOTO labels
        this converts goto labels(string) to opline numbers(long)
        we need opline numbers for CFG generation, otherwise the optimizer will
