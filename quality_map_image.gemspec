Gem::Specification.new do |s|
  s.name = %q{quality_map_image}
  s.version = "0.0.4"
  s.date = %q{2020-07-10}
  s.summary = %q{Generate an image based on given values at integer representation of coordinates}
  s.files = Dir[
    "lib/**",
    "**/quality_map_c/**",
  ]
  s.require_path = "lib"
  s.author = "Tyler Fink"
  s.email = "tfink419@gmail.com"
  s.homepage = "https://tyler.finks.site"
  s.extensions = "ext/quality_map_c/extconf.rb"
end