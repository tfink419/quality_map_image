require "quality_map_c/quality_map_c"
require "quality_map_image/version"

module QualityMapImage
  def self.get_image(south_west_int, north_east_int, step_int, points)
    
    image_blob = QualityMapC::Image.buildImage(south_west_int, north_east_int, step_int, points)
    QualityMapC::Image.destroyImage
    image_blob
  end

  def self.save_test_image
    require "quality_map_image/test_image"
    image_blob = QualityMapC::Image.buildImage(TEST_SOUTH_WEST, TEST_NORTH_EAST, TEST_STEP_INT, TEST_POINTS)
    open('test_image.png', 'w') { |f|
      f.puts image_blob
    }
    QualityMapC::Image.destroyImage
    return true
  end
end