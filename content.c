/*
   +----------------------------------------------------------------------+
   | Turck MMCache for PHP Version 4                                      |
   +----------------------------------------------------------------------+
   | Copyright (c) 2002-2003 TurckSoft, St. Petersburg                    |
   | http://www.turcksoft.com                                             |
   +----------------------------------------------------------------------+
   | This program is free software; you can redistribute it and/or        |
   | modify it under the terms of the GNU General Public License          |
   | as published by the Free Software Foundation; either version 2       |
   | of the License, or (at your option) any later version.               |
   |                                                                      |
   | This program is distributed in the hope that it will be useful,      |
   | but WITHOUT ANY WARRANTY; without even the implied warranty of       |
   | MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        |
   | GNU General Public License for more details.                         |
   |                                                                      |
   | You should have received a copy of the GNU General Public License    |
   | along with this program; if not, write to the Free Software          |
   | Foundation, Inc., 59 Temple Place - Suite 330, Boston,               |
   | MA  02111-1307, USA.                                                 |
   |                                                                      |
   | A copy is availble at http://www.gnu.org/copyleft/gpl.txt            |
   +----------------------------------------------------------------------+
   | Author: Dmitry Stogov <mmcache@turckware.ru>                         |
   +----------------------------------------------------------------------+
   $Id$
*/

#include "eaccelerator.h"

#ifdef HAVE_EACCELERATOR
#ifdef WITH_EACCELERATOR_CONTENT_CACHING

#include "SAPI.h"

#define EACCELERATOR_COMPRESS_MIN 128

static int (*eaccelerator_old_header_handler)(sapi_header_struct *sapi_header, sapi_headers_struct *sapi_headers TSRMLS_DC);

static int eaccelerator_check_compression(sapi_header_struct *sapi_header TSRMLS_DC) {
  if (strstr(sapi_header->header, "Content-Type") == sapi_header->header) {
    char *ch = sapi_header->header + sizeof("Content-Type") - 1;
    while (*ch != '\0' && *ch != ':') {ch++;}
    if (*ch == ':') {ch++;}
    while (*ch == ' ') {ch++;}
    if (strstr(ch, "text") != ch) {
      MMCG(compress_content) = 0;
      return 0;
    }
  } else if (strstr(sapi_header->header, "Content-Encoding") == sapi_header->header) {
    MMCG(compress_content) = 0;
    return 0;
  }
  return 1;
}

static void eaccelerator_free_header(sapi_header_struct *sapi_header) {
  efree(sapi_header->header);
}

static int eaccelerator_header_handler(sapi_header_struct *sapi_header, sapi_headers_struct *sapi_headers TSRMLS_DC) {
  if (MMCG(content_headers) != NULL) {
    sapi_header_struct x;
    memcpy(&x, sapi_header, sizeof(sapi_header_struct));
    x.header = estrndup(sapi_header->header, sapi_header->header_len);
    zend_llist_add_element(MMCG(content_headers), &x);
  }
  eaccelerator_check_compression(sapi_header TSRMLS_CC);
  if (eaccelerator_old_header_handler) {
    return eaccelerator_old_header_handler(sapi_header, sapi_headers TSRMLS_CC);
  } else {
    return SAPI_HEADER_ADD;
  }
}

void eaccelerator_content_cache_startup() {
  if (eaccelerator_content_cache_place != eaccelerator_none) {
    eaccelerator_old_header_handler = sapi_module.header_handler;
    sapi_module.header_handler = eaccelerator_header_handler;
  }
}

void eaccelerator_content_cache_shutdown() {
  if (eaccelerator_content_cache_place != eaccelerator_none) {
    sapi_module.header_handler = eaccelerator_old_header_handler;
  }
}

