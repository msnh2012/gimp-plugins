/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 * Decompose plug-in (C) 1997 Peter Kirchgessner
 * e-mail: peter@kirchgessner.net, WWW: http://www.kirchgessner.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
 * This filter decomposes RGB-images into several types of channels
 */

/* Event history:
 * V 1.00, PK, 29-Jul-97, Creation
 * V 1.01, PK, 19-Mar-99, Update for GIMP V1.1.3
 *                        Prepare for localization
 *                        Use g_message() in interactive mode
 */
static char ident[] = "@(#) GIMP Decompose plug-in v1.01 19-Mar-99";

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "gtk/gtk.h"
#include "libgimp/gimp.h"
#include "libgimp/stdplugins-intl.h"

/* Declare local functions
 */
static void      query  (void);
static void      run    (char      *name,
			 int        nparams,
			 GParam    *param,
			 int       *nreturn_vals,
			 GParam   **return_vals);

static gint32    decompose (gint32  image_id,
                            gint32  drawable_ID,
                            char    *extract_type,
                            gint32  *drawable_ID_dst);

static gint32 create_new_image (char *filename, guint width, guint height,
                GImageType type, gint32 *layer_ID, GDrawable **drawable,
                GPixelRgn *pixel_rgn);

static void show_message (const char *msg);
static int cmp_icase (char *s1, char *s2);
static void rgb_to_hsv (unsigned char *r, unsigned char *g, unsigned char *b,
                        unsigned char *h, unsigned char *s, unsigned char *v);

static void extract_rgb      (unsigned char *src, int bpp, int numpix,
                              unsigned char **dst);
static void extract_red      (unsigned char *src, int bpp, int numpix,
                              unsigned char **dst);
static void extract_green    (unsigned char *src, int bpp, int numpix,
                              unsigned char **dst);
static void extract_blue     (unsigned char *src, int bpp, int numpix,
                              unsigned char **dst);
static void extract_alpha    (unsigned char *src, int bpp, int numpix,
                              unsigned char **dst);
static void extract_hsv      (unsigned char *src, int bpp, int numpix,
                              unsigned char **dst);
static void extract_hue      (unsigned char *src, int bpp, int numpix,
                              unsigned char **dst);
static void extract_sat      (unsigned char *src, int bpp, int numpix,
                              unsigned char **dst);
static void extract_val      (unsigned char *src, int bpp, int numpix,
                              unsigned char **dst);
static void extract_cmy      (unsigned char *src, int bpp, int numpix,
                              unsigned char **dst);
static void extract_cyan     (unsigned char *src, int bpp, int numpix,
                              unsigned char **dst);
static void extract_magenta  (unsigned char *src, int bpp, int numpix,
                              unsigned char **dst);
static void extract_yellow   (unsigned char *src, int bpp, int numpix,
                              unsigned char **dst);
static void extract_cmyk     (unsigned char *src, int bpp, int numpix,
                              unsigned char **dst);
static void extract_cyank    (unsigned char *src, int bpp, int numpix,
                              unsigned char **dst);
static void extract_magentak (unsigned char *src, int bpp, int numpix,
                              unsigned char **dst);
static void extract_yellowk  (unsigned char *src, int bpp, int numpix,
                              unsigned char **dst);


static gint      decompose_dialog (void);

static void      decompose_close_callback (GtkWidget *widget,
                                           gpointer   data);
static void      decompose_ok_callback    (GtkWidget *widget,
                                           gpointer   data);
static void      decompose_toggle_update  (GtkWidget *widget,
                                           gpointer   data);

/* Maximum number of new images generated by an extraction */
#define MAX_EXTRACT_IMAGES 4

/* Description of an extraction */
typedef struct {
 char *type;            /* What to extract */
 int  dialog;           /* Dialog-Flag. Set it to 1 if you want to appear */
                        /* this extract function within the dialog */
 int  num_images;       /* Number of images to create */
 char *channel_name[MAX_EXTRACT_IMAGES];   /* Names of channels to extract */
                        /* Function that performs the extraction */
 void (*extract_fun)(unsigned char *src, int bpp, int numpix,
                     unsigned char **dst);
} EXTRACT;

