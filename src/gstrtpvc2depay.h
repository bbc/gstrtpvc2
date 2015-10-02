/* GStreamer
 * James Weaver <james.barrett@bbc.co.uk> (C) BBC <2015>
 * Copyright (C) <2006> Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __GST_RTP_VC2_DEPAY_H__
#define __GST_RTP_VC2_DEPAY_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/rtp/gstrtpbasedepayload.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_VC2_DEPAY \
  (gst_rtp_vc2_depay_get_type())
#define GST_RTP_VC2_DEPAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_VC2_DEPAY,GstRtpVC2Depay))
#define GST_RTP_VC2_DEPAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_VC2_DEPAY,GstRtpVC2DepayClass))
#define GST_IS_RTP_VC2_DEPAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_VC2_DEPAY))
#define GST_IS_RTP_VC2_DEPAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_VC2_DEPAY))

typedef struct _GstRtpVC2Depay GstRtpVC2Depay;
typedef struct _GstRtpVC2DepayClass GstRtpVC2DepayClass;

struct _GstRtpVC2Depay
{
  GstRTPBaseDepayload depayload;

  GstAdapter *adapter;
  gboolean    wait_start;

  guint32   last_parse_info_offset;
  gboolean  in_picture;
  guint32   picture_number;
  gint      picture_size;
};

struct _GstRtpVC2DepayClass
{
  GstRTPBaseDepayloadClass parent_class;
};

enum _GstRtpVC2DepayParseCode {
  GSTRTPVC2DEPAYPARSECODE_SEQUENCE_HEADER = 0x00,
  GSTRTPVC2DEPAYPARSECODE_END_OF_SEQUENCE = 0x10,
  GSTRTPVC2DEPAYPARSECODE_HQ_FRAGMENT     = 0xEC,
};

GType gst_rtp_vc2_depay_get_type (void);

gboolean gst_rtp_vc2_depay_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_RTP_VC2_DEPAY_H__ */
