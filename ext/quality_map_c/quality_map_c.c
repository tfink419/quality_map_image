#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "gd.h"
#include "gdfontt.h"
#include "gdfonts.h"
#include "gdfontmb.h"
#include "gdfontl.h"
#include "gdfontg.h"

#include "ruby/ruby.h"
#include "ruby/encoding.h"

#include "gradient.h"

const int ALPHA = 64;

void *pngPointer = NULL;
gdImagePtr im;

void checkPointArray(VALUE point) {
  const char* pointError =
    "Points have format: [[p1_lat, p1_long, p1_quality], [p2_lat, p2_long, p2_quality], ...]";

  Check_Type(point, T_ARRAY);

  if(RARRAY_LEN(point) != 3) {
    rb_raise(rb_eArgError, "%s", pointError);
  }
}

static VALUE buildImage(VALUE self, VALUE southWestIntRuby, VALUE northEastIntRuby, VALUE stepIntRuby, VALUE pointsRuby) {
  /* Declare the image */
  /* Declare output files */
  /* Declare color indexes */

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

  int* colors = malloc(GRADIENT_MAP_SIZE * sizeof(int));

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
  free(colors);

  int imageSize = 1;

  /* Generate a blob of the image */
  pngPointer = gdImagePngPtr(im, &imageSize);

  if(pngPointer) {
    fflush(stdout);
    return rb_str_new(pngPointer, imageSize);
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

void Init_quality_map_c(void) {
  VALUE QualityMapC = rb_define_module("QualityMapC");
  VALUE Image = rb_define_class_under(QualityMapC, "Image", rb_cObject);

  rb_define_singleton_method(Image, "buildImage", buildImage, 4);
  rb_define_singleton_method(Image, "destroyImage", destroyImage, 0);
}