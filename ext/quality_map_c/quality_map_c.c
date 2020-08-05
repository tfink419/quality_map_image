#include "stdio.h"
#include "math.h"
#include "stdlib.h"
#include "vips/vips.h"

#include "ruby/ruby.h"
#include "ruby/encoding.h"

#include "gradient.h"


#define DEFAULT_MULTIPLY_CONST 1048576 // 2 ^ 19

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
      last = rb_ary_entry(rb_ary_entry(rb_ary_entry(ary,i),j),coordsLength-1);
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

  *poly = malloc((*length) * 4 * sizeof **poly);
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

static int MemArrayToPngPointerWithFilter(VipsObject *scope, unsigned char *image_mem, unsigned char* found_mem, long size, void **pngPointer, int *imageSize) {
  VipsImage **ims = (VipsImage **) vips_object_local_array( scope, 7 );
  if(!(ims[0] = vips_image_new_from_memory( image_mem, 4 * size * size, size, size, 4, VIPS_FORMAT_UCHAR)))
    return -1;
  if(vips_copy(ims[0], ims+1, "bands", 1, "format", VIPS_FORMAT_UINT, NULL))
    return -1;
  if(found_mem) {
    // Apply median rank filtering on holes
    if(!(ims[2] = vips_image_new_from_memory( found_mem, size * size, size, size, 1, VIPS_FORMAT_UCHAR )) ||
      vips_median( ims[1], ims+3, 3, NULL ) ||
      vips_equal_const1( ims[2], ims+4, 1, NULL ) ||
      vips_ifthenelse( ims[4], ims[1], ims[3], ims+4, NULL ) )
        return -1;
  }
  else {
    ims[4] = ims[1];
    ims[1] = NULL;
  }
  if(vips_copy(ims[4], ims+5, "bands", 4, "format", VIPS_FORMAT_UCHAR, NULL))
    return -1;

  if( vips_pngsave_buffer(ims[5], pngPointer, imageSize, "compression", 9, NULL) )
    return -1;

  return 0;
}

