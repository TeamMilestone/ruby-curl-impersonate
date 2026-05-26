require "mkmf"
require "shellwords"

# Resolve libcurl-impersonate location.
#
# Priority:
#   1. ENV["CURL_IMPERSONATE_DIR"]                     — explicit override (CI)
#   2. ext/curl_impersonate/vendor/<arch>/             — bundled in precompiled gem
#   3. pkg-config --libs --cflags libcurl-impersonate  — system install (brew/apt)
#
# In all cases we end up statically linking libcurl-impersonate.a so that the
# resulting .bundle / .so has BoringSSL baked in and the end user does not need
# the library installed at runtime.

def configure_from_dir(dir)
  raise "CURL_IMPERSONATE_DIR=#{dir} does not exist" unless File.directory?(dir)

  lib_dir = File.join(dir, "lib")
  include_dir = File.join(dir, "include")

  $LIBPATH.unshift(lib_dir) if File.directory?(lib_dir)
  $CFLAGS << " -I#{include_dir.shellescape}" if File.directory?(include_dir)
  $CFLAGS << " -I#{File.join(include_dir, "curl-impersonate").shellescape}" if File.directory?(File.join(include_dir, "curl-impersonate"))

  static_archive = File.join(lib_dir, "libcurl-impersonate.a")
  unless File.exist?(static_archive)
    raise "libcurl-impersonate.a not found under #{lib_dir}"
  end

  # Link the static archive directly so its symbols (including curl_easy_impersonate)
  # end up in our .bundle / .so.
  $LDFLAGS << " #{static_archive.shellescape}"

  # libcurl-impersonate.a bundles BoringSSL, nghttp{2,3}, ngtcp2, zstd, and
  # brotli statically — but its remaining external dependencies have to be
  # linked separately. The list differs by target platform; we use the
  # vendor directory name as a hint when one is available, otherwise fall
  # back to RUBY_PLATFORM.
  target = File.basename(dir)
  target = RUBY_PLATFORM if target.empty? || target == "vendor"

  case target
  when /darwin/
    # Source: pkg-config --libs libcurl-impersonate on brew installation.
    $LDFLAGS << " -framework CoreFoundation -framework SystemConfiguration"
    $LDFLAGS << " -framework Security -framework LDAP"
    $libs    = "#{$libs} -lresolv -liconv -lz -lc++"
  when /linux/
    # Linux release tarballs are GNU-ABI; idn2 and zstd are dynamic.
    $libs    = "#{$libs} -lz -lidn2 -lzstd -lpthread -ldl -lm -lstdc++"
  end
end

def configure_from_pkg_config
  unless pkg_config("libcurl-impersonate")
    raise "pkg-config could not locate libcurl-impersonate"
  end

  # pkg_config sets $LIBS to "-lcurl-impersonate -lz ...". We want the static .a
  # baked into the .bundle, so prepend the absolute path to the archive and strip
  # the dynamic -lcurl-impersonate flag.
  libdir = `pkg-config --variable=libdir libcurl-impersonate`.strip
  static_archive = File.join(libdir, "libcurl-impersonate.a")
  unless File.exist?(static_archive)
    raise "libcurl-impersonate.a not found at #{static_archive}"
  end

  $libs = $libs.to_s.gsub(/-lcurl-impersonate\b/, "").strip
  $LDFLAGS << " #{static_archive.shellescape}"
end

# Look for vendored libcurl-impersonate under any of these directory names.
# RUBY_PLATFORM on Darwin includes the OS major version ("arm64-darwin25") but
# the rake-compiler / rubygems platform triple does not ("arm64-darwin"), so
# we accept either. The order matters — most-specific wins.
vendor_candidates = [
  RUBY_PLATFORM,                       # arm64-darwin25, x86_64-linux, ...
  RUBY_PLATFORM.sub(/\d+\z/, ""),      # arm64-darwin
  RUBY_PLATFORM.sub(/-gnu\z/, ""),     # x86_64-linux (from x86_64-linux-gnu)
].uniq.map { |t| File.join(__dir__, "vendor", t) }

vendor_arch_dir = vendor_candidates.find { |d| File.directory?(d) }

if (override = ENV["CURL_IMPERSONATE_DIR"]) && !override.empty?
  warn "[curl_impersonate] using CURL_IMPERSONATE_DIR=#{override}"
  configure_from_dir(override)
elsif vendor_arch_dir
  warn "[curl_impersonate] using vendored libcurl-impersonate at #{vendor_arch_dir}"
  configure_from_dir(vendor_arch_dir)
else
  warn "[curl_impersonate] using pkg-config libcurl-impersonate"
  configure_from_pkg_config
end

# Sanity check: ensure we can find the impersonate header. brew installs it
# under curl-impersonate/curl_impersonate.h; some custom layouts may place it
# elsewhere. If missing, fall back to declaring the prototype inline (handled
# in curl_impersonate.c).
have_header("curl-impersonate/curl_impersonate.h")
have_header("curl/curl.h") or abort("curl/curl.h not found — install curl development headers")

create_makefile("curl_impersonate/curl_impersonate")
