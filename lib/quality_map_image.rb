require "quality_map_c/quality_map_c"
require "quality_map_image/version"

module QualityMapImage
  METHOD_MAP = {
    "LogExpSum" => 0,
    "First" => 1
  }

  def self.get_image(south_west_int, north_east_int, step_int, points)
    QualityMapC::Image.buildImage(south_west_int, north_east_int, step_int, points)
  end

  def self.quality_of_points(lat, lng, lat_range, lng_range, polygons, quality_calc_method, quality_calc_value)
    QualityMapC::Point.qualityOfPoints(lat, lng, lat_range, lng_range, polygons, METHOD_MAP[quality_calc_method], quality_calc_value)
  end

  def self.quality_of_point(lat, lng, polygons, quality_calc_method, quality_calc_value)
    QualityMapC::Point.qualityOfPoint(lat, lng, polygons, METHOD_MAP[quality_calc_method], quality_calc_value)
  end
end