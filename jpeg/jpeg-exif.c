/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
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
 * EXIF-handling code for the jpeg plugin.  May eventually be better
 * to move this stuff into libgimpbase and make it available for
 * other plugins.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include <jpeglib.h>
#include <jerror.h>

#ifdef HAVE_EXIF

#include <libexif/exif-content.h>
#include <libexif/exif-data.h>
#include <libexif/exif-utils.h>

#define EXIF_HEADER_SIZE 8

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "gimpexif.h"

#include "jpeg.h"

#include "libgimp/stdplugins-intl.h"


static gboolean      jpeg_query                (const gchar *msg);


void
jpeg_apply_exif_data_to_image (const gchar   *filename,
                               const gint32   image_ID)
{
  ExifData     *exif_data     = NULL;
  ExifEntry    *entry;
  gint          byte_order;

  exif_data = exif_data_new_from_file (filename);
  if (!exif_data)
    return;

  /*
   * Unfortunately libexif may return a non-null exif_data even if the file
   * contains no exif data.  We check for validity by making sure it
   * has an ExifVersion tag.
  */
  if (! exif_content_get_entry (exif_data->ifd[EXIF_IFD_EXIF],
                                EXIF_TAG_EXIF_VERSION))
    return;

  gimp_metadata_store_exif (image_ID, exif_data);

  byte_order = exif_data_get_byte_order (exif_data);

  /* get orientation and rotate image accordingly if necessary */
  if ((entry = exif_content_get_entry (exif_data->ifd[EXIF_IFD_0],
                                       EXIF_TAG_ORIENTATION)))
    {
      gint orient = exif_get_short (entry->data, byte_order);

      if (load_interactive && orient > 1 && orient <= 8)
        if (jpeg_query (_("According to the EXIF data, this image is rotated. "
                          "Would you like GIMP to rotate it into the standard "
                          "orientation?")))
          {
            switch (orient)
              {
              case 1: /* standard orientation, do nothing */
                break;
              case 2: /* flipped right-left */
                gimp_image_flip (image_ID, GIMP_ORIENTATION_HORIZONTAL);
                break;
              case 3: /* rotated 180 */
                break;
              case 4: /* flipped top-bottom */
                gimp_image_flip (image_ID, GIMP_ORIENTATION_VERTICAL);
                break;
              case 5: /* flipped diagonally around '\' */
                gimp_image_rotate (image_ID, GIMP_ROTATE_90);
                gimp_image_flip (image_ID, GIMP_ORIENTATION_HORIZONTAL);
                break;
              case 6: /* 90 CW */
                gimp_image_rotate (image_ID, GIMP_ROTATE_90);
                break;
              case 7: /* flipped diagonally around '/' */
                gimp_image_rotate (image_ID, GIMP_ROTATE_90);
                gimp_image_flip (image_ID, GIMP_ORIENTATION_VERTICAL);
                break;
              case 8: /* 90 CCW */
                gimp_image_rotate (image_ID, GIMP_ROTATE_270);
                break;
              default: /* can't happen */
                break;
              }
        }
    }


  exif_data_unref (exif_data);
}


