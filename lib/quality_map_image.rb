require "quality_map_c/quality_map_c"
require "quality_map_image/version"

module QualityMapImage
  def self.get_image(south_west_int, north_east_int, step_int, points)
    
    image_blob = QualityMapC::Image.buildImage(south_west_int, north_east_int, step_int, points)
    QualityMapC::Image.destroyImage
    image_blob
  end
end