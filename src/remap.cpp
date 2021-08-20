/* Remap VideoAggregator plugin
 * Copyright (C) 2021 Vladislav Bortnikov <bortnikov@rerotor.ru>
 *
 * Based on the source code of gst-base/compositor plugin,
 * OpenCV's gstreamer backend and others.
 *
 * Original compositor copyrights:
 * Copyright (C) 2004, 2008 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) 2014 Mathieu Duponchelle <mathieu.duponchelle@opencreed.com>
 * Copyright (C) 2014 Thibault Saunier <tsaunier@gnome.org>
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

/**
 * SECTION:element-remap
 * @title: remap
 *
 * Remap can accepts and produces BGRA streams. For each of the
 * requested sink pads it will compare the incoming geometry (based on maps)
 * and framerate to define the output parameters. Indeed output video frames
 * will have the geometry of the biggest sink map and the framerate of the
 * fastest incoming one.
 *
 * Individual parameters for each input stream can be configured on the
 * #GstRemapPad:
 *
 * * "xpos": The x-coordinate position of the top-left corner of the picture
 * (#gint)
 * * "ypos": The y-coordinate position of the top-left corner of the picture
 * (#gint)
 * * "width": The width of the picture; READONLY
 * (#gint)
 * * "height": The height of the picture; READONLY
 * (#gint)
 * * "maps": The filepath to *.yml file, containing both maps for cv::remap
 * (#gstring)
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "remap.h"

#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

GST_DEBUG_CATEGORY_STATIC(gst_remap_debug);
#define GST_CAT_DEFAULT gst_remap_debug

#define FORMATS " {  BGRA } "

static GstStaticPadTemplate src_factory
    = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
        GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE(FORMATS)));

static GstStaticPadTemplate sink_factory
    = GST_STATIC_PAD_TEMPLATE("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST,
        GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE(GST_VIDEO_FORMATS_ALL)));

static void gst_remap_child_proxy_init(gpointer g_iface, gpointer iface_data);

#define DEFAULT_PAD_XPOS 0
#define DEFAULT_PAD_YPOS 0
#define DEFAULT_PAD_WIDTH 0
#define DEFAULT_PAD_HEIGHT 0
enum {
    PROP_PAD_0,
    PROP_PAD_XPOS,
    PROP_PAD_YPOS,
    PROP_PAD_WIDTH,
    PROP_PAD_HEIGHT,
    PROP_PAD_MAPS
};

G_DEFINE_TYPE(
    GstRemapPad, gst_remap_pad, GST_TYPE_VIDEO_AGGREGATOR_CONVERT_PAD);

static void gst_remap_pad_get_property(
    GObject* object, guint prop_id, GValue* value, GParamSpec* pspec)
{
    GstRemapPad* pad = GST_REMAP_PAD(object);

    switch (prop_id) {
    case PROP_PAD_XPOS:
        g_value_set_int(value, pad->xpos);
        break;
    case PROP_PAD_YPOS:
        g_value_set_int(value, pad->ypos);
        break;
    case PROP_PAD_WIDTH:
        g_value_set_int(value, pad->width);
        break;
    case PROP_PAD_HEIGHT:
        g_value_set_int(value, pad->height);
        break;
    case PROP_PAD_MAPS:
        g_value_set_string(value, pad->maps);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_remap_pad_set_property(
    GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec)
{
    GstRemapPad* pad = GST_REMAP_PAD(object);

    gboolean map_changed = false;
    switch (prop_id) {
    case PROP_PAD_XPOS:
        pad->xpos = g_value_get_int(value);
        break;
    case PROP_PAD_YPOS:
        pad->ypos = g_value_get_int(value);
        break;
    case PROP_PAD_WIDTH:
        // readonly
        break;
    case PROP_PAD_HEIGHT:
        // readonly
        break;
    case PROP_PAD_MAPS:
        pad->maps = g_value_get_string(value);
        map_changed = true;
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }

    if (map_changed) {
        // TODO: Check for errors
        cv::FileStorage fs(pad->maps, cv::FileStorage::READ);
        fs["x"] >> pad->_mapx;
        fs["y"] >> pad->_mapy;
        fs.release();

        pad->width = pad->_mapx.cols;
        pad->height = pad->_mapx.rows;
        gst_video_aggregator_convert_pad_update_conversion_info(
            GST_VIDEO_AGGREGATOR_CONVERT_PAD(pad));
    }
}

static void _mixer_pad_get_output_size(GstRemapPad* comp_pad, gint out_par_n,
    gint out_par_d, gint* width, gint* height)
{
    GstVideoAggregatorPad* vagg_pad = GST_VIDEO_AGGREGATOR_PAD(comp_pad);
    gint pad_width, pad_height;
    guint dar_n, dar_d;

    /* FIXME: Anything better we can do here? */
    if (!vagg_pad->info.finfo
        || vagg_pad->info.finfo->format == GST_VIDEO_FORMAT_UNKNOWN) {
        GST_DEBUG_OBJECT(comp_pad, "Have no caps yet");
        *width = 0;
        *height = 0;
        return;
    }

    pad_width = comp_pad->width <= 0 ? GST_VIDEO_INFO_WIDTH(&vagg_pad->info)
                                     : comp_pad->width;
    pad_height = comp_pad->height <= 0 ? GST_VIDEO_INFO_HEIGHT(&vagg_pad->info)
                                       : comp_pad->height;

    if (!gst_video_calculate_display_ratio(&dar_n, &dar_d, pad_width,
            pad_height, GST_VIDEO_INFO_PAR_N(&vagg_pad->info),
            GST_VIDEO_INFO_PAR_D(&vagg_pad->info), out_par_n, out_par_d)) {
        GST_WARNING_OBJECT(comp_pad, "Cannot calculate display aspect ratio");
        *width = *height = 0;
        return;
    }
    GST_LOG_OBJECT(comp_pad, "scaling %ux%u by %u/%u (%u/%u / %u/%u)",
        pad_width, pad_height, dar_n, dar_d,
        GST_VIDEO_INFO_PAR_N(&vagg_pad->info),
        GST_VIDEO_INFO_PAR_D(&vagg_pad->info), out_par_n, out_par_d);

    /* Pick either height or width, whichever is an integer multiple of the
     * display aspect ratio. However, prefer preserving the height to account
     * for interlaced video. */
    if (pad_height % dar_n == 0) {
        pad_width = gst_util_uint64_scale_int(pad_height, dar_n, dar_d);
    } else if (pad_width % dar_d == 0) {
        pad_height = gst_util_uint64_scale_int(pad_width, dar_d, dar_n);
    } else {
        pad_width = gst_util_uint64_scale_int(pad_height, dar_n, dar_d);
    }

    *width = pad_width;
    *height = pad_height;
}