static int eaccelerator_is_not_modified(zval* return_value TSRMLS_DC) {
  char  etag[256];
  zval  **server_vars, **match;

  if (!SG(headers_sent)) {
    sprintf(etag,"ETag: eaccelerator-%u",eaccelerator_crc32(Z_STRVAL_P(return_value),Z_STRLEN_P(return_value)));
    sapi_add_header(etag, strlen(etag), 1);
    if (zend_hash_find(&EG(symbol_table), "_SERVER", sizeof("_SERVER"), (void **) &server_vars) == SUCCESS &&
        Z_TYPE_PP(server_vars) == IS_ARRAY &&
        zend_hash_find(Z_ARRVAL_PP(server_vars), "HTTP_IF_NONE_MATCH", sizeof("HTTP_IF_NONE_MATCH"), (void **) &match)==SUCCESS &&
        Z_TYPE_PP(match) == IS_STRING) {
      if (strcmp(etag+6,Z_STRVAL_PP(match)) == 0 &&
          sapi_add_header("HTTP/1.0 304", sizeof("HTTP/1.0 304") - 1, 1) == SUCCESS &&
          sapi_add_header("Status: 304 Not Modified", sizeof("Status: 304 Not Modified") - 1, 1) == SUCCESS) {
        zval_dtor(return_value);
        return_value->value.str.val = empty_string;
        return_value->value.str.len = 0;
        /*fprintf(stderr,"\nnot-modified\n");*/
        return 1;
      }
    }
  }
  return 0;
}

static void eaccelerator_put_page(const char* key, int key_len, zval* content, time_t ttl  TSRMLS_DC) {
  zval cache_array;
  zval *cache_content;
  INIT_ZVAL(cache_array);
  array_init(&cache_array);
  MAKE_STD_ZVAL(cache_content);
  if (MMCG(content_headers) && (MMCG(content_headers)->count > 0)) {
    zend_llist_element *p = MMCG(content_headers)->head;
    zval *headers;
    MAKE_STD_ZVAL(headers);
    array_init(headers);
    while (p != NULL) {
      sapi_header_struct* h = (sapi_header_struct*)&p->data;
      char* s = emalloc(h->header_len+2);
      s[0] = h->replace?'1':'0';
      memcpy(s+1, h->header, h->header_len+1);
      add_next_index_stringl(headers, s, h->header_len+1, 0);
      p = p->next;
    }
    add_assoc_zval(&cache_array, "headers", headers);
  }
  memcpy(cache_content, content, sizeof(zval));
  zval_copy_ctor(cache_content);
  cache_content->is_ref = 0;
  cache_content->refcount = 1;
  add_assoc_zval(&cache_array, "content", cache_content);
  eaccelerator_put(key, key_len , &cache_array, ttl, eaccelerator_content_cache_place TSRMLS_CC);
  zval_dtor(&cache_array);
}

static int eaccelerator_send_header(zval **header TSRMLS_DC) {
  sapi_add_header_ex(Z_STRVAL_PP(header)+1, Z_STRLEN_PP(header)-1, 1,
                     (zend_bool)((Z_STRVAL_PP(header)[0] == '0')?0:1) TSRMLS_CC);
  return SUCCESS;
}

static int eaccelerator_get_page(const char* key, int key_len, zval* return_value TSRMLS_DC) {
  int   ret = 0;
  zval cache_array;
  zval **headers;
  zval **content;
  if (eaccelerator_get(key, key_len, &cache_array, eaccelerator_content_cache_place TSRMLS_CC)) {
    if (Z_TYPE(cache_array) == IS_ARRAY) {
      if (zend_hash_find(Z_ARRVAL(cache_array),"content",sizeof("content"),(void**)&content) == SUCCESS &&
         Z_TYPE_PP(content) == IS_STRING) {
        if (zend_hash_find(Z_ARRVAL(cache_array),"headers",sizeof("headers"),(void**)&headers) == SUCCESS &&
           Z_TYPE_PP(headers) == IS_ARRAY) {
          zend_hash_apply(Z_ARRVAL_PP(headers), (apply_func_t)eaccelerator_send_header TSRMLS_CC);
        }
        memcpy(return_value,*content, sizeof(zval));
        zval_copy_ctor(return_value);
        ret = 1;
      }
    }
    zval_dtor(&cache_array);
  }
  return ret;
}

