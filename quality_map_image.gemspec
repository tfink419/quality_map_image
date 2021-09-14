require './lib/quality_map_image/version'

Gem::Specification.new do |s|
  s.name = %q{quality_map_image}
  s.version = QualityMapImage::VERSION
  s.date = %q{2020-07-10}
  s.summary = %q{Generate an image based on given values at integer representation of coordinates}
  s.files = Dir[
    "lib/**",
    "**/quality_map_image/**",
    "**/quality_map_c/**",
  ]
  s.require_path = "lib"
  s.author = "Tyler Fink"
  s.email = "tfink419@gmail.com"
  s.homepage = "https://tyler.finks.site"
  s.extensions = "ext/quality_map_c/extconf.rb"
  s.required_ruby_version = Gem::Requirement.new(">= 2.6.5")
  s.add_runtime_dependency 'gradient', '~> 0.5.1'
  s.add_runtime_dependency 'ruby-vips'
end