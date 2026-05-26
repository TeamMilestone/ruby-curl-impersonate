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

## 3. Ruby에서의 구현 접근 — C extension (mkmf) 채택

**결정 (2026-05-27)**: C extension으로 구현. 이유는 업스트림이 정적 라이브러리(`.a`)만 배포하기 때문.

당초 FFI(ruby-ffi)가 더 짧고 읽기 쉬워 1순위 후보였지만, 실제 환경 검증에서 다음이 확인됐다:

- `libcurl-impersonate` 업스트림(lexiforest) macOS arm64 릴리스 tarball 내용:
  ```
  libcurl-impersonate.a
  libcurl-impersonate.la
  libngtcp2_crypto_ossl.a
  ```
- `.dylib`(또는 `.so`)이 아예 없음 — BoringSSL을 정적 링크하기 때문에 의도된 설계로 추정
- Ruby FFI는 `dlopen`으로 동적 라이브러리만 로드 가능 → 사용 불가
- Go 버전은 cgo로 `.a`를 정적 링크하므로 작동. Ruby에서 같은 결과를 얻으려면 mkmf로 C extension을 빌드해야 함

| 옵션 | 채택 여부 | 비고 |
|------|----------|------|
| FFI (ruby-ffi) | ❌ | 업스트림에 dylib 없음 |
| **C extension (mkmf)** | ✅ | Go cgo와 1:1 대응, `.a` 정적 링크 |
| 기존 `curb` 재활용 | ❌ | 시스템 libcurl에 링크되어 `curl_easy_impersonate` 심볼 없음 |

### 3.1 배포 전략 — A2: precompiled platform gems

사용자가 `brew install`을 선행하지 않아도 되도록, RubyGems에 플랫폼별 precompiled gem을 publish한다. `nokogiri`/`sqlite3` 방식.

- **native gem** (소스 gem): `extconf.rb`가 빌드 시점에 `pkg-config --libs libcurl-impersonate` 또는 vendored `.a`를 찾아 링크. 개발자/특수 플랫폼용
- **platform gems**: `rake-compiler-dock`으로 CI에서 미리 빌드 → 4종(`arm64-darwin`, `x86_64-darwin`, `aarch64-linux`, `x86_64-linux`)
- 빌드 시 lexiforest 업스트림 tarball을 다운로드해 `.a` 추출 → 정적 링크 → `.bundle`/`.so`를 gem에 포함

사용자 입장에서 `gem install curl_impersonate`는 zero-dependency (RubyGems가 자동으로 매칭 플랫폼 gem을 받음).

## 4. 사전 요구사항

### 4.1 일반 사용자 (precompiled gem 사용 시)
- 없음. `gem install curl_impersonate`만 실행하면 됨.

### 4.2 소스 gem 빌드 시 (개발자 / 지원되지 않는 플랫폼)
- 컴파일러 (gcc/clang) + make
- 다음 둘 중 하나:
  - 시스템에 `libcurl-impersonate` 설치 (`brew install teammilestone/tap/libcurl-impersonate` 등) → `pkg-config --libs libcurl-impersonate` 동작
  - 또는 환경변수 `CURL_IMPERSONATE_DIR`로 `.a`와 헤더 디렉터리 직접 지정

### 4.3 CI에서 precompiled gem 빌드 시
- `rake-compiler-dock` Docker 환경 (Linux 크로스 컴파일)
- macOS 빌드는 macOS 러너에서 직접

## 5. 디렉터리 구조

```
ruby-curl-impersonate/
├── docs/
│   └── HANDOFF.md                      # 이 문서
├── ext/
│   └── curl_impersonate/
│       ├── extconf.rb                  # mkmf: pkg-config / vendored .a 분기
│       ├── curl_impersonate.c          # C 래퍼 (do_request, callbacks)
│       └── vendor/                     # CI에서 다운로드한 업스트림 .a (gitignored)
├── lib/
│   ├── curl_impersonate.rb             # Ruby 진입점, require ext + 공개 API
│   └── curl_impersonate/
│       ├── version.rb
│       ├── curl_impersonate.bundle     # 컴파일된 ext (gitignored, rake-compiler 출력)
│       ├── response.rb                 # Response 구조체
│       └── cookies.rb                  # extract_cookies (Ruby)
├── spec/
│   ├── spec_helper.rb
│   └── curl_impersonate_spec.rb
├── .github/workflows/
│   └── release.yml                     # rake-compiler-dock 매트릭스 빌드
├── curl_impersonate.gemspec            # spec.extensions = [extconf.rb]
├── Gemfile
├── Rakefile                            # Rake::ExtensionTask + cross-compile
├── README.md
└── LICENSE                             # MIT (Go 프로젝트와 동일)
```

