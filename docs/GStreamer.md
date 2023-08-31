# GStreamer

## Snap pipeline to play pipeline

### Play pipeline

```bash
gst-launch-1.0 -v udpsrc port=27901 caps="application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264, payload=(int)96" ! rtph264depay ! avdec_h264 ! videoconvert ! autovideosink
```

### Snap pipeline

```bash
gst-launch-1.0 -v v4l2src device=/dev/video0 ! videoconvert ! videoscale ! video/x-raw, framerate=30/1, width=320, height=240 ! videoscale ! videoconvert ! x264enc tune=zerolatency bitrate=500 speed-preset=superfast ! rtph264pay ! udpsink host=127.0.0.1 port=27901
```

## Snap pipeline to composite pipeline to play pipeline

### Play pipeline

```bash
gst-launch-1.0 -v udpsrc port=27901 caps="application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264, payload=(int)96" ! rtph264depay ! avdec_h264 ! videoconvert ! autovideosink
```

### Composite pipeline

```bash
gst-launch-1.0 -v compositor name=compositor background=black zero-size-is-unscaled=false ! videobox autocrop=true ! capsfilter caps="video/x-raw, width=320, height=240" ! x264enc tune=zerolatency bitrate=500 speed-preset=superfast ! rtph264pay ! udpsink host=127.0.0.1 port=27901 udpsrc port=27900 caps="application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264, payload=(int)96" ! rtph264depay ! avdec_h264 ! videoscale ! videoconvert ! video/x-raw, framerate=30/1, width=320, height=240 ! compositor.
```

### Snap pipeline

```bash
gst-launch-1.0 -v v4l2src device=/dev/video0 ! videoconvert ! videoscale ! video/x-raw, framerate=30/1, width=320, height=240 ! videoscale ! videoconvert ! x264enc tune=zerolatency bitrate=500 speed-preset=superfast ! rtph264pay ! udpsink host=127.0.0.1 port=27900
```
