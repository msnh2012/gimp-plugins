/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 * SUN raster reading and writing code Copyright (C) 1996 Peter Kirchgessner
 * (email: peter@kirchgessner.net, WWW: http://www.kirchgessner.net)
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
 *
 */

/* This program was written using pages 625-629 of the book
 * "Encyclopedia of Graphics File Formats", Murray/van Ryper,
 * O'Reilly & Associates Inc.
 * Bug reports or suggestions should be e-mailed to peter@kirchgessner.net
 */

/* Event history:
 * V 1.00, PK, 25-Jul-96: First try
 * V 1.90, PK, 15-Mar-97: Upgrade to work with GIMP V0.99
 * V 1.91, PK, 05-Apr-97: Return all arguments, even in case of an error
 * V 1.92, PK, 18-May-97: Ignore EOF-error on reading image data
 * V 1.93, PK, 05-Oct-97: Parse rc file
 * V 1.94, PK, 12-Oct-97: No progress bars for non-interactive mode
 * V 1.95, nn, 20-Dec-97: Initialize some variable
 * V 1.96, PK, 21-Nov-99: Internationalization
 * V 1.97, PK, 20-Dec-00: Recognize extensions .rs and .ras too
 */
static char ident[] = "@(#) GIMP SunRaster file-plugin v1.97  20-Dec-00";

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


typedef int WRITE_FUN(void*,size_t,size_t,FILE*);

typedef gulong  L_CARD32;
typedef gushort L_CARD16;
typedef guchar  L_CARD8;

/* Fileheader of SunRaster files */
typedef struct
{
  L_CARD32 l_ras_magic;    /* Magic Number */
  L_CARD32 l_ras_width;    /* Width */
  L_CARD32 l_ras_height;   /* Height */
  L_CARD32 l_ras_depth;    /* Number of bits per pixel (1,8,24,32) */
  L_CARD32 l_ras_length;   /* Length of image data (but may also be 0) */
  L_CARD32 l_ras_type;     /* Encoding */
  L_CARD32 l_ras_maptype;  /* Type of colormap */
  L_CARD32 l_ras_maplength;/* Number of bytes for colormap */
} L_SUNFILEHEADER;

/* Sun-raster magic */
#define RAS_MAGIC 0x59a66a95

#define RAS_TYPE_STD 1    /* Standard uncompressed format */
#define RAS_TYPE_RLE 2    /* Runlength compression format */

typedef struct
{
  gint val;   /* The value that is to be repeated */
  gint n;     /* How many times it is repeated */
} RLEBUF;


/* Declare some local functions.
 */
static void   query      (void);
static void   run        (gchar   *name,
                          gint     nparams,
                          GimpParam  *param,
                          gint    *nreturn_vals,
                          GimpParam **return_vals);

static gint32 load_image (gchar   *filename);
static gint   save_image (gchar   *filename,
                          gint32   image_ID,
                          gint32   drawable_ID);

static void set_color_table (gint32, L_SUNFILEHEADER *, unsigned char *);

static gint32 create_new_image (char *filename, guint width, guint height,
				GimpImageBaseType type, gint32 *layer_ID, GimpDrawable **drawable,
				GimpPixelRgn *pixel_rgn);

static gint32 load_sun_d1   (char *, FILE *, L_SUNFILEHEADER *, unsigned char *);
static gint32 load_sun_d8   (char *, FILE *, L_SUNFILEHEADER *, unsigned char *);
static gint32 load_sun_d24  (char *, FILE *, L_SUNFILEHEADER *, unsigned char *);
static gint32 load_sun_d32  (char *, FILE *, L_SUNFILEHEADER *, unsigned char *);

static L_CARD32 read_card32 (FILE *, int *);

static void write_card32    (FILE *, L_CARD32);

static void byte2bit        (unsigned char *, int, unsigned char *, int);

static void rle_startread   (FILE *);
static int  rle_fread       (char *, int, int, FILE *);
static int  rle_fgetc       (FILE *);
#define rle_getc(fp) ((rlebuf.n > 0) ? (rlebuf.n)--,rlebuf.val : rle_fgetc (fp))

static void rle_startwrite  (FILE *);
static int  rle_fwrite      (char *, int, int, FILE *);
static int  rle_fputc       (int, FILE *);
static int  rle_putrun      (int, int, FILE *);
static void rle_endwrite    (FILE *);
#define rle_putc rle_fputc

static void read_sun_header  (FILE *, L_SUNFILEHEADER *);
static void write_sun_header (FILE *, L_SUNFILEHEADER *);
static void read_sun_cols    (FILE *, L_SUNFILEHEADER *, unsigned char *);
static void write_sun_cols   (FILE *, L_SUNFILEHEADER *, unsigned char *);

static gint save_index       (FILE *, gint32, gint32 , int, int);
static gint save_rgb         (FILE *, gint32, gint32, int);

static int read_msb_first = 1;
static RLEBUF rlebuf;

/* Dialog-handling */
static gint   save_dialog              (void);
static void   save_ok_callback         (GtkWidget *widget,
                                        gpointer   data);

/* Portability kludge */
static int my_fwrite (void *ptr, int size, int nmemb, FILE *stream);

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};


/* Save info  */
typedef struct
{
  gint  rle;  /*  rle or standard */
} SUNRASSaveVals;

typedef struct
{
  gint  run;  /*  run  */
} SUNRASSaveInterface;


static SUNRASSaveVals psvals =
{
  TRUE     /* rle */
};

static SUNRASSaveInterface psint =
{
  FALSE     /* run */
};


/* The run mode */
static GimpRunModeType l_run_mode;

MAIN ()