// quality_calc_method is an
static VALUE qualityOfPointsImage(VALUE self,  VALUE multiply_const_ruby, VALUE lat_start_ruby, VALUE lng_start_ruby, VALUE range_ruby, VALUE polygons, VALUE quality_scale_ruby, VALUE quality_calc_method_ruby, VALUE quality_calc_value_ruby) {
  // X is lng, Y is lat
  long lng_start = NUM2INT(lng_start_ruby);
  long lat_start = NUM2INT(lat_start_ruby);
  double multiply_const = NUM2DBL(multiply_const_ruby);

  long range = NUM2INT(range_ruby);

  long lng_end = (range+lng_start-1);
  long lat_end = (range+lat_start-1);
  long polygons_length = RARRAY_LEN(polygons);

  enum QualityCalcMethod quality_calc_method = (enum QualityCalcMethod) NUM2INT(quality_calc_method_ruby);
  double quality_calc_value = NUM2DBL(quality_calc_value_ruby);
  double quality_scale = NUM2DBL(quality_scale_ruby);

  double *qualities;

  unsigned char *image_mem = malloc(range * range * 4 * sizeof *image_mem);
  unsigned char *found_mem = NULL;

  switch(quality_calc_method) {
    case QualityLogExpSum:
      qualities = malloc(polygons_length * sizeof *qualities);
      break;
    case QualityFirst:
      found_mem = malloc(range * range * sizeof *found_mem);
      break;
    default:
      break;
  }
  // Structure of Pointer
  // Polygons ->
  // Vectors -> x1, y1, x2, y2, x1, y1, x2, y2...
  long **polygons_as_vectors = malloc(polygons_length * sizeof *polygons_as_vectors);
  long *polygons_vectors_lengths = malloc(polygons_length * sizeof *polygons_vectors_lengths);

  for(long i = 0; i < polygons_length; i++) {
    RubyPointArrayToCVectorArray(rb_ary_entry(rb_ary_entry(polygons, i),0), polygons_as_vectors+i, polygons_vectors_lengths+i, multiply_const);
  }

  double value;

  unsigned long fixed_value;

  int allBlank = 1;

  for(long point[2] = {0, lat_end}, image_pos = 0, found_pos = 0; point[1] >= lat_start; point[1]--) {
    for(point[0] = lng_start; point[0] <= lng_end; point[0]++, found_pos++, image_pos += 4) {
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
          image_mem[image_pos] = (fixed_value >> 24) & 0xFF; // red
          image_mem[image_pos+1] = (fixed_value >> 16) & 0xFF; // green
          image_mem[image_pos+2] = (fixed_value >> 8) & 0xFF; // blue
          image_mem[image_pos+3] = fixed_value & 0xFF; // alpha
          if(fixed_value) {
            allBlank = 0;
          }
          break;
        }
        case QualityFirst:
        {
          found_mem[found_pos] = 0;
          for(long i = 0; i < polygons_length; i++) {
            if(PointInPolygon(point, polygons_as_vectors[i], polygons_vectors_lengths[i])) {
              value = NUM2DBL(rb_ary_entry(rb_ary_entry(polygons, i),1))*quality_scale;
              if(value > UINT32_MAX)
                fixed_value = UINT32_MAX;
              else if(value < 0)
                fixed_value = 0;
              else
                fixed_value = value;
              image_mem[image_pos] = (fixed_value >> 24) & 0xFF; // red
              image_mem[image_pos+1] = (fixed_value >> 16) & 0xFF; // green
              image_mem[image_pos+2] = (fixed_value >> 8) & 0xFF; // blue
              image_mem[image_pos+3] = fixed_value & 0xFF; // alpha
              found_mem[found_pos] = 1;
              allBlank = 0;
              break;
            }
          }
          if(!found_mem[found_pos]) {
            image_mem[image_pos] = image_mem[image_pos+1] = image_mem[image_pos+2] = image_mem[image_pos+3] = 0;
          }
        }
          break;
        default:
          rb_raise(rb_eRuntimeError, "%s", "Unknown Calc Method Type Chosen");
      }
    }
  }

  if(allBlank) {
    free(image_mem);
    switch(quality_calc_method) {
      case QualityLogExpSum:
        free(qualities);
        break;
      case QualityFirst:
        free(found_mem);
        break;
      default:
        break;
    }
    for(long i = 0; i < polygons_length; i++) {
      free(polygons_as_vectors[i]);
    }
    free(polygons_as_vectors);
    free(polygons_vectors_lengths);
    return Qnil;
  }

  size_t imageSize;
  void *pngPointer;
  VipsObject *scope;
  scope = VIPS_OBJECT( vips_image_new() );
  if(MemArrayToPngPointerWithFilter(scope, image_mem, found_mem, range, &pngPointer, &imageSize))
    vips_error_exit( NULL );

  VALUE ruby_blob = rb_str_new((char *)pngPointer, imageSize);

  g_free(pngPointer);
  g_object_unref( scope );
  free(image_mem);
  switch(quality_calc_method) {
    case QualityLogExpSum:
      free(qualities);
      break;
    case QualityFirst:
      free(found_mem);
      break;
    default:
      break;
  }
  for(long i = 0; i < polygons_length; i++) {
    free(polygons_as_vectors[i]);
  }
  free(polygons_as_vectors);
  free(polygons_vectors_lengths);

  return ruby_blob;
}

long num = 0;