static void eaccelerator_compress(char* key, int key_len, zval* return_value, time_t ttl TSRMLS_DC) {
  zval  **server_vars, **encoding;

  if (MMCG(compression_enabled) &&
      MMCG(compress_content) &&
      !SG(headers_sent) &&
      zend_hash_find(&EG(symbol_table), "_SERVER", sizeof("_SERVER"), (void **) &server_vars) == SUCCESS &&
      Z_TYPE_PP(server_vars) == IS_ARRAY &&
      zend_hash_find(Z_ARRVAL_PP(server_vars), "HTTP_ACCEPT_ENCODING", sizeof("HTTP_ACCEPT_ENCODING"), (void **) &encoding)==SUCCESS &&
      Z_TYPE_PP(encoding) == IS_STRING &&
      Z_TYPE_P(return_value) == IS_STRING &&
      Z_STRLEN_P(return_value) >= EACCELERATOR_COMPRESS_MIN) {
    char* zkey = NULL;
    int   zkey_len;
    char* enc;
    zval  func;
    zval* params[2];
    zval  gzstring;
    int   gzip = 0;
    zval  level;

    zend_llist_element *p = SG(sapi_headers).headers.head;
    while (p != NULL) {
      sapi_header_struct* h = (sapi_header_struct*)&p->data;
      if (!eaccelerator_check_compression(h TSRMLS_CC)) {
        eaccelerator_is_not_modified(return_value TSRMLS_CC);
        return;
      }
      p = p->next;
    }

    if (strstr(Z_STRVAL_PP(encoding),"x-gzip")) {
      zkey_len = sizeof("gzip_") + key_len - 1;
      zkey = emalloc(zkey_len+1);
      memcpy(zkey,"gzip_",sizeof("gzip_")-1);
      memcpy(zkey+sizeof("gzip_")-1,key,key_len+1);
      ZVAL_STRING(&func, "gzcompress", 0);
      enc = "Content-Encoding: x-gzip";
      params[0] = return_value;
      gzip = 1;
    } else if (strstr(Z_STRVAL_PP(encoding),"gzip")) {
      zkey_len = sizeof("gzip_") + key_len - 1;
      zkey = emalloc(zkey_len+1);
      memcpy(zkey,"gzip_",sizeof("gzip_")-1);
      memcpy(zkey+sizeof("gzip_")-1,key,key_len+1);
      ZVAL_STRING(&func, "gzcompress", 0);
      enc = "Content-Encoding: gzip";
      params[0] = return_value;
      gzip = 1;
    } else if (strstr(Z_STRVAL_PP(encoding),"deflate")) {
      zkey_len = sizeof("deflate_") + key_len - 1;
      zkey = emalloc(zkey_len+1);
      memcpy(zkey,"deflate_",sizeof("deflate_")-1);
      memcpy(zkey+sizeof("deflate_")-1,key,key_len+1);
      ZVAL_STRING(&func, "gzdeflate", 0);
      enc = "Content-Encoding: deflate";
      params[0] = return_value;
    } else {
      eaccelerator_is_not_modified(return_value TSRMLS_CC);
      return;
    }
    INIT_ZVAL(level);
    ZVAL_LONG(&level,MMCG(compress_level));
    params[1] = &level;
    if (zkey != NULL &&
        zend_hash_exists(EG(function_table), Z_STRVAL(func), Z_STRLEN(func)+1) &&
        call_user_function(CG(function_table), (zval**)NULL, &func, &gzstring, 2, params TSRMLS_CC) == SUCCESS &&
        gzstring.type == IS_STRING) {
      if (gzip) {
        char* ret = emalloc(gzstring.value.str.len+13);
        unsigned long crc32 = eaccelerator_crc32(Z_STRVAL_P(return_value),Z_STRLEN_P(return_value));
        ret[0] = '\x1f';
        ret[1] = '\x8b';
        ret[2] = '\x08';
        ret[3] = '\x00';
        ret[4] = '\x00';
        ret[5] = '\x00';
        ret[6] = '\x00';
        ret[7] = '\x00';
        ret[8] = '\x00';
        ret[9] = '\x03';
        memcpy(ret+10,gzstring.value.str.val+2,gzstring.value.str.len-6);
        ret[gzstring.value.str.len+4]  = (char)(crc32 & 0xff);
        ret[gzstring.value.str.len+5]  = (char)((crc32 >> 8) & 0xff);
        ret[gzstring.value.str.len+6]  = (char)((crc32 >> 16) & 0xff);
        ret[gzstring.value.str.len+7]  = (char)((crc32 >> 24) & 0xff);
        ret[gzstring.value.str.len+8]  = (char)(return_value->value.str.len & 0xff);
        ret[gzstring.value.str.len+9]  = (char)((return_value->value.str.len >> 8) & 0xff);
        ret[gzstring.value.str.len+10] = (char)((return_value->value.str.len >> 16) & 0xff);
        ret[gzstring.value.str.len+11] = (char)((return_value->value.str.len >> 24) & 0xff);
        ret[gzstring.value.str.len+12] = '\x00';
        STR_FREE(gzstring.value.str.val);
        gzstring.value.str.val = ret;
        gzstring.value.str.len += 12;
      }
      eaccelerator_put_page(zkey, zkey_len, &gzstring, ttl TSRMLS_CC);
      if (!eaccelerator_is_not_modified(&gzstring TSRMLS_CC) &&
          sapi_add_header(enc, strlen(enc), 1) == SUCCESS &&
          sapi_add_header("Vary: Accept-Encoding", sizeof("Vary: Accept-Encoding") - 1, 1) == SUCCESS) {
      }
      efree(zkey);
      zval_dtor(return_value);
      memcpy(return_value,&gzstring,sizeof(zval));
      return;
    }
    if (zkey != NULL) {
      efree(zkey);
    }
  }
  eaccelerator_is_not_modified(return_value TSRMLS_CC);
}

