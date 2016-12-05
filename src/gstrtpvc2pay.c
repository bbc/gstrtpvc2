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

#include <string.h>
#include <stdlib.h>

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/pbutils/pbutils.h>


#include "gstrtpvc2pay.h"

GST_DEBUG_CATEGORY_STATIC (rtpvc2pay_debug);
#define GST_CAT_DEFAULT (rtpvc2pay_debug)

/* references:
 *
 * https://tools.ietf.org/html/draft-ietf-payload-rtp-vc2hq-01
 */

static GstStaticPadTemplate gst_rtp_vc2_pay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vc2;"
                     "video/x-dirac")
    );

static GstStaticPadTemplate gst_rtp_vc2_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"VC2\"")
    );

static void gst_rtp_vc2_pay_finalize (GObject * object);

static void gst_rtp_vc2_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_vc2_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_rtp_vc2_pay_getcaps (GstRTPBasePayload * payload,
    GstPad * pad, GstCaps * filter);
static gboolean gst_rtp_vc2_pay_setcaps (GstRTPBasePayload * basepayload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_vc2_pay_handle_buffer (GstRTPBasePayload * pad,
    GstBuffer * buffer);
static gboolean gst_rtp_vc2_pay_sink_event (GstRTPBasePayload * payload,
    GstEvent * event);
static GstStateChangeReturn gst_rtp_vc2_pay_change_state (GstElement *
    element, GstStateChange transition);

#define gst_rtp_vc2_pay_parent_class parent_class
G_DEFINE_TYPE (GstRtpVC2Pay, gst_rtp_vc2_pay, GST_TYPE_RTP_BASE_PAYLOAD);

static void
gst_rtp_vc2_pay_class_init (GstRtpVC2PayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPBasePayloadClass *gstrtpbasepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstrtpbasepayload_class = (GstRTPBasePayloadClass *) klass;

  gobject_class->set_property = gst_rtp_vc2_pay_set_property;
  gobject_class->get_property = gst_rtp_vc2_pay_get_property;

  gobject_class->finalize = gst_rtp_vc2_pay_finalize;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_vc2_pay_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_vc2_pay_sink_template));

  gst_element_class_set_static_metadata (gstelement_class, "RTP VC2 payloader",
      "Codec/Payloader/Network/RTP",
      "Payload-encode VC2 video into RTP packets (https://tools.ietf.org/html/draft-ietf-payload-rtp-vc2hq-01)",
      "James Weaver <james.barrett@bbc.co.uk>");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_vc2_pay_change_state);

  gstrtpbasepayload_class->get_caps = gst_rtp_vc2_pay_getcaps;
  gstrtpbasepayload_class->set_caps = gst_rtp_vc2_pay_setcaps;
  gstrtpbasepayload_class->handle_buffer = gst_rtp_vc2_pay_handle_buffer;
  gstrtpbasepayload_class->sink_event = gst_rtp_vc2_pay_sink_event;

  GST_DEBUG_CATEGORY_INIT (rtpvc2pay_debug, "rtpvc2pay", 0,
      "VC2 RTP Payloader");
}

static void
gst_rtp_vc2_pay_init (GstRtpVC2Pay * rtpvc2pay)
{
  rtpvc2pay->adapter = gst_adapter_new ();
  rtpvc2pay->storedsize = 0;

  rtpvc2pay->state = GSTRTPVC2PAYSTATE_UNSYNC;

  rtpvc2pay->seq_hdr = NULL;
  rtpvc2pay->next_ext_seq_num = 0;
}