static EXTRACT extract[] = {
  { N_("RGB"),     1,  3, { N_("red"), N_("green"), N_("blue") }, extract_rgb },
  { N_("Red"),     0,  1, { N_("red") }, extract_red },
  { N_("Green"),   0,  1, { N_("green") }, extract_green },
  { N_("Blue"),    0,  1, { N_("blue") }, extract_blue },
  { N_("HSV"),     1,  3, { N_("hue"), N_("saturation"), N_("value") },
                          extract_hsv },
  { N_("Hue"),     0,  1, { N_("hue") }, extract_hue },
  { N_("Saturation"),0,1, { N_("saturation") }, extract_sat },
  { N_("Value"),   0,  1, { N_("value") }, extract_val },
  { N_("CMY"),     1,  3, { N_("cyan"), N_("magenta"), N_("yellow") },
                          extract_cmy },
  { N_("Cyan"),    0,  1, { N_("cyan") }, extract_cyan },
  { N_("Magenta"), 0,  1, { N_("magenta") }, extract_magenta },
  { N_("Yellow"),  0,  1, { N_("yellow") }, extract_yellow },
  { N_("CMYK"),    1,  4, { N_("cyan_k"), N_("magenta_k"), N_("yellow_k"),
                            N_("black") }, extract_cmyk },
  { N_("Cyan_K"),  0,  1, { N_("cyan_k") }, extract_cyank },
  { N_("Magenta_K"), 0,1, { N_("magenta_k") }, extract_magentak },
  { N_("Yellow_K"), 0, 1, { N_("yellow_k") }, extract_yellowk },
  { N_("Alpha"),   1,  1, { N_("alpha") }, extract_alpha }
};

/* Number of types of extractions */
#define NUM_EXTRACT_TYPES (sizeof (extract)/sizeof (extract[0]))


typedef struct {
  char extract_type[32];
} DecoVals;

typedef struct {
  gint extract_flag[NUM_EXTRACT_TYPES];
  gint run;
} DecoInterface;

GPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

static DecoVals decovals =
{
  "rgb"     /* Decompose type */
};

static DecoInterface decoint =
{
  { 0 },    /*  extract flags */
  FALSE     /*  run  */
};

static GRunModeType run_mode;


MAIN ()

static void
query ()
{
  static GParamDef args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE, "image", "Input image (unused)" },
    { PARAM_DRAWABLE, "drawable", "Input drawable" },
    { PARAM_STRING, "decompose_type", "What to decompose: RGB, Red, Green,\
 Blue, HSV, Hue, Saturation, Value, CMY, Cyan, Magenta, Yellow, CMYK,\
 Cyan_K, Magenta_K, Yellow_K, Alpha" }
  };
  static GParamDef return_vals[] =
  {
    { PARAM_IMAGE, "new_image", "Output gray image" },
    { PARAM_IMAGE, "new_image",
        "Output gray image (N/A for single channel extract)" },
    { PARAM_IMAGE, "new_image",
        "Output gray image (N/A for single channel extract)" },
    { PARAM_IMAGE, "new_image",
        "Output gray image (N/A for single channel extract)" },
  };
  static int nargs = sizeof (args) / sizeof (args[0]);
  static int nreturn_vals = sizeof (return_vals) / sizeof (return_vals[0]);

  INIT_I18N ();

  gimp_install_procedure ("plug_in_decompose",
			  _("Decompose an image into different types of channels"),
			  _("This function creates new gray images with\
 different channel information in each of them"),
			  "Peter Kirchgessner",
			  "Peter Kirchgessner (peter@kirchgessner.net)",
			  "1997",
			  N_("<Image>/Image/Channel Ops/Decompose"),
			  "RGB*",
			  PROC_PLUG_IN,
			  nargs, nreturn_vals,
			  args, return_vals);
}

static void show_message (const char *message)
{
 if (run_mode == RUN_INTERACTIVE)
   g_message (message);
 else
   printf ("%s\n", message);
}