static VALUE qualityOfPoint(VALUE self, VALUE lat, VALUE lng, VALUE polygons, VALUE quality_calc_method_ruby, VALUE quality_calc_value_ruby) {
  // X is lng, Y is lat
  long point[2] = { NUM2DBL(lng)*DEFAULT_MULTIPLY_CONST, NUM2DBL(lat)*DEFAULT_MULTIPLY_CONST };
  enum QualityCalcMethod quality_calc_method = (enum QualityCalcMethod) NUM2INT(quality_calc_method_ruby);
  double quality_calc_value = NUM2DBL(quality_calc_value_ruby);

  long polygons_length = RARRAY_LEN(polygons);

  double *qualities;
  switch(quality_calc_method) {
    case QualityLogExpSum:
      qualities = malloc(polygons_length * sizeof *qualities);
      break;
    default:
      break;
  }
  VALUE ids = rb_ary_new();
  long **polygons_as_vectors = malloc(polygons_length * sizeof *polygons_as_vectors);
  long *polygons_vectors_lengths = malloc(polygons_length * sizeof *polygons_vectors_lengths);

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
      free(qualities);
      break;
    default:
      break;
  }
  for(long i = 0; i < polygons_length; i++) {
    free(polygons_as_vectors[i]);
  }
  free(polygons_as_vectors);
  free(polygons_vectors_lengths);
  return rb_ary_new_from_args(2, DBL2NUM(quality), ids);
}

static int LogExpSumImages(VipsObject *scope, VipsImage **images, long num_images, VipsImage **image, double exp) {
  VipsImage **ims = (VipsImage **) vips_object_local_array( scope, num_images +6);
  VipsImage *zero_im, *temp_im1, *temp_im2;
  zero_im = vips_image_new_from_image1( images[0], 0 );
  if(!zero_im)
    return -1;
  for(long i = 0; i < num_images; i++) {
    // Add exp ^ pixel_value to each other
    // If pixel_value == 0, drop it to 0
    if(vips_math2_const(images[i], &temp_im1, VIPS_OPERATION_MATH2_WOP, &exp, 1, NULL) ||
      vips_equal(zero_im, images[i], &temp_im2, NULL) ||
      vips_ifthenelse(temp_im2, zero_im, temp_im1, ims+i, NULL))
        return -1;
    g_object_unref(temp_im1);
    g_object_unref(temp_im2);
  }
  // take sum
  // turn sum_value into 1 if == 0
  // take log_exp(sum)
  if(!(ims[num_images] = vips_image_new_from_image1( images[0], 1 )) ||
    vips_sum(ims, ims+num_images+1, num_images, NULL) ||
    vips_equal(zero_im, ims[num_images+1], ims+num_images+2, NULL) ||
    vips_ifthenelse(ims[num_images+2], ims[num_images], ims[num_images+1], ims+num_images+3, NULL) ||
    vips_log(ims[num_images+3], ims+num_images+4, NULL) ||
    vips_linear1(ims[num_images+4], image, 1/log(exp), 0, NULL))
      return -1;
  g_object_unref(zero_im);
  return 0;
}