static void
query (void)
{
  static GimpParamDef load_args[] =
  {
    { GIMP_PDB_INT32,  "run_mode",      "Interactive, non-interactive" },
    { GIMP_PDB_STRING, "filename",      "The name of the file to load" },
    { GIMP_PDB_STRING, "raw_filename",  "The name of the file to load" }
  };
  static gint nload_args = sizeof (load_args) / sizeof (load_args[0]);

  static GimpParamDef load_return_vals[] =
  {
    { GIMP_PDB_IMAGE,  "image",         "Output image" }
  };
  static gint nload_return_vals = (sizeof (load_return_vals) /
				   sizeof (load_return_vals[0]));

  static GimpParamDef save_args[] =
  {
    { GIMP_PDB_INT32,    "run_mode",     "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE,    "image",        "Input image" },
    { GIMP_PDB_DRAWABLE, "drawable",     "Drawable to save" },
    { GIMP_PDB_STRING,   "filename",     "The name of the file to save the image in" },
    { GIMP_PDB_STRING,   "raw_filename", "The name of the file to save the image in" },
    { GIMP_PDB_INT32,    "rle",          "Specify non-zero for rle output, zero for standard output" }
  };
  static gint nsave_args = sizeof (save_args) / sizeof (save_args[0]);

  gimp_install_procedure ("file_sunras_load",
                          "load file of the SunRaster file format",
                          "load file of the SunRaster file format",
                          "Peter Kirchgessner",
                          "Peter Kirchgessner",
                          "1996",
                          "<Load>/SUNRAS",
                          NULL,
                          GIMP_PLUGIN,
                          nload_args, nload_return_vals,
                          load_args, load_return_vals);

  gimp_install_procedure ("file_sunras_save",
                          "save file in the SunRaster file format",
                          "SUNRAS saving handles all image types except "
			  "those with alpha channels.",
                          "Peter Kirchgessner",
                          "Peter Kirchgessner",
                          "1996",
                          "<Save>/SUNRAS",
                          "RGB, GRAY, INDEXED",
                          GIMP_PLUGIN,
                          nsave_args, 0,
                          save_args, NULL);

  /* Magic information for sunras would be "0,long,0x59a66a95" */
  gimp_register_magic_load_handler ("file_sunras_load", 
				    "im1,im8,im24,im32,rs,ras",
				    "",
				    "0,long,0x59a66a95");
  gimp_register_save_handler       ("file_sunras_save", 
				    "im1,im8,im24,im32,rs,ras",
				    "");
}


static void
run (gchar   *name,
     gint     nparams,
     GimpParam  *param,
     gint    *nreturn_vals,
     GimpParam **return_vals)
{
  static GimpParam values[2];
  GimpRunModeType  run_mode;
  GimpPDBStatusType   status = GIMP_PDB_SUCCESS;
  gint32        image_ID;
  gint32        drawable_ID;
  GimpExportReturnType export = GIMP_EXPORT_CANCEL;

  l_run_mode = run_mode = param[0].data.d_int32;

  *nreturn_vals = 1;
  *return_vals  = values;
  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;

  if (strcmp (name, "file_sunras_load") == 0)
    {
      INIT_I18N_UI();

      image_ID = load_image (param[1].data.d_string);

      if (image_ID != -1)
	{
	  *nreturn_vals = 2;
	  values[1].type         = GIMP_PDB_IMAGE;
	  values[1].data.d_image = image_ID;
	}
      else
	{
	  status = GIMP_PDB_EXECUTION_ERROR;
	}
    }
  else if (strcmp (name, "file_sunras_save") == 0)
    {
      INIT_I18N_UI();

      image_ID = param[1].data.d_int32;
      drawable_ID = param[2].data.d_int32;

      /*  eventually export the image */ 
      switch (run_mode)
	{
	case GIMP_RUN_INTERACTIVE:
	case GIMP_RUN_WITH_LAST_VALS:
	  gimp_ui_init ("sunras", FALSE);
	  export = gimp_export_image (&image_ID, &drawable_ID, "SUNRAS", 
				      (GIMP_EXPORT_CAN_HANDLE_RGB |
				       GIMP_EXPORT_CAN_HANDLE_GRAY |
				       GIMP_EXPORT_CAN_HANDLE_INDEXED));
	  if (export == GIMP_EXPORT_CANCEL)
	    {
	      values[0].data.d_status = GIMP_PDB_CANCEL;
	      return;
	  }
	  break;
	default:
	  break;
	}

      switch (run_mode)
        {
        case GIMP_RUN_INTERACTIVE:
          /*  Possibly retrieve data  */
          gimp_get_data ("file_sunras_save", &psvals);

          /*  First acquire information with a dialog  */
          if (! save_dialog ())
            status = GIMP_PDB_CANCEL;
          break;

        case GIMP_RUN_NONINTERACTIVE:
          /*  Make sure all the arguments are there!  */
          if (nparams != 6)
	    {
	      status = GIMP_PDB_CALLING_ERROR;
	    }
          else
	    {
	      psvals.rle = (param[5].data.d_int32) ? TRUE : FALSE;
	    }
          break;

        case GIMP_RUN_WITH_LAST_VALS:
          /*  Possibly retrieve data  */
          gimp_get_data ("file_sunras_save", &psvals);
          break;

        default:
          break;
        }

      if (status == GIMP_PDB_SUCCESS)
	{
	  if (save_image (param[3].data.d_string, image_ID, drawable_ID))
	    {
	      /*  Store psvals data  */
	      gimp_set_data ("file_sunras_save", &psvals,
			     sizeof (SUNRASSaveVals));
	    }
	  else
	    {
	      status = GIMP_PDB_EXECUTION_ERROR;
	    }
	}

      if (export == GIMP_EXPORT_EXPORT)
	gimp_image_delete (image_ID);
    }
  else
    {
      status = GIMP_PDB_CALLING_ERROR;
    }

  values[0].data.d_status = status;
}


static gint32
load_image (gchar *filename)
{
  gint32 image_ID;
  FILE *ifp;
  char *temp = ident; /* Just to satisfy gcc/lint */
  L_SUNFILEHEADER sunhdr;
  guchar *suncolmap = NULL;

  ifp = fopen (filename, "rb");
  if (!ifp)
    {
      g_message (_("Can't open file for reading"));
      return (-1);
    }

  read_msb_first = 1;   /* SUN raster is always most significant byte first */

  read_sun_header (ifp, &sunhdr);
  if (sunhdr.l_ras_magic != RAS_MAGIC)
    {
      g_message (_("Can't open file as SUN-raster-file"));
      fclose (ifp);
      return (-1);
    }

  if ((sunhdr.l_ras_type < 0) || (sunhdr.l_ras_type > 5))
    {
      g_message (_("The type of this SUN-rasterfile is not supported"));
      fclose (ifp);
      return (-1);
    }

  /* Is there a RGB colourmap ? */
  if ((sunhdr.l_ras_maptype == 1) && (sunhdr.l_ras_maplength > 0))
    {
      suncolmap = g_new (guchar, sunhdr.l_ras_maplength);

      read_sun_cols (ifp, &sunhdr, suncolmap);
#ifdef DEBUG
      {
	int j, ncols;
	printf ("File %s\n",filename);
	ncols = sunhdr.l_ras_maplength/3;
	for (j=0; j < ncols; j++)
	  printf ("Entry 0x%08x: 0x%04x,  0x%04x, 0x%04x\n",
		  j,suncolmap[j],suncolmap[j+ncols],suncolmap[j+2*ncols]);
      }
#endif
      if (sunhdr.l_ras_magic != RAS_MAGIC)
	{
	  g_message (_("Can't read color entries"));
	  fclose (ifp);
	  return (-1);
	}
    }
  else if (sunhdr.l_ras_maplength > 0)
    {
      g_message (_("Type of colormap not supported"));
      fseek (ifp, (sizeof (L_SUNFILEHEADER)/sizeof (L_CARD32))
	     *4 + sunhdr.l_ras_maplength, SEEK_SET);
    }
  
  if (l_run_mode != GIMP_RUN_NONINTERACTIVE)
    {
      temp = g_strdup_printf (_("Loading %s:"), filename);
      gimp_progress_init (temp);
      g_free (temp);
    }

  switch (sunhdr.l_ras_depth)
    {
    case 1:    /* bitmap */
      image_ID = load_sun_d1 (filename, ifp, &sunhdr, suncolmap);
      break;

    case 8:    /* 256 colours */
      image_ID = load_sun_d8 (filename, ifp, &sunhdr, suncolmap);
      break;

    case 24:   /* True color */
      image_ID = load_sun_d24 (filename, ifp, &sunhdr, suncolmap);
      break;

    case 32:   /* True color with extra byte */
      image_ID = load_sun_d32 (filename, ifp, &sunhdr, suncolmap);
      break;

    default:
      image_ID = -1;
      break;
    }

  fclose (ifp);

  g_free (suncolmap);

  if (image_ID == -1)
    {
      g_message (_("This image depth is not supported"));
      return (-1);
    }

  return (image_ID);
}


static gint
save_image (gchar  *filename,
            gint32  image_ID,
            gint32  drawable_ID)
{
  FILE* ofp;
  GimpImageType drawable_type;
  gint retval;
  char *temp;

  drawable_type = gimp_drawable_type (drawable_ID);

  /*  Make sure we're not saving an image with an alpha channel  */
  if (gimp_drawable_has_alpha (drawable_ID))
    {
      g_message (_("SUNRAS save cannot handle images with alpha channels"));
      return FALSE;
    }

  switch (drawable_type)
    {
    case GIMP_INDEXED_IMAGE:
    case GIMP_GRAY_IMAGE:
    case GIMP_RGB_IMAGE:
      break;
    default:
      g_message (_("Can't operate on unknown image types"));
      return (FALSE);
      break;
    }

  /* Open the output file. */
  ofp = fopen (filename, "wb");
  if (!ofp)
    {
      g_message (_("Can't open file for writing"));
      return (FALSE);
    }

  if (l_run_mode != GIMP_RUN_NONINTERACTIVE)
    {
      temp = g_strdup_printf (_("Saving %s:"), filename);
      gimp_progress_init (temp);
      g_free (temp);
    }

  if (drawable_type == GIMP_INDEXED_IMAGE)
    retval = save_index (ofp,image_ID, drawable_ID, 0, (int)psvals.rle);
  else if (drawable_type == GIMP_GRAY_IMAGE)
    retval = save_index (ofp,image_ID, drawable_ID, 1, (int)psvals.rle);
  else if (drawable_type == GIMP_RGB_IMAGE)
    retval = save_rgb (ofp,image_ID, drawable_ID, (int)psvals.rle);
  else
    retval = FALSE;

  fclose (ofp);

  return (retval);
}


static L_CARD32
read_card32 (FILE *ifp,
	     gint *err)
{
  L_CARD32 c;

  if (read_msb_first)
    {
      c = (((L_CARD32)(getc (ifp))) << 24);
      c |= (((L_CARD32)(getc (ifp))) << 16);
      c |= (((L_CARD32)(getc (ifp))) << 8);
      c |= ((L_CARD32)(*err = getc (ifp)));
    }
  else
    {
      c = ((L_CARD32)(getc (ifp)));
      c |= (((L_CARD32)(getc (ifp))) << 8);
      c |= (((L_CARD32)(getc (ifp))) << 16);
      c |= (((L_CARD32)(*err = getc (ifp))) << 24);
    }

  *err = (*err < 0);
  return (c);
}


static void 
write_card32 (FILE     *ofp,
	      L_CARD32  c)
{
  putc ((int)((c >> 24) & 0xff), ofp);
  putc ((int)((c >> 16) & 0xff), ofp);
  putc ((int)((c >> 8) & 0xff), ofp);
  putc ((int)((c) & 0xff), ofp);
}


/* Convert n bytes of 0/1 to a line of bits */
static void 
byte2bit (guchar *byteline,
	  gint    width,
	  guchar *bitline,
	  gint    invert)
{
  register guchar bitval;
  guchar rest[8];

  while (width >= 8)
    {
      bitval = 0;
      if (*(byteline++)) bitval |= 0x80;
      if (*(byteline++)) bitval |= 0x40;
      if (*(byteline++)) bitval |= 0x20;
      if (*(byteline++)) bitval |= 0x10;
      if (*(byteline++)) bitval |= 0x08;
      if (*(byteline++)) bitval |= 0x04;
      if (*(byteline++)) bitval |= 0x02;
      if (*(byteline++)) bitval |= 0x01;
      *(bitline++) = invert ? ~bitval : bitval;
      width -= 8;
    }
  if (width > 0)
    {
      memset (rest, 0, 8);
      memcpy (rest, byteline, width);
      bitval = 0;
      byteline = rest;
      if (*(byteline++)) bitval |= 0x80;
      if (*(byteline++)) bitval |= 0x40;
      if (*(byteline++)) bitval |= 0x20;
      if (*(byteline++)) bitval |= 0x10;
      if (*(byteline++)) bitval |= 0x08;
      if (*(byteline++)) bitval |= 0x04;
      if (*(byteline++)) bitval |= 0x02;
      *bitline = invert ? ~bitval : bitval;
    }
}


/* Start reading Runlength Encoded Data */
static void 
rle_startread (FILE *ifp)
{ /* Clear RLE-buffer */
  rlebuf.val = rlebuf.n = 0;
}


/* Read uncompressed elements from RLE-stream */
static gint
rle_fread (gchar *ptr,
	   gint   sz,
	   gint   nelem,
	   FILE  *ifp)
{
  int elem_read, cnt, val, err = 0;

  for (elem_read = 0; elem_read < nelem; elem_read++)
    {
      for (cnt = 0; cnt < sz; cnt++)
	{
	  val = rle_getc (ifp);
	  if (val < 0) { err = 1; break; }
	  *(ptr++) = (char)val;
	}
      if (err) break;
    }
  return (elem_read);
}


/* Get one byte of uncompressed data from RLE-stream */
static gint
rle_fgetc (FILE *ifp)
{
  int flag, runcnt, runval;
  
  if (rlebuf.n > 0)    /* Something in the buffer ? */
    {
      (rlebuf.n)--;
      return (rlebuf.val);
    }
  
  /* Nothing in the buffer. We have to read something */
  if ((flag = getc (ifp)) < 0) return (-1);
  if (flag != 0x0080) return (flag);    /* Single byte run ? */
  
  if ((runcnt = getc (ifp)) < 0) return (-1);
  if (runcnt == 0) return (0x0080);     /* Single 0x80 ? */
  
  /* The run */
  if ((runval = getc (ifp)) < 0) return (-1);
  rlebuf.n = runcnt;
  rlebuf.val = runval;
  return (runval);
}


/* Start writing Runlength Encoded Data */
static void 
rle_startwrite (FILE *ofp)
{ /* Clear RLE-buffer */
  rlebuf.val = rlebuf.n = 0;
}


/* Write uncompressed elements to RLE-stream */
static gint
rle_fwrite (gchar *ptr,
	    gint   sz,
	    gint   nelem,
	    FILE  *ofp)
{
  int elem_write, cnt, val, err = 0;
  guchar *pixels = (unsigned char *)ptr;
  
  for (elem_write = 0; elem_write < nelem; elem_write++)
    {
      for (cnt = 0; cnt < sz; cnt++)
	{
	  val = rle_fputc (*(pixels++), ofp);
	  if (val < 0) { err = 1; break; }
	}
      if (err) break;
    }
  return (elem_write);
}


/* Write uncompressed character to RLE-stream */
static gint
rle_fputc (gint  val,
	   FILE *ofp)
{
  int retval;
  
  if (rlebuf.n == 0)    /* Nothing in the buffer ? Save the value */
    {
      rlebuf.n = 1;
      return (rlebuf.val = val);
    }
  
  /* Something in the buffer */
  
  if (rlebuf.val == val)   /* Same value in the buffer ? */
    {
      (rlebuf.n)++;
      if (rlebuf.n == 257) /* Can not be encoded in a single run ? */
	{
	  retval = rle_putrun (256, rlebuf.val, ofp);
	  if (retval < 0) return (retval);
	  rlebuf.n -= 256;
	}
      return (val);
    }
  
  /* Something different in the buffer ? Write out the run */
  
  retval = rle_putrun (rlebuf.n, rlebuf.val, ofp);
  if (retval < 0) return (retval);
  
  /* Save the new value */
  rlebuf.n = 1;
  return (rlebuf.val = val);
}


/* Write out a run with 0 < n < 257 */
static gint 
rle_putrun (gint  n,
	    gint  val,
	    FILE *ofp)
{
  int retval, flag = 0x80;

  /* Useful to write a 3 byte run ? */
  if ((n > 2) || ((n == 2) && (val == flag)))
    {
      putc (flag, ofp);
      putc (n-1, ofp);
      retval = putc (val, ofp);
    }
  else if (n == 2) /* Write two single runs (could not be value 0x80) */
    {
      putc (val, ofp);
      retval = putc (val, ofp);
    }
  else  /* Write a single run */
    {
      if (val == flag)
	retval = putc (flag, ofp), putc (0x00, ofp);
      else
	retval = putc (val, ofp);
    }
  
  return ((retval < 0) ? retval : val);
}


/* End writing Runlength Encoded Data */
static void 
rle_endwrite (FILE *ofp)    
{
  if (rlebuf.n > 0)
    {
      rle_putrun (rlebuf.n, rlebuf.val, ofp);
      rlebuf.val = rlebuf.n = 0; /* Clear RLE-buffer */
    }
}


static void
read_sun_header (FILE            *ifp,
                 L_SUNFILEHEADER *sunhdr)
{
  int j, err;
  L_CARD32 *cp;
  
  cp = (L_CARD32 *)sunhdr;
  
  /* Read in all 32-bit values of the header and check for byte order */
  for (j = 0; j < sizeof (L_SUNFILEHEADER)/sizeof(sunhdr->l_ras_magic); j++)
    {
      *(cp++) = read_card32 (ifp, &err);
      if (err) break;
    }
  
  if (err) sunhdr->l_ras_magic = 0;  /* Not a valid SUN-raster file */
}


/* Write out a SUN-fileheader */

static void
write_sun_header (FILE            *ofp,
                  L_SUNFILEHEADER *sunhdr)
{
  int j, hdr_entries;
  L_CARD32 *cp;
  
  hdr_entries = sizeof (L_SUNFILEHEADER)/sizeof(sunhdr->l_ras_magic);
  
  cp = (L_CARD32 *)sunhdr;
  
  /* Write out all 32-bit values of the header and check for byte order */
  for (j = 0; j < sizeof (L_SUNFILEHEADER)/sizeof(sunhdr->l_ras_magic); j++)
    {
      write_card32 (ofp, *(cp++));
    }
}


/* Read the sun colourmap */

static void
read_sun_cols (FILE            *ifp,
               L_SUNFILEHEADER *sunhdr,
               guchar          *colormap)
{
  int ncols, err = 0;
  
  /* Read in SUN-raster Colormap */
  ncols = sunhdr->l_ras_maplength / 3;
  if (ncols <= 0)
    err = 1;
  else
    err = (fread (colormap, 3, ncols, ifp) != ncols);
  
  if (err) sunhdr->l_ras_magic = 0;  /* Not a valid SUN-raster file */
}


/* Write a sun colourmap */

static void
write_sun_cols (FILE            *ofp,
                L_SUNFILEHEADER *sunhdr,
                guchar          *colormap)
{
  int ncols;
  
  ncols = sunhdr->l_ras_maplength / 3;
  fwrite (colormap, 3, ncols, ofp);
}


/* Set a GIMP colourtable using the sun colourmap */

static void
set_color_table (gint32           image_ID,
		 L_SUNFILEHEADER *sunhdr,
		 guchar          *suncolmap)
{
  int ncols, j;
  guchar ColorMap[256*3];

  ncols = sunhdr->l_ras_maplength / 3;
  if (ncols <= 0) return;

  for (j = 0; j < ncols; j++)
    {
      ColorMap[j*3]   = suncolmap[j];
      ColorMap[j*3+1] = suncolmap[j+ncols];
      ColorMap[j*3+2] = suncolmap[j+2*ncols];
    }

#ifdef DEBUG
  printf ("Set GIMP colortable:\n");
  for (j = 0; j < ncols; j++)
    printf ("%3d: 0x%02x 0x%02x 0x%02x\n", j,
	    ColorMap[j*3], ColorMap[j*3+1], ColorMap[j*3+2]);
#endif
  gimp_image_set_cmap (image_ID, ColorMap, ncols);
}


/* Create an image. Sets layer_ID, drawable and rgn. Returns image_ID */
static gint32
create_new_image (gchar              *filename,
                  guint               width,
                  guint               height,
                  GimpImageBaseType   type,
                  gint32             *layer_ID,
                  GimpDrawable      **drawable,
                  GimpPixelRgn       *pixel_rgn)
{
  gint32 image_ID;
  GimpImageType gdtype;

  switch (type)
    {
    case GIMP_RGB:
      gdtype = GIMP_RGB_IMAGE;
      break;
    case GIMP_GRAY: 
      gdtype = GIMP_GRAY_IMAGE;
      break;
    case GIMP_INDEXED:
      gdtype = GIMP_INDEXED_IMAGE;
      break;
    default:
      g_warning ("Unsupported image type");
      return -1;
    }

  image_ID = gimp_image_new (width, height, type);
  gimp_image_set_filename (image_ID, filename);

  *layer_ID = gimp_layer_new (image_ID, _("Background"), width, height,
                            gdtype, 100, GIMP_NORMAL_MODE);
  gimp_image_add_layer (image_ID, *layer_ID, 0);

  *drawable = gimp_drawable_get (*layer_ID);
  gimp_pixel_rgn_init (pixel_rgn, *drawable, 0, 0, (*drawable)->width,
		       (*drawable)->height, TRUE, FALSE);

  return (image_ID);
}


/* Load SUN-raster-file with depth 1 */
static gint32
load_sun_d1 (gchar           *filename,
             FILE            *ifp,
             L_SUNFILEHEADER *sunhdr,
             guchar          *suncolmap)
{
  register int pix8;
  int width, height, linepad, scan_lines, tile_height;
  int i, j;
  guchar *dest, *data;
  gint32 layer_ID, image_ID;
  GimpPixelRgn pixel_rgn;
  GimpDrawable *drawable;
  guchar bit2byte[256*8];
  L_SUNFILEHEADER sun_bwhdr;
  guchar sun_bwcolmap[6] = { 255,0,255,0,255,0 };
  int err = 0, rle = (sunhdr->l_ras_type == RAS_TYPE_RLE);

  width = sunhdr->l_ras_width;
  height = sunhdr->l_ras_height;

  image_ID = create_new_image (filename, width, height, GIMP_INDEXED,
			       &layer_ID, &drawable, &pixel_rgn);

  tile_height = gimp_tile_height ();
  data = g_malloc (tile_height * width);

  if (suncolmap != NULL)   /* Set up the specified colour map */
    {
      set_color_table (image_ID, sunhdr, suncolmap);
    }
  else   /* No colourmap available. Set up a dummy b/w-colourmap */
    {      /* Copy the original header and simulate b/w-colourmap */
      memcpy ((char *)&sun_bwhdr,(char *)sunhdr,sizeof (L_SUNFILEHEADER));
      sun_bwhdr.l_ras_maptype = 2;
      sun_bwhdr.l_ras_maplength = 6;
      set_color_table (image_ID, &sun_bwhdr, sun_bwcolmap);
    }

  /* Get an array for mapping 8 bits in a byte to 8 bytes */
  dest = bit2byte;
  for (j = 0; j < 256; j++)
    for (i = 7; i >= 0; i--)
      *(dest++) = ((j & (1 << i)) != 0);

  linepad = (((sunhdr->l_ras_width+7)/8) % 2); /* Check for 16bit align */

  if (rle) rle_startread (ifp);

  dest = data;
  scan_lines = 0;

  for (i = 0; i < height; i++)
    {
      j = width;
      while (j >= 8)
	{
	  pix8 = rle ? rle_getc (ifp) : getc (ifp);
	  if (pix8 < 0) { err = 1; pix8 = 0; }

	  memcpy (dest, bit2byte + pix8*8, 8);
	  dest += 8;
	  j -= 8;
	}

      if (j > 0)
	{
	  pix8 = rle ? rle_getc (ifp) : getc (ifp);
	  if (pix8 < 0) { err = 1; pix8 = 0; }

	  memcpy (dest, bit2byte + pix8*8, j);
	  dest += j;
	}

      if (linepad)
	err |= ((rle ? rle_getc (ifp) : getc (ifp)) < 0);

      scan_lines++;

      if ((l_run_mode != GIMP_RUN_NONINTERACTIVE) && ((i % 20) == 0))
	gimp_progress_update ((double)(i+1) / (double)height);

      if ((scan_lines == tile_height) || ((i+1) == height))
	{
	  gimp_pixel_rgn_set_rect (&pixel_rgn, data, 0, i-scan_lines+1,
				   width, scan_lines);
	  scan_lines = 0;
	  dest = data;
	}
    }

  g_free (data);

  if (err)
    g_message (_("EOF encountered on reading"));

  gimp_drawable_flush (drawable);

  return (image_ID);
}


/* Load SUN-raster-file with depth 8 */

static gint32
load_sun_d8 (gchar           *filename,
             FILE            *ifp,
             L_SUNFILEHEADER *sunhdr,
             guchar          *suncolmap)
{
  int width, height, linepad, i, j;
  int greyscale, ncols;
  int scan_lines, tile_height;
  guchar *dest, *data;
  gint32 layer_ID, image_ID;
  GimpPixelRgn pixel_rgn;
  GimpDrawable *drawable;
  int err = 0, rle = (sunhdr->l_ras_type == RAS_TYPE_RLE);

  width = sunhdr->l_ras_width;
  height = sunhdr->l_ras_height;

  /* This could also be a greyscale image. Check it */
  ncols = sunhdr->l_ras_maplength / 3;

  greyscale = 1;  /* Also greyscale if no colourmap present */

  if ((ncols > 0) && (suncolmap != NULL))
    {
      for (j = 0; j < ncols; j++)
	{
	  if (   (suncolmap[j] != j)
		 || (suncolmap[j+ncols] != j)
		 || (suncolmap[j+2*ncols] != j))
	    {
	      greyscale = 0;
	      break;
	    }
	}
    }

  image_ID = create_new_image (filename, width, height,
			       greyscale ? GIMP_GRAY : GIMP_INDEXED,
			       &layer_ID, &drawable, &pixel_rgn);

  tile_height = gimp_tile_height ();
  data = g_malloc (tile_height * width);

  if (!greyscale)
    set_color_table (image_ID, sunhdr, suncolmap);

  linepad = (sunhdr->l_ras_width % 2);

  if (rle) rle_startread (ifp);  /* Initialize RLE-buffer */

  dest = data;
  scan_lines = 0;

  for (i = 0; i < height; i++)
    {
      memset ((char *)dest, 0, width);
      err |= ((rle ? rle_fread ((char *)dest, 1, width, ifp)
               : fread ((char *)dest, 1, width, ifp)) != width);

      if (linepad)
	err |= ((rle ? rle_getc (ifp) : getc (ifp)) < 0);

      dest += width;
      scan_lines++;

      if ((l_run_mode != GIMP_RUN_NONINTERACTIVE) && ((i % 20) == 0))
	gimp_progress_update ((double)(i+1) / (double)height);

      if ((scan_lines == tile_height) || ((i+1) == height))
	{
	  gimp_pixel_rgn_set_rect (&pixel_rgn, data, 0, i-scan_lines+1,
				   width, scan_lines);
	  scan_lines = 0;
	  dest = data;
	}
    }

  g_free (data);

  if (err)
    g_message (_("EOF encountered on reading"));

  gimp_drawable_flush (drawable);

  return (image_ID);
}


/* Load SUN-raster-file with depth 24 */
static gint32
load_sun_d24 (gchar            *filename,
              FILE             *ifp,
              L_SUNFILEHEADER  *sunhdr,
              guchar           *suncolmap)
{
  register guchar *dest, blue;
  guchar *data;
  int width, height, linepad, tile_height, scan_lines;
  int i, j;
  gint32 layer_ID, image_ID;
  GimpPixelRgn pixel_rgn;
  GimpDrawable *drawable;
  int err = 0, rle = (sunhdr->l_ras_type == RAS_TYPE_RLE);

  width = sunhdr->l_ras_width;
  height = sunhdr->l_ras_height;

  image_ID = create_new_image (filename, width, height, GIMP_RGB,
			       &layer_ID, &drawable, &pixel_rgn);

  tile_height = gimp_tile_height ();
  data = g_malloc (tile_height * width * 3);

  linepad = ((sunhdr->l_ras_width*3) % 2);

  if (rle) rle_startread (ifp);  /* Initialize RLE-buffer */

  dest = data;
  scan_lines = 0;

  for (i = 0; i < height; i++)
    {
      memset ((char *)dest, 0, 3*width);
      err |= ((rle ? rle_fread ((char *)dest, 3, width, ifp)
               : fread ((char *)dest, 3, width, ifp)) != width);

      if (linepad)
	err |= ((rle ? rle_getc (ifp) : getc (ifp)) < 0);

      if (sunhdr->l_ras_type == 3) /* RGB-format ? That is what GIMP wants */
	{
	  dest += width*3;
	}
      else                         /* We have BGR format. Correct it */
	{
	  for (j = 0; j < width; j++)
	    {
	      blue = *dest;
	      *dest = *(dest+2);
	      *(dest+2) = blue;
	      dest += 3;
	    }
	}

      scan_lines++;

      if ((l_run_mode != GIMP_RUN_NONINTERACTIVE) && ((i % 20) == 0))
	gimp_progress_update ((double)(i+1) / (double)height);

      if ((scan_lines == tile_height) || ((i+1) == height))
	{
	  gimp_pixel_rgn_set_rect (&pixel_rgn, data, 0, i-scan_lines+1,
				   width, scan_lines);
	  scan_lines = 0;
	  dest = data;
	}
    }

  g_free (data);

  if (err)
    g_message (_("EOF encountered on reading"));

  gimp_drawable_flush (drawable);

  return (image_ID);
}


/* Load SUN-raster-file with depth 32 */

static gint32
load_sun_d32 (gchar           *filename,
              FILE            *ifp,
              L_SUNFILEHEADER *sunhdr,
              guchar          *suncolmap)
{
  register guchar *dest, blue;
  guchar *data;
  int width, height, tile_height, scan_lines;
  int i, j;
  gint32 layer_ID, image_ID;
  GimpPixelRgn pixel_rgn;
  GimpDrawable *drawable;
  int err = 0, cerr, rle = (sunhdr->l_ras_type == RAS_TYPE_RLE);
  width = sunhdr->l_ras_width;
  height = sunhdr->l_ras_height;

  /* initialize */

  cerr = 0;

  image_ID = create_new_image (filename, width, height, GIMP_RGB,
			       &layer_ID, &drawable, &pixel_rgn);

  tile_height = gimp_tile_height ();
  data = g_malloc (tile_height * width * 3);

  if (rle) rle_startread (ifp);  /* Initialize RLE-buffer */

  dest = data;
  scan_lines = 0;

  for (i = 0; i < height; i++)
    {
      if (rle)
	{
	  for (j = 0; j < width; j++)
	    {
	      rle_getc (ifp);   /* Skip unused byte */
	      *(dest++) = rle_getc (ifp);
	      *(dest++) = rle_getc (ifp);
	      *(dest++) = (cerr = (rle_getc (ifp)));
	    }
	}
      else
	{
	  for (j = 0; j < width; j++)
	    {
	      getc (ifp);   /* Skip unused byte */
	      *(dest++) = getc (ifp);
	      *(dest++) = getc (ifp);
	      *(dest++) = (cerr = (getc (ifp)));
	    }
	}
      err |= (cerr < 0);

      if (sunhdr->l_ras_type != 3) /* BGR format ? Correct it */
	{
	  for (j = 0; j < width; j++)
	    {
	      dest -= 3;
	      blue = *dest;
	      *dest = *(dest+2);
	      *(dest+2) = blue;
	    }
	  dest += width*3;
	}

      scan_lines++;

      if ((l_run_mode != GIMP_RUN_NONINTERACTIVE) && ((i % 20) == 0))
	gimp_progress_update ((double)(i+1) / (double)height);

      if ((scan_lines == tile_height) || ((i+1) == height))
	{
	  gimp_pixel_rgn_set_rect (&pixel_rgn, data, 0, i-scan_lines+1,
				   width, scan_lines);
	  scan_lines = 0;
	  dest = data;
	}
    }

  g_free (data);

  if (err)
    g_message (_("EOF encountered on reading"));

  gimp_drawable_flush (drawable);

  return (image_ID);
}


static gint
save_index (FILE    *ofp,
            gint32  image_ID,
            gint32  drawable_ID,
            gint    grey,
            gint    rle)
{ 
  int height, width, linepad, i, j;
  int ncols, bw, is_bw, is_wb, bpl;
  int tile_height;
  long tmp = 0;
  guchar *cmap, *bwline = NULL;
  guchar *data, *src;
  L_SUNFILEHEADER sunhdr;
  guchar sun_colormap[256*3];
  static guchar sun_bwmap[6] = { 0,255,0,255,0,255 };
  static guchar sun_wbmap[6] = { 255,0,255,0,255,0 };
  unsigned char *suncolmap = sun_colormap;
  GimpPixelRgn pixel_rgn;
  GimpDrawable *drawable;
  GimpImageType drawable_type;
  WRITE_FUN *write_fun;

  drawable = gimp_drawable_get (drawable_ID);
  drawable_type = gimp_drawable_type (drawable_ID);
  width = drawable->width;
  height = drawable->height;
  tile_height = gimp_tile_height ();
  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, width, height, FALSE, FALSE);

  /* allocate a buffer for retrieving information from the pixel region  */
  src = data = (unsigned char *)g_malloc (tile_height * width * drawable->bpp);

  /* Fill SUN-color map */
  if (grey)
    {
      ncols = 256;

      for (j = 0; j < ncols; j++)
	{
	  suncolmap[j] = j;
	  suncolmap[j+ncols] = j;
	  suncolmap[j+ncols*2] = j;
	}
    }
  else
    {
      cmap = gimp_image_get_cmap (image_ID, &ncols);

      for (j = 0; j < ncols; j++)
	{
	  suncolmap[j] = *(cmap++);
	  suncolmap[j+ncols] = *(cmap++);
	  suncolmap[j+ncols*2] = *(cmap++);
	}
    }

  bw = (ncols == 2);   /* Maybe this is a two-colour image */
  if (bw)
    {
      bwline = g_malloc ((width+7)/8);
      if (bwline == NULL) bw = 0;
    }

  is_bw = is_wb = 0;
  if (bw)    /* The Sun-OS imagetool generates index 0 for white and */
    {          /* index 1 for black. Do the same without colourtable. */
      is_bw = (memcmp (suncolmap, sun_bwmap, 6) == 0);
      is_wb = (memcmp (suncolmap, sun_wbmap, 6) == 0);
    }

  /* Number of data bytes per line */
  bpl = bw ?  (width+7)/8 : width;
  linepad = bpl % 2;

  /* Fill in the SUN header */
  sunhdr.l_ras_magic = RAS_MAGIC;
  sunhdr.l_ras_width = width;
  sunhdr.l_ras_height = height;
  sunhdr.l_ras_depth = bw ? 1 : 8;
  sunhdr.l_ras_length = (bpl+linepad) * height;
  sunhdr.l_ras_type = (rle) ? RAS_TYPE_RLE : RAS_TYPE_STD;
  if (is_bw || is_wb)   /* No colourtable for real b/w images */
    {
      sunhdr.l_ras_maptype = 0;   /* No colourmap */
      sunhdr.l_ras_maplength = 0; /* Length of colourmap */
    }
  else
    {
      sunhdr.l_ras_maptype = 1;   /* RGB colourmap */
      sunhdr.l_ras_maplength = ncols*3; /* Length of colourmap */
    }

  write_sun_header (ofp, &sunhdr);

  if (sunhdr.l_ras_maplength > 0)
    write_sun_cols (ofp, &sunhdr, suncolmap);

#define GET_INDEX_TILE(begin) \
  {int scan_lines; \
    scan_lines = (i+tile_height-1 < height) ? tile_height : (height-i); \
    gimp_pixel_rgn_get_rect (&pixel_rgn, begin, 0, i, width, scan_lines); \
    src = begin; }

  if (rle) { write_fun = (WRITE_FUN *)&rle_fwrite; rle_startwrite (ofp); }
  else write_fun = (WRITE_FUN *)&my_fwrite;

  if (bw)  /* Two colour image */
    {
      for (i = 0; i < height; i++)
	{
	  if ((i % tile_height) == 0) GET_INDEX_TILE (data); /* Get more data */
	  byte2bit (src, width, bwline, is_bw);
	  (*write_fun) (bwline, bpl, 1, ofp);
	  if (linepad) (*write_fun) ((char *)&tmp, linepad, 1, ofp);
	  src += width;

	  if ((l_run_mode != GIMP_RUN_NONINTERACTIVE) && ((i % 20) == 0))
	    gimp_progress_update ((double) i / (double) height);
	}
    }
  else   /* Colour or grey-image */
    {
      for (i = 0; i < height; i++)
	{
	  if ((i % tile_height) == 0) GET_INDEX_TILE (data); /* Get more data */
	  (*write_fun) ((char *)src, width, 1, ofp);
	  if (linepad) (*write_fun) ((char *)&tmp, linepad, 1, ofp);
	  src += width;

	  if ((l_run_mode != GIMP_RUN_NONINTERACTIVE) && ((i % 20) == 0))
	    gimp_progress_update ((double) i / (double) height);
	}
    }

  if (rle) 
    rle_endwrite (ofp);

  g_free (data);

  if (bwline) 
    g_free (bwline);

  gimp_drawable_detach (drawable);

  if (ferror (ofp))
    {
      g_message (_("Write error occured"));
      return (FALSE);
    }
  return (TRUE);
#undef GET_INDEX_TILE
}


