require "json"
require "curl_impersonate"

RSpec.configure do |config|
  config.expect_with :rspec do |c|
    c.syntax = :expect
  end
  config.disable_monkey_patching!
  config.order = :random
  Kernel.srand config.seed

  # Network tests are tagged :network. Set CCI_OFFLINE=1 to skip them entirely.
  config.filter_run_excluding(network: true) if ENV["CCI_OFFLINE"] == "1"

  # Proxy test requires CCI_TEST_PROXY env var with a working proxy URL.
  config.filter_run_excluding(proxy: true) unless ENV["CCI_TEST_PROXY"]
end