static void
run (char    *name,
     int      nparams,
     GParam  *param,
     int     *nreturn_vals,
     GParam **return_vals)
{
  static GParam values[MAX_EXTRACT_IMAGES+1];
  GStatusType status = STATUS_SUCCESS;
  GDrawableType drawable_type;
  gint32 num_images;
  gint32 image_ID_extract[MAX_EXTRACT_IMAGES];
  gint j;

  INIT_I18N ();

  run_mode = param[0].data.d_int32;

  *nreturn_vals = MAX_EXTRACT_IMAGES+1;
  *return_vals = values;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;
  for (j = 0; j < MAX_EXTRACT_IMAGES; j++)
  {
    values[j+1].type = PARAM_IMAGE;
    values[j+1].data.d_int32 = -1;
  }

  switch (run_mode)
    {
    case RUN_INTERACTIVE:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_decompose", &decovals);

      /*  First acquire information with a dialog  */
      if (! decompose_dialog ())
	return;
      break;

    case RUN_NONINTERACTIVE:
      /*  Make sure all the arguments are there!  */
      if (nparams != 4)
	status = STATUS_CALLING_ERROR;
      if (status == STATUS_SUCCESS)
	{
          strncpy (decovals.extract_type, param[3].data.d_string,
                   sizeof (decovals.extract_type));
          decovals.extract_type[sizeof (decovals.extract_type)-1] = '\0';
	}
      break;

    case RUN_WITH_LAST_VALS:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_decompose", &decovals);
      break;

    default:
      break;
    }

  /*  Make sure that the drawable is RGB color  */
  drawable_type = gimp_drawable_type (param[2].data.d_drawable);
  if ((drawable_type != RGB_IMAGE) && (drawable_type != RGBA_IMAGE))
  {
    show_message (_("plug_in_decompose: Can only work on RGB*_IMAGE"));
    status = STATUS_CALLING_ERROR;
  }
  if (status == STATUS_SUCCESS)
    {
      if (run_mode != RUN_NONINTERACTIVE)
        gimp_progress_init (_("Decomposing..."));

      num_images = decompose (param[1].data.d_image, param[2].data.d_drawable,
                              decovals.extract_type, image_ID_extract);

      if (num_images <= 0)
      {
        status = STATUS_EXECUTION_ERROR;
      }
      else
      {
        for (j = 0; j < num_images; j++)
        {
          values[j+1].data.d_int32 = image_ID_extract[j];
          gimp_image_enable_undo (image_ID_extract[j]);
          gimp_image_clean_all (image_ID_extract[j]);
          if (run_mode != RUN_NONINTERACTIVE)
            gimp_display_new (image_ID_extract[j]);
        }

        /*  Store data  */
        if (run_mode == RUN_INTERACTIVE)
          gimp_set_data ("plug_in_decompose", &decovals, sizeof (DecoVals));
      }
    }

  values[0].data.d_status = status;
}


/* Decompose an image. It returns the number of new (gray) images.
   The image IDs for the new images are returned in image_ID_dst.
   On failure, -1 is returned.
*/
static gint32
decompose (gint32 image_ID,
           gint32 drawable_ID,
           char   *extract_type,
           gint32 *image_ID_dst)