static void
gst_rtp_vc2_pay_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_rtp_vc2_pay_getcaps (GstRTPBasePayload * payload, GstPad * pad,
    GstCaps * filter)
{
  GstCaps *template_caps;
  GstCaps *allowed_caps;
  GstCaps *caps, *icaps;
  gboolean append_unrestricted;
  guint i;

  allowed_caps =
      gst_pad_peer_query_caps (GST_RTP_BASE_PAYLOAD_SRCPAD (payload), NULL);

  if (allowed_caps == NULL)
    return NULL;

  template_caps =
      gst_static_pad_template_get_caps (&gst_rtp_vc2_pay_sink_template);

  if (gst_caps_is_any (allowed_caps)) {
    caps = gst_caps_ref (template_caps);
    goto done;
  }

  if (gst_caps_is_empty (allowed_caps)) {
    caps = gst_caps_ref (allowed_caps);
    goto done;
  }

  caps = gst_caps_new_empty ();

  append_unrestricted = FALSE;
  for (i = 0; i < gst_caps_get_size (allowed_caps); i++) {
    GstStructure *new_s = gst_structure_new_empty ("video/x-dirac");

    caps = gst_caps_merge_structure (caps, new_s);
  }

  if (append_unrestricted) {
    caps =
        gst_caps_merge_structure (caps, gst_structure_new ("video/x-dirac", NULL,
            NULL));
  }

  icaps = gst_caps_intersect (caps, template_caps);
  gst_caps_unref (caps);
  caps = icaps;

done:

  gst_caps_unref (template_caps);
  gst_caps_unref (allowed_caps);

  GST_LOG_OBJECT (payload, "returning caps %" GST_PTR_FORMAT, caps);
  return caps;
}

static gboolean
gst_rtp_vc2_pay_setcaps (GstRTPBasePayload * basepayload, GstCaps * caps)
{
  gst_rtp_base_payload_set_options (basepayload, "video", TRUE, "VC2", 90000);
  gst_rtp_base_payload_set_outcaps (basepayload, NULL);

  return TRUE;
}

static gboolean gst_rtp_vc2_pay_extract_parse_info(GstAdapter *adapter, int adaptersize, int offset, GstRtpVC2PayParseInfo *info) {
  unsigned char buf[13];
  GstRtpVC2PayParseInfo first, next;

  if (offset + 13 > adaptersize)
    return FALSE;
  
  gst_adapter_copy(adapter, buf, offset, 13);
  if (buf[0] != 0x42 || buf[1] != 0x42 || buf[2] != 0x43 || buf[3] != 0x44)
    return FALSE;
  first.parse_code        = ((unsigned char)buf[ 4]);
  first.next_parse_offset = (((unsigned int)buf[ 5] << 24) |
                             ((unsigned int)buf[ 6] << 16) |
                             ((unsigned int)buf[ 7] <<  8) |
                             ((unsigned int)buf[ 8] <<  0));
  first.prev_parse_offset = (((unsigned int)buf[ 9] << 24) |
                             ((unsigned int)buf[10] << 16) |
                             ((unsigned int)buf[11] <<  8) |
                             ((unsigned int)buf[12] <<  0));

  if (first.parse_code == GSTRTPVC2PAYPARSECODE_END_OF_SEQUENCE) {
    *info = first;
    return TRUE;
  }

  gssize nextoffset = offset + first.next_parse_offset;
  if (first.next_parse_offset == 0)
    nextoffset = gst_adapter_masked_scan_uint32(adapter, 0xFFFFFFFF, 0x42424344, offset + 13, adaptersize - offset - 13);

  while (TRUE) {
    if (nextoffset < 0) {
      return FALSE;
    }

    if (nextoffset + 13 > adaptersize) {
      return FALSE;
    }

    gst_adapter_copy(adapter, buf, nextoffset, 13);
    if (buf[0] != 0x42 || buf[1] != 0x42 || buf[2] != 0x43 || buf[3] != 0x44)
      return FALSE;
    next.parse_code        = ((unsigned char)buf[ 4]);
    next.next_parse_offset = (((unsigned int)buf[ 5] << 24) |
                              ((unsigned int)buf[ 6] << 16) |
                              ((unsigned int)buf[ 7] <<  8) |
                              ((unsigned int)buf[ 8] <<  0));
    next.prev_parse_offset = (((unsigned int)buf[ 9] << 24) |
                              ((unsigned int)buf[10] << 16) |
                              ((unsigned int)buf[11] <<  8) |
                              ((unsigned int)buf[12] <<  0));

    if (nextoffset - offset == next.prev_parse_offset) {
      first.next_parse_offset = nextoffset - offset;
      *info = first;
      return TRUE;
    }

    if (first.next_parse_offset != 0)
      return FALSE;

    nextoffset = gst_adapter_masked_scan_uint32(adapter, 0xFFFFFFFF, 0x42424344, nextoffset + 4, adaptersize - nextoffset - 4);
  }
  return FALSE;
}

