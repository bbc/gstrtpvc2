/* ex: set tabstop=2 shiftwidth=2 expandtab: */
/* GStreamer VC2 RTP
 *
 * James Weaver <james.barrett@bbc.co.uk> (C) BBC <2015>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "vc2vlcparse.h"
#include <stdlib.h>

vc2_vlc_decoder* vc2_vlc_decoder_new      (guint8 *data, gssize size) {
  vc2_vlc_decoder* r = (vc2_vlc_decoder *)malloc(sizeof(vc2_vlc_decoder));
  r->start  = data;
  r->bit    = 7;
  r->offset = 0;
  r->size   = size;
  return r;
}

gint vc2_vlc_decoder_read_bit(vc2_vlc_decoder *decoder) {
  if (decoder->offset >= decoder->size)
    return 1;

  gint d = ((decoder->start[decoder->offset]) >> decoder->bit)&0x1;
  decoder->bit--;
  if (decoder->bit < 0) {
    decoder->offset++;
    decoder->bit = 7;
  }
  return d;
}

gboolean         vc2_vlc_decoder_overrun  (vc2_vlc_decoder *decoder) {
  if (decoder->offset >= decoder->size)
    return TRUE;
  return FALSE;
}

gboolean         vc2_vlc_decoder_read_bool(vc2_vlc_decoder *decoder) {
  return (gboolean)vc2_vlc_decoder_read_bit(decoder);
}

guint32          vc2_vlc_decoder_read_uint(vc2_vlc_decoder *decoder) {
  guint32 d = 1;
  while (vc2_vlc_decoder_read_bit(decoder) == 0) {
    d <<= 1;
    d |= vc2_vlc_decoder_read_bit(decoder);
  }

  return d - 1;
}

gssize           vc2_vlc_decoder_length   (vc2_vlc_decoder *decoder) {
  if (decoder->bit == 7)
    return decoder->offset;
  else
    return decoder->offset + 1;
}

void             vc2_vlc_decoder_free     (vc2_vlc_decoder *decoder) {
  free((void *)decoder);
}

struct _base_video_format_info {
  guint32 frame_width;
  guint32 frame_height;
  gboolean interlaced;
} BASE_VIDEO_FORMAT_INFO[] = {
  { 640, 480, FALSE },
  { 176, 120, FALSE },
  { 176, 144, FALSE },
  { 352, 240, FALSE },
  { 352, 288, FALSE },
  { 704, 480, FALSE },
  { 704, 576, FALSE },
  { 720, 480, TRUE },
  { 720, 576, TRUE },
  { 1280, 720, FALSE },
  { 1280, 720, FALSE },
  { 1920, 1080, TRUE },
  { 1920, 1080, TRUE },
  { 1920, 1080, FALSE },
  { 1920, 1080, FALSE },
  { 2048, 1080, FALSE },
  { 4096, 2160, FALSE },
  { 3840, 2160, FALSE },
  { 3840, 2160, FALSE },
  { 7680, 4320, FALSE },
  { 7680, 4320, FALSE },
  { 1920, 1080, FALSE },
  { 720, 486, TRUE },
};

vc2_sequence_header* vc2_sequence_header_new (GstBuffer *buf) {
  vc2_sequence_header* hdr = (vc2_sequence_header*)malloc(sizeof(vc2_sequence_header));
  hdr->buf            = gst_buffer_ref(buf);
  hdr->length         = 0;
  hdr->picture_width  = 0;
  hdr->picture_height = 0;
  hdr->interlaced     = FALSE;

  {
    GstMapInfo info;
    gst_buffer_map(hdr->buf, &info, GST_MAP_READ);
    vc2_vlc_decoder* dec = vc2_vlc_decoder_new(info.data, info.size);

    unsigned int major_version = vc2_vlc_decoder_read_uint(dec);
    vc2_vlc_decoder_read_uint(dec);
    unsigned int profile       = vc2_vlc_decoder_read_uint(dec);
    vc2_vlc_decoder_read_uint(dec);

    if (major_version != 2 || profile != 3) {
      vc2_vlc_decoder_free(dec);
      gst_buffer_unmap(hdr->buf, &info);
      gst_buffer_unref(hdr->buf);
      free(hdr);
      return NULL;
    }

    unsigned int base_video_format = vc2_vlc_decoder_read_uint(dec);
    unsigned int frame_width  = BASE_VIDEO_FORMAT_INFO[base_video_format].frame_width;
    unsigned int frame_height = BASE_VIDEO_FORMAT_INFO[base_video_format].frame_height;
    gboolean     interlaced   = BASE_VIDEO_FORMAT_INFO[base_video_format].interlaced;

    if (vc2_vlc_decoder_read_bool(dec)) {
      frame_width  = vc2_vlc_decoder_read_uint(dec);
      frame_height = vc2_vlc_decoder_read_uint(dec);
    }

    if (vc2_vlc_decoder_read_bool(dec)) {
      vc2_vlc_decoder_read_uint(dec);
    }

    if (vc2_vlc_decoder_read_bool(dec)) {
      vc2_vlc_decoder_read_uint(dec);
    }

    if (vc2_vlc_decoder_read_bool(dec)) {
      if (vc2_vlc_decoder_read_uint(dec) == 0) {
        vc2_vlc_decoder_read_uint(dec);
        vc2_vlc_decoder_read_uint(dec);
      }
    }

    if (vc2_vlc_decoder_read_bool(dec)) {
      if (vc2_vlc_decoder_read_uint(dec) == 0) {
        vc2_vlc_decoder_read_uint(dec);
        vc2_vlc_decoder_read_uint(dec);
      }
    }

    if (vc2_vlc_decoder_read_bool(dec)) {
      vc2_vlc_decoder_read_uint(dec);
      vc2_vlc_decoder_read_uint(dec);
      vc2_vlc_decoder_read_uint(dec);
      vc2_vlc_decoder_read_uint(dec);
    }

    if (vc2_vlc_decoder_read_bool(dec)) {
      if (vc2_vlc_decoder_read_uint(dec) == 0) {
        vc2_vlc_decoder_read_uint(dec);
        vc2_vlc_decoder_read_uint(dec);
        vc2_vlc_decoder_read_uint(dec);
        vc2_vlc_decoder_read_uint(dec);
      }
    }

    if (vc2_vlc_decoder_read_bool(dec)) {
      if (vc2_vlc_decoder_read_uint(dec) == 0) {
        if (vc2_vlc_decoder_read_bool(dec)) {
          vc2_vlc_decoder_read_uint(dec);
        }

        if (vc2_vlc_decoder_read_bool(dec)) {
          vc2_vlc_decoder_read_uint(dec);
        }

        if (vc2_vlc_decoder_read_bool(dec)) {
          vc2_vlc_decoder_read_uint(dec);
        }
      }
    }

    int picture_coding_mode = vc2_vlc_decoder_read_uint(dec);
    if (picture_coding_mode == 0) {
      interlaced = FALSE;
    } else {
      interlaced = TRUE;
    }
    
    hdr->picture_width = frame_width;
    if (interlaced) {
      hdr->picture_height = frame_height/2;
      hdr->interlaced = TRUE;
    } else {
      hdr->picture_height = frame_height;
      hdr->interlaced = FALSE;
    }

    vc2_vlc_decoder_free(dec);
    gst_buffer_unmap(hdr->buf, &info);
  }

  return hdr;
}

gboolean             vc2_sequence_header_cmp (vc2_sequence_header* hdr, GstBuffer *buf, gsize size) {
  if (hdr == NULL)
    return FALSE;

  if (hdr->length > size)
    return FALSE;

  gboolean r = TRUE;
  {
    char *data = malloc(hdr->length);
    gst_buffer_extract(hdr->buf, 0, data, hdr->length);

    if (gst_buffer_memcmp(buf, 0, data, hdr->length))
      r = FALSE;

    free(data);
  }

  return r;
}

void                 vc2_sequence_header_free(vc2_sequence_header* hdr) {
  if (hdr != NULL) {
    gst_buffer_unref(hdr->buf);
    free(hdr);
  }
}

vc2_hq_transform_parameters* vc2_hq_transform_parameters_new (guint8 *data, gssize data_size) {
  vc2_hq_transform_parameters* params = (vc2_hq_transform_parameters *)malloc(sizeof(vc2_hq_transform_parameters));
  vc2_vlc_decoder* dec = vc2_vlc_decoder_new(data, data_size);
  int i;

  params->wavelet_index      = vc2_vlc_decoder_read_uint(dec);
  params->dwt_depth          = vc2_vlc_decoder_read_uint(dec);
  params->slices_x           = vc2_vlc_decoder_read_uint(dec);
  params->slices_y           = vc2_vlc_decoder_read_uint(dec);
  params->slice_prefix_bytes = vc2_vlc_decoder_read_uint(dec);
  params->slice_size_scalar  = vc2_vlc_decoder_read_uint(dec);

  if (vc2_vlc_decoder_read_bool(dec)) {
    vc2_vlc_decoder_read_uint(dec);
    for (i = 1; i < params->dwt_depth; i++) {
      vc2_vlc_decoder_read_uint(dec);
      vc2_vlc_decoder_read_uint(dec);
      vc2_vlc_decoder_read_uint(dec);
    }
  }

  params->coded_size = vc2_vlc_decoder_length(dec);

  if (vc2_vlc_decoder_overrun(dec)) {
    vc2_vlc_decoder_free(dec);
    free(params);
    return NULL;
  }

  vc2_vlc_decoder_free(dec);
  return params;
}

void vc2_hq_transform_parameters_free(vc2_hq_transform_parameters* params) {
  if (params)
    free(params);
}
