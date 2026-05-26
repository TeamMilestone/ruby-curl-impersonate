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

/* Growable byte buffer for accumulating callback chunks. Mirrors the
 * realloc-based pattern in the Go reference implementation. */
struct buffer {
  char *data;
  size_t len;
  size_t cap;
};

static void buffer_init(struct buffer *b) {
  b->data = NULL;
  b->len = 0;
  b->cap = 0;
}

static void buffer_free(struct buffer *b) {
  free(b->data);
  b->data = NULL;
  b->len = 0;
  b->cap = 0;
}

/* Append `n` bytes. Returns 0 on success, -1 on allocation failure. */
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
  struct buffer *buf = (struct buffer *)userdata;
  if (buffer_append(buf, ptr, total) != 0) {
    return 0; /* signal write error to libcurl */
  }
  return total;
}

static size_t write_header_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
  size_t total = size * nmemb;
  struct buffer *buf = (struct buffer *)userdata;
  if (buffer_append(buf, ptr, total) != 0) {
    return 0;
  }
  return total;
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

static VALUE rb_cci_native_version(VALUE self) {
  (void)self;
  return rb_str_new_cstr(curl_version());
}

/* Stage 5 signature:
 *   _do_request_native(url, impersonate, timeout_sec) -> Response
 * Response is a Struct with members (status_code, body, headers).
 *
 * Stages 6+ extend this with headers, POST, proxy, redirects. */
static VALUE rb_cci_do_request(VALUE self, VALUE rb_url, VALUE rb_impersonate, VALUE rb_timeout) {
  (void)self;
  Check_Type(rb_url, T_STRING);
  Check_Type(rb_impersonate, T_STRING);

  const char *url = StringValueCStr(rb_url);
  const char *impersonate = StringValueCStr(rb_impersonate);
  long timeout_sec = NUM2LONG(rb_timeout);

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

  struct buffer body, headers;
  buffer_init(&body);
  buffer_init(&headers);

  curl_easy_setopt(handle, CURLOPT_URL, url);
  curl_easy_setopt(handle, CURLOPT_TIMEOUT, timeout_sec);
  curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_body_cb);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, write_header_cb);
  curl_easy_setopt(handle, CURLOPT_HEADERDATA, &headers);

  struct perform_args args = { .handle = handle, .result = CURLE_OK };
  rb_thread_call_without_gvl(perform_without_gvl, &args, RUBY_UBF_IO, NULL);

  if (args.result != CURLE_OK) {
    char errbuf[256];
    strncpy(errbuf, curl_easy_strerror(args.result), sizeof(errbuf) - 1);
    errbuf[sizeof(errbuf) - 1] = '\0';
    buffer_free(&body);
    buffer_free(&headers);
    curl_easy_cleanup(handle);
    rb_raise(eError, "curl_easy_perform failed: %s", errbuf);
  }

  long status_code = 0;
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status_code);

  VALUE rb_body    = rb_str_new(body.data ? body.data : "", body.len);
  VALUE rb_headers = rb_str_new(headers.data ? headers.data : "", headers.len);

  buffer_free(&body);
  buffer_free(&headers);
  curl_easy_cleanup(handle);

  return rb_struct_new(cResponse, LONG2NUM(status_code), rb_body, rb_headers);
}

void Init_curl_impersonate(void) {
  curl_global_init(CURL_GLOBAL_DEFAULT);

  mCurlImpersonate = rb_define_module("CurlImpersonate");
  eError    = rb_const_get(mCurlImpersonate, rb_intern("Error"));
  cResponse = rb_const_get(mCurlImpersonate, rb_intern("Response"));

  rb_define_singleton_method(mCurlImpersonate, "_native_curl_version",
                             rb_cci_native_version, 0);
  rb_define_singleton_method(mCurlImpersonate, "_do_request_native",
                             rb_cci_do_request, 3);
}