static gssize gst_rtp_vc2_pay_find_parse_info(GstAdapter *adapter, int adaptersize, gssize offset) {
  gssize parse_info_offset = offset;
  while(TRUE) {
    GstRtpVC2PayParseInfo info;
    parse_info_offset = gst_adapter_masked_scan_uint32(adapter, 0xFFFFFFFF, 0x42424344, parse_info_offset, adaptersize - parse_info_offset);
    if (parse_info_offset < 0)
      break;

    if (gst_rtp_vc2_pay_extract_parse_info(adapter, adaptersize, parse_info_offset, &info))
      break;
  }
  return parse_info_offset;
}

static GstFlowReturn
gst_rtp_vc2_pay_payload_seqhdr(GstRTPBasePayload * basepayload, GstClockTime dts, GstClockTime pts);

static GstFlowReturn
gst_rtp_vc2_pay_payload_hqpicture(GstRTPBasePayload * basepayload, GstBuffer *buffer);

static GstFlowReturn
gst_rtp_vc2_pay_payload_eos(GstRTPBasePayload * basepayload, GstClockTime dts, GstClockTime pts);

static GstFlowReturn
gst_rtp_vc2_pay_handle_buffer (GstRTPBasePayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpVC2Pay *rtpvc2pay;
  GstFlowReturn ret;

  rtpvc2pay = GST_RTP_VC2_PAY (basepayload);

  if (buffer) {
    gst_adapter_push(rtpvc2pay->adapter, buffer);
    rtpvc2pay->storedsize += gst_buffer_get_size (buffer);
  }

  /* Data has been pushed into adapter, now process the data in the adapter */
  ret = GST_FLOW_OK;
  while (ret == GST_FLOW_OK && rtpvc2pay->storedsize >= 13) {
    if (rtpvc2pay->state == GSTRTPVC2PAYSTATE_UNSYNC) {
      /* We are not synchronised with a sequence */
      gssize parse_info_offset = gst_rtp_vc2_pay_find_parse_info(rtpvc2pay->adapter, rtpvc2pay->storedsize, 0);
      if (parse_info_offset < 0)
        break;

      gst_adapter_flush(rtpvc2pay->adapter, parse_info_offset);
      rtpvc2pay->storedsize -= parse_info_offset;
      rtpvc2pay->state = GSTRTPVC2PAYSTATE_SYNC;
    }

    GstRtpVC2PayParseInfo info;
    if (!gst_rtp_vc2_pay_extract_parse_info(rtpvc2pay->adapter, rtpvc2pay->storedsize, 0, &info))
      break;

    if (info.parse_code == GSTRTPVC2PAYPARSECODE_END_OF_SEQUENCE) {
    }

    switch(info.parse_code) {
    case GSTRTPVC2PAYPARSECODE_SEQUENCE_HEADER:
      {
        gst_adapter_flush(rtpvc2pay->adapter, 13);
        GstBuffer *outbuf = gst_adapter_take_buffer_fast (rtpvc2pay->adapter, info.next_parse_offset - 13);
        rtpvc2pay->storedsize -= info.next_parse_offset;

        rtpvc2pay->pts = GST_BUFFER_PTS (outbuf);
        rtpvc2pay->dts = GST_BUFFER_DTS (outbuf);

        if (!vc2_sequence_header_cmp(rtpvc2pay->seq_hdr, outbuf, info.next_parse_offset - 13)) {
          vc2_sequence_header_free(rtpvc2pay->seq_hdr);
          rtpvc2pay->seq_hdr = vc2_sequence_header_new(outbuf);
        }
        gst_buffer_unref(outbuf);

        ret = gst_rtp_vc2_pay_payload_seqhdr(basepayload, rtpvc2pay->dts, rtpvc2pay->pts);
      }
      break;
    case GSTRTPVC2PAYPARSECODE_AUXILIARY_DATA:
    case GSTRTPVC2PAYPARSECODE_PADDING_DATA:
      {
        gst_adapter_flush(rtpvc2pay->adapter, info.next_parse_offset);
        rtpvc2pay->storedsize -= info.next_parse_offset;
      }
      break;
    case GSTRTPVC2PAYPARSECODE_HQ_PICTURE:
      {
        gst_adapter_flush(rtpvc2pay->adapter, 13);
        GstBuffer *outbuf = gst_adapter_take_buffer_fast (rtpvc2pay->adapter, info.next_parse_offset - 13);
        rtpvc2pay->storedsize -= info.next_parse_offset;

        rtpvc2pay->pts = GST_BUFFER_PTS (outbuf);
        rtpvc2pay->dts = GST_BUFFER_DTS (outbuf);

        ret = gst_rtp_vc2_pay_payload_hqpicture(basepayload, outbuf);
      }
      break;
    case GSTRTPVC2PAYPARSECODE_END_OF_SEQUENCE:
      {
        gst_adapter_flush(rtpvc2pay->adapter, 13);
        rtpvc2pay->storedsize -= 13;
        rtpvc2pay->state = GSTRTPVC2PAYSTATE_UNSYNC;

        ret = gst_rtp_vc2_pay_payload_eos(basepayload, rtpvc2pay->dts, rtpvc2pay->pts);
      }
      break;
    default:
      {
        gst_adapter_flush(rtpvc2pay->adapter, info.next_parse_offset);
        rtpvc2pay->storedsize -= info.next_parse_offset;
      }
      break;
    }

  }

  if (!buffer) {
    return GST_FLOW_EOS;
  }

  return ret;
}