void
jpeg_setup_exif_for_save (ExifData      *exif_data,
                          const gint32   image_ID)
{
  ExifRational  r;
  gdouble       xres, yres;
  ExifEntry    *entry;
  gint          byte_order = exif_data_get_byte_order (exif_data);

  /* set orientation to top - left */
  if ((entry = exif_content_get_entry (exif_data->ifd[EXIF_IFD_0],
                                       EXIF_TAG_ORIENTATION)))
    {
      exif_set_short (entry->data, byte_order, (ExifShort) 1);
    }

  /* set x and y resolution */
  gimp_image_get_resolution (image_ID, &xres, &yres);
  r.numerator =   xres;
  r.denominator = 1;
  if ((entry = exif_content_get_entry (exif_data->ifd[EXIF_IFD_0],
                                       EXIF_TAG_X_RESOLUTION)))
    {
      exif_set_rational (entry->data, byte_order, r);
    }
  r.numerator = yres;
  if ((entry = exif_content_get_entry (exif_data->ifd[EXIF_IFD_0],
                                       EXIF_TAG_Y_RESOLUTION)))
    {
      exif_set_rational (entry->data, byte_order, r);
    }

  /* set resolution unit, always inches */
  if ((entry = exif_content_get_entry (exif_data->ifd[EXIF_IFD_0],
                                       EXIF_TAG_RESOLUTION_UNIT)))
    {
      exif_set_short (entry->data, byte_order, (ExifShort) 2);
    }

  /* set software to "The GIMP" */
  if ((entry = exif_content_get_entry (exif_data->ifd[EXIF_IFD_0],
                                       EXIF_TAG_SOFTWARE)))
    {
      entry->data = g_strdup ("The GIMP");
      entry->size = strlen ("The GIMP") + 1;
      entry->components = entry->size;
    }

  /* set the width and height */
  if ((entry = exif_content_get_entry (exif_data->ifd[EXIF_IFD_0],
                                       EXIF_TAG_PIXEL_X_DIMENSION)))
    {
      exif_set_long (entry->data, byte_order,
                     (ExifLong) gimp_image_width (image_ID));
    }
  if ((entry = exif_content_get_entry (exif_data->ifd[EXIF_IFD_0],
                                       EXIF_TAG_PIXEL_Y_DIMENSION)))
    {
      exif_set_long (entry->data, byte_order,
                     (ExifLong) gimp_image_height (image_ID));
    }

  /*
   * set the date & time image was saved
   * note, date & time of original photo is stored elsewwhere, we
   * aren't losing it.
   */
  if ((entry = exif_content_get_entry (exif_data->ifd[EXIF_IFD_0],
                                       EXIF_TAG_DATE_TIME)))
    {
      /* small memory leak here */
      entry->data = NULL;
      exif_entry_initialize (entry, EXIF_TAG_DATE_TIME);
    }

  /* should set components configuration, don't know how */

  /*
   *remove entries that don't apply to jpeg
   *(may have come from tiff or raw)
  */
  gimp_exif_data_remove_entry(exif_data, EXIF_IFD_0, EXIF_TAG_COMPRESSION);
  gimp_exif_data_remove_entry(exif_data, EXIF_IFD_0, EXIF_TAG_IMAGE_WIDTH);
  gimp_exif_data_remove_entry(exif_data, EXIF_IFD_0, EXIF_TAG_IMAGE_LENGTH);
  gimp_exif_data_remove_entry(exif_data, EXIF_IFD_0, EXIF_TAG_BITS_PER_SAMPLE);
  gimp_exif_data_remove_entry(exif_data, EXIF_IFD_0, EXIF_TAG_SAMPLES_PER_PIXEL);
  gimp_exif_data_remove_entry(exif_data, EXIF_IFD_0, EXIF_TAG_PHOTOMETRIC_INTERPRETATION);
  gimp_exif_data_remove_entry(exif_data, EXIF_IFD_0, EXIF_TAG_STRIP_OFFSETS);
  gimp_exif_data_remove_entry(exif_data, EXIF_IFD_0, EXIF_TAG_PLANAR_CONFIGURATION);
  gimp_exif_data_remove_entry(exif_data, EXIF_IFD_0, EXIF_TAG_YCBCR_SUB_SAMPLING);

  /* should set thumbnail attributes */
}


static gboolean
jpeg_query (const gchar *msg)
{
  GtkWidget *dialog;
  gboolean   ret;

  dialog = gtk_message_dialog_new (NULL, 0,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_OK_CANCEL,
                                   "%s", msg);

  ret = gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);

  return (ret == GTK_RESPONSE_OK);
}


#endif /* HAVE_EXIF */
