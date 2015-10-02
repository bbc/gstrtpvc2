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

#ifndef __VC2_VLC_PARSE_H__
#define __VC2_VLC_PARSE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _vc2_vlc_decoder vc2_vlc_decoder;

struct _vc2_vlc_decoder {
  guint8 *start;
  int bit;
  int offset;
  int size;
};

vc2_vlc_decoder* vc2_vlc_decoder_new      (guint8 *data, gssize size);
gboolean         vc2_vlc_decoder_overrun  (vc2_vlc_decoder *decoder);
gboolean         vc2_vlc_decoder_read_bool(vc2_vlc_decoder *decoder);
guint32          vc2_vlc_decoder_read_uint(vc2_vlc_decoder *decoder);
gssize           vc2_vlc_decoder_length   (vc2_vlc_decoder *decoder);
void             vc2_vlc_decoder_free     (vc2_vlc_decoder *decoder);

typedef struct _vc2_sequence_header vc2_sequence_header;

struct _vc2_sequence_header {
  GstBuffer *buf;
  gssize length;

  guint32 picture_width;
  guint32 picture_height;
  gboolean interlaced;
};

vc2_sequence_header* vc2_sequence_header_new (GstBuffer *buf);
gboolean             vc2_sequence_header_cmp (vc2_sequence_header* hdr, GstBuffer *buf, gsize size);
void                 vc2_sequence_header_free(vc2_sequence_header* hdr);

typedef struct _vc2_hq_transform_parameters vc2_hq_transform_parameters;

struct _vc2_hq_transform_parameters {
  guint32 wavelet_index;
  guint32 dwt_depth;
  guint32 slices_x;
  guint32 slices_y;
  guint32 slice_prefix_bytes;
  guint32 slice_size_scalar;

  gsize   coded_size;
};

vc2_hq_transform_parameters* vc2_hq_transform_parameters_new (guint8 *data, gssize data_size);
void vc2_hq_transform_parameters_free(vc2_hq_transform_parameters* params);

G_END_DECLS

#endif /* __VC2_VLC_PARSE_H__ */