static int FixUpImage(VipsObject *scope, long size, VALUE images, VipsImage **out, long range_low, long range_high, double ratio, double scale, int invert, enum QualityCalcMethod quality_calc_method, double quality_calc_value) {
  Check_Type(images, T_ARRAY);
  long num_images = RARRAY_LEN(images);

  VipsImage *temp_im1, *temp_im2, *temp_band;
  VipsImage **ims = (VipsImage **) vips_object_local_array( scope, 10 );
  if(num_images > 0) {
    ratio = (GRADIENT_MAP_SIZE-1.0)/(range_high-range_low)*ratio;
    VipsImage **image_pieces = (VipsImage **) vips_object_local_array( scope, num_images );
    for(long i = 0; i < num_images; i++)
    {
      VipsImage **bands = (VipsImage **) vips_object_local_array( scope, 4 );
      // Scale ratio to range
      if(vips_pngload_buffer (RSTRING_PTR(rb_ary_entry(images, i)),
                      RSTRING_LEN(rb_ary_entry(images, i)),
                      &temp_im1, NULL))
        return -1;
      // Extract 4 UCHAR bands from image and turn it into a 1 band UINT
      for(long j = 0; j < 4; j++) {
        if(vips_extract_band(temp_im1, bands+j, j, NULL))
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
      // sum bands together then, pix = pix/scale
      if(vips_sum (bands, &temp_im2, 4, NULL) || 
          vips_linear1 (temp_im2, image_pieces+i, 1/scale, -range_low, NULL) )
        return -1;
      g_object_unref(temp_im2);
      g_object_unref(temp_im1);
    }

    switch(quality_calc_method) {
      case QualityFirst:
        ims[0] = image_pieces[0];
        image_pieces[0] = NULL;
        break;
      case QualityLogExpSum:
        if(LogExpSumImages(scope, image_pieces, num_images, ims, quality_calc_value))
          return -1;
        break;
      default:
        break;
    }

    // Now that its all summed together, subtract range, if we did this before, 0's would all be -5 added together,
    // e.g. [0,0].sum - 5 == -5 would become [0-5, 0-5].sum == -10
    if(vips_linear1 (ims[0], ims+1, 1, -range_low, NULL) )
      return -1;
    char path[256];
    sprintf(path, "vips-after-image%d.v", num);
    vips_image_write_to_file(ims[1], path, NULL);
    // if(pix < 0) { pix = 0 }, if(pix > high-low) { px = high-low}
    if( !(ims[2] = vips_image_new_from_image1( ims[1], 0 )) ||
        !(ims[3] = vips_image_new_from_image1( ims[1], range_high-range_low )) ||
        vips_less( ims[1], ims[2], ims+4, NULL ) ||
        vips_ifthenelse( ims[4], ims[2], ims[1], ims+5, NULL ) ||
        vips_more( ims[5], ims[3], ims+6, NULL ) ||
        vips_ifthenelse( ims[6], ims[3], ims[5], ims+7, NULL ) ) 
          return -1;

    if(invert) {
      // num = high-low-num
      if(vips_linear1(ims[7], ims+8, -1, range_high-range_low, NULL))
        return -1;
    } else {
      ims[8] = ims[7];
      ims[7] = NULL; // This shuts up a bunch of crazy glib memory erase errors
    }
    // Multiply by ratio and save
    if(vips_linear1(ims[8], out, ratio, 0, NULL))
      return -1;
  }
  else { // When no images were given
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
    "Image Data Array must have format: [range_low, range_high, ratio, scale, invert, quality_calc_method, quality_calc_value]";
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
    VipsImage **images_in = malloc(num_images * sizeof *images_in);
    long range_low, range_high;
    double ratio, scale;
    int invert;
    enum QualityCalcMethod quality_calc_method;
    double quality_calc_value;
    VipsObject *scope = VIPS_OBJECT( vips_image_new() );
    for(long i = 0; i < num_images; i++) {
      Check_Type(rb_ary_entry(image_data, i), T_ARRAY);
      if(RARRAY_LEN(rb_ary_entry(image_data, i)) != 7)
        rb_raise(rb_eRuntimeError, "%s", image_data_err);

      range_low = NUM2INT(rb_ary_entry(rb_ary_entry(image_data, i), 0));
      range_high = NUM2INT(rb_ary_entry(rb_ary_entry(image_data, i), 1));
      ratio = NUM2DBL(rb_ary_entry(rb_ary_entry(image_data, i), 2));
      scale = NUM2DBL(rb_ary_entry(rb_ary_entry(image_data, i), 3));
      invert = rb_ary_entry(rb_ary_entry(image_data, i), 4) == Qtrue;
      quality_calc_method = (enum QualityCalcMethod) NUM2INT(rb_ary_entry(rb_ary_entry(image_data, i), 5));
      quality_calc_value = NUM2DBL(rb_ary_entry(rb_ary_entry(image_data, i), 6));
      if(FixUpImage(
          scope,
          size,
          rb_ary_entry(images,i),
          images_in+i,
          range_low,
          range_high,
          ratio,
          scale,
          invert,
          quality_calc_method,
          quality_calc_value
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
    free(images_in);
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


void Init_quality_map_c(void) {
  VALUE QualityMapC = rb_define_module("QualityMapC");
  VALUE Image = rb_define_class_under(QualityMapC, "Image", rb_cObject);
  VALUE Point = rb_define_class_under(QualityMapC, "Point", rb_cObject);

  rb_define_singleton_method(Point, "qualityOfPointsImage", qualityOfPointsImage, 8);
  rb_define_singleton_method(Point, "qualityOfPoint", qualityOfPoint, 5);

  rb_define_singleton_method(Image, "buildImage", buildImage, 3);
}