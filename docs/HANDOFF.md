# ruby-curl-impersonate — 구현 핸드오프

## 1. 목표

Go 프로젝트 [`go-curl-impersonate`](https://github.com/TeamMilestone/go-curl-impersonate)와 **동일한 역할**을 하는 Ruby gem을 만든다.

- BoringSSL로 빌드된 **진짜 `libcurl-impersonate`** C 라이브러리를 호출
- 실제 브라우저(Chrome/Firefox/Safari)와 **동일한 TLS/JA3 핑거프린트** 생성
- 순수 Ruby TLS 재구현 라이브러리(`httpx`, `net/http` 등)와는 달리, 미세한 핑거프린트 차이 없음
- 참조 구현 위치: `/Users/wonsup-mini/projects/go-curl-impersonate/curl.go` (256줄, 단일 파일)

## 2. 참조 구현 요약 (Go 버전)

Go 버전은 cgo로 `libcurl-impersonate`를 직접 호출한다. 핵심 요소:

1. `curl_easy_impersonate(CURL*, const char *target, int default_headers)` — 표준 libcurl 헤더에 없는 확장 함수. **반드시 직접 선언**해서 링크해야 함.
2. `WriteMemoryCallback` / `HeaderCallback` — `realloc`으로 응답 바디/헤더를 누적하는 콜백
3. `curl_easy_setopt`로 URL/타임아웃/프록시/SSL 검증 off/HTTP 헤더/POST 바디 설정
4. 프록시 URL 파싱: `http://user:pass@host:port` → `CURLOPT_PROXY`(주소)와 `CURLOPT_PROXYUSERPWD`(인증) 분리
5. macOS 링크: `-L/opt/homebrew/lib -lidn2 -lzstd -licucore`
6. Linux 링크: `-lidn2 -lzstd`
7. `pkg-config: libcurl-impersonate`로 메인 라이브러리 링크
8. 공개 API: `DoRequest(url, proxy, impersonate, headers, postData, followRedirects, timeoutSec) -> (*Response, error)`, `ExtractCookies(headers) map[string]string`

## 3. Ruby에서의 구현 접근 — FFI 권장

두 가지 옵션이 있고, **FFI(ruby-ffi gem)를 권장**한다.

| 옵션 | 장점 | 단점 |
|------|------|------|
| **FFI (권장)** | gem install 시 컴파일러 불필요, 코드가 짧고 읽기 쉬움, Go cgo와 구조가 유사 | 콜백 GC 처리 주의 필요 |
| C extension (`mkmf`) | 약간의 성능 우위, `curb` gem과 동일 방식 | `extconf.rb` 작성, gem install 시 빌드 툴체인 필요, 디버깅 난이도↑ |

**Curb 같은 기존 gem 재활용은 불가**: `curb`는 시스템 libcurl에 링크되어 있고 `curl_easy_impersonate` 심볼이 없음. 직접 바인딩을 만들어야 한다.

## 4. 사전 요구사항 (사용자 시스템)

Go 프로젝트와 동일하다.

```bash
# macOS
brew tap TeamMilestone/tap
brew install libcurl-impersonate libidn2 zstd

# Ubuntu/Debian
apt-get install libidn2-dev libzstd-dev
# libcurl-impersonate는 GitHub releases에서 직접 설치
```

`pkg-config --libs libcurl-impersonate`가 동작해야 함. macOS Homebrew 설치 시 `/opt/homebrew/lib/pkgconfig/`에 `.pc` 파일이 있는지 확인.

## 5. 디렉터리 구조 (생성할 것)

```
ruby-curl-impersonate/
├── docs/
│   └── HANDOFF.md                      # 이 문서
├── lib/
│   ├── curl_impersonate.rb             # 진입점, FFI 로딩 + 공개 API
│   ├── curl_impersonate/
│   │   ├── version.rb
│   │   ├── ffi_bindings.rb             # FFI extern 선언
│   │   ├── response.rb                 # Response 구조체
│   │   └── cookies.rb                  # extract_cookies
├── spec/
│   ├── spec_helper.rb
│   └── curl_impersonate_spec.rb
├── curl_impersonate.gemspec
├── Gemfile
├── Rakefile
├── README.md
└── LICENSE                              # MIT (Go 프로젝트와 동일)
```

## 6. 구체적 작업 항목

### 6.1 gemspec / Gemfile

```ruby
# curl_impersonate.gemspec
Gem::Specification.new do |spec|
  spec.name          = "curl_impersonate"
  spec.version       = CurlImpersonate::VERSION
  spec.authors       = ["TeamMilestone"]
  spec.email         = ["dev@team-milestone.io"]
  spec.summary       = "Ruby bindings for libcurl-impersonate — browser-identical TLS fingerprints"
  spec.homepage      = "https://github.com/TeamMilestone/ruby-curl-impersonate"
  spec.license       = "MIT"
  spec.required_ruby_version = ">= 3.0"

  spec.files = Dir["lib/**/*", "LICENSE", "README.md"]
  spec.require_paths = ["lib"]

  spec.add_dependency "ffi", "~> 1.16"
  spec.add_development_dependency "rspec", "~> 3.12"
end
```

### 6.2 FFI 바인딩 — 핵심 파트

`curl_easy_impersonate`가 표준 헤더에 없는 확장 함수라는 점이 가장 중요한 함정. FFI에서는 그냥 `attach_function`으로 선언하면 동적 로더가 심볼을 찾아준다 (libcurl-impersonate 자체에 심볼이 export되어 있기 때문).

```ruby
# lib/curl_impersonate/ffi_bindings.rb
require "ffi"

module CurlImpersonate
  module FFIBindings
    extend FFI::Library

    # 라이브러리 로딩 — pkg-config 또는 알려진 경로에서
    lib_candidates = [
      "libcurl-impersonate-chrome.4.dylib",  # macOS Homebrew tap 설치 파일명 확인 필요
      "libcurl-impersonate.dylib",
      "libcurl-impersonate.so.4",
      "libcurl-impersonate.so",
      "/opt/homebrew/lib/libcurl-impersonate-chrome.4.dylib",
    ]
    ffi_lib lib_candidates

    # libcurl 표준 함수
    attach_function :curl_global_init,    [:long],            :int
    attach_function :curl_easy_init,      [],                 :pointer
    attach_function :curl_easy_cleanup,   [:pointer],         :void
    attach_function :curl_easy_setopt,    [:pointer, :int, :varargs], :int
    attach_function :curl_easy_perform,   [:pointer],         :int
    attach_function :curl_easy_getinfo,   [:pointer, :int, :varargs], :int
    attach_function :curl_easy_strerror,  [:int],             :string
    attach_function :curl_slist_append,   [:pointer, :string], :pointer
    attach_function :curl_slist_free_all, [:pointer],         :void

    # libcurl-impersonate 확장 함수 — 핵심
    attach_function :curl_easy_impersonate, [:pointer, :string, :int], :int

    # CURLOPT 상수 (curl.h에서 발췌해 직접 정의 — 값은 안정적임)
    CURLOPT_URL              = 10_002
    CURLOPT_WRITEFUNCTION    = 20_011
    CURLOPT_WRITEDATA        = 10_001
    CURLOPT_HEADERFUNCTION   = 20_079
    CURLOPT_HEADERDATA       = 10_029
    CURLOPT_TIMEOUT          = 13
    CURLOPT_ACCEPT_ENCODING  = 10_102
    CURLOPT_SSL_VERIFYPEER   = 64
    CURLOPT_SSL_VERIFYHOST   = 81
    CURLOPT_FOLLOWLOCATION   = 52
    CURLOPT_PROXY            = 10_004
    CURLOPT_PROXYUSERPWD     = 10_006
    CURLOPT_HTTPHEADER       = 10_023
    CURLOPT_POSTFIELDS       = 10_015
    CURLOPT_POSTFIELDSIZE    = 60

    CURLINFO_RESPONSE_CODE   = 0x200002

    CURL_GLOBAL_DEFAULT      = 3
  end
end
```

**주의사항**:
- CURLOPT 값은 `curl.h`에서 발췌. 옵션 종류별로 베이스(10000=문자열 포인터, 20000=함수 포인터)가 정해져 있어 stable함. 단, **실제 빌드 환경에서 한 번은 cross-check** 필요.
- `:varargs`로 `curl_easy_setopt`을 호출할 때 두 번째 인자의 실제 타입(`:long`, `:string`, `:pointer`)을 호출부에서 명시해야 함.

### 6.3 콜백 (write/header)

FFI 콜백은 **반드시 변수로 잡아두고 GC되지 않게** 유지해야 한다. Go의 `realloc` 누적 패턴 대신, Ruby에서는 호출 측이 `String#<<` 또는 `StringIO`로 누적하는 게 자연스럽다.

```ruby
# 콜백 시그니처: size_t (*)(void *contents, size_t size, size_t nmemb, void *userp)
WRITE_CALLBACK = FFI::Function.new(:size_t, [:pointer, :size_t, :size_t, :pointer]) do |contents, size, nmemb, _userp|
  realsize = size * nmemb
  # userp 대신 thread-local이나 Fiber-local로 누적 버퍼에 접근
  buffer = Thread.current[:cci_body_buffer]
  buffer << contents.read_bytes(realsize) if buffer
  realsize
end
```

대안 — 콜백 안에서 `userp`를 ID로 사용:
- `userp`에 `FFI::Pointer.new(:int, id)` 같은 식별자를 넣고, 호출 측에서 ID → 버퍼 매핑을 들고 있으면 멀티스레드 안전.
- 가장 간단한 방식: **요청마다 새 콜백 클로저를 만들고**, 그 클로저가 로컬 `String` 버퍼를 캡처. `do_request` 메서드 안에서 콜백을 생성하면 메서드 종료 시까지 GC되지 않음.

```ruby
body_buffer = +""
write_cb = FFI::Function.new(:size_t, [:pointer, :size_t, :size_t, :pointer]) do |ptr, sz, n, _|
  bytes = sz * n
  body_buffer << ptr.read_bytes(bytes)
  bytes
end
# write_cb는 do_request 끝까지 살아있음 — OK
```

### 6.4 공개 API

Go 버전의 시그니처를 Ruby 관용에 맞게 키워드 인자로 변환:

```ruby
# lib/curl_impersonate.rb
module CurlImpersonate
  Response = Struct.new(:status_code, :body, :headers, keyword_init: true)

  def self.do_request(
    url:,
    proxy: "",
    impersonate: "chrome131",
    headers: {},
    post_data: "",
    follow_redirects: true,
    timeout_sec: 15
  )
    # 1. curl_easy_init
    # 2. curl_easy_impersonate(handle, impersonate, 1)
    # 3. 옵션 세팅 — URL, 콜백, 타임아웃, accept-encoding, SSL_VERIFY 0, FOLLOWLOCATION
    # 4. 프록시 파싱 → CURLOPT_PROXY + CURLOPT_PROXYUSERPWD
    # 5. headers → curl_slist 빌드 → CURLOPT_HTTPHEADER
    # 6. post_data 있으면 CURLOPT_POSTFIELDS
    # 7. curl_easy_perform
    # 8. CURLINFO_RESPONSE_CODE 꺼내서 Response 구성
    # 9. cleanup (slist_free_all, easy_cleanup)
    # 10. CURLE_OK 아니면 raise
  end

  def self.extract_cookies(headers_str)
    cookies = {}
    headers_str.split("\r\n").each do |line|
      next unless line.downcase.start_with?("set-cookie:")
      pair = line[("set-cookie:".length)..].split(";", 2).first.to_s.strip
      k, v = pair.split("=", 2)
      cookies[k] = v if k && v
    end
    cookies
  end
end
```

`curl_global_init`은 모듈 로드 시 한 번만:

```ruby
CurlImpersonate::FFIBindings.curl_global_init(CurlImpersonate::FFIBindings::CURL_GLOBAL_DEFAULT)
```

### 6.5 프록시 URL 파싱

Go 버전 `parseProxyURL` 그대로 포팅:

```ruby
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
```

### 6.6 에러 처리

- `curl_easy_init`이 nil 반환 → `raise CurlImpersonate::Error, "curl_easy_init failed"`
- `curl_easy_impersonate` 결과 != 0 → `raise`
- `curl_easy_perform` 결과 != 0 → `curl_easy_strerror`로 메시지 변환해 `raise`
- 모든 에러는 `CurlImpersonate::Error < StandardError` 하위 클래스로

## 7. 테스트 계획

`spec/curl_impersonate_spec.rb`에 다음 케이스:

1. **기본 GET** — `https://httpbin.org/get` 200 응답, body에 `chrome` 흔적 (User-Agent)
2. **JA3 검증** — `https://tls.peet.ws/api/all` 호출, 응답 JSON의 `ja3_hash`가 Chrome 알려진 해시와 일치
3. **헤더 전달** — `https://httpbin.org/headers`에 커스텀 헤더 보내고 echo 확인
4. **POST** — `https://httpbin.org/post`에 JSON 바디 보내고 `json` 필드 확인
5. **리다이렉트** — `https://httpbin.org/redirect/2` follow=true/false 동작
6. **타임아웃** — `https://httpbin.org/delay/10`에 timeout=2로 호출 → raise
7. **쿠키 파싱** — `https://httpbin.org/cookies/set?a=1&b=2` 호출 후 `extract_cookies` 결과 확인
8. **프록시** (선택, 환경변수 있을 때만) — 인증 포함 프록시로 `https://httpbin.org/ip` 호출

## 8. README에 들어갈 내용

Go 프로젝트의 README를 거의 그대로 차용하되, Ruby 사용 예제로 교체:

```ruby
require "curl_impersonate"

resp = CurlImpersonate.do_request(
  url: "https://example.com",
  impersonate: "chrome131",
  headers: { "Accept-Language" => "en-US" },
  timeout_sec: 15
)

puts resp.status_code
puts resp.body[0, 100]
puts CurlImpersonate.extract_cookies(resp.headers)
```

## 9. 알려진 함정 / 주의사항

1. **`curl_easy_impersonate` 심볼명** — 빌드된 dylib에 실제로 export됐는지 `nm -gU /opt/homebrew/lib/libcurl-impersonate-chrome.4.dylib | grep impersonate`로 확인. 심볼이 없으면 라이브러리 빌드 자체가 문제.
2. **dylib 파일명** — Homebrew tap이 설치하는 정확한 dylib 파일명 확인 필요. `libcurl-impersonate-chrome.4.dylib`일 수도 있고 `libcurl-impersonate.4.dylib`일 수도 있음. `ffi_lib`에 후보 배열로 넘기면 첫 번째로 찾는 것을 사용.
3. **CURLOPT 상수** — `curl.h`에서 추출한 값을 직접 박아넣음. libcurl 메이저 버전 업그레이드 시 값이 바뀔 가능성은 거의 없으나, 7→8 같은 큰 변경에서는 재검증 권장.
4. **`curl_easy_setopt` varargs** — Ruby FFI에서 `:varargs`로 선언한 함수는 호출 시 `[:long, value]`, `[:string, value]`, `[:pointer, value]` 형태로 타입 힌트를 줘야 함. 잘못된 타입을 주면 silent corruption.
5. **콜백 GC** — `FFI::Function`은 변수로 잡아 메서드 스코프 동안 살려둘 것. 즉시 GC되면 segfault.
6. **`curl_slist`** — 빈 리스트는 NULL. 첫 `curl_slist_append(NULL, "...")`는 새 리스트를 만들어 반환. 항상 첫 인자에 이전 반환값을 다시 넘기는 누적 패턴.
7. **메모리** — FFI에서는 `read_bytes`로 콜백 데이터를 즉시 Ruby String으로 복사하므로, C쪽 메모리 수명은 콜백 호출 동안만 신경 쓰면 됨. Go 버전의 `free_response`에 해당하는 작업은 불필요 (libcurl 내부에서 처리).
8. **스레드 안전성** — `curl_easy_*`는 핸들 단위로 스레드 안전. 모듈 메서드는 매 호출마다 새 핸들을 만들므로 OK. 단 콜백이 클로저로 버퍼를 캡처하는 패턴이라 호출 간 독립적 — 문제없음.
9. **CURLOPT_SSL_VERIFYPEER/HOST = 0** — Go 버전이 SSL 검증을 끄고 있음. 핑거프린트 위장이 목적이라 의도된 설정이지만, README에 명시할 것.

## 10. 작업 순서 추천

1. `bundle init` → gemspec/Gemfile 작성
2. `lib/curl_impersonate/version.rb`, `ffi_bindings.rb` 작성 — **먼저 라이브러리 로딩과 `curl_easy_impersonate` 심볼 해석이 되는지 확인** (간단한 init/cleanup 스크립트로)
3. 가장 짧은 GET 한 번 통과시키기 (콜백 없이 응답 코드만)
4. write 콜백 붙여서 바디 수신
5. header 콜백 추가
6. 헤더/POST/프록시/타임아웃 옵션 추가
7. `extract_cookies` 구현
8. RSpec 스위트 작성
9. README + LICENSE(MIT) 추가
10. `rake build` → `gem push`로 RubyGems 배포 (선택)

## 11. 참고 링크

- Go 참조 구현: `/Users/wonsup-mini/projects/go-curl-impersonate/curl.go`
- 상위 라이브러리: https://github.com/lexiforest/curl-impersonate
- 동일 접근의 Python 바인딩: https://github.com/lexiforest/curl_cffi (구현 참고용으로 매우 유용 — Python `cffi`도 FFI와 패턴이 비슷)
- ruby-ffi 문서: https://github.com/ffi/ffi/wiki
- libcurl 옵션 레퍼런스: https://curl.se/libcurl/c/curl_easy_setopt.html
