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

void *pngPointer = NULL;
gdImagePtr im;

static VALUE buildImage(VALUE self) {
  /* Declare the image */
  /* Declare output files */
  /* Declare color indexes */
  int black;
  int white;

  /* Allocate the image: 64 pixels across by 64 pixels tall */
  im = gdImageCreate(64, 64);

  /* Allocate the color black (red, green and blue all minimum).
    Since this is the first color in a new image, it will
    be the background color. */
  black = gdImageColorAllocate(im, 0, 0, 0);

  /* Allocate the color white (red, green and blue all maximum). */
  white = gdImageColorAllocate(im, 255, 255, 255);

  /* Draw a line from the upper left to the lower right,
    using white color index. */
  gdImageLine(im, 0, 0, 63, 63, white);

  int imageSize;

  /* Output the image to the disk file in PNG format. */
  pngPointer = gdImagePngPtr(im, &imageSize);

  gdFree(pngPointer);

  // VALUE returnValues[1];
  // returnValues[0] = INT2NUM(imageSize);
  // returnValues[1] = pngPointer;

  // return rb_ary_new_from_values(1, returnValues);
  return rb_str_new(pngPointer, imageSize);
}

static VALUE destroyImage(VALUE self) {
  /* Destroy the image in memory. */
  gdFree(pngPointer);
  gdImageDestroy(im);
}

void Init_quality_map_c(void) {
  VALUE QualityMapC = rb_define_module("QualityMapC");
  VALUE Image = rb_define_class_under(QualityMapC, "Image", rb_cObject);

  rb_define_singleton_method(Image, "buildImage", buildImage, 0);
  rb_define_singleton_method(Image, "destroyImage", destroyImage, 0);
}