#include <arpa/inet.h>
#include <gio/gio.h>
#include <gst/gst.h>
#include <iostream>
#include <thread>

GstElement *make_playback_pipeline(GSocket *socket)
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
    std::string server_port = "27884";

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
            server_port = optarg;
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

    std::string capture_pipeline_desc_str;

    if (!is_test)
        capture_pipeline_desc_str = "v4l2src device=" + device + " ! videoconvert ! videoscale ! video/x-raw,framerate=30/1,width=320,height=240 ! videoscale ! videoconvert ! x264enc tune=zerolatency bitrate=500 speed-preset=superfast ! rtph264pay ! udpsink name=udpsink host=" + server_host + " port=" + server_port;
    else
        capture_pipeline_desc_str = "videotestsrc pattern=ball ! videoconvert ! videoscale ! video/x-raw,framerate=30/1,width=320,height=240 ! videoscale ! videoconvert ! x264enc tune=zerolatency bitrate=500 speed-preset=superfast ! rtph264pay ! udpsink name=udpsink host=" + server_host + " port=" + server_port;

    // Capture pipeline description
    const char *capture_pipeline_desc = capture_pipeline_desc_str.c_str();

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
    GstElement *playback_pipeline = make_playback_pipeline(gsock);

    // Create capture pipeline
    GstElement *capture_pipeline = gst_parse_launch(capture_pipeline_desc, nullptr);

    // Set up the UDP sink element of the capture pipeline
    GstElement *udpsink = gst_bin_get_by_name(GST_BIN(capture_pipeline), "udpsink");
    g_object_set(udpsink, "socket", gsock, nullptr);

    // Start playing the pipelines
    gst_element_set_state(playback_pipeline, GST_STATE_PLAYING);
    gst_element_set_state(capture_pipeline, GST_STATE_PLAYING);

    // Create a GLib Main Loop and set it to run
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(loop);

    // Free resources
    // TODO Verify cleanup
    gst_element_set_state(capture_pipeline, GST_STATE_NULL);
    gst_element_set_state(playback_pipeline, GST_STATE_NULL);
    gst_object_unref(capture_pipeline);
    gst_object_unref(playback_pipeline);
    g_main_loop_unref(loop);

    // Free GSocket object
    g_object_unref(gsock);

    return 0;
}
