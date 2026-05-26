require "fileutils"
require "rubygems/package"

# Custom native gem builder.
#
# rake-compiler exposes `native:<platform>` tasks only when a cross-Ruby is
# installed (rake-compiler-dock provides one inside its Linux containers).
# For macOS we build *natively* on the matching runner and then package the
# resulting .bundle into a platform-tagged gem ourselves.
#
# This task does the same job inside rake-compiler-dock for Linux too, because
# the cross-Ruby in the dock sees itself as RUBY_PLATFORM=<target-linux> — so
# `rake compile` is just a regular build, and we still need to override the
# gemspec platform to drop development deps + extensions.

CURL_IMPERSONATE_PLATFORMS.each_key do |target_platform|
  namespace :native do
    desc "Compile and package a precompiled gem for #{target_platform}"
    task target_platform.to_sym do
      vendor = File.join(VENDOR_ROOT, target_platform)
      unless File.directory?(vendor)
        raise "vendor/#{target_platform} not populated — run `rake vendor:#{target_platform}` first"
      end

      # Make sure we recompile fresh so the right vendored .a is linked,
      # not whatever happens to be in tmp/ from a previous build.
      Rake::Task["clobber"].invoke
      Rake::Task["compile"].invoke

      binaries = Dir["lib/curl_impersonate/curl_impersonate.{bundle,so}"]
      if binaries.empty?
        raise "no compiled binary found under lib/curl_impersonate/ after rake compile"
      end

      spec = Gem::Specification.load("curl_impersonate.gemspec").dup
      spec.platform   = target_platform
      spec.files      = (spec.files + binaries).uniq
      # Precompiled gem ships the .bundle/.so directly — no extconf.rb run on
      # the end user's machine.
      spec.extensions = []
      # Drop dev deps to keep the published gem lean.
      spec.dependencies.reject! { |d| d.type == :development }

      gem_path = Gem::Package.build(spec)
      FileUtils.mkdir_p("pkg")
      destination = File.join("pkg", File.basename(gem_path))
      FileUtils.mv(gem_path, destination)
      puts "[native:#{target_platform}] built #{destination}"
    end
  end
end

namespace :native do
  desc "Build precompiled gems for every supported platform (requires vendor:all)"
  task all: CURL_IMPERSONATE_PLATFORMS.keys.map { |p| "native:#{p}" }
end
