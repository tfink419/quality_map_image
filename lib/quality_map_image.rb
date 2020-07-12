require "quality_map_c/quality_map_c"
require "quality_map_image/version"
require "quality_map_image/test_values"

module QualityMapImage
  def self.get_image(south_west_int, north_east_int, step_int, points)
    
    image_blob = QualityMapC::Image.buildImage(south_west_int, north_east_int, step_int, points)
    QualityMapC::Image.destroyImage
    image_blob
  end

  def self.quality_of_points(lat, lng, range, polygons)
    clipper = QualityMapC::Point.createClipperSingleton # Its a singleton internally so no worries
    polygons = polygons.map{ |polygon_and_quality| 
      [
        polygon_and_quality[0].map { |coord| coord.map(&:to_f) },
        polygon_and_quality[1]
      ]
    }
    clipper.qualityOfPoints(lat, lng, range, polygons)
  end

  def self.quality_of_point(lat, lng, polygons)
    self.quality_of_points(lat, lng, 1, polygons)[0]
  end
end