/*
 * SPDX-FileCopyrightText: 2023 Harry Nakos <xnakos@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <arpa/inet.h>
#include <gio/gio.h>
#include <gst/gst.h>
#include <iostream>
#include <thread>

/**
 * Playback pipeline description: udpsrc name=udpsrc caps="application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264, payload=(int)96" ! rtph264depay ! avdec_h264 ! videoconvert ! autovideosink
 */
GstElement *playback_pipeline_make(GSocket *socket)
{
    GstElement *pipeline = gst_pipeline_new("playback-pipeline");

    GstElement *udpsrc = gst_element_factory_make("udpsrc", "udpsrc");
    GstElement *rtph264depay = gst_element_factory_make("rtph264depay", "rtph264depay");
    GstElement *avdec_h264 = gst_element_factory_make("avdec_h264", "avdec_h264");
    GstElement *videoconvert = gst_element_factory_make("videoconvert", "videoconvert");
    GstElement *autovideosink = gst_element_factory_make("autovideosink", "autovideosink");

    if (!pipeline || !udpsrc || !rtph264depay || !avdec_h264 || !videoconvert || !autovideosink)
    {
        g_printerr("Failed to create playback pipeline elements.\n");
        return nullptr;
    }

    GstCaps *caps = gst_caps_new_simple("application/x-rtp",
                                        "media", G_TYPE_STRING, "video",
                                        "clock-rate", G_TYPE_INT, 90000,
                                        "encoding-name", G_TYPE_STRING, "H264",
                                        "payload", G_TYPE_INT, 96,
                                        nullptr);

    g_object_set(udpsrc, "caps", caps, "socket", socket, nullptr);

    gst_caps_unref(caps);

    gst_bin_add_many(GST_BIN(pipeline), udpsrc, rtph264depay, avdec_h264, videoconvert, autovideosink, nullptr);

    if (!gst_element_link_many(udpsrc, rtph264depay, avdec_h264, videoconvert, autovideosink, nullptr))
    {
        g_printerr("Failed to link playback pipeline elements.\n");
        gst_object_unref(pipeline);
        return nullptr;
    }

    return pipeline;
}

/**
 * Capture pipeline description: v4l2src device=/dev/video0 ! videoconvert ! videoscale ! video/x-raw, framerate=30/1, width=320, height=240 ! videoscale ! videoconvert ! x264enc tune=zerolatency bitrate=500 speed-preset=superfast ! rtph264pay ! udpsink name=udpsink host=127.0.0.1 port=27884
 * Test capture pipeline description: videotestsrc pattern=ball ! videoconvert ! videoscale ! video/x-raw, framerate=30/1, width=320, height=240 ! videoscale ! videoconvert ! x264enc tune=zerolatency bitrate=500 speed-preset=superfast ! rtph264pay ! udpsink name=udpsink host=127.0.0.1 port=27884
 */