static gint
save_rgb (FILE   *ofp,
          gint32  image_ID,
          gint32  drawable_ID,
          gint    rle)
{
  int height, width, tile_height, linepad;
  int i, j, bpp;
  guchar *data, *src;
  L_SUNFILEHEADER sunhdr;
  GimpPixelRgn pixel_rgn;
  GimpDrawable *drawable;
  GimpImageType drawable_type;

  drawable = gimp_drawable_get (drawable_ID);
  drawable_type = gimp_drawable_type (drawable_ID);
  width = drawable->width;
  height = drawable->height;
  tile_height = gimp_tile_height ();
  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, width, height, FALSE, FALSE);

  /* allocate a buffer for retrieving information from the pixel region  */
  src = data = (guchar *) g_malloc (tile_height * width * drawable->bpp);

/* #define SUNRAS_32 */
#ifdef SUNRAS_32
  bpp = 4;
#else
  bpp = 3;
#endif
  linepad = (width * bpp) % 2;

  /* Fill in the SUN header */
  sunhdr.l_ras_magic = RAS_MAGIC;
  sunhdr.l_ras_width = width;
  sunhdr.l_ras_height = height;
  sunhdr.l_ras_depth = 8 * bpp;
  sunhdr.l_ras_length = (width*bpp + linepad)*height;
  sunhdr.l_ras_type = (rle) ? RAS_TYPE_RLE : RAS_TYPE_STD;
  sunhdr.l_ras_maptype = 0;   /* No colourmap */
  sunhdr.l_ras_maplength = 0; /* Length of colourmap */

  write_sun_header (ofp, &sunhdr);

