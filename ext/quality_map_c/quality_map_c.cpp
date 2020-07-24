#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <iostream>
#include <vips/vips.h>
#include <png.h>

#include "ruby/ruby.h"
#include "ruby/encoding.h"

#include "gradient.hpp"

using namespace std;

#define MULTIPLY_CONST 10000000
#define COORD_MULTIPLE 1000
#define MULTIPLE_DIFF  10000


extern "C" {

enum QualityCalcMethod {QualityLogExpSum = 0, QualityFirst = 1}; 


const int ALPHA = 64;

void *png_pointer = NULL;

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

int PointInPolygon(long *point, long *polygon, long polygon_vectors_length) {
  int intersections = 0;
  long num_values = polygon_vectors_length*4;
  for(long ind = 0; ind < num_values; ind += 4) {
    if ( ((polygon[ind+1]>point[1]) != (polygon[ind+3]>point[1])) &&
     (point[0] < ((long long) polygon[ind+2]-polygon[ind]) * ((long long)point[1]-polygon[ind+1]) / ((long long)polygon[ind+3]-polygon[ind+1]) + polygon[ind]) ) {
       intersections++;
    }
  }
  return (intersections & 1) == 1;
};

// Adding all lines works perfectly fine for any number 
// of polygons with or without holes when using line sweep
static void
RubyPointArrayToCVectorArray(VALUE ary, long **poly, long *length)
{
  *length = 0;
  const char* earg =
    "Paths have format: [[p0_x, p0_y], [p1_x, p1_y], ...]";

  Check_Type(ary, T_ARRAY);
  long polygonsLength = RARRAY_LEN(ary);

  VALUE first, last;
  long x1, x2, y1, y2;

  long i, j, k, polygonLength, coordsLength;

  for(i = 0; i < polygonsLength; i++) {
    Check_Type(rb_ary_entry(ary,i), T_ARRAY);
    polygonLength = RARRAY_LEN(rb_ary_entry(ary,i));
    for(j = 0; j < polygonLength; j++) {
      Check_Type(rb_ary_entry(rb_ary_entry(ary,i),j), T_ARRAY);
      coordsLength = RARRAY_LEN(rb_ary_entry(rb_ary_entry(ary,i),j));
      first = rb_ary_entry(rb_ary_entry(rb_ary_entry(ary,i),j),0);
      Check_Type(first, T_ARRAY);
      if(RARRAY_LEN(first) != 2) {
        rb_raise(rb_eArgError, "%s", earg);
      }
      last = rb_ary_entry(rb_ary_entry(rb_ary_entry(ary,i),j),0);
      Check_Type(last, T_ARRAY);
      if(RARRAY_LEN(last) != 2) {
        rb_raise(rb_eArgError, "%s", earg);
      }
      x1 = NUM2DBL(rb_ary_entry(first, 0)) * MULTIPLY_CONST;
      x2 = NUM2DBL(rb_ary_entry(last, 0)) * MULTIPLY_CONST;
      y1 = NUM2DBL(rb_ary_entry(first, 1)) * MULTIPLY_CONST;
      y2 = NUM2DBL(rb_ary_entry(last, 1)) * MULTIPLY_CONST;

      // Make sure the first point is the last point
      if(x1 != x2 || y1 != y2) {
        rb_ary_push(rb_ary_entry(rb_ary_entry(ary,i),j), first);
        coordsLength++;
      }
      *length += coordsLength-1;
    }
  }

  *poly = new long[(*length)*4];
  long poly_ind = 0;
  VALUE coord1, coord2;

  for(long i = 0; i < polygonsLength; i++) {
    polygonLength = RARRAY_LEN(rb_ary_entry(ary,i));
    for(j = 0; j < polygonLength; j++) {
      coordsLength = RARRAY_LEN(rb_ary_entry(rb_ary_entry(ary,i),j));
      for(k = 1; k < coordsLength; k++) {
        coord1 = rb_ary_entry(rb_ary_entry(rb_ary_entry(ary,i),j),k-1);
        coord2 = rb_ary_entry(rb_ary_entry(rb_ary_entry(ary,i),j),k);
        // Already checked coord1 in the first loop or previous iteration of this loop
        Check_Type(coord2, T_ARRAY);
        if(RARRAY_LEN(coord2) != 2) {
          rb_raise(rb_eArgError, "%s", earg);
        }
        (*poly)[poly_ind++] = NUM2DBL(rb_ary_entry(coord1, 0)) * MULTIPLY_CONST;
        (*poly)[poly_ind++] = NUM2DBL(rb_ary_entry(coord1, 1)) * MULTIPLY_CONST;
        (*poly)[poly_ind++] = NUM2DBL(rb_ary_entry(coord2, 0)) * MULTIPLY_CONST;
        (*poly)[poly_ind++] = NUM2DBL(rb_ary_entry(coord2, 1)) * MULTIPLY_CONST;
      }
    }
  }


  // TODO: Sort vectors by lowest x position so 
  // the sweeping algorithm can stop when it gets to the points x
}

static void checkPointArray(VALUE point) {
  const char* pointError =
    "Points have format: [[p1_lat, p1_long, p1_quality], [p2_lat, p2_long, p2_quality], ...]";

  Check_Type(point, T_ARRAY);

  if(RARRAY_LEN(point) != 3) {
    rb_raise(rb_eArgError, "%s", pointError);
  }
}

long QualitiesOfPoint(long point[2], double *&qualities, long **&polygons_as_vectors, long *&polygons_vectors_lengths, VALUE &polygons, long &polygons_length, VALUE ids) {
  long num_qualities = 0;
  for(long i = 0; i < polygons_length; i++) {
    if(PointInPolygon(point, polygons_as_vectors[i], polygons_vectors_lengths[i])) {
      qualities[num_qualities++] = NUM2DBL(rb_ary_entry(rb_ary_entry(polygons, i),1));
      if(ids) {
        rb_ary_push(ids, rb_ary_entry(rb_ary_entry(polygons, i),2));
      }
    }
  }
  return num_qualities;
}

void user_write_data(png_structp png_ptr, png_bytep data, png_size_t length) {
  VALUE * ruby_blob_p = (VALUE *) png_get_io_ptr(png_ptr);
  rb_str_cat(*ruby_blob_p, (char *) data, length);
}

// quality_calc_method is an
static VALUE qualityOfPointsImage(VALUE self, VALUE lat_start_ruby, VALUE lng_start_ruby, 
  VALUE lat_range_ruby, VALUE lng_range_ruby, VALUE polygons, VALUE quality_scale_ruby, VALUE quality_calc_method_ruby, VALUE quality_calc_value_ruby) {
  // X is lng, Y is lat
  long lng_start = ((long)NUM2INT(lng_start_ruby))*MULTIPLE_DIFF;
  long lat_start = ((long)NUM2INT(lat_start_ruby))*MULTIPLE_DIFF;

  long lat_range = NUM2INT(lat_range_ruby), lng_range = NUM2INT(lng_range_ruby);

  long lng_end = ((long)lng_range*MULTIPLE_DIFF)+lng_start-MULTIPLE_DIFF;
  long lat_end = ((long)lat_range*MULTIPLE_DIFF)+lat_start-MULTIPLE_DIFF;
  long polygons_length = RARRAY_LEN(polygons);

  enum QualityCalcMethod quality_calc_method = (enum QualityCalcMethod) NUM2INT(quality_calc_method_ruby);
  double quality_calc_value = NUM2DBL(quality_calc_value_ruby);
  double quality_scale = NUM2DBL(quality_scale_ruby);

  double *qualities;
  switch(quality_calc_method) {
    case QualityLogExpSum:
      qualities = new double[polygons_length];
      break;
    default:
      break;
  }

  png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if(!png_ptr) rb_raise(rb_eRuntimeError, "%s", "Png Base structure failed to be created.");
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if(!info_ptr)
  {
    png_destroy_write_struct(&png_ptr,
      (png_infopp)NULL);
    rb_raise(rb_eRuntimeError, "%s", "Png Info structure failed to be created.");
  }
  setjmp(png_jmpbuf(png_ptr));
  png_set_write_status_fn(png_ptr, NULL);
  png_set_IHDR(png_ptr, info_ptr, lng_range, lat_range,
      16, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
      PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  VALUE ruby_blob = rb_str_new2("");
  png_set_write_fn(png_ptr, &ruby_blob, user_write_data, NULL);
  png_set_compression_level(png_ptr, 1); // Z_BEST_SPEED
  // png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);
  png_write_info(png_ptr, info_ptr);

  png_bytep row = new png_byte[lng_range*2];

  // Structure of Pointer
  // Polygons ->
  // Vectors -> x1, y1, x2, y2, x1, y1, x2, y2...
  long **polygons_as_vectors = new long *[polygons_length];
  long *polygons_vectors_lengths = new long[polygons_length];

  for(long i = 0; i < polygons_length; i++) {
    RubyPointArrayToCVectorArray(rb_ary_entry(rb_ary_entry(polygons, i),0), polygons_as_vectors+i, polygons_vectors_lengths+i);
  }

  unsigned short value;
  unsigned char low, high;

  for(long point[2] = {0, lat_end}, x; point[1] >= lat_start; point[1] -= MULTIPLE_DIFF) {
    for(point[0] = lng_start, x = 0; point[0] <= lng_end; point[0] += MULTIPLE_DIFF, x += 2) {
      switch(quality_calc_method) {
        case QualityLogExpSum:
        {
          long num_qualities = QualitiesOfPoint(point, qualities, polygons_as_vectors, polygons_vectors_lengths, polygons, polygons_length, NULL);
          value = LogExpSum(qualities, num_qualities, quality_calc_value)*quality_scale;
          high = value & 0xFF;
          low = value >> 8;
          row[x] = high;
          row[x+1] = low;
          break;
        }
        case QualityFirst:
        {
          bool found = false;
          for(long i = 0; i < polygons_length; i++) {
            if(PointInPolygon(point, polygons_as_vectors[i], polygons_vectors_lengths[i])) {
              value = NUM2DBL(rb_ary_entry(rb_ary_entry(polygons, i),1))*quality_scale;
              high = value & 0xFF;
              low = value >> 8;
              row[x] = high;
              row[x+1] = low;
              found = true;
              break;
            }
          }
          if(!found) {
            row[x] = row[x+1] = 0;
          }
        }
          break;
        default:
          rb_raise(rb_eRuntimeError, "%s", "Unknown Calc Method Type Chosen");
      }
    }
    png_write_row(png_ptr, row);
  }
  png_write_end(png_ptr, info_ptr);
  png_destroy_write_struct(&png_ptr, &info_ptr);
  delete[] row;

  switch(quality_calc_method) {
    case QualityLogExpSum:
      delete[] qualities;
      break;
    default:
      break;
  }
  for(long i = 0; i < polygons_length; i++) {
    delete[] polygons_as_vectors[i];
  }
  delete[] polygons_as_vectors;
  delete[] polygons_vectors_lengths;
  return ruby_blob;
}

static VALUE qualityOfPoint(VALUE self, VALUE lat, VALUE lng, VALUE polygons, VALUE quality_calc_method_ruby, VALUE quality_calc_value_ruby) {
  // X is lng, Y is lat
  long point[2] = { ((long)NUM2INT(lng))*MULTIPLE_DIFF, ((long)NUM2INT(lat))*MULTIPLE_DIFF };
  enum QualityCalcMethod quality_calc_method = (enum QualityCalcMethod) NUM2INT(quality_calc_method_ruby);
  double quality_calc_value = NUM2DBL(quality_calc_value_ruby);

  long polygons_length = RARRAY_LEN(polygons);

  double *qualities;
  switch(quality_calc_method) {
    case QualityLogExpSum:
      qualities = new double[polygons_length];
      break;
    default:
      break;
  }
  VALUE ids = rb_ary_new();
  long **polygons_as_vectors = new long *[polygons_length];
  long *polygons_vectors_lengths = new long[polygons_length];

  for(long i = 0; i < polygons_length; i++) {
    RubyPointArrayToCVectorArray(rb_ary_entry(rb_ary_entry(polygons, i),0), polygons_as_vectors+i, polygons_vectors_lengths+i);
  }

  double quality = 0;
  switch(quality_calc_method) {
    case QualityLogExpSum:
    {
      long num_qualities = QualitiesOfPoint(point, qualities, polygons_as_vectors, polygons_vectors_lengths, polygons, polygons_length, ids);
      quality = LogExpSum(qualities, num_qualities, quality_calc_value);
      break;
    }
    case QualityFirst:
      for(long i = 0; i < polygons_length; i++) {
        if(PointInPolygon(point, polygons_as_vectors[i], polygons_vectors_lengths[i])) {
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
    default:
      break;
  }
  for(long i = 0; i < polygons_length; i++) {
    delete[] polygons_as_vectors[i];
  }
  delete[] polygons_as_vectors;
  delete[] polygons_vectors_lengths;
  return rb_ary_new_from_args(2, DBL2NUM(quality), ids);
}

static VALUE buildImage(VALUE self, VALUE size_ruby, VALUE images, VALUE image_data) {
  const char* image_data_err =
    "Image Data Array must have format: [range_low, range_high, multiple, scale, invert]";

  Check_Type(images, T_ARRAY);
  Check_Type(image_data, T_ARRAY);

  long num_images;
  long size = NUM2INT(size_ruby);
  long full_size = size*size;

  if((num_images = RARRAY_LEN(images)) == 0) return rb_str_new2("");

  if(RARRAY_LEN(rb_ary_entry(image_data, 0)) != 5)
    rb_raise(rb_eRuntimeError, "%s", image_data_err);
  
  VipsImage **images_in = new VipsImage*[num_images];
  VipsImage *image_1, *image_2, *image_3;
  long range_low, range_high;
  double multiple, scale;
  bool invert;

  for(long i = 0; i < num_images; i++) {
    if(vips_pngload_buffer (RSTRING_PTR(rb_ary_entry(images, i)),
                    RSTRING_LEN(rb_ary_entry(images, i)),
                    &image_1, NULL))
      vips_error_exit( NULL );
    range_low = NUM2INT(rb_ary_entry(rb_ary_entry(image_data, i), 0));
    range_high = NUM2INT(rb_ary_entry(rb_ary_entry(image_data, i), 1));
    multiple = NUM2DBL(rb_ary_entry(rb_ary_entry(image_data, i), 2));
    multiple = (GRADIENT_MAP_SIZE-1)/(range_high-range_low)*multiple;
    scale = NUM2DBL(rb_ary_entry(rb_ary_entry(image_data, i), 3));
    invert = rb_ary_entry(rb_ary_entry(image_data, i), 4) == Qtrue;

    // num = num*1/scale - low
    if(vips_linear1(image_1, &image_2, 1/scale, 0-range_low, NULL))
      vips_error_exit( NULL );
    g_object_unref(image_1);

    // num = (num.abs+num)/2
    if(vips_abs (image_1, &image_2, NULL))
      vips_error_exit( NULL );
    if(vips_add (image_1, image_2, &image_3, NULL))
      vips_error_exit( NULL );
    g_object_unref(image_1);
    g_object_unref(image_2);
    if(vips_linear1(image_3, &image_1, 0.5, 0, NULL))
      vips_error_exit( NULL );
    g_object_unref(image_3);

    // num = high-low-num
    if(vips_linear1(image_1, &image_2, -1, range_high-range_low, NULL))
      vips_error_exit( NULL );
    g_object_unref(image_1);

    // num = (num.abs+num)/2
    if(vips_abs (image_2, &image_1, NULL))
      vips_error_exit( NULL );
    if(vips_add (image_1, image_2, &image_3, NULL))
      vips_error_exit( NULL );
    g_object_unref(image_1);
    g_object_unref(image_2);
    if(vips_linear1(image_3, &image_1, 0.5, 0, NULL))
      vips_error_exit( NULL );
    g_object_unref(image_3);

    if(!invert) {
      // num = high-low-num
      if(vips_linear1(image_2, &image_1, -1, range_high-range_low, NULL))
        vips_error_exit( NULL );
      g_object_unref(image_2);
      image_2 = image_1;
    } // else num was already inverted

    // Multiply by multiple
    if(vips_linear1(image_2, &image_1, multiple, 0, NULL))
      vips_error_exit( NULL );
    g_object_unref(image_2);

    // Round and cast to uchar then save to the array
    if(vips_round (image_1, &image_2, VIPS_OPERATION_ROUND_RINT, NULL))
      vips_error_exit( NULL );
    g_object_unref(image_1);
    if(vips_cast_uchar(image_2, images_in+i, NULL))
      vips_error_exit( NULL );
    g_object_unref(image_2);
  }

  if(vips_sum (images_in, &image_1, num_images, NULL))
    vips_error_exit( NULL );

  for(long i = 0; i < num_images; i++) {
    g_object_unref( images_in[i] );
  }
  delete[] images_in;

  VipsImage *lookup_table;

  // False colorize
  lookup_table = vips_image_new_from_memory( GRADIENT_MAP, GRADIENT_MAP_CHANNELS * GRADIENT_MAP_SIZE, GRADIENT_MAP_SIZE, 1, GRADIENT_MAP_CHANNELS, VIPS_FORMAT_UCHAR );
  if(!lookup_table) vips_error_exit( NULL );
  lookup_table->Type = VIPS_INTERPRETATION_sRGB;
  if(vips_maplut (image_1, &image_2, lookup_table , NULL))
    vips_error_exit( NULL );
  g_object_unref(image_1);
  g_object_unref(lookup_table);

  void *pngPointer = NULL;
  size_t imageSize;
  if( vips_pngsave_buffer(image_2, &pngPointer, &imageSize, NULL) )
    vips_error_exit( NULL );
  g_object_unref(image_2);
  
  VALUE stringRuby = rb_str_new((char *)pngPointer, imageSize);
  g_free(pngPointer);
  return stringRuby;
  return Qtrue;
}

typedef VALUE (*ruby_method)(...);

void Init_quality_map_c(void) {
  VALUE QualityMapC = rb_define_module("QualityMapC");
  VALUE Image = rb_define_class_under(QualityMapC, "Image", rb_cObject);
  VALUE Point = rb_define_class_under(QualityMapC, "Point", rb_cObject);

  rb_define_singleton_method(Point, "qualityOfPointsImage", (ruby_method) qualityOfPointsImage, 8);
  rb_define_singleton_method(Point, "qualityOfPoint", (ruby_method) qualityOfPoint, 5);

  rb_define_singleton_method(Image, "buildImage", (ruby_method) buildImage, 3);
}

} // extern "C"