{
  int i, j, extract_idx, scan_lines;
  int height, width, tile_height, num_images;
  unsigned char *src = (unsigned char *)ident;  /* Just to satisfy gcc/lint */
  char filename[1024];
  unsigned char *dst[MAX_EXTRACT_IMAGES];
  gint32 layer_ID_dst[MAX_EXTRACT_IMAGES];
  GDrawable *drawable_src, *drawable_dst[MAX_EXTRACT_IMAGES];
  GPixelRgn pixel_rgn_src, pixel_rgn_dst[MAX_EXTRACT_IMAGES];

  extract_idx = -1;   /* Search extract type */
  for (j = 0; j < NUM_EXTRACT_TYPES; j++)
  {
    if (cmp_icase (extract_type, extract[j].type) == 0)
    {
      extract_idx = j;
      break;
    }
  }
  if (extract_idx < 0) return (-1);

  /* Check structure of source image */
  drawable_src = gimp_drawable_get (drawable_ID);
  if (drawable_src->bpp < 3)
  {
    show_message (_("decompose: not an RGB image"));
    return (-1);
  }
  if (   (extract[extract_idx].extract_fun == extract_alpha)
      && (!gimp_drawable_has_alpha (drawable_ID)))
  {
    show_message (_("decompose: No alpha channel available"));
    return (-1);
  }

  width = drawable_src->width;
  height = drawable_src->height;

  tile_height = gimp_tile_height ();
  gimp_pixel_rgn_init (&pixel_rgn_src, drawable_src, 0, 0, width, height,
                       FALSE, FALSE);

  /* allocate a buffer for retrieving information from the src pixel region  */
  src = (unsigned char *)g_malloc (tile_height * width * drawable_src->bpp);

  /* Create all new gray images */
  num_images = extract[extract_idx].num_images;
  if (num_images > MAX_EXTRACT_IMAGES) num_images = MAX_EXTRACT_IMAGES;

  for (j = 0; j < num_images; j++)
  {
    sprintf (filename, "%s-%s", gimp_image_get_filename (image_ID),
             extract[extract_idx].channel_name[j]);

    image_ID_dst[j] = create_new_image (filename, width, height, GRAY,
                         layer_ID_dst+j, drawable_dst+j, pixel_rgn_dst+j);
    dst[j] = (unsigned char *)g_malloc (tile_height * width);
  }
  if (dst[num_images-1] == NULL)
  {
    show_message (_("decompose: out of memory"));
    for (j = 0; j < num_images; j++)
    {
      if (dst[j] != NULL) g_free (dst[j]);
    }
    return (-1);
  }

  i = 0;
  while (i < height)
  {
    /* Get source pixel region */
    scan_lines = (i+tile_height-1 < height) ? tile_height : (height-i);
    gimp_pixel_rgn_get_rect (&pixel_rgn_src, src, 0, i, width, scan_lines);

    /* Extract the channel information */
    extract[extract_idx].extract_fun (src, drawable_src->bpp, scan_lines*width,
                                      dst);

    /* Set destination pixel regions */
    for (j = 0; j < num_images; j++)
      gimp_pixel_rgn_set_rect (&(pixel_rgn_dst[j]), dst[j], 0, i, width,
                               scan_lines);
    i += scan_lines;

    if (run_mode != RUN_NONINTERACTIVE)
      gimp_progress_update (((double)i) / (double)height);
  }
  g_free (src);
  for (j = 0; j < num_images; j++)
  {
    gimp_drawable_flush (drawable_dst[j]);
    gimp_drawable_detach (drawable_dst[j]);
    g_free (dst[j]);
  }

  gimp_drawable_flush (drawable_src);
  gimp_drawable_detach (drawable_src);

  return (num_images);
}


/* Create an image. Sets layer_ID, drawable and rgn. Returns image_ID */
static gint32
create_new_image (char *filename,
                  guint width,
                  guint height,
                  GImageType type,
                  gint32 *layer_ID,
                  GDrawable **drawable,
                  GPixelRgn *pixel_rgn)

{gint32 image_ID;
 GDrawableType gdtype;

 if (type == GRAY) gdtype = GRAY_IMAGE;
 else if (type == INDEXED) gdtype = INDEXED_IMAGE;
 else gdtype = RGB_IMAGE;

 image_ID = gimp_image_new (width, height, type);
 gimp_image_set_filename (image_ID, filename);

 *layer_ID = gimp_layer_new (image_ID, _("Background"), width, height,
                            gdtype, 100, NORMAL_MODE);
 gimp_image_add_layer (image_ID, *layer_ID, 0);

 *drawable = gimp_drawable_get (*layer_ID);
 gimp_pixel_rgn_init (pixel_rgn, *drawable, 0, 0, (*drawable)->width,
                      (*drawable)->height, TRUE, FALSE);

 return (image_ID);
}


/* Compare two strings ignoring case (could also be done by strcasecmp() */
/* but is it available everywhere ?) */
static int cmp_icase (char *s1, char *s2)

{int c1, c2;

  c1 = toupper (*s1);  c2 = toupper (*s2);
  while (*s1 && *s2)
  {
    if (c1 != c2) return (c2 - c1);
    c1 = toupper (*(++s1));  c2 = toupper (*(++s2));
  }
  return (c2 - c1);
}


/* Convert RGB to HSV. This routine was taken from decompose plug-in
   of GIMP V 0.54 and modified a little bit.
*/
static void rgb_to_hsv (unsigned char *r, unsigned char *g, unsigned char *b,
                        unsigned char *h, unsigned char *s, unsigned char *v)

{
  int red = (int)*r, green = (int)*g, blue = (int)*b;
  double hue;
  int min, max, delta, sat_i;

  if (red > green)
    {
      if (red > blue)
        max = red;
      else
        max = blue;

      if (green < blue)
        min = green;
      else
        min = blue;
    }
  else
    {
      if (green > blue)
        max = green;
      else
        max = blue;

      if (red < blue)
        min = red;
      else
        min = blue;
    }

  *v = (unsigned char)max;

  if (max != 0)
    sat_i = ((max - min) * 255) / max;
  else
    sat_i = 0;

  *s = (unsigned char)sat_i;

  if (sat_i == 0)
  {
    *h = 0;
  }
  else
  {
    delta = max - min;
    if (red == max)
      hue =       (green - blue) / (double)delta;
    else if (green == max)
      hue = 2.0 + (blue - red) / (double)delta;
    else
      hue = 4.0 + (red - green) / (double)delta;
    hue *= 42.5;

    if (hue < 0.0)
      hue += 255.0;
    if (hue > 255.0)
      hue -= 255.0;

    *h = (unsigned char)hue;
  }
}