static void gst_remap_pad_create_conversion_info(
    GstVideoAggregatorConvertPad* pad, GstVideoAggregator* vagg,
    GstVideoInfo* conversion_info)
{
    GstRemapPad* cpad = GST_REMAP_PAD(pad);
    gint width, height;

    GST_VIDEO_AGGREGATOR_CONVERT_PAD_CLASS(gst_remap_pad_parent_class)
        ->create_conversion_info(pad, vagg, conversion_info);
    if (!conversion_info->finfo)
        return;
    if (cpad->_mapx.empty() || cpad->_mapy.empty())
        return;

    _mixer_pad_get_output_size(cpad, GST_VIDEO_INFO_PAR_N(&vagg->info),
        GST_VIDEO_INFO_PAR_D(&vagg->info), &width, &height);

    // save original width and height of a frame
    width = GST_VIDEO_INFO_WIDTH(conversion_info);
    height = GST_VIDEO_INFO_HEIGHT(conversion_info);

    /* The only thing that can change here is the width
     * and height, otherwise set_info would've been called */
    if (GST_VIDEO_INFO_WIDTH(conversion_info) != width
        || GST_VIDEO_INFO_HEIGHT(conversion_info) != height) {
        GstVideoInfo tmp_info;

        /* Initialize with the wanted video format and our original width and
         * height as we don't want to rescale. Then copy over the wanted
         * colorimetry, and chroma-site and our current pixel-aspect-ratio
         * and other relevant fields.
         */
        gst_video_info_set_format(
            &tmp_info, GST_VIDEO_INFO_FORMAT(conversion_info), width, height);
        tmp_info.chroma_site = conversion_info->chroma_site;
        tmp_info.colorimetry = conversion_info->colorimetry;
        tmp_info.par_n = conversion_info->par_n;
        tmp_info.par_d = conversion_info->par_d;
        tmp_info.fps_n = conversion_info->fps_n;
        tmp_info.fps_d = conversion_info->fps_d;
        tmp_info.flags = conversion_info->flags;
        tmp_info.interlace_mode = conversion_info->interlace_mode;

        *conversion_info = tmp_info;
    }
}

