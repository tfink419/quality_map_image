require 'quality_map_image'
require 'json'

QUALITY_OF_POINT_LAT=0
QUALITY_OF_POINT_LONG=0
QUALITY_OF_POINT_POLYGONS=[
  [[[[[-1,-1], [-1, 1], [1, 1], [1, -1], [-1,-1]]]], 10, 1],
  [[[[[-2,-2], [-2, 2], [2, 2], [2, -2], [-2,-2]]]], 10, 2],
  [[[[[-3,-3], [-3, 3], [3, 3], [3, -3], [-3,-3]]]], 10, 3]
]
QUALITY_OF_POINT_POLYGONS_WITH_HOLE=[
  [[[[[-3,-3], [-3, 3], [3, 3], [3, -3], [-3,-3]], [[-1,-1], [-1, 1], [1, 1], [1, -1], [-1,-1]]]], 10, 1]
]

QUALITY_OF_POINTS_CENSUS_TRACT_LAT = 36.9*1000
QUALITY_OF_POINTS_CENSUS_TRACT_LONG = -102.8*1000
QUALITY_OF_POINTS_CENSUS_TRACT_WIDTH_HEIGHT = 100
QUALITY_OF_POINTS_CENSUS_TRACT_POLYGONS = JSON.parse(File.read("spec/dumps/census_tract_polygon_dump"))

QUALITY_OF_POINTS_SMOL_LAT = 0
QUALITY_OF_POINTS_SMOL_LONG = 0
QUALITY_OF_POINTS_SMOL_SCALE = 1000
QUALITY_OF_POINTS_SMOL_WIDTH_HEIGHT = 100
QUALITY_OF_POINTS_SMOL_POLYGONS = [
  [[[[[0,0], [0, 0.05], [0.05, 0.05], [0.05, 0], [0, 0]]]], 10]
]

RSpec.describe QualityMapImage do
  it "has a version number" do
    expect(subject::VERSION).not_to be nil
  end

  it "should return an image representing the data" do
    test_response = subject.quality_of_points_image(QUALITY_OF_POINTS_CENSUS_TRACT_LAT, QUALITY_OF_POINTS_CENSUS_TRACT_LONG,
    QUALITY_OF_POINTS_CENSUS_TRACT_WIDTH_HEIGHT, QUALITY_OF_POINTS_CENSUS_TRACT_WIDTH_HEIGHT, QUALITY_OF_POINTS_CENSUS_TRACT_POLYGONS, 100, "First", 1)
    expect(test_response).to be_truthy
    File.write("test_image.png", test_response)
  end

  it "should return a smol image representing the data" do
    test_response = subject.quality_of_points_image(QUALITY_OF_POINTS_SMOL_LAT, QUALITY_OF_POINTS_SMOL_LONG,
    QUALITY_OF_POINTS_SMOL_WIDTH_HEIGHT, QUALITY_OF_POINTS_SMOL_WIDTH_HEIGHT, QUALITY_OF_POINTS_SMOL_POLYGONS, QUALITY_OF_POINTS_SMOL_SCALE, "First", 1)
    expect(test_response).to be_truthy
    File.write("test_image2.png", test_response)
  end

  it "gets the First quality of point and returns id" do
    expect(subject.quality_of_point(QUALITY_OF_POINT_LAT, QUALITY_OF_POINT_LONG, QUALITY_OF_POINT_POLYGONS, "First", 1)).to eq([10, [1]] )
  end

  it "gets the LogExpSum quality of point and returns ids" do
    expect(subject.quality_of_point(QUALITY_OF_POINT_LAT, QUALITY_OF_POINT_LONG, QUALITY_OF_POINT_POLYGONS, "LogExpSum", 1.7)).to eq([12.070399166401323, [1, 2, 3]] )
  end

  it "gets doesnt get a quality for 0,0 because there is a hole there" do
    expect(subject.quality_of_point(QUALITY_OF_POINT_LAT, QUALITY_OF_POINT_LONG, QUALITY_OF_POINT_POLYGONS_WITH_HOLE, "First", 1)).to eq([0, []] )
  end

  it "builds an image from the 2 test images" do
    images = []
    image_data = [[5, 40, 0.5, 100, true], [0, 10, 0.5, 1000, false]]
    images << File.read("test_image.png")
    images << File.read("test_image2.png")
    test_response = subject.get_image(100, images, image_data)
    expect(test_response).to be_truthy
    File.write("test_image3.png", test_response)
  end
end
