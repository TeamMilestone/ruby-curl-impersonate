require_relative "lib/curl_impersonate/version"

Gem::Specification.new do |spec|
  spec.name          = "curl_impersonate"
  spec.version       = CurlImpersonate::VERSION
  spec.authors       = ["TeamMilestone"]
  spec.email         = ["alfonso@team-milestone.io"]

  spec.summary       = "Ruby bindings for libcurl-impersonate — browser-identical TLS fingerprints"
  spec.description   = "Generate real-browser TLS/JA3 fingerprints from Ruby by calling the BoringSSL-backed libcurl-impersonate C library."
  spec.homepage      = "https://github.com/TeamMilestone/ruby-curl-impersonate"
  spec.license       = "MIT"
  spec.required_ruby_version = ">= 3.0"

  spec.metadata["homepage_uri"]    = spec.homepage
  spec.metadata["source_code_uri"] = spec.homepage
  spec.metadata["bug_tracker_uri"] = "#{spec.homepage}/issues"

  spec.files = Dir[
    "lib/**/*.rb",
    "ext/**/*.{c,h,rb}",
    "LICENSE",
    "README.md",
    "CHANGELOG.md"
  ]
  spec.require_paths = ["lib"]
  spec.extensions    = ["ext/curl_impersonate/extconf.rb"]

  spec.add_development_dependency "rake", "~> 13.0"
  spec.add_development_dependency "rake-compiler", "~> 1.2"
  spec.add_development_dependency "rake-compiler-dock", "~> 1.5"
  spec.add_development_dependency "rspec", "~> 3.12"
end
