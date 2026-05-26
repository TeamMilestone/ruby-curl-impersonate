#include <ruby.h>
#include <ruby/thread.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CURL_IMPERSONATE_CURL_IMPERSONATE_H
#  include <curl-impersonate/curl_impersonate.h>
#else
extern CURLcode curl_easy_impersonate(CURL *curl, const char *target, int default_headers);
#endif

static VALUE mCurlImpersonate;
static VALUE cResponse;
static VALUE eError;

struct buffer {
  char *data;
  size_t len;
  size_t cap;
};

static void buffer_init(struct buffer *b) { b->data = NULL; b->len = 0; b->cap = 0; }
static void buffer_free(struct buffer *b) { free(b->data); b->data = NULL; b->len = 0; b->cap = 0; }

static int buffer_append(struct buffer *b, const char *src, size_t n) {
  size_t needed = b->len + n;
  if (needed > b->cap) {
    size_t new_cap = b->cap == 0 ? 4096 : b->cap;
    while (new_cap < needed) new_cap *= 2;
    char *p = realloc(b->data, new_cap);
    if (!p) return -1;
    b->data = p;
    b->cap = new_cap;
  }
  memcpy(b->data + b->len, src, n);
  b->len += n;
  return 0;
}

static size_t write_body_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
  size_t total = size * nmemb;
  return buffer_append((struct buffer *)userdata, ptr, total) == 0 ? total : 0;
}

static size_t write_header_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
  size_t total = size * nmemb;
  return buffer_append((struct buffer *)userdata, ptr, total) == 0 ? total : 0;
}

struct perform_args {
  CURL *handle;
  CURLcode result;
};

static void *perform_without_gvl(void *data) {
  struct perform_args *args = (struct perform_args *)data;
  args->result = curl_easy_perform(args->handle);
  return NULL;
}

/* Iterator state passed to rb_hash_foreach to build a curl_slist of "Key: Value"
 * strings from a Ruby Hash. */
struct slist_build {
  struct curl_slist *list;
  int error; /* non-zero on first conversion failure */
};

static int build_header_slist(VALUE key, VALUE val, VALUE arg) {
  struct slist_build *s = (struct slist_build *)arg;
  if (s->error) return ST_STOP;

  VALUE k = rb_check_string_type(key);
  VALUE v = rb_check_string_type(val);
  if (NIL_P(k) || NIL_P(v)) { s->error = 1; return ST_STOP; }

  /* "<key>: <value>\0" — RFC 7230 header line */
  long klen = RSTRING_LEN(k);
  long vlen = RSTRING_LEN(v);
  char *line = malloc((size_t)klen + 2 + (size_t)vlen + 1);
  if (!line) { s->error = 1; return ST_STOP; }

  memcpy(line, RSTRING_PTR(k), klen);
  line[klen] = ':';
  line[klen + 1] = ' ';
  memcpy(line + klen + 2, RSTRING_PTR(v), vlen);
  line[klen + 2 + vlen] = '\0';

  struct curl_slist *next = curl_slist_append(s->list, line);
  free(line);
  if (!next) { s->error = 1; return ST_STOP; }
  s->list = next;
  return ST_CONTINUE;
}

static VALUE rb_cci_native_version(VALUE self) {
  (void)self;
  return rb_str_new_cstr(curl_version());
}

/* Signature (stage 6):
 *   _do_request_native(url, impersonate, headers, post_data,
 *                      follow_redirects, timeout_sec) -> Response
 *
 *   url               : String
 *   impersonate       : String (e.g. "chrome131")
 *   headers           : Hash<String, String>
 *   post_data         : String — empty string means GET, non-empty means POST
 *   follow_redirects  : true / false
 *   timeout_sec       : Integer
 *
 * Stage 7 adds the proxy parameter. */