/* Extract functions */

static void extract_rgb (unsigned char *src, int bpp, int numpix,
                         unsigned char **dst)

{register unsigned char *rgb_src = src;
 register unsigned char *red_dst = dst[0];
 register unsigned char *green_dst = dst[1];
 register unsigned char *blue_dst = dst[2];
 register int count = numpix, offset = bpp-3;

 while (count-- > 0)
 {
   *(red_dst++) = *(rgb_src++);
   *(green_dst++) = *(rgb_src++);
   *(blue_dst++) = *(rgb_src++);
   rgb_src += offset;
 }
}


static void extract_red (unsigned char *src, int bpp, int numpix,
                         unsigned char **dst)

{register unsigned char *rgb_src = src;
 register unsigned char *red_dst = dst[0];
 register int count = numpix, offset = bpp;

 while (count-- > 0)
 {
   *(red_dst++) = *rgb_src;
   rgb_src += offset;
 }
}


static void extract_green (unsigned char *src, int bpp, int numpix,
                           unsigned char **dst)

{register unsigned char *rgb_src = src+1;
 register unsigned char *green_dst = dst[0];
 register int count = numpix, offset = bpp;

 while (count-- > 0)
 {
   *(green_dst++) = *rgb_src;
   rgb_src += offset;
 }
}


static void extract_blue (unsigned char *src, int bpp, int numpix,
                          unsigned char **dst)

{register unsigned char *rgb_src = src+2;
 register unsigned char *blue_dst = dst[0];
 register int count = numpix, offset = bpp;

 while (count-- > 0)
 {
   *(blue_dst++) = *rgb_src;
   rgb_src += offset;
 }
}


static void extract_alpha (unsigned char *src, int bpp, int numpix,
                           unsigned char **dst)

{register unsigned char *rgb_src = src+3;
 register unsigned char *alpha_dst = dst[0];
 register int count = numpix, offset = bpp;

 while (count-- > 0)
 {
   *(alpha_dst++) = *rgb_src;
   rgb_src += offset;
 }
}


static void extract_cmy (unsigned char *src, int bpp, int numpix,
                         unsigned char **dst)

{register unsigned char *rgb_src = src;
 register unsigned char *cyan_dst = dst[0];
 register unsigned char *magenta_dst = dst[1];
 register unsigned char *yellow_dst = dst[2];
 register int count = numpix, offset = bpp-3;

 while (count-- > 0)
 {
   *(cyan_dst++) = 255 - *(rgb_src++);
   *(magenta_dst++) = 255 - *(rgb_src++);
   *(yellow_dst++) = 255 - *(rgb_src++);
   rgb_src += offset;
 }
}


static void extract_hsv (unsigned char *src, int bpp, int numpix,
                         unsigned char **dst)

{register unsigned char *rgb_src = src;
 register unsigned char *hue_dst = dst[0];
 register unsigned char *sat_dst = dst[1];
 register unsigned char *val_dst = dst[2];
 register int count = numpix, offset = bpp;

 while (count-- > 0)
 {
   rgb_to_hsv (rgb_src, rgb_src+1, rgb_src+2, hue_dst++, sat_dst++, val_dst++);
   rgb_src += offset;
 }
}


static void extract_hue (unsigned char *src, int bpp, int numpix,
                         unsigned char **dst)

{register unsigned char *rgb_src = src;
 register unsigned char *hue_dst = dst[0];
 unsigned char dmy;
 unsigned char *dummy = &dmy;
 register int count = numpix, offset = bpp;

 while (count-- > 0)
 {
   rgb_to_hsv (rgb_src, rgb_src+1, rgb_src+2, hue_dst++, dummy, dummy);
   rgb_src += offset;
 }
}


static void extract_sat (unsigned char *src, int bpp, int numpix,
                         unsigned char **dst)

