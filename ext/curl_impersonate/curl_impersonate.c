#include <ruby.h>
#include <curl/curl.h>

#ifdef HAVE_CURL_IMPERSONATE_CURL_IMPERSONATE_H
#  include <curl-impersonate/curl_impersonate.h>
#else
/* Header not installed on this system — declare the prototype ourselves.
 * The symbol is exported by libcurl-impersonate.a regardless. */
extern CURLcode curl_easy_impersonate(CURL *curl, const char *target, int default_headers);
#endif

static VALUE mCurlImpersonate;

/* Forward declarations for upcoming stages. */
static VALUE rb_cci_native_version(VALUE self) {
  return rb_str_new_cstr(curl_version());
}

void Init_curl_impersonate(void) {
  /* Initialize libcurl globally once when the extension is loaded.
   * Safe to call multiple times per libcurl docs but we only need this once. */
  curl_global_init(CURL_GLOBAL_DEFAULT);

  mCurlImpersonate = rb_define_module("CurlImpersonate");
  rb_define_singleton_method(mCurlImpersonate, "_native_curl_version",
                             rb_cci_native_version, 0);
}
