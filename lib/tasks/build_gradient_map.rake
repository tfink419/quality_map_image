require 'gradient'

task :build => %s(build_gradient_map)

task :build_gradient_map do
  GRADIENT_MAP = Gradient::Map.new(
    Gradient::Point.new(0, Color::RGB.new(255, 0, 0), 0.5),
    Gradient::Point.new(0.2, Color::RGB.new(255, 165, 0), 0.5),
    Gradient::Point.new(0.5, Color::RGB.new(255, 255, 0), 0.5),
    Gradient::Point.new(0.8, Color::RGB.new(0, 255, 0), 0.5),
    Gradient::Point.new(1, Color::RGB.new(0, 255, 255), 0.5)
  )
  colors = []

  (0..100).each do |num|
    color = GRADIENT_MAP.at(num/100.0).color
    colors << [color.red.to_i, color.green.to_i, color.blue.to_i]
  end
  output = "#include \"gradient.hpp\""
  output += "\n#define GRADIENT_MAP_SIZE_PRE #{colors.length}"
  output += "\nconst int GRADIENT_MAP_SIZE = GRADIENT_MAP_SIZE_PRE;"
  output += "\nconst int GRADIENT_MAP[GRADIENT_MAP_SIZE_PRE][3] = {"+colors.map{ |color| "{#{color.join(",")}}"}.join(",")+"};"

  open('ext/quality_map_c/gradient.cpp', 'w') { |f|
    f.puts output
  }
end