## 6. 구체적 작업 항목

### 6.1 gemspec / Gemfile / Rakefile

`spec.extensions = ["ext/curl_impersonate/extconf.rb"]`가 핵심. `gem install` 시 RubyGems가 자동으로 `extconf.rb` → `make`를 호출한다. precompiled 플랫폼 gem은 이 단계를 건너뛰고 빌드된 `.bundle`/`.so`를 그대로 사용.

Rakefile은 `Rake::ExtensionTask`로 `rake compile`을 제공. 빌드 산출물은 `lib/curl_impersonate/curl_impersonate.{bundle,so}`로 들어간다.

### 6.2 ext/curl_impersonate/extconf.rb

탐색 순서:

1. 환경변수 `CURL_IMPERSONATE_DIR`이 있으면 그걸 사용 (CI cross-compile용)
2. `ext/curl_impersonate/vendor/<arch>/` 디렉터리에 `.a`가 있으면 사용 (precompiled gem 빌드용)
3. `pkg-config --libs --cflags libcurl-impersonate`로 시스템 설치 발견 시 사용
4. 모두 실패 → 친절한 에러 메시지로 abort

링크: `-lcurl-impersonate -lz -liconv` + macOS 시스템 프레임워크 (`CoreFoundation`, `Security`, `SystemConfiguration`, `LDAP`, `resolv`, `c++`)
헤더: `curl-impersonate/curl_impersonate.h`(brew 설치 시) 또는 직접 `curl_easy_impersonate` 외부 선언

### 6.3 ext/curl_impersonate/curl_impersonate.c — Go 버전 1:1 포팅

Go `curl.go`의 구조를 거의 그대로 가져온다:

- `WriteMemoryCallback` / `HeaderCallback`: realloc 기반 누적 (Go 버전 동일)
- `do_request_impl(url, proxy, impersonate, headers, post_data, follow, timeout)`:
  1. `curl_easy_init`
  2. `curl_easy_impersonate(handle, target, 1)`
  3. `curl_easy_setopt` 시리즈
  4. 프록시 분리 → `CURLOPT_PROXY` + `CURLOPT_PROXYUSERPWD`
  5. headers Hash → `curl_slist_append` 반복 → `CURLOPT_HTTPHEADER`
  6. `curl_easy_perform`
  7. `curl_easy_getinfo(CURLINFO_RESPONSE_CODE)`
  8. `rb_struct_new`으로 `Response` 만들어 반환
  9. cleanup (slist_free_all, easy_cleanup, free buffers)
- `Init_curl_impersonate`: `rb_define_module("CurlImpersonate")` + `rb_define_singleton_method(..., "_do_request_native", ...)` 

C ↔ Ruby 변환은 `rb_str_new`, `rb_hash_foreach`, `rb_check_string_type` 등 표준 API 사용. GVL은 `curl_easy_perform` 동안 해제(`rb_thread_call_without_gvl`) — 다른 스레드가 계속 진행 가능.

### 6.4 공개 API (Ruby)

Ruby 측은 얇은 래퍼만 — 모든 무거운 작업은 C에서 한다.

```ruby
# lib/curl_impersonate.rb
require "curl_impersonate/version"
require "curl_impersonate/curl_impersonate"  # 컴파일된 ext
require "curl_impersonate/response"
require "curl_impersonate/cookies"

module CurlImpersonate
  class Error < StandardError; end

  def self.do_request(url:, proxy: "", impersonate: "chrome131",
                      headers: {}, post_data: "",
                      follow_redirects: true, timeout_sec: 15)
    _do_request_native(url, proxy, impersonate, headers, post_data,
                       follow_redirects, timeout_sec)
  end
end
```

`Response`는 `lib/curl_impersonate/response.rb`에서 Ruby Struct로 정의. C 측에서 `rb_struct_new`로 인스턴스 생성.

### 6.5 프록시 URL 파싱

Go `parseProxyURL`을 C로 포팅 (호출이 자주 일어나지 않으므로 Ruby로 해도 무방). 입력 `http://user:pass@host:port` → `("http://host:port", "user:pass")` 분리.

### 6.6 에러 처리