static GstBuffer *
gst_rtp_vc2_buffer_new_allocate (GstRTPBasePayload *payload,
                                 guint8 parse_code,
                                 gboolean interlace,
                                 gboolean second_field) {
  GstMapInfo info;
  GstBuffer *outbuf = NULL;
  GstBuffer *hdrbuf = NULL;

  outbuf = gst_rtp_buffer_new_allocate (0, 0, 0);
  if (outbuf == NULL)
    return NULL;

  hdrbuf = gst_buffer_new_allocate(NULL, 4, NULL);
  if (hdrbuf == NULL)
    return NULL;

  if (!gst_buffer_map(hdrbuf, &info, GST_MAP_WRITE))
    return NULL;

  info.data[0] = 0x00;
  info.data[1] = 0x00;
  info.data[2] = (interlace)?((second_field)?(0x03):(0x02)):(0x00);
  info.data[3] = parse_code;

  gst_buffer_unmap(hdrbuf, &info);

  outbuf = gst_buffer_append (outbuf, hdrbuf);

  return outbuf;
}

static GstBuffer *
gst_rtp_vc2_hq_picture_buffer_new_with_data(GstRTPBasePayload *payload,
                                            guint32 picture_number,
                                            guint16 slice_prefix_bytes,
                                            guint16 slice_size_scaler,
                                            guint8 *data,
                                            gsize size,
                                            gint n_slices,
                                            gint slice_x,
                                            gint slice_y) {
  GstBuffer *outbuf;
  GstBuffer *hdrbuf;
  GstRtpVC2Pay *rtpvc2pay;
  gboolean interlace, second_field;
  gint hdr_length;
  GstMapInfo info;

  rtpvc2pay = GST_RTP_VC2_PAY (payload);

  interlace = rtpvc2pay->seq_hdr->interlaced;
  second_field = (interlace && (picture_number&0x1));

  hdr_length = (n_slices == 0)?(12):(16);

  outbuf = gst_rtp_vc2_buffer_new_allocate(payload, 0xEC, interlace, second_field);
  hdrbuf = gst_buffer_new_allocate(NULL, hdr_length + size, NULL);

  if (!gst_buffer_map(hdrbuf, &info, GST_MAP_WRITE))
    return NULL;

  info.data[ 0] = (picture_number >> 24)&0xFF;
  info.data[ 1] = (picture_number >> 16)&0xFF;
  info.data[ 2] = (picture_number >>  8)&0xFF;
  info.data[ 3] = (picture_number >>  0)&0xFF;
  info.data[ 4] = (slice_prefix_bytes >> 16)&0xFF;
  info.data[ 5] = (slice_prefix_bytes >>  0)&0xFF;
  info.data[ 6] = (slice_size_scaler >> 16)&0xFF;
  info.data[ 7] = (slice_size_scaler >>  0)&0xFF;
  info.data[ 8] = (size >>  8)&0xFF;
  info.data[ 9] = (size >>  0)&0xFF;
  info.data[10] = (n_slices >>  8)&0xFF;
  info.data[11] = (n_slices >>  0)&0xFF;

  if (n_slices) {
    info.data[12] = (slice_x >>  8)&0xFF;
    info.data[13] = (slice_x >>  0)&0xFF;
    info.data[14] = (slice_y >>  8)&0xFF;
    info.data[15] = (slice_y >>  0)&0xFF;
  }

  memcpy(info.data + hdr_length, data, size);

  gst_buffer_unmap(hdrbuf, &info);

  outbuf = gst_buffer_append (outbuf, hdrbuf);

  return outbuf;
}