{register unsigned char *rgb_src = src;
 register unsigned char *sat_dst = dst[0];
 unsigned char dmy;
 unsigned char *dummy = &dmy;
 register int count = numpix, offset = bpp;

 while (count-- > 0)
 {
   rgb_to_hsv (rgb_src, rgb_src+1, rgb_src+2, dummy, sat_dst++, dummy);
   rgb_src += offset;
 }
}


static void extract_val (unsigned char *src, int bpp, int numpix,
                         unsigned char **dst)

{register unsigned char *rgb_src = src;
 register unsigned char *val_dst = dst[0];
 unsigned char dmy;
 unsigned char *dummy = &dmy;
 register int count = numpix, offset = bpp;

 while (count-- > 0)
 {
   rgb_to_hsv (rgb_src, rgb_src+1, rgb_src+2, dummy, dummy, val_dst++);
   rgb_src += offset;
 }
}


static void extract_cyan (unsigned char *src, int bpp, int numpix,
                          unsigned char **dst)

{register unsigned char *rgb_src = src;
 register unsigned char *cyan_dst = dst[0];
 register int count = numpix, offset = bpp-1;

 while (count-- > 0)
 {
   *(cyan_dst++) = 255 - *(rgb_src++);
   rgb_src += offset;
 }
}


static void extract_magenta (unsigned char *src, int bpp, int numpix,
                             unsigned char **dst)

{register unsigned char *rgb_src = src+1;
 register unsigned char *magenta_dst = dst[0];
 register int count = numpix, offset = bpp-1;

 while (count-- > 0)
 {
   *(magenta_dst++) = 255 - *(rgb_src++);
   rgb_src += offset;
 }
}


static void extract_yellow (unsigned char *src, int bpp, int numpix,
                            unsigned char **dst)

{register unsigned char *rgb_src = src+2;
 register unsigned char *yellow_dst = dst[0];
 register int count = numpix, offset = bpp-1;

 while (count-- > 0)
 {
   *(yellow_dst++) = 255 - *(rgb_src++);
   rgb_src += offset;
 }
}


static void extract_cmyk (unsigned char *src, int bpp, int numpix,
                          unsigned char **dst)

{register unsigned char *rgb_src = src;
 register unsigned char *cyan_dst = dst[0];
 register unsigned char *magenta_dst = dst[1];
 register unsigned char *yellow_dst = dst[2];
 register unsigned char *black_dst = dst[3];
 register unsigned char k, s;
 register int count = numpix, offset = bpp-3;

 while (count-- > 0)
 {
   *cyan_dst = k = 255 - *(rgb_src++);
   *magenta_dst = s = 255 - *(rgb_src++);
   if (s < k) k = s;
   *yellow_dst = s = 255 - *(rgb_src++);
   if (s < k) k = s;   /* Black intensity is minimum of c, m, y */
   if (k)
   {
     *cyan_dst -= k;     /* Remove common part of c, m, y */
     *magenta_dst -= k;
     *yellow_dst -= k;
   }
   cyan_dst++;
   magenta_dst++;
   yellow_dst++;
   *(black_dst++) = k;

   rgb_src += offset;
 }
}


static void extract_cyank (unsigned char *src, int bpp, int numpix,
                           unsigned char **dst)

{register unsigned char *rgb_src = src;
 register unsigned char *cyan_dst = dst[0];
 register unsigned char s, k;
 register int count = numpix, offset = bpp-3;

 while (count-- > 0)
 {
   *cyan_dst = k = 255 - *(rgb_src++);
   s = 255 - *(rgb_src++);  /* magenta */
   if (s < k) k = s;
   s = 255 - *(rgb_src++);  /* yellow */
   if (s < k) k = s;
   if (k) *cyan_dst -= k;
   cyan_dst++;

   rgb_src += offset;
 }
}


static void extract_magentak (unsigned char *src, int bpp, int numpix,
                              unsigned char **dst)

{register unsigned char *rgb_src = src;
 register unsigned char *magenta_dst = dst[0];
 register unsigned char s, k;
 register int count = numpix, offset = bpp-3;

 while (count-- > 0)
 {
   k = 255 - *(rgb_src++);  /* cyan */
   *magenta_dst = s = 255 - *(rgb_src++);  /* magenta */
   if (s < k) k = s;
   s = 255 - *(rgb_src++);  /* yellow */
   if (s < k) k = s;
   if (k) *magenta_dst -= k;
   magenta_dst++;

   rgb_src += offset;
 }
}


