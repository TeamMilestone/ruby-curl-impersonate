require "bundler/gem_tasks"
require "rake/extensiontask"
require "rspec/core/rake_task"

GEMSPEC = Gem::Specification.load("curl_impersonate.gemspec")

Dir.glob("tasks/*.rake").each { |f| load f }

Rake::ExtensionTask.new("curl_impersonate", GEMSPEC) do |ext|
  ext.lib_dir = "lib/curl_impersonate"

  # Cross-compilation matrix. The actual cross-compilation is driven by
  # `rake native:<platform> gem` running inside rake-compiler-dock; the
  # vendor task makes the right libcurl-impersonate.a available for each
  # target before that runs.
  ext.cross_compile  = true
  ext.cross_platform = CURL_IMPERSONATE_PLATFORMS.keys
  ext.cross_config_options << "--enable-cross-build"

  # Before compilation runs for a given platform, ensure the vendored
  # static archive for that platform is on disk. This is what the
  # GitHub Actions release workflow relies on.
  ext.cross_compiling do |gemspec|
    gemspec.files.reject! { |f| f.start_with?("ext/curl_impersonate/vendor/") }
    gemspec.dependencies.reject! { |d| d.type == :development }
  end
end

RSpec::Core::RakeTask.new(:spec)

task default: %i[compile spec]