static VALUE rb_cci_do_request(int argc, VALUE *argv, VALUE self) {
  (void)self;
  if (argc != 6) {
    rb_raise(rb_eArgError, "wrong number of arguments (given %d, expected 6)", argc);
  }

  VALUE rb_url = argv[0];
  VALUE rb_impersonate = argv[1];
  VALUE rb_headers = argv[2];
  VALUE rb_post_data = argv[3];
  VALUE rb_follow = argv[4];
  VALUE rb_timeout = argv[5];

  Check_Type(rb_url, T_STRING);
  Check_Type(rb_impersonate, T_STRING);
  Check_Type(rb_headers, T_HASH);
  Check_Type(rb_post_data, T_STRING);

  const char *url = StringValueCStr(rb_url);
  const char *impersonate = StringValueCStr(rb_impersonate);
  long timeout_sec = NUM2LONG(rb_timeout);
  long follow = RTEST(rb_follow) ? 1L : 0L;

  CURL *handle = curl_easy_init();
  if (!handle) {
    rb_raise(eError, "curl_easy_init failed");
  }

  CURLcode rc = curl_easy_impersonate(handle, impersonate, 1);
  if (rc != CURLE_OK) {
    curl_easy_cleanup(handle);
    rb_raise(eError, "curl_easy_impersonate(%s) failed: %s",
             impersonate, curl_easy_strerror(rc));
  }

  struct buffer body, headers_buf;
  buffer_init(&body);
  buffer_init(&headers_buf);

  curl_easy_setopt(handle, CURLOPT_URL, url);
  curl_easy_setopt(handle, CURLOPT_TIMEOUT, timeout_sec);
  curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, follow);
  curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
  /* Empty string enables all built-in encodings (gzip, br, zstd if available) —
   * essential for matching browser Accept-Encoding fingerprint. */
  curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_body_cb);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, write_header_cb);
  curl_easy_setopt(handle, CURLOPT_HEADERDATA, &headers_buf);

  /* POST body. We use COPYPOSTFIELDS so libcurl owns the copy and we can
   * release the Ruby String after setopt returns. POSTFIELDSIZE is set
   * explicitly so binary bodies with embedded NULs work. */
  long post_len = RSTRING_LEN(rb_post_data);
  if (post_len > 0) {
    curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, post_len);
    curl_easy_setopt(handle, CURLOPT_COPYPOSTFIELDS, RSTRING_PTR(rb_post_data));
  }

  /* Custom headers. Build a curl_slist from the Ruby Hash. */
  struct slist_build sb = { .list = NULL, .error = 0 };
  if (RHASH_SIZE(rb_headers) > 0) {
    rb_hash_foreach(rb_headers, build_header_slist, (VALUE)&sb);
    if (sb.error) {
      curl_slist_free_all(sb.list);
      buffer_free(&body);
      buffer_free(&headers_buf);
      curl_easy_cleanup(handle);
      rb_raise(eError, "failed to build header list (non-string key/value or OOM)");
    }
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, sb.list);
  }

  struct perform_args args = { .handle = handle, .result = CURLE_OK };
  rb_thread_call_without_gvl(perform_without_gvl, &args, RUBY_UBF_IO, NULL);

  if (args.result != CURLE_OK) {
    char errbuf[256];
    strncpy(errbuf, curl_easy_strerror(args.result), sizeof(errbuf) - 1);
    errbuf[sizeof(errbuf) - 1] = '\0';
    curl_slist_free_all(sb.list);
    buffer_free(&body);
    buffer_free(&headers_buf);
    curl_easy_cleanup(handle);
    rb_raise(eError, "curl_easy_perform failed: %s", errbuf);
  }

  long status_code = 0;
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status_code);

  VALUE rb_body    = rb_str_new(body.data ? body.data : "", body.len);
  VALUE rb_hdr_str = rb_str_new(headers_buf.data ? headers_buf.data : "", headers_buf.len);

  curl_slist_free_all(sb.list);
  buffer_free(&body);
  buffer_free(&headers_buf);
  curl_easy_cleanup(handle);

  return rb_struct_new(cResponse, LONG2NUM(status_code), rb_body, rb_hdr_str);
}

void Init_curl_impersonate(void) {
  curl_global_init(CURL_GLOBAL_DEFAULT);

  mCurlImpersonate = rb_define_module("CurlImpersonate");
  eError    = rb_const_get(mCurlImpersonate, rb_intern("Error"));
  cResponse = rb_const_get(mCurlImpersonate, rb_intern("Response"));

  rb_define_singleton_method(mCurlImpersonate, "_native_curl_version",
                             rb_cci_native_version, 0);
  rb_define_singleton_method(mCurlImpersonate, "_do_request_native",
                             rb_cci_do_request, -1);
}