/* This really shouldn't be here ... */
struct _GstRTPBasePayloadPrivate
{
  gboolean ts_offset_random;
  gboolean seqnum_offset_random;
  gboolean ssrc_random;
  guint16 next_seqnum;
  gboolean perfect_rtptime;
  gint notified_first_timestamp;

  guint64 base_offset;
  gint64 base_rtime;

  gint64 prop_max_ptime;
  gint64 caps_max_ptime;

  gboolean negotiated;

  gboolean delay_segment;
  GstEvent *pending_segment;
};

static GstFlowReturn
gst_rtp_vc2_payload_push (GstRTPBasePayload *payload,
                          GstBuffer *buffer) {
  GstRtpVC2Pay *rtpvc2pay;
  GstFlowReturn res;
  GstRTPBuffer rtp;
  guint8 *pld;

  memset(&rtp, 0, sizeof(GstRTPBuffer));
  rtpvc2pay = GST_RTP_VC2_PAY (payload);

  
  if (!gst_rtp_buffer_map (buffer,
                           GST_MAP_WRITE,
                           &rtp))
    return GST_FLOW_ERROR;
  
  pld = gst_rtp_buffer_get_payload(&rtp);
  pld[0] = (rtpvc2pay->next_ext_seq_num >> 8)&0xFF;
  pld[1] = (rtpvc2pay->next_ext_seq_num >> 0)&0xFF;
  gst_rtp_buffer_unmap(&rtp);

  res = gst_rtp_base_payload_push(payload, buffer);
  if (payload->priv->next_seqnum == 0) {
    rtpvc2pay->next_ext_seq_num++;
  }
  return res;
}

static GstFlowReturn
gst_rtp_vc2_pay_payload_seqhdr(GstRTPBasePayload * basepayload, GstClockTime dts, GstClockTime pts) {
  GstRtpVC2Pay *rtpvc2pay;
  GstBuffer *outbuf;

  rtpvc2pay = GST_RTP_VC2_PAY (basepayload);

  outbuf = gst_rtp_vc2_buffer_new_allocate (basepayload, 0x00, FALSE, FALSE);
  if (outbuf == NULL)
    return GST_FLOW_ERROR;

  GST_BUFFER_PTS (outbuf) = pts;
  GST_BUFFER_DTS (outbuf) = dts;

  outbuf = gst_buffer_append (outbuf, gst_buffer_ref(rtpvc2pay->seq_hdr->buf));

  return gst_rtp_vc2_payload_push(basepayload, outbuf);
}

static GstFlowReturn
gst_rtp_vc2_pay_payload_eos(GstRTPBasePayload * basepayload, GstClockTime dts, GstClockTime pts) {
  GstBuffer *outbuf;

  outbuf = gst_rtp_vc2_buffer_new_allocate (basepayload, 0x10, FALSE, FALSE);
  if (outbuf == NULL)
    return GST_FLOW_ERROR;

  GST_BUFFER_PTS (outbuf) = pts;
  GST_BUFFER_DTS (outbuf) = dts;

  return gst_rtp_vc2_payload_push(basepayload, outbuf);
}