#define GET_RGB_TILE(begin) \
  {int scan_lines; \
    scan_lines = (i+tile_height-1 < height) ? tile_height : (height-i); \
    gimp_pixel_rgn_get_rect (&pixel_rgn, begin, 0, i, width, scan_lines); \
    src = begin; }

  if (!rle)
    {
      for (i = 0; i < height; i++)
	{
	  if ((i % tile_height) == 0) GET_RGB_TILE (data); /* Get more data */
	  for (j = 0; j < width; j++)
	    {
	      if (bpp == 4) putc (0, ofp);   /* Dummy */
	      putc (*(src+2), ofp);          /* Blue */
	      putc (*(src+1), ofp);          /* Green */
	      putc (*src, ofp);              /* Red */
	      src += 3;
	    }
	  for (j = 0; j < linepad; j++)
	    putc (0, ofp);
	  
	  if ((l_run_mode != GIMP_RUN_NONINTERACTIVE) && ((i % 20) == 0))
	    gimp_progress_update ((double) i / (double) height);
	}
    }
  else  /* Write runlength encoded */
    {
      rle_startwrite (ofp);

      for (i = 0; i < height; i++)
	{
	  if ((i % tile_height) == 0) GET_RGB_TILE (data); /* Get more data */
	  for (j = 0; j < width; j++)
	    {
	      if (bpp == 4) rle_putc (0, ofp);   /* Dummy */
	      rle_putc (*(src+2), ofp);          /* Blue */
	      rle_putc (*(src+1), ofp);          /* Green */
	      rle_putc (*src, ofp);              /* Red */
	      src += 3;
	    }
	  for (j = 0; j < linepad; j++)
	    rle_putc (0, ofp);

	  if ((l_run_mode != GIMP_RUN_NONINTERACTIVE) && ((i % 20) == 0))
	    gimp_progress_update ((double) i / (double) height);
	}

      rle_endwrite (ofp);
    }
  g_free (data);

  gimp_drawable_detach (drawable);

  if (ferror (ofp))
    {
      g_message (_("Write error occured"));
      return (FALSE);
    }
  return (TRUE);
