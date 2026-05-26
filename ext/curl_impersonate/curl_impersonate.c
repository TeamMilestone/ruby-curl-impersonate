#include <ruby.h>
#include <ruby/thread.h>
#include <curl/curl.h>
#include <string.h>

#ifdef HAVE_CURL_IMPERSONATE_CURL_IMPERSONATE_H
#  include <curl-impersonate/curl_impersonate.h>
#else
/* Header not installed on this system — declare the prototype ourselves.
 * The symbol is exported by libcurl-impersonate.a regardless. */
extern CURLcode curl_easy_impersonate(CURL *curl, const char *target, int default_headers);
#endif

static VALUE mCurlImpersonate;
static VALUE eError;

/* Discard write callback — drops the body. Used in stage 4 before we wire up
 * proper buffer accumulation in stage 5. */
static size_t discard_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
  (void)ptr; (void)userdata;
  return size * nmemb;
}

/* Arguments passed into the GVL-free perform call. */
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

/* Signature (stage 4 — minimal):
 *   _do_request_native(url, impersonate, timeout_sec) -> Integer (HTTP status code)
 *
 * Body, headers, redirects, proxy, POST etc. are added in later stages.
 * SSL verification is disabled (matches Go reference implementation — the
 * point of impersonation is to mimic browser TLS regardless of trust chain). */
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

  curl_easy_setopt(handle, CURLOPT_URL, url);
  curl_easy_setopt(handle, CURLOPT_TIMEOUT, timeout_sec);
  curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, discard_write_cb);

  struct perform_args args = { .handle = handle, .result = CURLE_OK };
  rb_thread_call_without_gvl(perform_without_gvl, &args, RUBY_UBF_IO, NULL);

  if (args.result != CURLE_OK) {
    /* Copy the error string before cleanup invalidates it. */
    char errbuf[256];
    strncpy(errbuf, curl_easy_strerror(args.result), sizeof(errbuf) - 1);
    errbuf[sizeof(errbuf) - 1] = '\0';
    curl_easy_cleanup(handle);
    rb_raise(eError, "curl_easy_perform failed: %s", errbuf);
  }

  long status_code = 0;
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status_code);

  curl_easy_cleanup(handle);

  return LONG2NUM(status_code);
}

void Init_curl_impersonate(void) {
  curl_global_init(CURL_GLOBAL_DEFAULT);

  mCurlImpersonate = rb_define_module("CurlImpersonate");
  eError = rb_const_get(mCurlImpersonate, rb_intern("Error"));

  rb_define_singleton_method(mCurlImpersonate, "_native_curl_version",
                             rb_cci_native_version, 0);
  rb_define_singleton_method(mCurlImpersonate, "_do_request_native",
                             rb_cci_do_request, 3);
}