static void extract_yellowk (unsigned char *src, int bpp, int numpix,
                             unsigned char **dst)

{register unsigned char *rgb_src = src;
 register unsigned char *yellow_dst = dst[0];
 register unsigned char s, k;
 register int count = numpix, offset = bpp-3;

 while (count-- > 0)
 {
   k = 255 - *(rgb_src++);  /* cyan */
   s = 255 - *(rgb_src++);  /* magenta */
   if (s < k) k = s;
   *yellow_dst = s = 255 - *(rgb_src++);
   if (s < k) k = s;
   if (k) *yellow_dst -= k;
   yellow_dst++;

   rgb_src += offset;
 }
}


static gint
decompose_dialog (void)
{
  GtkWidget *dlg;
  GtkWidget *button;
  GtkWidget *toggle;
  GtkWidget *frame;
  GtkWidget *vbox;
  GSList *group;
  gchar **argv;
  gint argc;
  int j;

  argc = 1;
  argv = g_new (gchar *, 1);
  argv[0] = g_strdup (_("Decompose"));

  gtk_init (&argc, &argv);
  gtk_rc_parse (gimp_gtkrc ());

  dlg = gtk_dialog_new ();
  gtk_window_set_title (GTK_WINDOW (dlg), _("Decompose"));
  gtk_window_set_position (GTK_WINDOW (dlg), GTK_WIN_POS_MOUSE);
  gtk_signal_connect (GTK_OBJECT (dlg), "destroy",
		      (GtkSignalFunc) decompose_close_callback,
		      NULL);

  /*  Action area  */
  button = gtk_button_new_with_label (_("OK"));
  GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
                      (GtkSignalFunc) decompose_ok_callback,
                      dlg);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->action_area), button,
                      TRUE, TRUE, 0);
  gtk_widget_grab_default (button);
  gtk_widget_show (button);

  button = gtk_button_new_with_label (_("Cancel"));
  GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
			     (GtkSignalFunc) gtk_widget_destroy,
			     GTK_OBJECT (dlg));
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->action_area), button,
                      TRUE, TRUE, 0);
  gtk_widget_show (button);

  /*  parameter settings  */
  frame = gtk_frame_new (_("Extract channels:"));
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
  gtk_container_set_border_width (GTK_CONTAINER (frame), 5);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), frame, TRUE, TRUE, 0);

  vbox = gtk_vbox_new (FALSE, 5);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);
  gtk_container_add (GTK_CONTAINER (frame), vbox);

  group = NULL;
  for (j = 0; j < NUM_EXTRACT_TYPES; j++)
  {
    if (!extract[j].dialog) continue;
    toggle = gtk_radio_button_new_with_label (group, extract[j].type);
    group = gtk_radio_button_group (GTK_RADIO_BUTTON (toggle));
    gtk_box_pack_start (GTK_BOX (vbox), toggle, TRUE, TRUE, 0);
    decoint.extract_flag[j] =
       (cmp_icase (decovals.extract_type, extract[j].type) == 0);
    gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
                        (GtkSignalFunc) decompose_toggle_update,
                        &(decoint.extract_flag[j]));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle),
				  decoint.extract_flag[j]);
    gtk_widget_show (toggle);
  }
  gtk_widget_show (vbox);
  gtk_widget_show (frame);
  gtk_widget_show (dlg);

  gtk_main ();
  gdk_flush ();

  return decoint.run;
}


/*  Decompose interface functions  */

static void
decompose_close_callback (GtkWidget *widget,
			  gpointer   data)
{
  gtk_main_quit ();
}

static void
decompose_ok_callback (GtkWidget *widget,
		       gpointer   data)
{int j;

  decoint.run = TRUE;
  gtk_widget_destroy (GTK_WIDGET (data));

  for (j = 0; j < NUM_EXTRACT_TYPES; j++)
  {
    if (decoint.extract_flag[j])
    {
      strcpy (decovals.extract_type, extract[j].type);
      break;
    }
  }
}

static void
decompose_toggle_update (GtkWidget *widget,
			 gpointer   data)
{
  gint *toggle_val;

  toggle_val = (int *) data;

  if (GTK_TOGGLE_BUTTON (widget)->active)
    *toggle_val = TRUE;
  else
    *toggle_val = FALSE;
}
