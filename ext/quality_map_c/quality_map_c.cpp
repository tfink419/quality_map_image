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
#define LOG_EXP 1.7

// Taken from ruby-clipper https://github.com/mieko/rbclipper/blob/master/ext/clipper/rbclipper.cpp
static inline Clipper*
XCLIPPER(VALUE x)
{
  Clipper* clipper;
  Data_Get_Struct(x, Clipper, clipper);
  return clipper;
}

extern "C" {

const int ALPHA = 64;

void *pngPointer = NULL;
gdImagePtr im;

static double logExpSum(double *values, long valuesLength) {
  if(!valuesLength) {
    return 0.0;
  }
  double sum = 0.0;
  for(long i = 0; i < valuesLength; i++) {
    sum += pow(LOG_EXP,values[i]);
  }
  return log(sum)/log(LOG_EXP);
}

// Taken from ruby-clipper
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

    poly->push_back(IntPoint((long64)(NUM2DBL(px) * MULTIPLY_CONST), (long64)(NUM2DBL(py) * MULTIPLY_CONST)));
  }
}

// Taken from ruby-clipper
// Is invoked by Data_Wrap_Struct when its closed I guess
static void
rbclipper_free(void* ptr)
{
  delete (Clipper*) ptr;
}

VALUE rbclipperSingleton = 0;

// Taken from ruby-clipper, I added a way to make it a singleton
static VALUE
rbclipper_new(VALUE klass)
{
  if(!rbclipperSingleton) {
    Clipper* ptr = new Clipper;
    rbclipperSingleton = Data_Wrap_Struct(klass, 0, rbclipper_free, ptr);
    rb_obj_call_init(rbclipperSingleton, 0, 0);
  }
  return rbclipperSingleton;
}

static void checkPointArray(VALUE point) {
  const char* pointError =
    "Points have format: [[p1_lat, p1_long, p1_quality], [p2_lat, p2_long, p2_quality], ...]";

  Check_Type(point, T_ARRAY);

  if(RARRAY_LEN(point) != 3) {
    rb_raise(rb_eArgError, "%s", pointError);
  }
}

static VALUE qualityOfPoints(VALUE self, VALUE lat, VALUE lngStart, VALUE rangeRuby, VALUE polygons) {
  // X is lng, Y is lat
  long64 x = ((long64)NUM2INT(lngStart))*MULTIPLE_DIFF;
  long64 y = ((long64)NUM2INT(lat))*MULTIPLE_DIFF;

  long64 endPoint = ((long64)NUM2INT(rangeRuby)*MULTIPLE_DIFF)+x-MULTIPLE_DIFF;
  long polygonsLength = RARRAY_LEN(polygons);

  VALUE pointQualities = rb_ary_new();

  for(; x <= endPoint; x += MULTIPLE_DIFF) {
    double *qualities = new double[polygonsLength];
    long numQualities = 0;

    for(long i = 0; i < polygonsLength; i++) {
      ClipperLib::Path polygon;
      ary_to_polygon(rb_ary_entry(rb_ary_entry(polygons, i),0), &polygon);

      if(ClipperLib::PointInPolygon(IntPoint(x, y), polygon)) {
        qualities[numQualities++] = NUM2DBL(rb_ary_entry(rb_ary_entry(polygons, i),1));
      }
    }
    rb_ary_push(pointQualities, DBL2NUM(logExpSum(qualities, numQualities)));
    delete[] qualities;
  }

  return pointQualities;
}

