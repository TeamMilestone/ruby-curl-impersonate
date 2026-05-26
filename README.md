# curl_impersonate

Ruby bindings for [libcurl-impersonate](https://github.com/lexiforest/curl-impersonate) — generate real-browser TLS / JA3 / JA4 fingerprints from Ruby.

Unlike pure-Ruby HTTP libraries (`net/http`, `httpx`, `faraday`, …) whose TLS handshakes are unmistakably "not a browser," this gem links the same BoringSSL-backed `libcurl-impersonate` build that the `curl-impersonate` project ships, so the TLS ClientHello, HTTP/2 SETTINGS frame, and header ordering are byte-identical to Chrome, Firefox, or Safari.

This is a Ruby port of [`go-curl-impersonate`](https://github.com/TeamMilestone/go-curl-impersonate). The public API matches it 1:1 within Ruby idioms.

## Installation

```ruby
# Gemfile
gem "curl_impersonate"
```

On platforms with a precompiled gem (`arm64-darwin`, `x86_64-linux-gnu`), `gem install` is a single step with no external dependencies — the BoringSSL build is statically linked into the gem's native extension.

On other platforms you will need to build from source; see [Building from source](#building-from-source).

## Quick start

```ruby
require "curl_impersonate"

resp = CurlImpersonate.do_request(
  url: "https://example.com",
  impersonate: "chrome131",
  headers: { "Accept-Language" => "en-US" },
  timeout_sec: 15,
)

resp.status_code  # => 200
resp.success?     # => true
resp.body         # => "<!doctype html>..."
resp.headers      # => "HTTP/2 200\r\ncontent-type: text/html\r\n..."

CurlImpersonate.extract_cookies(resp.headers)  # => { "name" => "value", ... }
```

## API

### `CurlImpersonate.do_request(...)` → `Response`

| keyword | type | default | notes |
|---------|------|---------|-------|
| `url:` | `String` | — | Required. |
| `impersonate:` | `String` | `"chrome131"` | Any target supported by libcurl-impersonate. Examples: `chrome131`, `firefox133`, `safari180`. See the [upstream targets](https://github.com/lexiforest/curl-impersonate#supported-browsers). |
| `headers:` | `Hash<String, String>` | `{}` | Custom headers merged on top of the browser default set. |
| `post_data:` | `String` | `""` | Non-empty switches the request to POST and is sent as the body verbatim. |
| `follow_redirects:` | `Boolean` | `true` | Maps to `CURLOPT_FOLLOWLOCATION`. |
| `timeout_sec:` | `Integer` | `15` | Total request timeout. |
| `proxy:` | `String` | `""` | `"scheme://user:pass@host:port"`. Credentials are split out and passed via `CURLOPT_PROXYUSERPWD`. |

Returns a `CurlImpersonate::Response` Struct with `status_code` (Integer), `body` (String), and `headers` (String — raw `\r\n`-separated lines). On any libcurl error (resolution, timeout, TLS handshake, …) raises `CurlImpersonate::Error`.

`curl_easy_perform` runs without the GVL, so other Ruby threads continue executing during the network wait.

### `CurlImpersonate.extract_cookies(headers_str)` → `Hash`

Parses `Set-Cookie:` lines from a raw header string and returns a `Hash<String, String>` of cookie names → values. Attributes (`Path`, `Domain`, `HttpOnly`, `Expires`, `Secure`, …) are discarded. Later cookies with the same name overwrite earlier ones.

## Building from source

You'll need:

- A C compiler (`clang` / `gcc`) and `make`
- `libcurl-impersonate` 1.5.x installed via one of:
  - **macOS**: `brew install teammilestone/tap/libcurl-impersonate`
  - **Linux**: download the matching tarball from [lexiforest/curl-impersonate releases](https://github.com/lexiforest/curl-impersonate/releases) and copy `libcurl-impersonate.a` plus headers into a system path discoverable by `pkg-config`
  - Or set `CURL_IMPERSONATE_DIR=/path/to/install` (containing `lib/libcurl-impersonate.a` and `include/`) before `gem install`

```bash
gem install curl_impersonate --platform=ruby   # forces source compilation
```

## Caveats

- **SSL certificate verification is disabled** (`CURLOPT_SSL_VERIFYPEER = 0`, `CURLOPT_SSL_VERIFYHOST = 0`). This matches the Go reference implementation. The premise of TLS impersonation is that you are mimicking a browser TLS stack regardless of trust chain validation; if you need real verification, this gem is the wrong tool.
- **JA3 hash varies between requests.** Chrome injects random GREASE values into the cipher and extension lists on every TLS handshake, so the JA3 string and its hash change each time. This is the correct Chrome behavior — match on **JA4** (which sorts before hashing) if you need a stable fingerprint to assert against.
- **Status of this gem**: under active development; API is not stable until 1.0.

## License

MIT — see [LICENSE](LICENSE).