static void eaccelerator_destroy_headers(TSRMLS_D) {
  if (MMCG(content_headers) != NULL) {
    zend_llist_destroy(MMCG(content_headers));
    efree(MMCG(content_headers));
    MMCG(content_headers) = NULL;
  }
}

PHP_FUNCTION(_eaccelerator_output_handler) {
  zval* content;
  long  status;
  char* s;
  char* key;
  int   key_len = 0;
  time_t ttl = 0;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
                            "z|l", &content, &status) == FAILURE) {
    eaccelerator_destroy_headers(TSRMLS_C);
    return;
  }
  memcpy(return_value, content, sizeof(zval));
  s = key = return_value->value.str.val;
  if ((status & PHP_OUTPUT_HANDLER_START) != 0) {
    while (*s) {++s;}
    ttl = atoi(key);
    s = key = s+1;
    if (s - return_value->value.str.val > return_value->value.str.len) {
      zval_copy_ctor(return_value);
      eaccelerator_destroy_headers(TSRMLS_C);
      return;
    }
    while (*s) {++s;}
    key_len = atoi(key);
    s = key = s+1;
    if (s - return_value->value.str.val > return_value->value.str.len) {
      zval_copy_ctor(return_value);
      eaccelerator_destroy_headers(TSRMLS_C);
      return;
    }
    while (*s) {++s;}
    ++s;
    if (s - return_value->value.str.val > return_value->value.str.len) {
      zval_copy_ctor(return_value);
      eaccelerator_destroy_headers(TSRMLS_C);
      return;
    }
  }
  return_value->value.str.len -= (s-return_value->value.str.val);
  return_value->value.str.val = s;
  zval_copy_ctor(return_value);
  if ((status & PHP_OUTPUT_HANDLER_START) != 0 &&
      (status & PHP_OUTPUT_HANDLER_END) != 0 &&
      !(PG(connection_status) & PHP_CONNECTION_ABORTED) != 0) {
    eaccelerator_put_page(key, key_len , return_value, ttl TSRMLS_CC);
    eaccelerator_compress(key, key_len, return_value, ttl TSRMLS_CC);
  }
  eaccelerator_destroy_headers(TSRMLS_C);
}