static void gst_remap_pad_class_init(GstRemapPadClass* klass)
{
    GObjectClass* gobject_class = (GObjectClass*)klass;
    GstVideoAggregatorPadClass* vaggpadclass
        = (GstVideoAggregatorPadClass*)klass;
    GstVideoAggregatorConvertPadClass* vaggcpadclass
        = (GstVideoAggregatorConvertPadClass*)klass;

    gobject_class->set_property = gst_remap_pad_set_property;
    gobject_class->get_property = gst_remap_pad_get_property;

    g_object_class_install_property(gobject_class, PROP_PAD_XPOS,
        g_param_spec_int("xpos", "X Position", "X Position of the picture",
            G_MININT, G_MAXINT, DEFAULT_PAD_XPOS,
            (GParamFlags)(G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE
                | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_PAD_YPOS,
        g_param_spec_int("ypos", "Y Position", "Y Position of the picture",
            G_MININT, G_MAXINT, DEFAULT_PAD_YPOS,
            GParamFlags(G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE
                | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_PAD_WIDTH,
        g_param_spec_int("width", "Width", "Width of the picture", G_MININT,
            G_MAXINT, DEFAULT_PAD_WIDTH,
            GParamFlags(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_PAD_HEIGHT,
        g_param_spec_int("height", "Height", "Height of the picture", G_MININT,
            G_MAXINT, DEFAULT_PAD_HEIGHT,
            GParamFlags(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_PAD_MAPS,
        g_param_spec_string("maps", "Maps", "File path to maps", "",
            GParamFlags(G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE
                | G_PARAM_STATIC_STRINGS)));
    // vaggpadclass->prepare_frame
    //    = GST_DEBUG_FUNCPTR(gst_remap_pad_prepare_frame);

    vaggcpadclass->create_conversion_info
        = GST_DEBUG_FUNCPTR(gst_remap_pad_create_conversion_info);
}

static void gst_remap_pad_init(GstRemapPad* compo_pad)
{
    compo_pad->xpos = DEFAULT_PAD_XPOS;
    compo_pad->ypos = DEFAULT_PAD_YPOS;
    compo_pad->maps = "";
    compo_pad->_mapx = cv::Mat();
    compo_pad->_mapy = cv::Mat();
}

/* GstRemap */
enum {
    PROP_0,
};

static void gst_remap_get_property(
    GObject* object, guint prop_id, GValue* value, GParamSpec* pspec)
{
    GstRemap* self = GST_REMAP(object);

    switch (prop_id) {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_remap_set_property(
    GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec)
{
    GstRemap* self = GST_REMAP(object);

    switch (prop_id) {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

#define gst_remap_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstRemap, gst_remap, GST_TYPE_VIDEO_AGGREGATOR,
    G_IMPLEMENT_INTERFACE(GST_TYPE_CHILD_PROXY, gst_remap_child_proxy_init));

static gboolean set_functions(GstRemap* self, GstVideoInfo* info)
{
    gboolean ret = TRUE;
    return ret;
}

static GstCaps* _fixate_caps(GstAggregator* agg, GstCaps* caps)
{
    GstVideoAggregator* vagg = GST_VIDEO_AGGREGATOR(agg);
    GList* l;
    gint best_width = -1, best_height = -1;
    gint best_fps_n = -1, best_fps_d = -1;
    gint par_n, par_d;
    gdouble best_fps = 0.;
    GstCaps* ret = NULL;
    GstStructure* s;

    ret = gst_caps_make_writable(caps);

    /* we need this to calculate how large to make the output frame */
    s = gst_caps_get_structure(ret, 0);
    if (gst_structure_has_field(s, "pixel-aspect-ratio")) {
        gst_structure_fixate_field_nearest_fraction(
            s, "pixel-aspect-ratio", 1, 1);
        gst_structure_get_fraction(s, "pixel-aspect-ratio", &par_n, &par_d);
    } else {
        par_n = par_d = 1;
    }

    GST_OBJECT_LOCK(vagg);
    for (l = GST_ELEMENT(vagg)->sinkpads; l; l = l->next) {
        GstVideoAggregatorPad* vaggpad = (GstVideoAggregatorPad*)l->data;
        GstRemapPad* remap_pad = GST_REMAP_PAD(vaggpad);
        gint this_width, this_height;
        gint width, height;
        gint fps_n, fps_d;
        gdouble cur_fps;

        fps_n = GST_VIDEO_INFO_FPS_N(&vaggpad->info);
        fps_d = GST_VIDEO_INFO_FPS_D(&vaggpad->info);
        _mixer_pad_get_output_size(remap_pad, par_n, par_d, &width, &height);

        if (width == 0 || height == 0)
            continue;

        this_width = width + MAX(remap_pad->xpos, 0);
        this_height = height + MAX(remap_pad->ypos, 0);

        if (best_width < this_width)
            best_width = this_width;
        if (best_height < this_height)
            best_height = this_height;

        if (fps_d == 0)
            cur_fps = 0.0;
        else
            gst_util_fraction_to_double(fps_n, fps_d, &cur_fps);

        if (best_fps < cur_fps) {
            best_fps = cur_fps;
            best_fps_n = fps_n;
            best_fps_d = fps_d;
        }
    }
    GST_OBJECT_UNLOCK(vagg);

    if (best_fps_n <= 0 || best_fps_d <= 0 || best_fps == 0.0) {
        best_fps_n = 25;
        best_fps_d = 1;
        best_fps = 25.0;
    }

    gst_structure_fixate_field_nearest_int(s, "width", best_width);
    gst_structure_fixate_field_nearest_int(s, "height", best_height);
    gst_structure_fixate_field_nearest_fraction(
        s, "framerate", best_fps_n, best_fps_d);
    ret = gst_caps_fixate(ret);

    return ret;
}

static gboolean _negotiated_caps(GstAggregator* agg, GstCaps* caps)
{
    GstVideoInfo v_info;

    GST_DEBUG_OBJECT(agg, "Negotiated caps %" GST_PTR_FORMAT, caps);

    if (!gst_video_info_from_caps(&v_info, caps))
        return FALSE;

    if (!set_functions(GST_REMAP(agg), &v_info)) {
        GST_ERROR_OBJECT(agg, "Failed to setup vfuncs");
        return FALSE;
    }

    return GST_AGGREGATOR_CLASS(parent_class)->negotiated_src_caps(agg, caps);
}

static void _get_mat_from_frame(GstVideoFrame* frame, cv::Mat& mat)
{
    int frame_width = GST_VIDEO_FRAME_WIDTH(frame);
    int frame_height = GST_VIDEO_FRAME_HEIGHT(frame);
    cv::Size sz = cv::Size(frame_width, frame_height);
    size_t step = GST_VIDEO_FRAME_PLANE_STRIDE(frame, 0);
    mat = cv::Mat(sz, CV_8UC4,
        frame->map->data + GST_VIDEO_FRAME_PLANE_OFFSET(frame, 0), step);
}

static GstFlowReturn gst_remap_aggregate_frames(
    GstVideoAggregator* vagg, GstBuffer* outbuf)
{
    GList* l;
    GstVideoFrame out_frame, *outframe;
    guint drawn_pads = 0;

    if (!gst_video_frame_map(&out_frame, &vagg->info, outbuf, GST_MAP_WRITE)) {
        GST_WARNING_OBJECT(vagg, "Could not map output buffer");
        return GST_FLOW_ERROR;
    }

    outframe = &out_frame;

    GST_OBJECT_LOCK(vagg);
    for (l = GST_ELEMENT(vagg)->sinkpads; l; l = l->next) {
        GstVideoAggregatorPad* pad = (GstVideoAggregatorPad*)l->data;
        GstRemapPad* compo_pad = GST_REMAP_PAD(pad);
        GstVideoFrame* prepared_frame
            = gst_video_aggregator_pad_get_prepared_frame(pad);

        if (prepared_frame != NULL) {
            cv::Mat frame, outmat;
            _get_mat_from_frame(prepared_frame, frame);
            _get_mat_from_frame(outframe, outmat);
            cv::Mat roi(outmat,
                cv::Rect(compo_pad->xpos, compo_pad->ypos, compo_pad->width,
                    compo_pad->height));
            cv::remap(frame, outmat, compo_pad->_mapx, compo_pad->_mapy,
                cv::INTER_LINEAR, cv::BORDER_TRANSPARENT);
            drawn_pads++;
        }
    }
    GST_OBJECT_UNLOCK(vagg);

    gst_video_frame_unmap(outframe);

    return GST_FLOW_OK;
}

static GstPad* gst_remap_request_new_pad(GstElement* element,
    GstPadTemplate* templ, const gchar* req_name, const GstCaps* caps)
{
    GstPad* newpad;

    newpad = (GstPad*)GST_ELEMENT_CLASS(parent_class)
                 ->request_new_pad(element, templ, req_name, caps);

    if (newpad == NULL)
        goto could_not_create;

    gst_child_proxy_child_added(
        GST_CHILD_PROXY(element), G_OBJECT(newpad), GST_OBJECT_NAME(newpad));

    return newpad;

could_not_create : {
    GST_DEBUG_OBJECT(element, "could not create/add pad");
    return NULL;
}
}

static void gst_remap_release_pad(GstElement* element, GstPad* pad)
{
    GstRemap* remap;

    remap = GST_REMAP(element);

    GST_DEBUG_OBJECT(remap, "release pad %s:%s", GST_DEBUG_PAD_NAME(pad));

    gst_child_proxy_child_removed(
        GST_CHILD_PROXY(remap), G_OBJECT(pad), GST_OBJECT_NAME(pad));

    GST_ELEMENT_CLASS(parent_class)->release_pad(element, pad);
}

static gboolean _sink_query(
    GstAggregator* agg, GstAggregatorPad* bpad, GstQuery* query)
{
    switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_ALLOCATION: {
        GstCaps* caps;
        GstVideoInfo info;
        GstBufferPool* pool;
        guint size;
        GstStructure* structure;

        gst_query_parse_allocation(query, &caps, NULL);

        if (caps == NULL)
            return FALSE;

        if (!gst_video_info_from_caps(&info, caps))
            return FALSE;

        size = GST_VIDEO_INFO_SIZE(&info);

        pool = gst_video_buffer_pool_new();

        structure = gst_buffer_pool_get_config(pool);
        gst_buffer_pool_config_set_params(structure, caps, size, 0, 0);

        if (!gst_buffer_pool_set_config(pool, structure)) {
            gst_object_unref(pool);
            return FALSE;
        }

        gst_query_add_allocation_pool(query, pool, size, 0, 0);
        gst_object_unref(pool);
        gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);

        return TRUE;
    }
    default:
        return GST_AGGREGATOR_CLASS(parent_class)
            ->sink_query(agg, bpad, query);
    }
}

/* GObject boilerplate */
static void gst_remap_class_init(GstRemapClass* klass)
{
    GObjectClass* gobject_class = (GObjectClass*)klass;
    GstElementClass* gstelement_class = (GstElementClass*)klass;
    GstVideoAggregatorClass* videoaggregator_class
        = (GstVideoAggregatorClass*)klass;
    GstAggregatorClass* agg_class = (GstAggregatorClass*)klass;

    gobject_class->get_property = gst_remap_get_property;
    gobject_class->set_property = gst_remap_set_property;

    gstelement_class->request_new_pad
        = GST_DEBUG_FUNCPTR(gst_remap_request_new_pad);
    gstelement_class->release_pad = GST_DEBUG_FUNCPTR(gst_remap_release_pad);
    agg_class->sink_query = _sink_query;
    agg_class->fixate_src_caps = _fixate_caps;
    agg_class->negotiated_src_caps = _negotiated_caps;
    videoaggregator_class->aggregate_frames = gst_remap_aggregate_frames;

    gst_element_class_add_static_pad_template_with_gtype(
        gstelement_class, &src_factory, GST_TYPE_AGGREGATOR_PAD);
    gst_element_class_add_static_pad_template_with_gtype(
        gstelement_class, &sink_factory, GST_TYPE_REMAP_PAD);

    gst_element_class_set_static_metadata(gstelement_class, "Remap",
        "Filter/Editor/Video/Remap", "Composite multiple video streams",
        "Wim Taymans <wim@fluendo.com>, "
        "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

    gst_type_mark_as_plugin_api(GST_TYPE_REMAP_PAD, GstPluginAPIFlags(0));
}

static void gst_remap_init(GstRemap* self)
{ /* initialize variables */
}

/* GstChildProxy implementation */
static GObject* gst_remap_child_proxy_get_child_by_index(
    GstChildProxy* child_proxy, guint index)
{
    GstRemap* remap = GST_REMAP(child_proxy);
    GObject* obj = NULL;

    GST_OBJECT_LOCK(remap);
    obj = (GObject*)g_list_nth_data(GST_ELEMENT_CAST(remap)->sinkpads, index);
    if (obj)
        gst_object_ref(obj);
    GST_OBJECT_UNLOCK(remap);

    return obj;
}

static guint gst_remap_child_proxy_get_children_count(
    GstChildProxy* child_proxy)
{
    guint count = 0;
    GstRemap* remap = GST_REMAP(child_proxy);

    GST_OBJECT_LOCK(remap);
    count = GST_ELEMENT_CAST(remap)->numsinkpads;
    GST_OBJECT_UNLOCK(remap);
    GST_INFO_OBJECT(remap, "Children Count: %d", count);

    return count;
}

static void gst_remap_child_proxy_init(gpointer g_iface, gpointer iface_data)
{
    GstChildProxyInterface* iface = (GstChildProxyInterface*)g_iface;

    iface->get_child_by_index = gst_remap_child_proxy_get_child_by_index;
    iface->get_children_count = gst_remap_child_proxy_get_children_count;
}

/* Element registration */
static gboolean plugin_init(GstPlugin* plugin)
{
    GST_DEBUG_CATEGORY_INIT(gst_remap_debug, "remap", 0, "remap");

    return gst_element_register(
        plugin, "remap", GST_RANK_PRIMARY + 1, GST_TYPE_REMAP);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, kiplugins,
    GST_PACKAGE_NAME, plugin_init, PACKAGE_VERSION, GST_LICENSE, PACKAGE,
    GST_PACKAGE_ORIGIN)
