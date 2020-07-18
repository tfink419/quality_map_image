QUALITY_OF_POINT_LAT=0
QUALITY_OF_POINT_LONG=0
QUALITY_OF_POINT_POLYGONS=[
  [[[-1,-1], [-1, 1], [1, 1], [1, -1], [-1,-1]], 10, 1],
  [[[-2,-2], [-2, 2], [2, 2], [2, -2], [-2,-2]], 10, 2],
  [[[-3,-3], [-3, 3], [3, 3], [3, -3], [-3,-3]], 10, 3]
]
QUALITY_OF_POINT_RESPONSE = [12.070399166401323, [1, 2, 3]]

BUILD_IMAGE_SOUTHWEST = [0,0]
BUILD_IMAGE_NORTHEAST = [4,4]
BUILD_IMAGE_STEP = 1
BUILD_IMAGE_POINTS = [[0, 0, 10], [1, 1, 10], [2, 2, 10], [3, 3, 10], [4, 4, 10]]
require 'quality_map_image'

RSpec.describe QualityMapImage do
  it "has a version number" do
    expect(subject::VERSION).not_to be nil
  end

  it "gets the quality of point and returns ids" do
    expect(subject.quality_of_point(QUALITY_OF_POINT_LAT, QUALITY_OF_POINT_LONG, QUALITY_OF_POINT_POLYGONS)).to eq([12.070399166401323, [1, 2, 3]] )
  end

  it "builds a 5x5 image with a red background and a green line running from bottom-left to top-right" do
    test_response = subject.get_image(BUILD_IMAGE_SOUTHWEST, BUILD_IMAGE_NORTHEAST, BUILD_IMAGE_STEP, BUILD_IMAGE_POINTS)
    expect(test_response).to be_truthy
    File.write("test_image.png", test_response)
  end
end