#undef GET_RGB_TILE
}


/*  Save interface functions  */

static gint
save_dialog (void)
{
  GtkWidget *dlg;
  GtkWidget *frame;

  dlg = gimp_dialog_new (_("Save as SUNRAS"), "sunras",
			 gimp_standard_help_func, "filters/sunras.html",
			 GTK_WIN_POS_MOUSE,
			 FALSE, TRUE, FALSE,

			 GTK_STOCK_CANCEL, gtk_widget_destroy,
			 NULL, 1, NULL, FALSE, TRUE,
			 GTK_STOCK_OK, save_ok_callback,
			 NULL, NULL, NULL, TRUE, FALSE,

			 NULL);

  gtk_signal_connect (GTK_OBJECT (dlg), "destroy",
                      GTK_SIGNAL_FUNC (gtk_main_quit),
                      NULL);

  /*  file save type  */
  frame = gimp_radio_group_new2 (TRUE, _("Data Formatting"),
                                 G_CALLBACK (gimp_radio_button_update),
				 &psvals.rle, (gpointer) psvals.rle,

				 _("RunLength Encoded"), (gpointer) TRUE, NULL,
				 _("Standard"),          (gpointer) FALSE, NULL,

				 NULL);
  gtk_container_set_border_width (GTK_CONTAINER (frame), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), frame, TRUE, TRUE, 0);
  gtk_widget_show (frame);

  gtk_widget_show (dlg);

  gtk_main ();
  gdk_flush ();

  return psint.run;
}

static void
save_ok_callback (GtkWidget *widget,
                  gpointer   data)
{
  psint.run = TRUE;

  gtk_widget_destroy (GTK_WIDGET (data));
}

static int 
my_fwrite (void *ptr, 
	   int   size, 
	   int   nmemb, 
	   FILE *stream)
{
  return fwrite (ptr, size, nmemb, stream);
}
