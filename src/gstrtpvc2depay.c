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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include <gst/base/gstbitreader.h>
#include <gst/rtp/gstrtpbuffer.h>
#include "gstrtpvc2depay.h"

GST_DEBUG_CATEGORY_STATIC (rtpvc2depay_debug);
#define GST_CAT_DEFAULT (rtpvc2depay_debug)

static GstStaticPadTemplate gst_rtp_vc2_depay_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-dirac")
    );

static GstStaticPadTemplate gst_rtp_vc2_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"VC2\"")
    );

#define gst_rtp_vc2_depay_parent_class parent_class
G_DEFINE_TYPE (GstRtpVC2Depay, gst_rtp_vc2_depay,
    GST_TYPE_RTP_BASE_DEPAYLOAD);

static void gst_rtp_vc2_depay_finalize (GObject * object);

static GstStateChangeReturn gst_rtp_vc2_depay_change_state (GstElement *
    element, GstStateChange transition);

static GstBuffer *gst_rtp_vc2_depay_process (GstRTPBaseDepayload * depayload,
    GstBuffer * buf);
static gboolean gst_rtp_vc2_depay_setcaps (GstRTPBaseDepayload * filter,
    GstCaps * caps);
static gboolean gst_rtp_vc2_depay_handle_event (GstRTPBaseDepayload * depay,
    GstEvent * event);

static void
gst_rtp_vc2_depay_class_init (GstRtpVC2DepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPBaseDepayloadClass *gstrtpbasedepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstrtpbasedepayload_class = (GstRTPBaseDepayloadClass *) klass;

  gobject_class->finalize = gst_rtp_vc2_depay_finalize;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_vc2_depay_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_vc2_depay_sink_template));

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP VC2 depayloader", "Codec/Depayloader/Network/RTP",
      "Extracts VC2 video from RTP packets (ID draft-ietf-payload-rtp-vc2hq-01)",
      "James Weaver <james.barrett@bbc.co.uk>");
  gstelement_class->change_state = gst_rtp_vc2_depay_change_state;

  gstrtpbasedepayload_class->process = gst_rtp_vc2_depay_process;
  gstrtpbasedepayload_class->set_caps = gst_rtp_vc2_depay_setcaps;
  gstrtpbasedepayload_class->handle_event = gst_rtp_vc2_depay_handle_event;
}

static void
gst_rtp_vc2_depay_init (GstRtpVC2Depay * rtpvc2depay)
{
  rtpvc2depay->adapter = gst_adapter_new ();

  rtpvc2depay->wait_start             = TRUE;
  rtpvc2depay->last_parse_info_offset = 0;
  rtpvc2depay->in_picture             = FALSE;
  rtpvc2depay->picture_number         = 0;
}

static void
gst_rtp_vc2_depay_reset (GstRtpVC2Depay * rtpvc2depay)
{
  gst_adapter_clear (rtpvc2depay->adapter);

  rtpvc2depay->wait_start             = TRUE;
  rtpvc2depay->last_parse_info_offset = 0;
  rtpvc2depay->in_picture             = FALSE;
  rtpvc2depay->picture_number         = 0;
}

