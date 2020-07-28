require "quality_map_c/quality_map_c"
require "quality_map_image/version"
require 'gradient'
require 'ruby-vips'

module QualityMapImage
  METHOD_MAP = {
    "LogExpSum" => 0,
    "First" => 1
  }
      
  GRADIENT_MAP = Gradient::Map.new(
    Gradient::Point.new(0, Color::RGB.new(255, 0, 0), 0.5),
    Gradient::Point.new(0.2, Color::RGB.new(255, 165, 0), 0.5),
    Gradient::Point.new(0.5, Color::RGB.new(255, 255, 0), 0.5),
    Gradient::Point.new(0.8, Color::RGB.new(0, 255, 0), 0.5),
    Gradient::Point.new(1, Color::RGB.new(0, 255, 255), 0.5)
  )

  def self.colorized_quality_image(size, images, image_data)
    QualityMapC::Image.buildImage(size, images, image_data)
  end

  def self.quality_of_points_image(multiply_const, lat, lng, lat_range, lng_range, polygons, quality_scale, quality_calc_method, quality_calc_value)
    QualityMapC::Point.qualityOfPointsImage(multiply_const, lat, lng, lat_range, lng_range, polygons, quality_scale, METHOD_MAP[quality_calc_method], quality_calc_value)
  end

  def self.quality_of_point(lat, lng, polygons, quality_calc_method, quality_calc_value)
    QualityMapC::Point.qualityOfPoint(lat, lng, polygons, METHOD_MAP[quality_calc_method], quality_calc_value)
  end

  def self.colorized_quality_image_ruby(size, images, image_data)
    colors = []

    colors = (0..255).sum([]) do |num|
      color = GRADIENT_MAP.at(num/255.0).color
      [color.red.to_i, color.green.to_i, color.blue.to_i, 128]
    end
    fixed_images = (0...images.length).map do |i|
      image = Vips::Image.new_from_buffer images[i], "", access: :sequential
      bands = image.bandsplit
      bands = bands.each_with_index.map do |band, ind|
        band = (band.cast(:uint) << (8*(3-ind)))
      end
      image = Vips::Image.sum(bands)
      range_low = image_data[i][0]
      range_high = image_data[i][1]
      ratio = image_data[i][2]
      ratio = (colors.length-1).to_f/(range_high-range_low)*ratio
      scale = image_data[i][3]
      invert = image_data[i][4];
      
      lut = Vips::Image.identity
      lut = lut.linear 1/scale, 0-range_low
      lut = (lut < 0).ifthenelse(0, lut)
      lut = (lut > range_high-range_low).ifthenelse(range_high-range_low, lut)
      lut = (range_high-range_low)-lut if invert
      lut = lut * ratio
      image.maplut lut
      image.round(:rint).cast(:uchar)
    end
    image = Vips::Image.sum(fixed_images)
    color_lut = Vips::Image.new_from_array(colors)
    color_lut.Type = :rgba
    pp color_lut.interpretation
    # color_lut.interpretation = :rgba
    image.maplut(color_lut).pngsave_buffer(compression: 9, strip: true)
  end

  # 
  def self.subsample4(size, top_left, top_right, bottom_left, bottom_right)
    v_top_left, v_top_right, v_bottom_left, v_bottom_right =
      [top_left, top_right, bottom_left, bottom_right].map do |image|
      Vips::Image.new_from_buffer image, "", access: :sequential
    end
    top = v_top_left.merge(v_top_right, :horizontal, -size, 0)
    bottom = v_bottom_left.merge(v_bottom_right, :horizontal, -size, 0)
    top.
    merge(bottom, :vertical, 0, -size).
    subsample(2, 2).
    pngsave_buffer(compression: 9, strip: true)
  end
end