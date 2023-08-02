#include <arpa/inet.h>
#include <gst/gst.h>
#include <poll.h>
#include <unistd.h>
// #include <chrono>
#include <iostream>
#include <set>
#include <thread>

// TODO Check whether this should be higher
const int BUFFER_SIZE = 4096;
// TODO Rename this to PORT, after the other one below it is discarded
const int PORT_EXT = 62000;
// TODO Discard this and all related functionality
const int PORT_INT = 63000;

struct sockaddr_in_cmp
{
    bool operator()(const sockaddr_in &lhs, const sockaddr_in &rhs) const
    {
        if (lhs.sin_addr.s_addr != rhs.sin_addr.s_addr)
            return lhs.sin_addr.s_addr < rhs.sin_addr.s_addr;
        return lhs.sin_port < rhs.sin_port;
    }
};

std::set<sockaddr_in, sockaddr_in_cmp> client_sockaddrs;

/**
 * Get data from the `appsink` at the end of the GStreamer pipeline and
 * send it to all active clients using the supplied socket.
 *
 * TODO Check whether using a callback for "new-sample" is better than
 * using a separate thread and blocking on `gst_app_sink_pull_sample`.
 */
// void route(GstAppSink *appsink, int socket_ext)
// {
//     while (true)
//     {
//         GstSample *sample = gst_app_sink_pull_sample(appsink);
//         GstBuffer *buffer = gst_sample_get_buffer(sample);
//         GstMapInfo info;

//         if (gst_buffer_map(buffer, &info, GST_MAP_READ))
//         {
//             const guint8 *buffer_data = info.data;
//             const gsize buffer_size = info.size;

//             // Send data to all active clients
//             // TODO Protect for the case of a simultaneous write
//             for (const auto &client_sockaddr : client_sockaddrs)
//             {
//                 if (sendto(socket_ext, buffer_data, buffer_size, 0, (struct sockaddr *)&client_sockaddr, sizeof(client_sockaddr)) < 0)
//                 {
//                     std::cerr << "Failed to send to client." << std::endl;
//                     continue;
//                 }
//             }

//             gst_buffer_unmap(buffer, &info);
//         }

//         gst_sample_unref(sample);
//     }
// }