PHP_FUNCTION(eaccelerator_cache_page) {
  char* key;
  int   key_len;
  long  ttl = 0;
  zval  **server_vars, **encoding;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
                          "s|l", &key, &key_len, &ttl) == FAILURE) {
    RETURN_FALSE;
  }
  if (eaccelerator_content_cache_place == eaccelerator_none) {
    RETURN_FALSE;
  }
  if (MMCG(content_headers) != NULL) {
    RETURN_FALSE;
  }
  if (MMCG(compression_enabled) &&
      MMCG(compress_content) &&
      !SG(headers_sent) &&
      zend_hash_find(&EG(symbol_table), "_SERVER", sizeof("_SERVER"), (void **) &server_vars) == SUCCESS &&
      Z_TYPE_PP(server_vars) == IS_ARRAY &&
      zend_hash_find(Z_ARRVAL_PP(server_vars), "HTTP_ACCEPT_ENCODING", sizeof("HTTP_ACCEPT_ENCODING"), (void **) &encoding)==SUCCESS &&
      Z_TYPE_PP(encoding) == IS_STRING) {
    char* zkey = NULL;
    char* enc = NULL;
    int   zkey_len = 0;
    if (strstr(Z_STRVAL_PP(encoding),"x-gzip")) {
      zkey_len = sizeof("gzip_") + key_len - 1;
      zkey = emalloc(zkey_len+1);
      memcpy(zkey,"gzip_",sizeof("gzip_")-1);
      memcpy(zkey+sizeof("gzip_")-1,key,key_len+1);
      enc = "Content-Encoding: x-gzip";
    } else if (strstr(Z_STRVAL_PP(encoding),"gzip")) {
      zkey_len = sizeof("gzip_") + key_len - 1;
      zkey = emalloc(zkey_len+1);
      memcpy(zkey,"gzip_",sizeof("gzip_")-1);
      memcpy(zkey+sizeof("gzip_")-1,key,key_len+1);
      enc = "Content-Encoding: gzip";
    } else if (strstr(Z_STRVAL_PP(encoding),"deflate")) {
      zkey_len = sizeof("deflate_") + key_len - 1;
      zkey = emalloc(zkey_len+1);
      memcpy(zkey,"deflate_",sizeof("deflate_")-1);
      memcpy(zkey+sizeof("deflate_")-1,key,key_len+1);
      enc = "Content-Encoding: deflate";
    }
    if (zkey != NULL &&
        eaccelerator_get_page(zkey, zkey_len, return_value TSRMLS_CC) &&
        return_value->type == IS_STRING) {
      if (!eaccelerator_is_not_modified(return_value TSRMLS_CC) &&
          sapi_add_header(enc, strlen(enc), 1) == SUCCESS &&
          sapi_add_header("Vary: Accept-Encoding", sizeof("Vary: Accept-Encoding") - 1, 1) == SUCCESS) {
        ZEND_WRITE(return_value->value.str.val, return_value->value.str.len);
      }
      efree(zkey);
      zend_bailout();
      RETURN_TRUE;
    }
    if (zkey != NULL) {
      efree(zkey);
    }
  }
  if (eaccelerator_get_page(key, key_len, return_value TSRMLS_CC) &&
      return_value->type == IS_STRING) {
    /*  Output is cached. Print it. */
    if (!(PG(connection_status) & PHP_CONNECTION_ABORTED)) {
      eaccelerator_compress(key, key_len, return_value, ttl TSRMLS_CC);
    }
    ZEND_WRITE(return_value->value.str.val, return_value->value.str.len);
    zend_bailout();
    RETURN_TRUE;
  } else {
    /* Output is not cached. Install Handler. */
    char ch = '\000';
#ifdef PHP_OUTPUT_HANDLER_USER
#if defined(PHP_MAJOR_VERSION) && defined(PHP_MINOR_VERSION) && \
    ((PHP_MAJOR_VERSION == 4 && PHP_MINOR_VERSION >= 3) || \
     (PHP_MAJOR_VERSION > 4))
    /* PHP 4.3.0 and above */
    zval handler;
    ZVAL_STRING(&handler, "_eaccelerator_output_handler", 0);
    php_start_ob_buffer(&handler, 0, 0 TSRMLS_CC);
    if (OG(active_ob_buffer).handler_name == NULL ||
        strcmp(OG(active_ob_buffer).handler_name,"_eaccelerator_output_handler") != 0) {
#else
    /* PHP 4.2.* */
    zval *handler;
    ALLOC_INIT_ZVAL(handler);
    ZVAL_STRING(handler, "_eaccelerator_output_handler", 1);
    php_start_ob_buffer(handler, 0, 0 TSRMLS_CC);
    if (OG(active_ob_buffer).handler_name == NULL ||
        strcmp(OG(active_ob_buffer).handler_name,"_eaccelerator_output_handler") != 0) {
#endif
#else
    /* PHP 4.1.2 and before */
    zval *handler;
    ALLOC_INIT_ZVAL(handler);
    ZVAL_STRING(handler, "_eaccelerator_output_handler", 1);
    if (php_start_ob_buffer(handler, 0 TSRMLS_CC) == FAILURE) {
#endif
      RETURN_FALSE;
    }
    zend_printf("%ld",ttl);
    ZEND_PUTC(ch);
    zend_printf("%d",key_len);
    ZEND_PUTC(ch);
    zend_printf("%s",key);
    ZEND_PUTC(ch);
    /* Init headers cache */
    MMCG(content_headers) = emalloc(sizeof(zend_llist));
    zend_llist_init(MMCG(content_headers), sizeof(sapi_header_struct), (void (*)(void *))eaccelerator_free_header, 0);
    RETURN_TRUE;
  }
  RETURN_FALSE;
}

PHP_FUNCTION(eaccelerator_rm_page) {
  char* key;
  int   key_len;
  char* zkey;
  int   zkey_len;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
                          "s", &key, &key_len) == FAILURE) {
    return;
  }
  if (eaccelerator_content_cache_place == eaccelerator_none) {
    RETURN_NULL();
  }
  zkey = do_alloca(key_len+16);
  eaccelerator_rm(key, key_len, eaccelerator_content_cache_place TSRMLS_CC);
  zkey_len = sizeof("gzip_") + key_len - 1;
  memcpy(zkey,"gzip_",sizeof("gzip_")-1);
  memcpy(zkey+sizeof("gzip_")-1,key,key_len+1);
  eaccelerator_rm(zkey, zkey_len, eaccelerator_content_cache_place TSRMLS_CC);
  zkey_len = sizeof("deflate_") + key_len - 1;
  memcpy(zkey,"deflate_",sizeof("deflate_")-1);
  memcpy(zkey+sizeof("deflate_")-1,key,key_len+1);
  eaccelerator_rm(zkey, zkey_len, eaccelerator_content_cache_place TSRMLS_CC);
  RETURN_NULL();
}