static GstFlowReturn
gst_rtp_vc2_pay_payload_hqpicture(GstRTPBasePayload * basepayload, GstBuffer *buffer) {
  GstClockTime dts, pts;
  guint32 picture_number;
  GstMapInfo info;
  vc2_hq_transform_parameters* params;
  gssize size;
  gint offset;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  gint offset_x, offset_y, slice_x, slice_y, offs;
  gint n_slices;
  uint mtu;
  gint slice_length;
  GstRTPBuffer rtp;

  mtu = basepayload->mtu;

  pts = GST_BUFFER_PTS (buffer);
  dts = GST_BUFFER_DTS (buffer);
  size = gst_buffer_get_size(buffer);

  if (size < 5)
    return GST_FLOW_ERROR;

  if (!gst_buffer_map(buffer, &info, GST_MAP_READ))
    return GST_FLOW_ERROR;

  picture_number = ((info.data[0] << 24) |
                    (info.data[1] << 16) |
                    (info.data[2] <<  8) |
                    (info.data[3] <<  0));


  params = vc2_hq_transform_parameters_new (info.data + 4, size - 4);
  if (!params) {
    gst_buffer_unmap(buffer, &info);
    gst_buffer_unref(buffer);
    return GST_FLOW_ERROR;
  }

  offset = 4;
  outbuf = gst_rtp_vc2_hq_picture_buffer_new_with_data(basepayload, picture_number, params->slice_prefix_bytes, params->slice_size_scalar,
                                                       info.data + offset, params->coded_size, 0, 0, 0);
  GST_BUFFER_PTS (outbuf) = pts;
  GST_BUFFER_DTS (outbuf) = dts;
  ret = gst_rtp_vc2_payload_push(basepayload, outbuf);
  offset += params->coded_size;

  slice_x  = 0;
  slice_y  = 0;
  offset_x = 0;
  offset_y = 0;
  n_slices = 0;
  offs = 0;

  while (ret == GST_FLOW_OK && slice_y < params->slices_y) {
    slice_length  = info.data[offset + offs + params->slice_prefix_bytes + 1]*params->slice_size_scalar + 2 + params->slice_prefix_bytes;
    slice_length += info.data[offset + offs + slice_length ]*params->slice_size_scalar + 1;
    slice_length += info.data[offset + offs + slice_length ]*params->slice_size_scalar + 1;

    if (n_slices > 0 && offs + slice_length + 16 > mtu) {

      outbuf = gst_rtp_vc2_hq_picture_buffer_new_with_data(basepayload, picture_number, params->slice_prefix_bytes, params->slice_size_scalar,
                                                           info.data + offset, offs, n_slices, offset_x, offset_y);
      GST_BUFFER_PTS (outbuf) = pts;
      GST_BUFFER_DTS (outbuf) = dts;
      ret = gst_rtp_vc2_payload_push(basepayload, outbuf);

      offset += offs;
      offs = 0;
      n_slices = 0;
      offset_x = slice_x;
      offset_y = slice_y;
    }

    offs += slice_length;
    n_slices++;
    slice_x++;
    if (slice_x >= params->slices_x) {
      slice_x = 0;
      slice_y++;
    }
  }

  if (ret == GST_FLOW_OK && offs > 0) {
    outbuf = gst_rtp_vc2_hq_picture_buffer_new_with_data(basepayload, picture_number,
                                                         params->slice_prefix_bytes, params->slice_size_scalar,
                                                         info.data + offset, offs, n_slices, offset_x, offset_y);
    GST_BUFFER_PTS (outbuf) = pts;
    GST_BUFFER_DTS (outbuf) = dts;
    memset(&rtp, 0, sizeof(rtp));
    gst_rtp_buffer_map(outbuf, GST_MAP_WRITE, &rtp);
    gst_rtp_buffer_set_marker(&rtp, TRUE);
    gst_rtp_buffer_unmap(&rtp);
    ret = gst_rtp_vc2_payload_push(basepayload, outbuf);
  }

  vc2_hq_transform_parameters_free(params);
  gst_buffer_unmap(buffer, &info);
  gst_buffer_unref(buffer);
  return ret;
}


static gboolean
gst_rtp_vc2_pay_sink_event (GstRTPBasePayload * payload, GstEvent * event)
{
  gboolean res;
  GstRtpVC2Pay *rtpvc2pay = GST_RTP_VC2_PAY (payload);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_adapter_clear (rtpvc2pay->adapter);
      rtpvc2pay->storedsize = 0;
      break;
    case GST_EVENT_EOS:
    {
      gst_rtp_vc2_pay_handle_buffer (payload, NULL);
      break;
    }
    default:
      break;
  }

  res = GST_RTP_BASE_PAYLOAD_CLASS (parent_class)->sink_event (payload, event);

  return res;
}

static GstStateChangeReturn
gst_rtp_vc2_pay_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRtpVC2Pay *rtpvc2pay = GST_RTP_VC2_PAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (rtpvc2pay->adapter);
      rtpvc2pay->storedsize = 0;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

static void
gst_rtp_vc2_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_vc2_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_rtp_vc2_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpvc2pay",
                               GST_RANK_SECONDARY, GST_TYPE_RTP_VC2_PAY);
}
