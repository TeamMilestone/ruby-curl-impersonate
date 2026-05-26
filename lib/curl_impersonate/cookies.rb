module CurlImpersonate
  # Extracts cookies from a raw HTTP header string (the value of Response#headers).
  # Returns a Hash<String, String>; later Set-Cookie lines with the same name
  # overwrite earlier ones (matches the Go reference implementation).
  #
  # Only the cookie name/value pair is kept — attributes such as Path, Domain,
  # Expires, Secure are discarded.
  def self.extract_cookies(headers_str)
    cookies = {}
    return cookies if headers_str.nil? || headers_str.empty?

    headers_str.split(/\r?\n/).each do |line|
      next unless line.downcase.start_with?("set-cookie:")
      pair = line[("set-cookie:".length)..].split(";", 2).first.to_s.strip
      next if pair.empty?
      name, value = pair.split("=", 2)
      cookies[name] = value.to_s if name && !name.empty?
    end

    cookies
  end
end