PHP_FUNCTION(eaccelerator_cache_output) {
  char* key;
  int   key_len;
  char* code;
  int   code_len;
  long  ttl = 0;
  char* eval_name;
  int ret = 0;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
                          "ss|l", &key, &key_len, &code, &code_len, &ttl) == FAILURE) {
    return;
  }
  if (eaccelerator_content_cache_place == eaccelerator_none) {
    eval_name = zend_make_compiled_string_description("eval()'d code" TSRMLS_CC);
    zend_eval_string(code, NULL, eval_name TSRMLS_CC);
    efree(eval_name);
    RETURN_FALSE;
  } else if (eaccelerator_get(key, key_len, return_value, eaccelerator_content_cache_place TSRMLS_CC) &&
      return_value->type == IS_STRING) {
    /*  Output is cached. Print it. */
    ZEND_WRITE(return_value->value.str.val, return_value->value.str.len);
    zval_dtor(return_value);
    RETURN_TRUE;
  } else {
    /* Output is not cached. Generate it and print. */
    eval_name = zend_make_compiled_string_description("eval()'d code" TSRMLS_CC);
#ifdef PHP_OUTPUT_HANDLER_USER
    /* PHP 4.2.0 and above */
    if (php_start_ob_buffer(NULL, 0, 0 TSRMLS_CC) == FAILURE) {
#else
    /* PHP 4.1.2 and before */
    if (php_start_ob_buffer(NULL, 0 TSRMLS_CC) == FAILURE) {
#endif
      zend_eval_string(code, NULL, eval_name TSRMLS_CC);
      efree(eval_name);
      RETURN_FALSE;
    }
    if (zend_eval_string(code, NULL, eval_name TSRMLS_CC) == SUCCESS) {
      if (php_ob_get_buffer(return_value TSRMLS_CC) == SUCCESS) {
        ret = eaccelerator_put(key, key_len, return_value, ttl, eaccelerator_content_cache_place TSRMLS_CC);
        zval_dtor(return_value);
      }
    }
    efree(eval_name);
    php_end_ob_buffer(1, 0 TSRMLS_CC);
    if (ret) {
      RETURN_TRUE;
    }
  }
  RETURN_FALSE;
}

PHP_FUNCTION(eaccelerator_cache_result) {
  char* key;
  int   key_len;
  char* code;
  int   code_len;
  long  ttl = 0;
  char* eval_name;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
                          "ss|l", &key, &key_len, &code, &code_len, &ttl) == FAILURE) {
    return;
  }
  if ((eaccelerator_content_cache_place != eaccelerator_none) &&
      eaccelerator_get(key, key_len, return_value, eaccelerator_content_cache_place TSRMLS_CC)) {
    /*  Return value is cached. Return it. */
    return;
  } else {
    /* Return value is not cached. Generate it and return. */
    eval_name = zend_make_compiled_string_description("eval()'d code" TSRMLS_CC);
    if (zend_eval_string(code, return_value, eval_name TSRMLS_CC) == SUCCESS &&
        eaccelerator_content_cache_place != eaccelerator_none) {

      /* clean garbage */
      while (EG(garbage_ptr)) {
        zval_ptr_dtor(&EG(garbage)[--EG(garbage_ptr)]);
      }

      eaccelerator_put(key, key_len, return_value, ttl, eaccelerator_content_cache_place TSRMLS_CC);
    }
    efree(eval_name);
    return;
  }
}

#endif
#endif