static void
gst_rtp_vc2_depay_finalize (GObject * object)
{
  GstRtpVC2Depay *rtpvc2depay;

  rtpvc2depay = GST_RTP_VC2_DEPAY (object);

  g_object_unref (rtpvc2depay->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtp_vc2_set_src_caps (GstRtpVC2Depay * rtpvc2depay)
{
  gboolean res;
  GstCaps *srccaps;

  srccaps = gst_caps_new_empty_simple ("video/x-dirac");

  res = gst_pad_set_caps (GST_RTP_BASE_DEPAYLOAD_SRCPAD (rtpvc2depay),
                          srccaps);
  gst_caps_unref (srccaps);

  return res;
}


static gboolean
gst_rtp_vc2_depay_setcaps (GstRTPBaseDepayload * depayload, GstCaps * caps)
{
  gint clock_rate;
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstRtpVC2Depay *rtpvc2depay;

  rtpvc2depay = GST_RTP_VC2_DEPAY (depayload);

  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    clock_rate = 90000;
  depayload->clock_rate = clock_rate;

  return gst_rtp_vc2_set_src_caps (rtpvc2depay);
}

static GstBuffer *
gst_rtp_vc2_depay_process_sequence_header (GstRTPBaseDepayload * depayload, guint8 *payload, gint length);

static GstBuffer *
gst_rtp_vc2_depay_process_hq_fragment (GstRTPBaseDepayload * depayload, guint8 *payload, gint length, gboolean I, gboolean F, gboolean M);

static GstBuffer *
gst_rtp_vc2_depay_process_end_of_sequence (GstRTPBaseDepayload * depayload);

static GstBuffer *
gst_rtp_vc2_depay_process (GstRTPBaseDepayload * depayload, GstBuffer * buf)
{
  GstRtpVC2Depay *rtpvc2depay;
  GstBuffer *outbuf = NULL;
  GstRTPBuffer rtp = { NULL };

  rtpvc2depay = GST_RTP_VC2_DEPAY (depayload);

  /* flush remaining data on discont */
  if (GST_BUFFER_IS_DISCONT (buf)) {
    gst_adapter_clear (rtpvc2depay->adapter);
    rtpvc2depay->wait_start             = TRUE;
    rtpvc2depay->last_parse_info_offset = 0;
    rtpvc2depay->in_picture             = FALSE;
    rtpvc2depay->picture_number         = 0;
  }

  {
      guint8 *payload;
      guint8 PC;
      gint   payload_length;
      gboolean I;
      gboolean F;
      gboolean M;

      gst_rtp_buffer_map(buf, GST_MAP_READ, &rtp);

      payload = gst_rtp_buffer_get_payload(&rtp);
      payload_length = gst_rtp_buffer_get_packet_len(&rtp) - gst_rtp_buffer_get_header_len(&rtp);

      PC = payload[3];
      I  = ((payload[2] & 0x2) != 0);
      F  = ((payload[2] & 0x1) != 0);
      M  = gst_rtp_buffer_get_marker(&rtp);

      switch(PC) {
      case GSTRTPVC2DEPAYPARSECODE_SEQUENCE_HEADER:
        {
          outbuf = gst_rtp_vc2_depay_process_sequence_header(depayload, payload + 4, payload_length - 4);
        }
        break;
      case GSTRTPVC2DEPAYPARSECODE_END_OF_SEQUENCE:
        {
          outbuf = gst_rtp_vc2_depay_process_end_of_sequence(depayload);
        }
        break;
      case GSTRTPVC2DEPAYPARSECODE_HQ_FRAGMENT:
        {
          outbuf = gst_rtp_vc2_depay_process_hq_fragment (depayload, payload + 4, payload_length - 4, I, F, M);
        }
        break;
      default:
        break;
      }

      gst_rtp_buffer_unmap(&rtp);
  }

  return outbuf;
}

static GstBuffer *
gst_rtp_vc2_depay_process_sequence_header (GstRTPBaseDepayload * depayload, guint8 *payload, gint length) {
  GstBuffer *outbuf = NULL;
  GstRtpVC2Depay *rtpvc2depay;
  GstMapInfo info;
  guint32 next_parse_info_offset;

  next_parse_info_offset = length + 13;
  rtpvc2depay = GST_RTP_VC2_DEPAY (depayload);

  outbuf = gst_buffer_new_allocate(NULL, next_parse_info_offset, NULL);
  if (!outbuf)
    return NULL;

  if (!gst_buffer_map(outbuf,
                      &info,
                      GST_MAP_WRITE)) {
    gst_buffer_unref(outbuf);
    return NULL;
  }

  info.data[ 0] = 0x42;
  info.data[ 1] = 0x42;
  info.data[ 2] = 0x43;
  info.data[ 3] = 0x44;
  info.data[ 4] = 0x00; // Sequence Header
  info.data[ 5] = (next_parse_info_offset >> 24)&0xFF;
  info.data[ 6] = (next_parse_info_offset >> 16)&0xFF;
  info.data[ 7] = (next_parse_info_offset >>  8)&0xFF;
  info.data[ 8] = (next_parse_info_offset >>  0)&0xFF;
  info.data[ 9] = (rtpvc2depay->last_parse_info_offset >> 24)&0xFF;
  info.data[10] = (rtpvc2depay->last_parse_info_offset >> 16)&0xFF;
  info.data[11] = (rtpvc2depay->last_parse_info_offset >>  8)&0xFF;
  info.data[12] = (rtpvc2depay->last_parse_info_offset >>  0)&0xFF;

  memcpy(info.data + 13, payload, length);

  rtpvc2depay->last_parse_info_offset = next_parse_info_offset;

  gst_buffer_unmap(outbuf, &info);

  return outbuf;
}

static GstBuffer *
gst_rtp_vc2_depay_process_hq_fragment (GstRTPBaseDepayload * depayload, guint8 *payload, gint length, gboolean I, gboolean F, gboolean M) {
  GstBuffer *outbuf = NULL;
  GstBuffer *buf;
  GstMapInfo info;
  guint32 picture_number;
  gint fragment_length;
  gint no_slices;
  GstRtpVC2Depay *rtpvc2depay;

  rtpvc2depay = GST_RTP_VC2_DEPAY (depayload);

  if (length < 12)
    return NULL;

  picture_number = ((payload[0] << 24) |
                    (payload[1] << 16) |
                    (payload[2] <<  8) |
                    (payload[3] <<  0));
  fragment_length = ((payload[8] << 8) |
                     (payload[9] << 0));
  no_slices = ((payload[10] << 8) |
               (payload[11] << 0));

  if (fragment_length > length - 12)
    return NULL;

  if (no_slices == 0) {
    /* Picture Parameters */
    gst_adapter_clear (rtpvc2depay->adapter);
    rtpvc2depay->in_picture     = TRUE;
    rtpvc2depay->picture_number = picture_number;
    rtpvc2depay->picture_size   = 0;

    buf = gst_buffer_new_allocate(NULL,
                                  fragment_length,
                                  NULL);
    if (!buf) {
      rtpvc2depay->in_picture     = FALSE;
      return NULL;
    }

    if (!gst_buffer_map(buf,
                        &info,
                        GST_MAP_WRITE)) {
      gst_buffer_unref(buf);
      rtpvc2depay->in_picture     = FALSE;
      return NULL;
    }

    memcpy(info.data, payload + 12, fragment_length);

    gst_buffer_unmap(buf, &info);

    gst_adapter_push(rtpvc2depay->adapter, buf);
    rtpvc2depay->picture_size += fragment_length;

    return NULL;
  } else {
    if (!rtpvc2depay->in_picture || (rtpvc2depay->picture_number != picture_number) || fragment_length > length - 16) {
      gst_adapter_clear (rtpvc2depay->adapter);
      rtpvc2depay->in_picture     = FALSE;
      return NULL;
    }

    buf = gst_buffer_new_allocate(NULL,
                                  fragment_length,
                                  NULL);
    if (!buf) {
      rtpvc2depay->in_picture     = FALSE;
      return NULL;
    }

    if (!gst_buffer_map(buf,
                        &info,
                        GST_MAP_WRITE)) {
      gst_buffer_unref(buf);
      rtpvc2depay->in_picture     = FALSE;
      return NULL;
    }

    memcpy(info.data, payload + 16, fragment_length);

    gst_buffer_unmap(buf, &info);

    gst_adapter_push(rtpvc2depay->adapter, buf);
    rtpvc2depay->picture_size += fragment_length;
  }

  if (M) {
    guint32 next_parse_info_offset;

    outbuf = gst_buffer_new_allocate(NULL,
                                     17,
                                     NULL);
    if (!outbuf) {
      rtpvc2depay->in_picture     = FALSE;
      return NULL;
    }

    if (!gst_buffer_map(outbuf,
                        &info,
                        GST_MAP_WRITE)) {
      gst_buffer_unref(outbuf);
      rtpvc2depay->in_picture     = FALSE;
      return NULL;
    }

    next_parse_info_offset = rtpvc2depay->picture_size + 17;

    info.data[ 0] = 0x42;
    info.data[ 1] = 0x42;
    info.data[ 2] = 0x43;
    info.data[ 3] = 0x44;
    info.data[ 4] = 0xE8; // HQ Picture
    info.data[ 5] = (next_parse_info_offset >> 24)&0xFF;
    info.data[ 6] = (next_parse_info_offset >> 16)&0xFF;
    info.data[ 7] = (next_parse_info_offset >>  8)&0xFF;
    info.data[ 8] = (next_parse_info_offset >>  0)&0xFF;
    info.data[ 9] = (rtpvc2depay->last_parse_info_offset >> 24)&0xFF;
    info.data[10] = (rtpvc2depay->last_parse_info_offset >> 16)&0xFF;
    info.data[11] = (rtpvc2depay->last_parse_info_offset >>  8)&0xFF;
    info.data[12] = (rtpvc2depay->last_parse_info_offset >>  0)&0xFF;
    info.data[13] = (rtpvc2depay->picture_number >> 24)&0xFF;
    info.data[14] = (rtpvc2depay->picture_number >> 16)&0xFF;
    info.data[15] = (rtpvc2depay->picture_number >>  8)&0xFF;
    info.data[16] = (rtpvc2depay->picture_number >>  0)&0xFF;

    gst_buffer_unmap(outbuf, &info);

    buf = gst_adapter_take_buffer(rtpvc2depay->adapter, rtpvc2depay->picture_size);
    rtpvc2depay->picture_size = 0;
    rtpvc2depay->in_picture   = FALSE;
    rtpvc2depay->last_parse_info_offset = next_parse_info_offset;

    outbuf = gst_buffer_append(outbuf, buf);
  }

  return outbuf;
}

static GstBuffer *
gst_rtp_vc2_depay_process_end_of_sequence (GstRTPBaseDepayload * depayload) {
  GstBuffer *outbuf = NULL;
  GstRtpVC2Depay *rtpvc2depay;
  GstMapInfo info;
  guint32 next_parse_info_offset;

  next_parse_info_offset = 0;
  rtpvc2depay = GST_RTP_VC2_DEPAY (depayload);

  outbuf = gst_buffer_new_allocate(NULL, 13, NULL);
  if (!outbuf)
    return NULL;

  if (!gst_buffer_map(outbuf,
                      &info,
                      GST_MAP_WRITE)) {
    gst_buffer_unref(outbuf);
    return NULL;
  }

  info.data[ 0] = 0x42;
  info.data[ 1] = 0x42;
  info.data[ 2] = 0x43;
  info.data[ 3] = 0x44;
  info.data[ 4] = 0x10; // End of Sequence
  info.data[ 5] = (next_parse_info_offset >> 24)&0xFF;
  info.data[ 6] = (next_parse_info_offset >> 16)&0xFF;
  info.data[ 7] = (next_parse_info_offset >>  8)&0xFF;
  info.data[ 8] = (next_parse_info_offset >>  0)&0xFF;
  info.data[ 9] = (rtpvc2depay->last_parse_info_offset >> 24)&0xFF;
  info.data[10] = (rtpvc2depay->last_parse_info_offset >> 16)&0xFF;
  info.data[11] = (rtpvc2depay->last_parse_info_offset >>  8)&0xFF;
  info.data[12] = (rtpvc2depay->last_parse_info_offset >>  0)&0xFF;

  rtpvc2depay->last_parse_info_offset = next_parse_info_offset;

  gst_buffer_unmap(outbuf, &info);

  return outbuf;
}

static gboolean
gst_rtp_vc2_depay_handle_event (GstRTPBaseDepayload * depay, GstEvent * event)
{
  GstRtpVC2Depay *rtpvc2depay;

  rtpvc2depay = GST_RTP_VC2_DEPAY (depay);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_rtp_vc2_depay_reset (rtpvc2depay);
      break;
    default:
      break;
  }

  return
      GST_RTP_BASE_DEPAYLOAD_CLASS (parent_class)->handle_event (depay, event);
}

static GstStateChangeReturn
gst_rtp_vc2_depay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpVC2Depay *rtpvc2depay;
  GstStateChangeReturn ret;

  rtpvc2depay = GST_RTP_VC2_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_rtp_vc2_depay_reset (rtpvc2depay);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtp_vc2_depay_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (rtpvc2depay_debug, "rtpvc2depay", 0,
      "VC2 Video RTP Depayloader");

  return gst_element_register (plugin, "rtpvc2depay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_VC2_DEPAY);
}
