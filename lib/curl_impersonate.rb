require "curl_impersonate/version"

module CurlImpersonate
  class Error < StandardError; end
end

require "curl_impersonate/response"
require "curl_impersonate/cookies"

# The C extension grabs CurlImpersonate::Error and CurlImpersonate::Response
# via rb_const_get during Init, so both must be defined before this require.
require "curl_impersonate/curl_impersonate"

module CurlImpersonate
  DEFAULT_IMPERSONATE = "chrome131".freeze
  DEFAULT_TIMEOUT_SEC = 15

  def self.do_request(url:,
                      impersonate: DEFAULT_IMPERSONATE,
                      headers: {},
                      post_data: "",
                      follow_redirects: true,
                      timeout_sec: DEFAULT_TIMEOUT_SEC,
                      proxy: "")
    string_headers = headers.each_with_object({}) { |(k, v), h| h[k.to_s] = v.to_s }
    proxy_url, proxy_userpwd = parse_proxy(proxy.to_s)
    _do_request_native(
      url.to_s,
      impersonate.to_s,
      string_headers,
      post_data.to_s,
      follow_redirects ? true : false,
      Integer(timeout_sec),
      proxy_url,
      proxy_userpwd,
    )
  end

  # Split "scheme://user:pass@host:port" into ("scheme://host:port", "user:pass").
  # Returns ("", "") for empty input. If there is no "@" the whole string is
  # treated as the proxy URL with empty auth. Port-only or scheme-less inputs
  # are passed through to libcurl, which has its own defaulting logic.
  def self.parse_proxy(proxy)
    return ["", ""] if proxy.nil? || proxy.empty?

    scheme = ""
    rest = proxy
    if (idx = proxy.index("://"))
      scheme = proxy[0..(idx + 2)]
      rest = proxy[(idx + 3)..]
    end

    if (idx = rest.rindex("@"))
      auth = rest[0...idx]
      host = rest[(idx + 1)..]
      [scheme + host, auth]
    else
      [proxy, ""]
    end
  end
end