int main()
{
    // Initialize GStreamer
    gst_init(nullptr, nullptr);

    // Processing pipeline description (currently only flips horizontally)
    // const char *processing_pipeline_desc = "appsrc name=appsrc caps=\"application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264, payload=(int)96\" ! rtph264depay ! decodebin ! videoscale ! videoconvert ! videoflip method=horizontal-flip ! x264enc tune=zerolatency bitrate=500 speed-preset=superfast ! rtph264pay ! appsink name=appsink";
    // Based on `gst-launch-1.0 compositor name=comp sink_0::xpos=0 sink_0::ypos=0 sink_1::xpos=320 sink_1::ypos=0 sink_2::xpos=640 sink_2::ypos=0 sink_3::xpos=0 sink_3::ypos=240 ! autovideosink videotestsrc pattern=white ! video/x-raw, framerate=30/1, width=320, height=240 ! comp. videotestsrc pattern=red ! videobox ! video/x-raw, framerate=60/1, width=320, height=240 ! comp. videotestsrc pattern=green ! videobox ! video/x-raw, framerate=30/1, width=320, height=240 ! comp. videotestsrc pattern=blue ! videobox ! video/x-raw, framerate=30/1, width=320, height=240 ! comp.`
    const char *processing_pipeline_desc = "compositor name=compositor sink_0::xpos=0 sink_0::ypos=0 sink_1::xpos=320 sink_1::ypos=0 sink_2::xpos=640 sink_2::ypos=0 sink_3::xpos=0 sink_3::ypos=240 ! x264enc tune=zerolatency bitrate=500 speed-preset=superfast ! rtph264pay ! udpsink name=udpsink host=127.0.0.1 port=63000 udpsrc name=udpsrc_0 port=5000 caps=\"application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264, payload=(int)96\" ! rtph264depay ! avdec_h264 ! videoscale ! videoconvert ! video/x-raw, framerate=30/1, width=320, height=240 ! compositor. videotestsrc pattern=red ! videobox ! video/x-raw, framerate=60/1, width=320, height=240 ! compositor. videotestsrc pattern=green ! videobox ! video/x-raw, framerate=30/1, width=320, height=240 ! compositor. videotestsrc pattern=blue ! videobox ! video/x-raw, framerate=30/1, width=320, height=240 ! compositor.";

    // Create processing pipeline
    GstElement *processing_pipeline = gst_parse_launch(processing_pipeline_desc, nullptr);

    // Get the appsrc element of the processing pipeline
    // GstElement *appsrc = gst_bin_get_by_name(GST_BIN(processing_pipeline), "appsrc");
    // The following is for a live timestamped stream
    // g_object_set(G_OBJECT(appsrc), "stream-type", 0, "is-live", TRUE, "format", GST_FORMAT_TIME, nullptr);

    // Get the appsink element of the processing pipeline
    // GstElement *appsink = gst_bin_get_by_name(GST_BIN(processing_pipeline), "appsink");

    // TODO Almost everything above is not cleaned up afterwards

    // Socket that external clients send to
    int socket_ext = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ext < 0)
    {
        std::cerr << "Failed to create socket_ext." << std::endl;
        return 1;
    }

    sockaddr_in sockaddr_ext{};
    sockaddr_ext.sin_family = AF_INET;
    inet_pton(AF_INET, "0.0.0.0", &(sockaddr_ext.sin_addr));
    sockaddr_ext.sin_port = htons(PORT_EXT);

    if (bind(socket_ext, (struct sockaddr *)&sockaddr_ext, sizeof(sockaddr_ext)) < 0)
    {
        std::cerr << "Failed to bind socket_ext." << std::endl;
        return 1;
    }

    // Socket that internal GStreamer sends to
    int socket_int = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_int < 0)
    {
        std::cerr << "Failed to create socket_int." << std::endl;
        return 1;
    }

    sockaddr_in sockaddr_int{};
    sockaddr_int.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &(sockaddr_int.sin_addr));
    sockaddr_int.sin_port = htons(PORT_INT);

    if (bind(socket_int, (struct sockaddr *)&sockaddr_int, sizeof(sockaddr_int)) < 0)
    {
        std::cerr << "Failed to bind socket_int." << std::endl;
        return 1;
    }

    struct pollfd fds[2];

    // TODO Check whether using the same buffer for both sockets is OK and whether it should be outside of the loop
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    // TODO Same check for these
    sockaddr_in client_sockaddr{};
    socklen_t client_sockaddr_len = sizeof(client_sockaddr);

    fds[0].fd = socket_ext;
    fds[0].events = POLLIN;
    fds[1].fd = socket_int;
    fds[1].events = POLLIN;

    // std::thread route_thread(route, GST_APP_SINK(appsink), socket_ext);

    // auto start_time = std::chrono::steady_clock::now();

    gst_element_set_state(processing_pipeline, GST_STATE_PLAYING);

    while (true)
    {
        // Block until a socket event occurs
        int poll_res = poll(fds, 2, -1);
        if (poll_res < 0)
        {
            std::cerr << "Poll error." << std::endl;
            return 1;
        }

        // Check whether socket_ext has data
        if (fds[0].revents & POLLIN)
        {
            // Receive from client
            bytes_read = recvfrom(socket_ext, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_sockaddr, &client_sockaddr_len);
            if (bytes_read < 0)
            {
                std::cerr << "Failed to receive from client." << std::endl;
                continue;
            }

            // TODO Remove this and buffer printing. This is for printing only and will break things if the buffer is full.
            // buffer[bytes_read] = '\0';
            // std::cout << "Received data on socket_ext: '" << buffer << "' from " << inet_ntoa(client_sockaddr.sin_addr) << ":" << ntohs(client_sockaddr.sin_port) << std::endl;

            // Add client to active clients, if client is not already there.
            if (client_sockaddrs.count(client_sockaddr) == 0)
            {
                client_sockaddrs.insert(client_sockaddr);
            }

            sockaddr_in sockaddr_udpsrc_0{};
            sockaddr_udpsrc_0.sin_family = AF_INET;
            inet_pton(AF_INET, "127.0.0.1", &(sockaddr_udpsrc_0.sin_addr));
            sockaddr_udpsrc_0.sin_port = htons(5000);

            if (sendto(socket_ext, buffer, bytes_read, 0, (struct sockaddr *)&sockaddr_udpsrc_0, sizeof(sockaddr_udpsrc_0)) < 0)
            {
                std::cerr << "Failed to send to GStreamer." << std::endl;
                continue;
            }

            // GstBuffer *gst_buffer = gst_buffer_new_allocate(nullptr, bytes_read, nullptr);
            // const auto bytes_copied = gst_buffer_fill(gst_buffer, 0, buffer, bytes_read);
            // auto now_time = std::chrono::steady_clock::now();
            // GST_BUFFER_PTS(gst_buffer) = std::chrono::duration_cast<std::chrono::nanoseconds>(now_time - start_time).count();
            // const auto result = gst_app_src_push_buffer(GST_APP_SRC(appsrc), gst_buffer);

            // if (result != GST_FLOW_OK)
            // {
            //     std::cerr << "Failed to push buffer to appsrc." << std::endl;
            //     gst_object_unref(processing_pipeline);
            //     return 1;
            // }
        }

        // Check whether socket_int has data
        if (fds[1].revents & POLLIN)
        {
            // Receive from GStreamer
            // TODO Check whether the same buffer and struct should be used for GStreamer
            bytes_read = recvfrom(socket_int, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_sockaddr, &client_sockaddr_len);
            if (bytes_read < 0)
            {
                std::cerr << "Failed to receive from GStreamer." << std::endl;
                continue;
            }
            // TODO Remove this and buffer printing. This is for printing only and will break things if the buffer is full.
            // buffer[bytes_read] = '\0';
            // std::cout << "Received data on socket_int: '" << buffer << "' from " << inet_ntoa(client_sockaddr.sin_addr) << ":" << ntohs(client_sockaddr.sin_port) << std::endl;

            // Send received data from GStreamer to all active clients
            for (const auto &client_sockaddr : client_sockaddrs)
            {
                if (sendto(socket_ext, buffer, bytes_read, 0, (struct sockaddr *)&client_sockaddr, sizeof(client_sockaddr)) < 0)
                {
                    std::cerr << "Failed to send to client." << std::endl;
                    continue;
                }
            }
        }

        // Print active clients
        std::cout << "---- Active clients ----" << std::endl;
        for (const auto &client_sockaddr : client_sockaddrs)
        {
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(client_sockaddr.sin_addr), client_ip, INET_ADDRSTRLEN);
            std::cout << client_ip << ":" << ntohs(client_sockaddr.sin_port) << std::endl;
        }
        std::cout << "------------------------" << std::endl;
    }

    close(socket_ext);
    close(socket_int);

    return 0;
}
