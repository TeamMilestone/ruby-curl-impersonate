require "bundler/gem_tasks"
require "rake/extensiontask"
require "rspec/core/rake_task"

GEMSPEC = Gem::Specification.load("curl_impersonate.gemspec")

Rake::ExtensionTask.new("curl_impersonate", GEMSPEC) do |ext|
  ext.lib_dir = "lib/curl_impersonate"
end

RSpec::Core::RakeTask.new(:spec)

task default: %i[compile spec]
