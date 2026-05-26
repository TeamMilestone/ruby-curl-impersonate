require "spec_helper"

RSpec.describe CurlImpersonate do
  describe ".parse_proxy" do
    it "returns empty pair for empty input" do
      expect(described_class.parse_proxy("")).to eq ["", ""]
      expect(described_class.parse_proxy(nil)).to eq ["", ""]
    end

    it "passes through a proxy URL without credentials" do
      expect(described_class.parse_proxy("http://proxy.example.com:8080"))
        .to eq ["http://proxy.example.com:8080", ""]
    end

    it "splits user:pass@host into proxy and proxy_userpwd" do
      expect(described_class.parse_proxy("http://u:p@proxy.example.com:8080"))
        .to eq ["http://proxy.example.com:8080", "u:p"]
    end

    it "handles credentials without a password" do
      expect(described_class.parse_proxy("socks5://user@host:1080"))
        .to eq ["socks5://host:1080", "user"]
    end

    it "passes scheme-less proxy strings through verbatim" do
      expect(described_class.parse_proxy("proxy.example.com:8080"))
        .to eq ["proxy.example.com:8080", ""]
    end
  end

  describe ".extract_cookies" do
    it "returns an empty hash when no Set-Cookie lines are present" do
      expect(described_class.extract_cookies("HTTP/2 200\r\nContent-Type: text/html\r\n"))
        .to eq({})
    end

    it "parses multiple cookies and discards attributes" do
      headers = "Set-Cookie: a=1\r\nSet-Cookie: b=2; Path=/; HttpOnly\r\n"
      expect(described_class.extract_cookies(headers)).to eq({ "a" => "1", "b" => "2" })
    end

    it "matches Set-Cookie case-insensitively" do
      expect(described_class.extract_cookies("set-cookie: foo=bar\r\n"))
        .to eq({ "foo" => "bar" })
    end

    it "keeps the last value when the same name appears multiple times" do
      headers = "Set-Cookie: foo=bar\r\nSet-Cookie: foo=baz\r\n"
      expect(described_class.extract_cookies(headers)).to eq({ "foo" => "baz" })
    end

    it "preserves cookies with empty values" do
      expect(described_class.extract_cookies("Set-Cookie: empty=\r\n"))
        .to eq({ "empty" => "" })
    end
  end

  describe "._native_curl_version" do
    it "reports the BoringSSL impersonate build" do
      expect(described_class._native_curl_version).to include("IMPERSONATE")
      expect(described_class._native_curl_version).to include("BoringSSL")
    end
  end

  describe ".do_request", :network do
    it "performs a basic GET" do
      r = described_class.do_request(url: "https://httpbin.org/get")
      expect(r).to be_a(CurlImpersonate::Response)
      expect(r.status_code).to eq 200
      expect(r).to be_success
      expect(r.body).not_to be_empty
      expect(r.headers).to include("content-type:").or include("Content-Type:")
    end

    it "echoes custom request headers" do
      r = described_class.do_request(
        url: "https://httpbin.org/headers",
        headers: { "X-Foo" => "bar", "Accept-Language" => "ko-KR" },
      )
      expect(r.status_code).to eq 200
      echoed = JSON.parse(r.body)["headers"]
      expect(echoed["X-Foo"]).to eq "bar"
      expect(echoed["Accept-Language"]).to eq "ko-KR"
    end

    it "sends form-encoded POST bodies" do
      r = described_class.do_request(
        url: "https://httpbin.org/post",
        post_data: "hello=world&n=42",
        headers: { "Content-Type" => "application/x-www-form-urlencoded" },
      )
      expect(r.status_code).to eq 200
      form = JSON.parse(r.body)["form"]
      expect(form).to eq({ "hello" => "world", "n" => "42" })
    end

    it "follows redirects when follow_redirects is true" do
      r = described_class.do_request(url: "https://httpbin.org/redirect/2", follow_redirects: true)
      expect(r.status_code).to eq 200
    end

    it "returns the redirect response when follow_redirects is false" do
      r = described_class.do_request(url: "https://httpbin.org/redirect/2", follow_redirects: false)
      expect([301, 302, 303, 307, 308]).to include(r.status_code)
    end

    it "raises when the server takes longer than timeout_sec" do
      expect {
        described_class.do_request(url: "https://httpbin.org/delay/10", timeout_sec: 2)
      }.to raise_error(CurlImpersonate::Error, /Timeout/)
    end

    it "captures Set-Cookie headers that extract_cookies can parse" do
      r = described_class.do_request(url: "https://httpbin.org/cookies/set?a=1&b=2")
      cookies = described_class.extract_cookies(r.headers)
      expect(cookies).to include("a" => "1", "b" => "2")
    end

    it "raises a CurlImpersonate::Error on unresolvable hosts" do
      expect {
        described_class.do_request(
          url: "https://this-host-does-not-exist-xyzzy.invalid",
          timeout_sec: 5,
        )
      }.to raise_error(CurlImpersonate::Error)
    end

    it "raises when given an unknown impersonate target" do
      expect {
        described_class.do_request(url: "https://httpbin.org/get", impersonate: "totally-fake-999")
      }.to raise_error(CurlImpersonate::Error, /curl_easy_impersonate/)
    end

    it "generates a Chrome 131 TLS fingerprint" do
      r = described_class.do_request(url: "https://tls.peet.ws/api/all", impersonate: "chrome131")
      expect(r.status_code).to eq 200
      data = JSON.parse(r.body)

      expect(data["user_agent"]).to include("Chrome/131")

      # JA4 sorts ciphers/extensions before hashing, so it is stable across
      # calls. JA3 is NOT stable because Chrome injects random GREASE values
      # into the cipher and extension lists on every connection — this is the
      # actual Chrome behavior we want to mimic, so the variability is correct.
      expect(data.dig("tls", "ja4")).to eq "t13d1516h2_8daaf6152771_02713d6af862"

      # Sanity-check the JA3 raw string has the right shape even though the
      # hash varies. Format: SSLVersion,Ciphers,Extensions,EllipticCurves,EllipticCurvePointFormats
      expect(data.dig("tls", "ja3")).to match(/\A\d+,(\d+-)*\d+,(\d+-)*\d+,(\d+-)*\d+,(\d+-)*\d+\z/)
    end

    context "with a working proxy", :proxy do
      it "routes the request through the configured proxy" do
        r = described_class.do_request(
          url: "https://httpbin.org/ip",
          proxy: ENV.fetch("CCI_TEST_PROXY"),
          timeout_sec: 10,
        )
        expect(r.status_code).to eq 200
      end
    end
  end
end
