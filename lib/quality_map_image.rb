require "quality_map_c/quality_map_c"
require "quality_map_image/version"

module QualityMapImage
  METHOD_MAP = {
    "LogExpSum" => 0,
    "First" => 1
  }

  def self.get_image(size, images, image_data)
    QualityMapC::Image.buildImage(size, images, image_data)
  end

  def self.quality_of_points_image(multiply_const, lat, lng, lat_range, lng_range, polygons, quality_scale, quality_calc_method, quality_calc_value)
    QualityMapC::Point.qualityOfPointsImage(multiply_const, lat, lng, lat_range, lng_range, polygons, quality_scale, METHOD_MAP[quality_calc_method], quality_calc_value)
  end

  def self.quality_of_point(lat, lng, polygons, quality_calc_method, quality_calc_value)
    QualityMapC::Point.qualityOfPoint(lat, lng, polygons, METHOD_MAP[quality_calc_method], quality_calc_value)
  end
end