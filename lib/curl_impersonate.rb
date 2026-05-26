require "curl_impersonate/version"

module CurlImpersonate
  class Error < StandardError; end
end

require "curl_impersonate/response"

# The C extension grabs CurlImpersonate::Error and CurlImpersonate::Response
# via rb_const_get during Init, so both must be defined before this require.
require "curl_impersonate/curl_impersonate"

module CurlImpersonate
  DEFAULT_IMPERSONATE = "chrome131".freeze
  DEFAULT_TIMEOUT_SEC = 15

  def self.do_request(url:, impersonate: DEFAULT_IMPERSONATE, timeout_sec: DEFAULT_TIMEOUT_SEC)
    _do_request_native(url.to_s, impersonate.to_s, Integer(timeout_sec))
  end
end
