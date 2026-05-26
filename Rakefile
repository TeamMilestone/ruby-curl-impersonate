require "bundler/gem_tasks"
require "rake/extensiontask"
require "rspec/core/rake_task"

GEMSPEC = Gem::Specification.load("curl_impersonate.gemspec")

# tasks/vendor.rake defines CURL_IMPERSONATE_PLATFORMS and VENDOR_ROOT, which
# tasks/native_gem.rake depends on. Load order matters; we do it explicitly
# rather than relying on lexical Dir.glob sort (which puts native_gem before
# vendor).
load "tasks/vendor.rake"
load "tasks/native_gem.rake"

Rake::ExtensionTask.new("curl_impersonate", GEMSPEC) do |ext|
  ext.lib_dir = "lib/curl_impersonate"
  # NOTE: we intentionally do *not* set ext.cross_compile / cross_platform.
  # rake-compiler's cross_compile machinery expects a pre-built cross Ruby for
  # every target, which we do not maintain. Per-platform packaging is handled
  # by the custom `native:<platform>` tasks defined in tasks/native_gem.rake.
end

RSpec::Core::RakeTask.new(:spec)

task default: %i[compile spec]
