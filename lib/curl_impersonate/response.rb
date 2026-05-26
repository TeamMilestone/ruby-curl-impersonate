module CurlImpersonate
  Response = Struct.new(:status_code, :body, :headers) do
    def success?
      (200..299).cover?(status_code)
    end
  end
end
