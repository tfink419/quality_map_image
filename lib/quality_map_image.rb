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

  def self.quality_of_points_image(multiply_const, lat, lng, range, polygons, quality_scale, quality_calc_method, quality_calc_value)
    QualityMapC::Point.qualityOfPointsImage(multiply_const, lat, lng, range, polygons, quality_scale, METHOD_MAP[quality_calc_method], quality_calc_value)
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
      if image
        image = bandjoin(Vips::Image.new_from_buffer images[i], "", access: :sequential)
        range_low = image_data[i][0]
        range_high = image_data[i][1]
        ratio = image_data[i][2]
        ratio = (colors.length-1).to_f/(range_high-range_low)*ratio
        scale = image_data[i][3]
        invert = image_data[i][4]
        
        image = image.linear 1/scale, 0-range_low
        image = (image < 0).ifthenelse(0, image)
        image = (image > range_high-range_low).ifthenelse(range_high-range_low, lut)
        image = (range_high-range_low)-image if invert
        image = image * ratio
        image.round(:rint).cast(:uchar)
      else
        Vips::Image.black(size, size)
      end
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
    arr = [top_left, top_right, bottom_left, bottom_right]
    return nil if !arr.any?
    v_top_left, v_top_right, v_bottom_left, v_bottom_right =
      arr.map do |image|
      if image
        Vips::Image.new_from_buffer image, "", access: :sequential
      else
        Vips::Image.black(size, size, bands:4)
      end
    end
    top = v_top_left.merge(v_top_right, :horizontal, -size, 0)
    bottom = v_bottom_left.merge(v_bottom_right, :horizontal, -size, 0)
    top.
      merge(bottom, :vertical, 0, -size).
      subsample(2, 2).
      pngsave_buffer(compression: 9, strip: true)
  end

  # This is just test code, the real use case is in C
  def self.clean_zeros(image_blob)
    image = bandjoin(Vips::Image.new_from_buffer image_blob, "", access: :sequential)
    median_ranked = image.median(2)
    image = (image == 0).ifthenelse(median_ranked, image)
    split_into_bands(image).
      pngsave_buffer(compression: 9, strip: true)
  end

  private

  def self.bandjoin(image)
    image.copy(format: :uint, bands: 1)
  end

  def self.split_into_bands(image)
    image.copy(format: :uchar, bands: 4)
  end
end