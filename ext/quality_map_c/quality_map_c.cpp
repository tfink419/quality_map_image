#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <iostream>
#include "gd.h"
#include "gdfontt.h"
#include "gdfonts.h"
#include "gdfontmb.h"
#include "gdfontl.h"
#include "gdfontg.h"

#include "ruby/ruby.h"
#include "ruby/encoding.h"

#include "gradient.hpp"
#include "clipper.hpp"

using namespace std;
using namespace ClipperLib;

#define MULTIPLY_CONST 1000000000
#define COORD_MULTIPLE 1000
#define MULTIPLE_DIFF  1000000


extern "C" {

enum QualityCalcMethod {QualityLogExpSum = 0, QualityFirst = 1}; 


const int ALPHA = 64;

void *png_pointer = NULL;
gdImagePtr im;

static double LogExpSum(double *values, long values_length, double log_exp) {
  if(!values_length) {
    return 0.0;
  }
  double sum = 0.0;
  for(long i = 0; i < values_length; i++) {
    sum += pow(log_exp,values[i]);
  }
  return log(sum)/log(log_exp);
}

// Taken from ruby-clipper https://github.com/mieko/rbclipper/blob/master/ext/clipper/rbclipper.cpp
static void
ary_to_polygon(VALUE ary, ClipperLib::Path* poly)
{
  const char* earg =
    "Paths have format: [[p0_x, p0_y], [p1_x, p1_y], ...]";

  Check_Type(ary, T_ARRAY);

  long aryLength = RARRAY_LEN(ary);

  for(long i = 0; i < aryLength; i++) {
    VALUE sub = rb_ary_entry(ary, i);
    Check_Type(sub, T_ARRAY);

    if(RARRAY_LEN(sub) != 2) {
      rb_raise(rb_eArgError, "%s", earg);
    }

    VALUE px = rb_ary_entry(sub, 0);
    VALUE py = rb_ary_entry(sub, 1);

    poly->push_back(IntPoint((long long)(NUM2DBL(px) * MULTIPLY_CONST), (long long)(NUM2DBL(py) * MULTIPLY_CONST)));
  }
}

static void checkPointArray(VALUE point) {
  const char* pointError =
    "Points have format: [[p1_lat, p1_long, p1_quality], [p2_lat, p2_long, p2_quality], ...]";

  Check_Type(point, T_ARRAY);

  if(RARRAY_LEN(point) != 3) {
    rb_raise(rb_eArgError, "%s", pointError);
  }
}

long QualitiesOfPoint(long long &x, long long &y, double *&qualities, ClipperLib::Path *&clipper_polygons, VALUE &polygons, long &polygons_length, VALUE ids) {
  long num_qualities = 0;
  IntPoint intPoint = IntPoint(x, y);
  for(long i = 0; i < polygons_length; i++) {
    if(PointInPolygon(intPoint, clipper_polygons[i])) {
      qualities[num_qualities++] = NUM2DBL(rb_ary_entry(rb_ary_entry(polygons, i),1));
      if(ids) {
        rb_ary_push(ids, rb_ary_entry(rb_ary_entry(polygons, i),2));
      }
    }
  }
  return num_qualities;
}

// quality_calc_method is an
static VALUE qualityOfPoints(VALUE self, VALUE lat_start, VALUE lng_start, 
  VALUE lat_range_ruby, VALUE lng_range_ruby, VALUE polygons, VALUE quality_calc_method_ruby, VALUE quality_calc_value_ruby) {
  // X is lng, Y is lat
  long long x_start = ((long long)NUM2INT(lng_start))*MULTIPLE_DIFF;
  long long y = ((long long)NUM2INT(lat_start))*MULTIPLE_DIFF;

  long lat_range = NUM2INT(lat_range_ruby), lng_range = NUM2INT(lng_range_ruby);

  long long lng_end = ((long long)lng_range*MULTIPLE_DIFF)+x_start-MULTIPLE_DIFF;
  long long lat_end = ((long long)lat_range*MULTIPLE_DIFF)+y-MULTIPLE_DIFF;
  long polygons_length = RARRAY_LEN(polygons);

  enum QualityCalcMethod quality_calc_method = (enum QualityCalcMethod) NUM2INT(quality_calc_method_ruby);
  double quality_calc_value = NUM2DBL(quality_calc_value_ruby);

  VALUE point_qualities = rb_ary_new2(lat_range*lng_range);

  double *qualities;
  switch(quality_calc_method) {
    case QualityLogExpSum:
      qualities = new double[polygons_length];
      break;
  }
  ClipperLib::Path *clipper_polygons = new ClipperLib::Path[polygons_length];

  for(long i = 0; i < polygons_length; i++) {
    ary_to_polygon(rb_ary_entry(rb_ary_entry(polygons, i),0), clipper_polygons+i);
  }
  long long x;
  
  for(; y <= lat_end; y += MULTIPLE_DIFF) {
    for(x = x_start; x <= lng_end; x += MULTIPLE_DIFF) {
      switch(quality_calc_method) {
        case QualityLogExpSum:
        {
          long num_qualities = QualitiesOfPoint(x, y, qualities, clipper_polygons, polygons, polygons_length, NULL);
          rb_ary_push(point_qualities, DBL2NUM(LogExpSum(qualities, num_qualities, quality_calc_value)));
          break;
        }
        case QualityFirst:
          for(long i = 0; i < polygons_length; i++) {
            if(PointInPolygon(IntPoint(x, y), clipper_polygons[i])) {
              rb_ary_push(point_qualities, rb_ary_entry(rb_ary_entry(polygons, i),1));
              break;
            }
          }
          break;
        default:
          rb_raise(rb_eRuntimeError, "%s", "Unknown Calc Method Type Chosen");
      }
    }
  }

  switch(quality_calc_method) {
    case QualityLogExpSum:
      delete[] qualities;
      break;
  }
  delete[] clipper_polygons;
  return point_qualities;
}

static VALUE qualityOfPoint(VALUE self, VALUE lat, VALUE lng, VALUE polygons, VALUE quality_calc_method_ruby, VALUE quality_calc_value_ruby) {
  // X is lng, Y is lat
  long long x = ((long long)NUM2INT(lng))*MULTIPLE_DIFF;
  long long y = ((long long)NUM2INT(lat))*MULTIPLE_DIFF;
  enum QualityCalcMethod quality_calc_method = (enum QualityCalcMethod) NUM2INT(quality_calc_method_ruby);
  double quality_calc_value = NUM2DBL(quality_calc_value_ruby);

  long polygons_length = RARRAY_LEN(polygons);

  double *qualities;
  switch(quality_calc_method) {
    case QualityLogExpSum:
      qualities = new double[polygons_length];
      break;
  }
  VALUE ids = rb_ary_new();
  ClipperLib::Path *clipper_polygons = new ClipperLib::Path[polygons_length];

  for(long i = 0; i < polygons_length; i++) {
    ary_to_polygon(rb_ary_entry(rb_ary_entry(polygons, i),0), clipper_polygons+i);
  }

  double quality = NULL;
  switch(quality_calc_method) {
    case QualityLogExpSum:
    {
      long num_qualities = QualitiesOfPoint(x, y, qualities, clipper_polygons, polygons, polygons_length, ids);
      quality = LogExpSum(qualities, num_qualities, quality_calc_value);
      break;
    }
    case QualityFirst:
      for(long i = 0; i < polygons_length; i++) {
        if(PointInPolygon(IntPoint(x, y), clipper_polygons[i])) {
          quality = NUM2DBL(rb_ary_entry(rb_ary_entry(polygons, i),1));
          rb_ary_push(ids, rb_ary_entry(rb_ary_entry(polygons, i),2));
          break;
        }
      }
      break;
    default:
      rb_raise(rb_eRuntimeError, "%s", "Unknown Calc Method Type Chosen");
  }

  switch(quality_calc_method) {
    case QualityLogExpSum:
      delete[] qualities;
      break;
  }
  delete[] clipper_polygons;
  return rb_ary_new_from_args(2, DBL2NUM(quality), ids);
}

static VALUE buildImage(VALUE self, VALUE south_west_int_ruby, VALUE north_east_int_ruby, VALUE step_int_ruby, VALUE points_ruby) {
  const char* coord_error =
    "Coordinates have format: [lat(int), long(int)]";
  const char* outer_points_array_error =
    "Outer Points Array must have format: [range_low, range_high, multiple, invert, points]";

  Check_Type(south_west_int_ruby, T_ARRAY);
  if(RARRAY_LEN(south_west_int_ruby) != 2) {
    rb_raise(rb_eArgError, "%s", coord_error);
  }
  Check_Type(north_east_int_ruby, T_ARRAY);
  if(RARRAY_LEN(north_east_int_ruby) != 2) {
    rb_raise(rb_eArgError, "%s", coord_error);
  }



  int south = NUM2INT(rb_ary_entry(south_west_int_ruby, 0));
  int west = NUM2INT(rb_ary_entry(south_west_int_ruby, 1));
  int north = NUM2INT(rb_ary_entry(north_east_int_ruby, 0));
  int east = NUM2INT(rb_ary_entry(north_east_int_ruby, 1));
  int step_int = NUM2INT(step_int_ruby);
  int width = ((east-west)/step_int)+1;
  int height = ((north-south)/step_int)+1;
  
  im = gdImageCreate(width, height);

  int* colors = new int[GRADIENT_MAP_SIZE];

  // The first color added is the background, it should be the quality 0 color
  for(int i = 0, pos = 0; i < GRADIENT_MAP_SIZE; i++, pos+=3) {
    colors[i] = gdImageColorAllocateAlpha(im, GRADIENT_MAP[pos], GRADIENT_MAP[pos+1], GRADIENT_MAP[pos+2], ALPHA);
  }

  Check_Type(points_ruby, T_ARRAY);

  long num_point_types = RARRAY_LEN(points_ruby);
  if(num_point_types) {

    VALUE point;
    long *points_indexes = new long[num_point_types];
    bool *current_point_exists = new bool[num_point_types];
    long *points_lengths = new long[num_point_types];
    int *current_point_lats = new int[num_point_types];
    int *current_point_longs = new int[num_point_types];
    double *points_lows = new double[num_point_types];
    double *points_highs = new double[num_point_types];
    double *points_multiples = new double[num_point_types];
    bool *points_should_inverts = new bool[num_point_types];
    double *current_point_values = new double[num_point_types];
    for(long i = 0; i < num_point_types; i++) {
      if(RARRAY_LEN(rb_ary_entry(points_ruby, i)) != 5) {
        rb_raise(rb_eArgError, "%s", outer_points_array_error);
      }
      points_indexes[i] = 0;
      points_lengths[i] = RARRAY_LEN(rb_ary_entry(rb_ary_entry(points_ruby, i),4));
      current_point_exists[i] = (points_lengths[i] > 0);
      points_lows[i] = NUM2DBL(rb_ary_entry(rb_ary_entry(points_ruby, i), 0));
      points_highs[i] = NUM2DBL(rb_ary_entry(rb_ary_entry(points_ruby, i), 1));
      points_multiples[i] = (GRADIENT_MAP_SIZE-1)/(points_highs[i]-points_lows[i])*NUM2DBL(rb_ary_entry(rb_ary_entry(points_ruby, i), 2));
      points_should_inverts[i] = rb_ary_entry(rb_ary_entry(points_ruby, i), 3) == Qtrue;
      if(current_point_exists[i]) {
        point = rb_ary_entry(rb_ary_entry(rb_ary_entry(points_ruby, i), 4),0);
        checkPointArray(point);
        current_point_lats[i] = NUM2INT(rb_ary_entry(point, 0));
        current_point_longs[i] = NUM2INT(rb_ary_entry(point, 1));
        current_point_values[i] = NUM2DBL(rb_ary_entry(point, 2));
      }
    }

    double quality, current_value;
    long i, x, y, lat, lng;
    // Iterate through coordinates, changing each pixel at that coordinate based on the point(s) there
    for(lat = south, y = height-1; lat <= north; lat += step_int, y--) {
      for(lng = west, x = 0; lng <= east; lng += step_int, x++) {
        quality = 0;
        for(i = 0; i < num_point_types; i++) {
          if(current_point_exists[i] && current_point_lats[i] == lat && current_point_longs[i] == lng) {
            current_value = current_point_values[i];
            if(++points_indexes[i] < points_lengths[i]) {
              point = rb_ary_entry(rb_ary_entry(rb_ary_entry(points_ruby, i), 4), points_indexes[i]);
              // checkPointArray(point); // Don't know how much this costs
              current_point_lats[i] = NUM2INT(rb_ary_entry(point, 0));
              current_point_longs[i] = NUM2INT(rb_ary_entry(point, 1));
              current_point_values[i] = NUM2DBL(rb_ary_entry(point, 2));
            }
            else {
              current_point_exists[i] = false;
            }
          }
          else {
            current_value = points_lows[i];
          }
          if(current_value > points_highs[i]) {
            current_value = points_highs[i];
          }
          if(current_value < points_lows[i]) {
            current_value = points_lows[i];
          }
          current_value -= points_lows[i];
          if(points_should_inverts[i]) {
            current_value = (points_highs[i]-points_lows[i])-current_value;
          }
          quality += (current_value)*points_multiples[i];
        }
        if(quality > 0) {
          gdImageSetPixel(im, x, y, colors[(int)quality]);
        }
      }
    }
    delete[] points_indexes;
    delete[] current_point_exists;
    delete[] points_lengths;
    delete[] current_point_lats;
    delete[] current_point_longs;
    delete[] current_point_values;
    delete[] points_should_inverts;
    delete[] points_multiples;
    delete[] points_lows;
    delete[] points_highs;
  }
  delete[] colors;

  int image_size = 1;

  /* Generate a blob of the image */
  png_pointer = gdImagePngPtr(im, &image_size);
  gdImageDestroy(im);

  fflush(stdout);
  if(!png_pointer) {
    rb_raise(rb_eRuntimeError, "%s", "Image blob creation failed.");
  }
  VALUE ruby_blob = rb_str_new((char *)png_pointer, image_size);
  gdFree(png_pointer);
  return ruby_blob;
}

typedef VALUE (*ruby_method)(...);

void Init_quality_map_c(void) {
  VALUE QualityMapC = rb_define_module("QualityMapC");
  VALUE Image = rb_define_class_under(QualityMapC, "Image", rb_cObject);
  VALUE Point = rb_define_class_under(QualityMapC, "Point", rb_cObject);

  rb_define_singleton_method(Point, "qualityOfPoints", (ruby_method) qualityOfPoints, 7);
  rb_define_singleton_method(Point, "qualityOfPoint", (ruby_method) qualityOfPoint, 5);

  rb_define_singleton_method(Image, "buildImage", (ruby_method) buildImage, 4);
}

} // extern "C"