static VALUE buildImage(VALUE self, VALUE southWestIntRuby, VALUE northEastIntRuby, VALUE stepIntRuby, VALUE pointsRuby) {
  const char* coordError =
    "Coordinates have format: [lat(int), long(int)]";

  Check_Type(southWestIntRuby, T_ARRAY);
  if(RARRAY_LEN(southWestIntRuby) != 2) {
    rb_raise(rb_eArgError, "%s", coordError);
  }
  Check_Type(northEastIntRuby, T_ARRAY);
  if(RARRAY_LEN(northEastIntRuby) != 2) {
    rb_raise(rb_eArgError, "%s", coordError);
  }
  Check_Type(pointsRuby, T_ARRAY);


  int south = NUM2INT(rb_ary_entry(southWestIntRuby, 0));
  int west = NUM2INT(rb_ary_entry(southWestIntRuby, 1));
  int north = NUM2INT(rb_ary_entry(northEastIntRuby, 0));
  int east = NUM2INT(rb_ary_entry(northEastIntRuby, 1));
  int stepInt = NUM2INT(stepIntRuby);

  int width = ((east-west)/stepInt)+1;
  int height = ((north-south)/stepInt)+1;

  im = gdImageCreate(width, height);

  int* colors = new int[GRADIENT_MAP_SIZE];

  // The first color added is the background, it should be the quality 0 color
  for(int i = 0; i < GRADIENT_MAP_SIZE; i++) {
    colors[i] = gdImageColorAllocateAlpha(im, GRADIENT_MAP[i][0], GRADIENT_MAP[i][1], GRADIENT_MAP[i][2], ALPHA);
  }

  fflush(stdout);

  unsigned int gstoreInd = 0;

  VALUE point;
  short currentPointExists = (RARRAY_LEN(pointsRuby) > 0);
  int currentPointLat, currentPointLong;
  double currentPointQuality;
  if(currentPointExists) {
    point = rb_ary_entry(pointsRuby, 0);
    checkPointArray(point);
    currentPointLat = NUM2INT(rb_ary_entry(point, 0));
    currentPointLong = NUM2INT(rb_ary_entry(point, 1));
    currentPointQuality = NUM2DBL(rb_ary_entry(point, 2));
  }
  // Iterate through coordinates, changing each pixel at that coordinate based on the point(s) there
  for(int lat = south, y = height-1; lat <= north; lat += stepInt, y--) {
    for(int lng = west, x = 0; lng <= east; lng += stepInt, x++) {
      double quality = 0;
      if(currentPointExists && currentPointLat == lat && currentPointLong == lng) {
        quality = currentPointQuality;
        if(++gstoreInd < RARRAY_LEN(pointsRuby)) {
          point = rb_ary_entry(pointsRuby, gstoreInd);
          checkPointArray(point);

          currentPointLat = NUM2INT(rb_ary_entry(point, 0));
          currentPointLong = NUM2INT(rb_ary_entry(point, 1));
          currentPointQuality = NUM2DBL(rb_ary_entry(point, 2));
        }
        else {
          currentPointExists = 0; // false
        }
      }
      if(quality > 12.5) {
        quality = 12.5;
      }
      if(quality < 0) {
        quality = 0;
      }
      if(quality > 0) {
        gdImageSetPixel(im, x, y, colors[(int)quality*8]);
      }
    }
  }
  delete[] colors;

  int imageSize = 1;

  /* Generate a blob of the image */
  pngPointer = gdImagePngPtr(im, &imageSize);

  if(pngPointer) {
    fflush(stdout);
    return rb_str_new((char *)pngPointer, imageSize);
  }
  else {
    printf("*********************************\nImage Creation failed...\n*********************************");
    fflush(stdout);
    return Qnil;
  }
}

static VALUE destroyImage(VALUE self) {
  /* Destroy the image in memory. */
  gdFree(pngPointer);
  gdImageDestroy(im);
}


typedef VALUE (*ruby_method)(...);

void Init_quality_map_c(void) {
  VALUE QualityMapC = rb_define_module("QualityMapC");
  VALUE Image = rb_define_class_under(QualityMapC, "Image", rb_cObject);
  VALUE Point = rb_define_class_under(QualityMapC, "Point", rb_cObject);


  rb_define_singleton_method(Point, "createClipperSingleton", (ruby_method) rbclipper_new, 0);
  rb_define_method(Point, "qualityOfPoints", (ruby_method) qualityOfPoints, 4);

  rb_define_singleton_method(Image, "buildImage", (ruby_method) buildImage, 4);
  rb_define_singleton_method(Image, "destroyImage", (ruby_method) destroyImage, 0);
}

} // extern "C"