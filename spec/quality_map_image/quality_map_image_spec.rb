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

PRECISION = 2**19
HIGHEST_NUM = 180.0
QUALITY_OF_POINTS_CENSUS_TRACT_LAT = 36.9*PRECISION/HIGHEST_NUM
QUALITY_OF_POINTS_CENSUS_TRACT_LONG = -102.8*PRECISION/HIGHEST_NUM
QUALITY_OF_POINTS_CENSUS_TRACT_WIDTH_HEIGHT = 291
QUALITY_OF_POINTS_CENSUS_TRACT_POLYGONS = JSON.parse(File.read("spec/dumps/census_tract_polygon_dump"))
QUALITY_OF_POINTS_CENSUS_TRACT_SCALE = 42000000

QUALITY_OF_POINTS_SQUARE_LAT = 0
QUALITY_OF_POINTS_SQUARE_LONG = 0
QUALITY_OF_POINTS_SQUARE_SCALE = 100000000
QUALITY_OF_POINTS_SQUARE_WIDTH_HEIGHT = 291
QUALITY_OF_POINTS_SQUARE_POLYGONS = [
  [[[[[0,0], [0, 0.05], [0.05, 0.05], [0.05, 0], [0, 0]]]], 10]
]

RSpec.describe QualityMapImage do
  it "has a version number" do
    expect(subject::VERSION).not_to be nil
  end

  it "should return an image representing the data" do
    test_response = subject.quality_of_points_image(PRECISION/HIGHEST_NUM, QUALITY_OF_POINTS_CENSUS_TRACT_LAT, QUALITY_OF_POINTS_CENSUS_TRACT_LONG,
    QUALITY_OF_POINTS_CENSUS_TRACT_WIDTH_HEIGHT, QUALITY_OF_POINTS_CENSUS_TRACT_POLYGONS, QUALITY_OF_POINTS_CENSUS_TRACT_SCALE, "First", 1)
    expect(test_response).to be_truthy
    File.write("test_images/test_image.png", test_response)
  end

  it "should return an image with a little square in the bottom left" do
    test_response = subject.quality_of_points_image(PRECISION/HIGHEST_NUM, QUALITY_OF_POINTS_SQUARE_LAT, QUALITY_OF_POINTS_SQUARE_LONG,
    QUALITY_OF_POINTS_SQUARE_WIDTH_HEIGHT, QUALITY_OF_POINTS_SQUARE_POLYGONS, QUALITY_OF_POINTS_SQUARE_SCALE, "LogExpSum", 2)
    expect(test_response).to be_truthy
    File.write("test_images/test_image2.png", test_response)
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

  it "builds an image with a greenish square in the bottom left and a red squigle in the top right" do
    images = []
    image_data = [[0, 10, 0.5, QUALITY_OF_POINTS_CENSUS_TRACT_SCALE, true, 'LogExpSum', 2], [0, 10, 0.5, QUALITY_OF_POINTS_SQUARE_SCALE, false, 'First', 1]]
    images << [File.read("test_images/test_image.png")]
    images << [File.read("test_images/test_image2.png")]
    test_response = subject.colorized_quality_image(291, images, image_data)
    expect(test_response).to be_truthy
    File.write("test_images/test_image3.png", test_response)
  end

  it "builds an image with a faded greenish square in the bottom left and a red-yellow squiggle in the top right" do
    images = []
    image_data = [[0, 40, 0.5, QUALITY_OF_POINTS_CENSUS_TRACT_SCALE, true, 'LogExpSum', 2], [0, 40, 0.5, QUALITY_OF_POINTS_SQUARE_SCALE, false, 'First', 1]]
    images << [File.read("test_images/test_image.png")]
    images << [File.read("test_images/test_image2.png")]
    test_response = subject.colorized_quality_image(291, images, image_data)
    expect(test_response).to be_truthy
    File.write("test_images/test_image4.png", test_response)
  end

  it "builds an image with a blue square in the bottom left and a red squiggle in the top right" do
    images = []
    image_data = [[0, 10, 0.1, QUALITY_OF_POINTS_CENSUS_TRACT_SCALE, true, 'LogExpSum', 2], [0, 10, 0.9, QUALITY_OF_POINTS_SQUARE_SCALE, false, 'First', 1]]
    images << [File.read("test_images/test_image.png")]
    images << [File.read("test_images/test_image2.png")]
    test_response = subject.colorized_quality_image(291, images, image_data)
    expect(test_response).to be_truthy
    File.write("test_images/test_image5.png", test_response)
  end

  it "builds a yellow image" do
    image_data = [[0, 40, 0.5, QUALITY_OF_POINTS_CENSUS_TRACT_SCALE, true, 'LogExpSum', 2], [0, 40, 0.5, QUALITY_OF_POINTS_SQUARE_SCALE, false, 'First', 1]]
    test_response = subject.colorized_quality_image(291, [[], []], image_data)
    expect(test_response).to be_truthy
    File.write("test_images/test_image6.png", test_response)
  end

  it 'should merge 4 images together and subsample it down to the original size' do
    top_left = File.read("test_images/test_image3.png")
    top_right = File.read("test_images/test_image4.png")
    bottom_left = File.read("test_images/test_image5.png")
    bottom_right = File.read("test_images/test_image6.png")
    test_response = subject.subsample4(291, top_left, top_right, bottom_left, bottom_right)
    expect(test_response).to be_truthy
    File.write("test_images/test_image7.png", test_response)
  end

  it 'should remove holes from the images' do
    images = []
    images << File.read("test_images/852-1.png")
    images << File.read("test_images/852-2.png")
    images << File.read("test_images/856.png")
    images.each_with_index do |image, i|
      test_response = subject.clean_zeros(image)
      expect(test_response).to be_truthy
      File.write("test_images/zeroes_cleaned#{i}.png", test_response)
    end
  end
end
