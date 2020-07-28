#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <iostream>
#include <vips/vips.h>

#include "ruby/ruby.h"
#include "ruby/encoding.h"

#include "gradient.hpp"

using namespace std;

#define DEFAULT_MULTIPLY_CONST 1048576 // 2 ^ 19


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
RubyPointArrayToCVectorArray(VALUE ary, long **poly, long *length, double multiply_const)
{
  *length = 0;
  const char* earg =
    "Paths have format: [[p0_x, p0_y], [p1_x, p1_y], ...]";

  Check_Type(ary, T_ARRAY);
  long polygonsLength = RARRAY_LEN(ary);

  VALUE first, last;
  double x1, x2, y1, y2;

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
      x1 = NUM2DBL(rb_ary_entry(first, 0));
      x2 = NUM2DBL(rb_ary_entry(last, 0));
      y1 = NUM2DBL(rb_ary_entry(first, 1));
      y2 = NUM2DBL(rb_ary_entry(last, 1));

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
        (*poly)[poly_ind++] = NUM2DBL(rb_ary_entry(coord1, 0)) * multiply_const;
        (*poly)[poly_ind++] = NUM2DBL(rb_ary_entry(coord1, 1)) * multiply_const;
        (*poly)[poly_ind++] = NUM2DBL(rb_ary_entry(coord2, 0)) * multiply_const;
        (*poly)[poly_ind++] = NUM2DBL(rb_ary_entry(coord2, 1)) * multiply_const;
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

long QualitiesOfPoint(long point[2], double *qualities, long **polygons_as_vectors, long *polygons_vectors_lengths, VALUE polygons, long polygons_length, VALUE ids) {
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

// quality_calc_method is an
static VALUE qualityOfPointsImage(VALUE self,  VALUE multiply_const_ruby, VALUE lat_start_ruby, VALUE lng_start_ruby, 
  VALUE lat_range_ruby, VALUE lng_range_ruby, VALUE polygons, VALUE quality_scale_ruby, VALUE quality_calc_method_ruby, VALUE quality_calc_value_ruby) {
  // X is lng, Y is lat
  long lng_start = NUM2INT(lng_start_ruby)*2;
  long lat_start = NUM2INT(lat_start_ruby)*2;
  double multiply_const = NUM2DBL(multiply_const_ruby)*2;

  long lat_range = NUM2INT(lat_range_ruby)*2, lng_range = NUM2INT(lng_range_ruby)*2;

  long lng_end = (lng_range+lng_start-1);
  long lat_end = (lat_range+lat_start-1);
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

  unsigned char *mem = new unsigned char[4 * lat_range * lng_range];

  // Structure of Pointer
  // Polygons ->
  // Vectors -> x1, y1, x2, y2, x1, y1, x2, y2...
  long **polygons_as_vectors = new long *[polygons_length];
  long *polygons_vectors_lengths = new long[polygons_length];

  for(long i = 0; i < polygons_length; i++) {
    RubyPointArrayToCVectorArray(rb_ary_entry(rb_ary_entry(polygons, i),0), polygons_as_vectors+i, polygons_vectors_lengths+i, multiply_const);
  }

  unsigned long fixed_value;
  double value;

  for(long point[2] = {0, lat_end}, pos = 0; point[1] >= lat_start; point[1]--) {
    for(point[0] = lng_start; point[0] <= lng_end; point[0]++, pos += 4) {
      switch(quality_calc_method) {
        case QualityLogExpSum:
        {
          long num_qualities = QualitiesOfPoint(
            point,
            qualities,
            polygons_as_vectors,
            polygons_vectors_lengths,
            polygons,
            polygons_length,
            NULL
          );
          value = LogExpSum(qualities, num_qualities, quality_calc_value)*quality_scale;
          if(value > UINT32_MAX)
            fixed_value = UINT32_MAX;
          else if(value < 0)
            fixed_value = 0;
          else
            fixed_value = value;
          mem[pos] = (fixed_value >> 24) & 0xFF; // red
          mem[pos+1] = (fixed_value >> 16) & 0xFF; // green
          mem[pos+2] = (fixed_value >> 8) & 0xFF; // blue
          mem[pos+3] = fixed_value & 0xFF; // alpha
          break;
        }
        case QualityFirst:
        {
          bool found = false;
          for(long i = 0; i < polygons_length; i++) {
            if(PointInPolygon(point, polygons_as_vectors[i], polygons_vectors_lengths[i])) {
              value = NUM2DBL(rb_ary_entry(rb_ary_entry(polygons, i),1))*quality_scale;
              if(value > UINT32_MAX)
                fixed_value = UINT32_MAX;
              else if(value < 0)
                fixed_value = 0;
              else
                fixed_value = value;
              mem[pos] = (fixed_value >> 24) & 0xFF; // red
              mem[pos+1] = (fixed_value >> 16) & 0xFF; // green
              mem[pos+2] = (fixed_value >> 8) & 0xFF; // blue
              mem[pos+3] = fixed_value & 0xFF; // alpha
              found = true;
              break;
            }
          }
          if(!found) {
            mem[pos] = mem[pos+1] = mem[pos+2] = mem[pos+3] = 0;
          }
        }
          break;
        default:
          rb_raise(rb_eRuntimeError, "%s", "Unknown Calc Method Type Chosen");
      }
    }
  }
  size_t imageSize;
  VipsImage *original, *subsampled;
  void *pngPointer;
  /* Turn the array we made into a vips_image */
  original = vips_image_new_from_memory( mem, 4 * lat_range * lng_range, lng_range, lat_range, 4, VIPS_FORMAT_UCHAR );
  original->Type = VIPS_INTERPRETATION_sRGB;
  if(!original) vips_error_exit( NULL );
  if( vips_subsample(original, &subsampled, 2, 2, NULL) )
    vips_error_exit( NULL );
  g_object_unref(original);
  if( vips_pngsave_buffer(subsampled, &pngPointer, &imageSize, "compression", 9, NULL) )
    vips_error_exit( NULL );
  g_object_unref(subsampled);
  
  VALUE ruby_blob = rb_str_new((char *)pngPointer, imageSize);
  g_free(pngPointer);

  delete[] mem;
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
  long point[2] = { NUM2DBL(lng)*DEFAULT_MULTIPLY_CONST, NUM2DBL(lat)*DEFAULT_MULTIPLY_CONST };
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
    RubyPointArrayToCVectorArray(rb_ary_entry(rb_ary_entry(polygons, i),0), polygons_as_vectors+i, polygons_vectors_lengths+i, DEFAULT_MULTIPLY_CONST);
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

static int fixUpImage(VipsObject *scope, long size, VALUE image, VipsImage **out, long range_low, long range_high, double ratio, double scale, int invert) {
  VipsImage *bands[4];
  VipsImage **ims = (VipsImage **) vips_object_local_array( scope, 10 );
  VipsImage *temp_band;
  if(!NIL_P(image)) {
    // Scale ratio to range
    ratio = (GRADIENT_MAP_SIZE-1.0)/(range_high-range_low)*ratio;
    if(vips_pngload_buffer (RSTRING_PTR(image),
                    RSTRING_LEN(image),
                    ims, NULL))
      return -1;
    // Extract 4 UCHAR bands from image and turn it into a 1 band UINT
    for(long j = 0; j < 4; j++) {
      if(vips_extract_band(ims[0], bands+j, j, NULL))
        return -1;
      bands[j]->BandFmt = VIPS_FORMAT_UCHAR;
      if(vips_cast_uint(bands[j], &temp_band, NULL))
        return -1;
      g_object_unref(bands[j]);
      bands[j] = temp_band;
      if(j < 3) {
        if(vips_lshift_const1 (bands[j], &temp_band, 8*(3-j), NULL))
          return -1;
        g_object_unref(bands[j]);
        bands[j] = temp_band;
      }
    }
    // sum bands together then, pix = pix/scale - low
    if(vips_sum (bands, ims+1, 4, NULL) || 
        vips_linear1 (ims[1], ims+2, 1/scale, -range_low, NULL) )
      return -1;
    for(long j = 0; j < 4; j++) {
      g_object_unref(bands[j]);
    }
    // if(pix < 0) { pix = 0 }, if(pix > high-low) { px = high-low}
    if( !(ims[3] = vips_image_new_from_image1( ims[1], 0 )) ||
        !(ims[4] = vips_image_new_from_image1( ims[1], range_high-range_low )) ||
        vips_less( ims[2], ims[3], ims+5, NULL ) ||
        vips_ifthenelse( ims[5], ims[3], ims[2], ims+6, NULL ) ||
        vips_more( ims[6], ims[4], ims+7, NULL ) ||
        vips_ifthenelse( ims[7], ims[4], ims[6], ims+8, NULL ) ) 
        return -1;

    if(invert) {
      // num = high-low-num
      if(vips_linear1(ims[8], ims+9, -1, range_high-range_low, NULL))
        return -1;
    } else {
      ims[9] = ims[8];
      ims[8] = NULL; // This shuts up a bunch of crazy glib memory erase errors
    }
    // Multiply by ratio and save
    if(vips_linear1(ims[9], out, ratio, 0, NULL))
      return -1;
  }
  else { // When no image was given
    // Make a black and return it
    if(vips_black (ims, size, size, "bands", 1, NULL))
      return -1;
    if(invert) { // make it a white image and multiply it by ratio
      if(vips_linear1(ims[0], &ims[1], 1, 255.0, NULL))
        return -1;

      if(vips_linear1(ims[1], out, ratio, 0, NULL))
        return -1;
    }
    else {
      *out = ims[0];
      ims[0] = NULL;
    }
  }
  return 0;
}

static VALUE buildImage(VALUE self, VALUE size_ruby, VALUE images, VALUE image_data) {
  const char* image_data_err =
    "Image Data Array must have format: [range_low, range_high, ratio, scale, invert]";
  const char* image_arrays_lineup_err =
    "Image Array and Image Data Array must have same lengths";

  Check_Type(images, T_ARRAY);
  Check_Type(image_data, T_ARRAY);

  long num_images;
  long size = NUM2INT(size_ruby);

  if((num_images = RARRAY_LEN(images)) != RARRAY_LEN(image_data))
    rb_raise(rb_eRuntimeError, "%s", image_arrays_lineup_err);
  
  VipsImage *image_1, *image_2;

  if(num_images) {
    VipsImage **images_in = new VipsImage*[num_images];
    long range_low, range_high;
    double ratio, scale;
    bool invert;
    VipsObject *scope;
    scope = VIPS_OBJECT( vips_image_new() );
    for(long i = 0; i < num_images; i++) {
      Check_Type(rb_ary_entry(image_data, i), T_ARRAY);
      if(RARRAY_LEN(rb_ary_entry(image_data, i)) != 5)
        rb_raise(rb_eRuntimeError, "%s", image_data_err);

      range_low = NUM2INT(rb_ary_entry(rb_ary_entry(image_data, i), 0));
      range_high = NUM2INT(rb_ary_entry(rb_ary_entry(image_data, i), 1));
      ratio = NUM2DBL(rb_ary_entry(rb_ary_entry(image_data, i), 2));
      scale = NUM2DBL(rb_ary_entry(rb_ary_entry(image_data, i), 3));
      invert = rb_ary_entry(rb_ary_entry(image_data, i), 4) == Qtrue;
      if(fixUpImage(
          scope,
          size,
          rb_ary_entry(images,i),
          images_in+i,
          range_low,
          range_high,
          ratio,
          scale,
          invert
        )) {
        g_object_unref( scope );
        vips_error_exit( NULL );
      }
    }
    g_object_unref( scope );

    // Sum together all those images
    if(vips_sum (images_in, &image_1, num_images, NULL))
      vips_error_exit( NULL );
    for(long i = 0; i < num_images; i++) {
      g_object_unref( images_in[i] );
    }
    delete[] images_in;
  }
  else {
    // Make a white image
    if(vips_black (&image_1, size, size, "bands", 1, NULL))
      vips_error_exit( NULL );
    if(vips_linear1(image_1, &image_2, 1, 255.0, NULL))
      vips_error_exit( NULL );
    g_object_unref(image_1);
    image_1 = image_2;
  }

  // Round and cast to uchar
  if(vips_round (image_1, &image_2, VIPS_OPERATION_ROUND_RINT, NULL))
    vips_error_exit( NULL );
  g_object_unref(image_1);
  if(vips_cast_uchar(image_2, &image_1, NULL))
    vips_error_exit( NULL );
  g_object_unref(image_2);


  VipsImage *lookup_table;

  // False colorize
  lookup_table = vips_image_new_from_memory(
    (void *) GRADIENT_MAP, 
    GRADIENT_MAP_CHANNELS * GRADIENT_MAP_SIZE,
    GRADIENT_MAP_SIZE,
    1,
    GRADIENT_MAP_CHANNELS,
    VIPS_FORMAT_UCHAR
  );
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
}

typedef VALUE (*ruby_method)(...);

void Init_quality_map_c(void) {
  VALUE QualityMapC = rb_define_module("QualityMapC");
  VALUE Image = rb_define_class_under(QualityMapC, "Image", rb_cObject);
  VALUE Point = rb_define_class_under(QualityMapC, "Point", rb_cObject);

  rb_define_singleton_method(Point, "qualityOfPointsImage", (ruby_method) qualityOfPointsImage, 9);
  rb_define_singleton_method(Point, "qualityOfPoint", (ruby_method) qualityOfPoint, 5);

  rb_define_singleton_method(Image, "buildImage", (ruby_method) buildImage, 3);
}

} // extern "C"