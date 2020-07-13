require "quality_map_c/quality_map_c"
require "quality_map_image/version"

module QualityMapImage
  def self.get_image(south_west_int, north_east_int, step_int, points)
    
    image_blob = QualityMapC::Image.buildImage(south_west_int, north_east_int, step_int, points)
    QualityMapC::Image.destroyImage
    image_blob
  end

  def self.quality_of_points(lat, lng, range, polygons)
    QualityMapC::Point.qualityOfPoints(lat, lng, range, polygons)
  end

  def self.quality_of_point(lat, lng, polygons)
    self.quality_of_points(lat, lng, 1, polygons)[0]
  end
end