/* KnotInspector OpenCV Remap aggregator
 * Copyright (C) 2021 Vladislav Bortnikov <bortnikov@rerotor.ru>
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

#ifndef __GST_REMAP_H__
#define __GST_REMAP_H__

#include <gst/gst.h>
#include <gst/video/gstvideoaggregator.h>
#include <gst/video/video.h>
#include <opencv2/core.hpp>

G_BEGIN_DECLS

#define GST_TYPE_REMAP (gst_remap_get_type())
G_DECLARE_FINAL_TYPE(GstRemap, gst_remap, GST, REMAP, GstVideoAggregator)

#define GST_TYPE_REMAP_PAD (gst_remap_pad_get_type())
G_DECLARE_FINAL_TYPE(
    GstRemapPad, gst_remap_pad, GST, REMAP_PAD, GstVideoAggregatorConvertPad)
/**
 * GstRemap:
 *
 * The opaque #GstRemap structure.
 */
struct _GstRemap {
    GstVideoAggregator videoaggregator;
};

/**
 * GstRemapPad:
 *
 * The opaque #GstRemapPad structure.
 */
struct _GstRemapPad {
    GstVideoAggregatorConvertPad parent;

    /* properties */
    gint xpos, ypos;
    gint width, height;
    const gchar* maps;

    /* maps */
    cv::UMat _mapx, _mapy;
};

G_END_DECLS
#endif /* __GST_REMAP_H__ */
