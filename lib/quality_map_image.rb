require "quality_map_c/quality_map_c"
class QualityMapImage
  VERSION = "0.0.4"
  def self.get_image
    
    puts QualityMapC::Image.buildImage
    QualityMapC::Image.destroyImage
  end
end