- C 측에서 모든 에러를 `rb_raise(rb_eCurlImpersonateError, ...)`로 던짐
- `curl_easy_init` 실패, `curl_easy_impersonate` 실패, `curl_easy_perform` 실패(`curl_easy_strerror` 메시지 첨부) 등
- Ruby 측에 `CurlImpersonate::Error < StandardError` 정의, C에서 `rb_const_get`으로 가져와 사용

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

1. **업스트림 정적 라이브러리만 배포** — `.a`만 존재. FFI 불가, C extension 필수. 이게 가장 큰 함정이었고 사전 환경 검증 단계에서 발견됨.
2. **`curl_easy_impersonate` 헤더 부재** — 표준 `curl/curl.h`에 없음. `brew install` 시 brew formula가 `curl-impersonate/curl_impersonate.h`를 직접 작성해 설치함. C extension에서는 그 헤더를 include하거나, 안 되면 직접 `extern CURLcode curl_easy_impersonate(CURL *, const char *, int);` 선언.
3. **링크 의존성** — `pkg-config --libs libcurl-impersonate` 결과를 참조. macOS는 `-lz -liconv -framework CoreFoundation -framework SystemConfiguration -framework Security -framework LDAP -lresolv -lc++` 필요. Linux는 `-lz -lidn2 -lzstd` 등.
4. **`curl_easy_setopt` 가변인자** — C에서 호출 시 옵션별로 정확한 타입(long, char *, void *, function pointer)을 넘겨야 함. 잘못 넘기면 silent corruption.
5. **`curl_slist`** — 빈 리스트는 NULL. 첫 `curl_slist_append(NULL, "...")`는 새 리스트를 만들어 반환. 항상 이전 반환값을 다시 넘기는 누적 패턴.
6. **CURLOPT_SSL_VERIFYPEER/HOST = 0** — Go 버전이 SSL 검증을 끔. 핑거프린트 위장이 목적이라 의도된 설정이지만, README에 명시.
7. **GVL 해제** — `curl_easy_perform`은 블로킹 네트워크 호출. `rb_thread_call_without_gvl`로 감싸지 않으면 다른 Ruby 스레드가 멈춤. C 콜백에서는 GVL 재획득(`rb_thread_call_with_gvl`)이 필요할 수 있으나, callback 안에서 Ruby 객체를 만들지 않고 단순 realloc 누적만 한다면 불필요.
8. **메모리** — C realloc 버퍼는 함수 끝에서 `free`. 응답이 Ruby `String`으로 복사되면 C 측 메모리는 즉시 해제.
9. **rake-compiler-dock과 정적 .a 다운로드** — cross-compile 컨테이너 안에서 lexiforest 업스트림 tarball을 다운로드해 `ext/curl_impersonate/vendor/<arch>/libcurl-impersonate.a`로 추출. 플랫폼별로 다른 tarball을 받아야 하므로 Rake 태스크에 분기 필요.
10. **gemspec `spec.files`에 vendor/ 제외** — 다운로드한 `.a`는 빌드 산출물이지 소스 아님. `.gitignore`에 vendor/ 추가, gemspec `Dir[]` 패턴에서도 제외.

## 10. 작업 순서

`docs/HANDOFF.md` 외부에서 추적 (Task tool 등). 큰 흐름:

1. 저장소/저작권/원격 ✓
2. gem 골격 (gemspec + Rakefile + version) ✓
3. 이 문서 업데이트 (이 단계)
4. C extension skeleton — `extconf.rb`, `Init_curl_impersonate`만, rake compile 통과
5. 최소 GET (status code만)
6. write/header 콜백 + Response struct
7. 옵션 풀세트 (headers/POST/timeout/redirects/SSL/encoding)
8. 프록시
9. `extract_cookies` (Ruby)
10. RSpec 스위트 + 실제 네트워크 테스트
11. README 완성
12. A2 precompiled gem 인프라 (`rake-compiler-dock` + GitHub Actions matrix)
13. RubyGems 배포 (별도)

## 11. 참고 링크

- Go 참조 구현: `/Users/wonsup-mini/projects/go-curl-impersonate/curl.go`
- 상위 라이브러리: https://github.com/lexiforest/curl-impersonate
- 동일 접근의 Python 바인딩: https://github.com/lexiforest/curl_cffi (구현 참고용으로 매우 유용)
- mkmf 가이드: https://docs.ruby-lang.org/en/master/MakeMakefile.html
- rake-compiler / rake-compiler-dock: https://github.com/rake-compiler/rake-compiler-dock
- libcurl 옵션 레퍼런스: https://curl.se/libcurl/c/curl_easy_setopt.html
