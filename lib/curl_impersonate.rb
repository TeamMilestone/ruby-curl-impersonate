require "curl_impersonate/version"

module CurlImpersonate
  class Error < StandardError; end
end

# The C extension defines methods on CurlImpersonate, so the module must exist
# before the extension is loaded.
require "curl_impersonate/curl_impersonate"

module CurlImpersonate
  DEFAULT_IMPERSONATE = "chrome131".freeze
  DEFAULT_TIMEOUT_SEC = 15

  # Stage 4: minimal GET — returns only the HTTP status code.
  # Stages 5+ will replace this with full Response objects.
  def self.do_request(url:, impersonate: DEFAULT_IMPERSONATE, timeout_sec: DEFAULT_TIMEOUT_SEC)
    _do_request_native(url.to_s, impersonate.to_s, Integer(timeout_sec))
  end
end
