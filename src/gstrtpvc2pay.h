/* GStreamer
 * Copyright (C) <2015> James Weaver <james.barrett@bbc.co.uk>
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

#ifndef __GST_RTP_VC2_PAY_H__
#define __GST_RTP_VC2_PAY_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/rtp/gstrtpbasepayload.h>

#include "vc2vlcparse.h"

G_BEGIN_DECLS

#define GST_TYPE_RTP_VC2_PAY \
  (gst_rtp_vc2_pay_get_type())
#define GST_RTP_VC2_PAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_VC2_PAY,GstRtpVC2Pay))
#define GST_RTP_VC2_PAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_VC2_PAY,GstRtpVC2PayClass))
#define GST_IS_RTP_VC2_PAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_VC2_PAY))
#define GST_IS_RTP_VC2_PAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_VC2_PAY))

typedef struct _GstRtpVC2Pay GstRtpVC2Pay;
typedef struct _GstRtpVC2PayClass GstRtpVC2PayClass;

typedef struct _GstRtpVC2PayParseInfo GstRtpVC2PayParseInfo;

enum _GstRtpVC2PayState {
  GSTRTPVC2PAYSTATE_UNSYNC = 0,
  GSTRTPVC2PAYSTATE_SYNC   = 1,
};

typedef enum _GstRtpVC2PayState GstRtpVC2PayState;

struct _GstRtpVC2Pay
{
  GstRTPBasePayload payload;

  GstAdapter *adapter;
  gssize storedsize;

  GstRtpVC2PayState state;

  vc2_sequence_header *seq_hdr;
  guint16 next_ext_seq_num;
  GstClockTime dts;
  GstClockTime pts;
};

struct _GstRtpVC2PayClass
{
  GstRTPBasePayloadClass parent_class;
};

enum _GstRtpVC2PayParseCode {
  GSTRTPVC2PAYPARSECODE_SEQUENCE_HEADER = 0x00,
  GSTRTPVC2PAYPARSECODE_END_OF_SEQUENCE = 0x10,
  GSTRTPVC2PAYPARSECODE_AUXILIARY_DATA  = 0x20,
  GSTRTPVC2PAYPARSECODE_PADDING_DATA    = 0x30,
  GSTRTPVC2PAYPARSECODE_HQ_PICTURE      = 0xE8,
};

struct _GstRtpVC2PayParseInfo {
  unsigned char parse_code;
  unsigned int next_parse_offset;
  unsigned int prev_parse_offset;
};

GType gst_rtp_vc2_pay_get_type (void);

gboolean gst_rtp_vc2_pay_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_RTP_VC2_PAY_H__ */
