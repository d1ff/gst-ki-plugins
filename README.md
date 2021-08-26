KnotInspector GStreamer plugins repository

# Dependencies

* GStreamer 1.18
* OpenCV 4.5.1

# Usage
Configure and build all plugins:
```
meson build
ninja -C build
```

Once the plugin is built you can either install system-wide it with `sudo ninja -C build install` (however, this will by default go into the /usr/local
prefix where it won't be picked up by a GStreamer installed from packages, so
you would need to set the GST_PLUGIN_PATH environment variable to include or
point to /usr/local/lib/gstreamer-1.0/ for your plugin to be found by a
from-package GStreamer).
Alternatively, you will find your plugin binary in build/ as libgstkiplugins.so or similar (the extension may vary), so you can also set the GST_PLUGIN_PATH environment variable to the build
directory (best to specify an absolute path though). You can also check if it has been built correctly with:
```
gst-inspect-1.0 build/libgstkiplugins.so
```

# Example

## Remap

Remap plugin acts like a compositor, but it uses `cv2::remap` function to
achieve the same goal. For this element to work, you need to supply a tiff
image, containing CV_32FC1 maps with `sink_%u::maps` property. You should use
`cv2::imwritemulti` to save them.

This element supports only BGRA frames.

```
gst-launch-1.0 \
    remap name=mix drop=true \
        sink_0::maps=0_small.tiff sink_0::xpos=989 \
        sink_1::maps=1_small.tiff sink_1::xpos=700 \
        sink_2::maps=2_small.tiff sink_2::xpos=402 \
        sink_3::maps=3_small.tiff sink_3::xpos=0 \
        ! videoconvert ! glimagesink sync=false \
    videotestsrc pattern=snow ! video/x-raw,width=1280,height=720 ! mix. \
    videotestsrc pattern=snow ! video/x-raw,width=1280,height=720 ! mix. \
    videotestsrc pattern=snow ! video/x-raw,width=1280,height=720 ! mix. \
    videotestsrc pattern=snow ! video/x-raw,width=1280,height=720 ! mix.
```