GstElement *capture_pipeline_make(bool is_test, std::string device, std::string server_host, int server_port, GSocket *socket)
{
    GstElement *pipeline = gst_pipeline_new("capture-pipeline");

    GstElement *src;
    if (is_test)
    {
        src = gst_element_factory_make("videotestsrc", "videotestsrc");
    }
    else
    {
        src = gst_element_factory_make("v4l2src", "v4l2src");
    }
    GstElement *videoconvert1 = gst_element_factory_make("videoconvert", "videoconvert1");
    GstElement *videoscale1 = gst_element_factory_make("videoscale", "videoscale1");
    GstElement *capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    GstElement *videoscale2 = gst_element_factory_make("videoscale", "videoscale2");
    GstElement *videoconvert2 = gst_element_factory_make("videoconvert", "videoconvert2");
    GstElement *x264enc = gst_element_factory_make("x264enc", "x264enc");
    GstElement *rtph264pay = gst_element_factory_make("rtph264pay", "rtph264pay");
    GstElement *udpsink = gst_element_factory_make("udpsink", "udpsink");

    if (!pipeline || !src || !videoconvert1 || !videoscale1 || !capsfilter || !videoscale2 || !videoconvert2 || !x264enc || !rtph264pay || !udpsink)
    {
        g_printerr("Failed to create capture pipeline elements.\n");
        return nullptr;
    }

    if (is_test)
    {
        // pattern: ball (18) – Moving ball
        g_object_set(src, "pattern", 18, nullptr);
    }
    else
    {
        g_object_set(src, "device", device.c_str(), nullptr);
    }

    // TODO Allow higher framerate, when supported by the webcam
    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        "framerate", GST_TYPE_FRACTION, 30, 1,
                                        "width", G_TYPE_INT, 320,
                                        "height", G_TYPE_INT, 240,
                                        nullptr);
    g_object_set(capsfilter, "caps", caps, nullptr);
    gst_caps_unref(caps);

    // tune: zerolatency (0x00000004) – Zero latency
    // bitrate: 500
    // speed-preset: ultrafast (1) – ultrafast / superfast (2) – superfast
    g_object_set(x264enc, "tune", 4, "bitrate", 500, "speed-preset", 2, nullptr);
    g_object_set(udpsink, "host", server_host.c_str(), "port", server_port, "socket", socket, nullptr);

    gst_bin_add_many(GST_BIN(pipeline), src, videoconvert1, videoscale1, capsfilter, videoscale2, videoconvert2, x264enc, rtph264pay, udpsink, nullptr);
    if (!gst_element_link_many(src, videoconvert1, videoscale1, capsfilter, videoscale2, videoconvert2, x264enc, rtph264pay, udpsink, nullptr))
    {
        g_printerr("Failed to link capture pipeline elements.\n");
        gst_object_unref(pipeline);
        return nullptr;
    }

    return pipeline;
}

void print_usage(char *program_name)
{
    fprintf(stderr, "Usage: %s [-t] [-d device] [-p serverport] [serverhost]\n", program_name);
}

int main(int argc, char *argv[])
{
    // Whether a videotestsrc instead of a webcam device will be used
    bool is_test = false;
    std::string device = "/dev/video0";
    std::string server_host = "127.0.0.1";
    int server_port = 27884;

    int opt;

    while ((opt = getopt(argc, argv, "td:p:h")) != -1)
    {
        switch (opt)
        {
        case 't':
            is_test = true;
            break;
        case 'd':
            device = optarg;
            break;
        case 'p':
            server_port = atoi(optarg);
            break;
        case 'h':
            print_usage(argv[0]);
            exit(EXIT_SUCCESS);
        default:
            print_usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (optind < argc)
    {
        server_host = argv[optind];
    }

    // Initialize GStreamer
    gst_init(nullptr, nullptr);

    // Socket for UDP communication with server
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1)
    {
        std::cerr << "Failed to create sock." << std::endl;
        return 1;
    }

    sockaddr_in client_sockaddr{};
    client_sockaddr.sin_family = AF_INET;
    inet_pton(AF_INET, "0.0.0.0", &(client_sockaddr.sin_addr));
    client_sockaddr.sin_port = htons(0); // Assign any available port
    if (bind(sock, (struct sockaddr *)&client_sockaddr, sizeof(client_sockaddr)) < 0)
    {
        std::cerr << "Failed to bind sock." << std::endl;
        close(sock);
        return 1;
    }

    // Create GSocket object for use with udpsrc and udpsink
    GSocket *gsock = g_socket_new_from_fd(sock, nullptr);

    // Create playback pipeline
    GstElement *playback_pipeline = playback_pipeline_make(gsock);

    // Create capture pipeline
    GstElement *capture_pipeline = capture_pipeline_make(is_test, device, server_host, server_port, gsock);

    // Start playing the pipelines
    gst_element_set_state(playback_pipeline, GST_STATE_PLAYING);
    gst_element_set_state(capture_pipeline, GST_STATE_PLAYING);

    // Create a GLib Main Loop and set it to run
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(loop);

    // Clean up
    gst_element_set_state(capture_pipeline, GST_STATE_NULL);
    gst_element_set_state(playback_pipeline, GST_STATE_NULL);
    gst_object_unref(capture_pipeline);
    gst_object_unref(playback_pipeline);
    g_main_loop_unref(loop);
    g_object_unref(gsock);

    return 0;
}
