#include <arpa/inet.h>
#include <gio/gio.h>
#include <gst/gst.h>
#include <poll.h>
#include <unistd.h>
#include <iostream>
#include <map>
#include <set>
#include <thread>
#include <vector>

// TODO Check whether this should be higher
const int BUFFER_SIZE = 4096;
const int SERVER_PORT = 62000;

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

// Maps client address to GStreamer pipeline udpsrc address
// FIXME Use full host/port address, not just the port
std::map<uint, sockaddr_in> client_routing;

// Maps client address to last activity time
// FIXME Use full host/port address, not just the port
std::map<uint, gint64> client_activity;

// GStreamer pipeline unused udpsrc socket addresses
// Use as a stack? Initialize in reverse?
// TODO Initialize with specific size for efficiency, with reserve?
std::vector<sockaddr_in> udpsrc_sockaddrs_available;

std::vector<int> udpsrc_socks;

std::vector<GSocket *> udpsrc_gsocks;

/**
 * Initialize udpsrc elements. FIXME Do this or change name and description.
 */
void init_udpsrcs()
{
    for (int i = 0; i < 2; i++)
    {
        int udpsrc_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (udpsrc_sock == -1)
        {
            std::cerr << "Failed to create udpsrc_sock." << std::endl;
            return;
        }

        sockaddr_in udpsrc_sockaddr{};
        udpsrc_sockaddr.sin_family = AF_INET;
        inet_pton(AF_INET, "127.0.0.1", &(udpsrc_sockaddr.sin_addr));
        udpsrc_sockaddr.sin_port = htons(0); // Assign any port

        if (bind(udpsrc_sock, (struct sockaddr *)&udpsrc_sockaddr, sizeof(udpsrc_sockaddr)) == -1)
        {
            std::cerr << "Failed to bind udpsrc_sock." << std::endl;
            return;
        }

        socklen_t udpsrc_sockaddr_len = sizeof(udpsrc_sockaddr);
        if (getsockname(udpsrc_sock, (struct sockaddr *)&udpsrc_sockaddr, &udpsrc_sockaddr_len) == -1)
        {
            std::cerr << "Failed to get udpsrc_sock name." << std::endl;
            return;
        }

        GSocket *udpsrc_gsock = g_socket_new_from_fd(udpsrc_sock, nullptr);

        if (udpsrc_gsock == nullptr)
        {
            std::cerr << "Failed to create udpsrc_gsock." << std::endl;
            return;
        }

        udpsrc_socks.push_back(udpsrc_sock);
        udpsrc_sockaddrs_available.push_back(udpsrc_sockaddr);
        udpsrc_gsocks.push_back(udpsrc_gsock);
    }
}

int main()
{
    // Initialize GStreamer
    gst_init(nullptr, nullptr);

    // Processing pipeline description (currently only flips horizontally)
    // const char *processing_pipeline_desc = "appsrc name=appsrc caps=\"application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264, payload=(int)96\" ! rtph264depay ! decodebin ! videoscale ! videoconvert ! videoflip method=horizontal-flip ! x264enc tune=zerolatency bitrate=500 speed-preset=superfast ! rtph264pay ! appsink name=appsink";
    // Based on `gst-launch-1.0 compositor name=comp sink_0::xpos=0 sink_0::ypos=0 sink_1::xpos=320 sink_1::ypos=0 sink_2::xpos=640 sink_2::ypos=0 sink_3::xpos=0 sink_3::ypos=240 ! autovideosink videotestsrc pattern=white ! video/x-raw, framerate=30/1, width=320, height=240 ! comp. videotestsrc pattern=red ! videobox ! video/x-raw, framerate=60/1, width=320, height=240 ! comp. videotestsrc pattern=green ! videobox ! video/x-raw, framerate=30/1, width=320, height=240 ! comp. videotestsrc pattern=blue ! videobox ! video/x-raw, framerate=30/1, width=320, height=240 ! comp.`
    const char *processing_pipeline_desc = "compositor name=compositor background=black sink_0::xpos=0 sink_0::ypos=0 sink_1::xpos=320 sink_1::ypos=0 sink_2::xpos=640 sink_2::ypos=0 sink_3::xpos=0 sink_3::ypos=240 ! x264enc tune=zerolatency bitrate=500 speed-preset=superfast ! rtph264pay ! udpsink name=udpsink host=127.0.0.1 udpsrc name=udpsrc_0 caps=\"application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264, payload=(int)96\" ! rtph264depay ! avdec_h264 ! videoscale ! videoconvert ! video/x-raw, framerate=30/1, width=320, height=240 ! compositor. videotestsrc pattern=red ! videobox ! video/x-raw, framerate=60/1, width=320, height=240 ! compositor. videotestsrc pattern=green ! videobox ! video/x-raw, framerate=30/1, width=320, height=240 ! compositor. udpsrc name=udpsrc_1 caps=\"application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264, payload=(int)96\" ! rtph264depay ! avdec_h264 ! videoscale ! videoconvert ! video/x-raw, framerate=30/1, width=320, height=240 ! compositor.";

    // Create processing pipeline
    GstElement *processing_pipeline = gst_parse_launch(processing_pipeline_desc, nullptr);

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
    sockaddr_ext.sin_port = htons(SERVER_PORT);

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
    sockaddr_int.sin_port = htons(0);

    if (bind(socket_int, (struct sockaddr *)&sockaddr_int, sizeof(sockaddr_int)) < 0)
    {
        std::cerr << "Failed to bind socket_int." << std::endl;
        return 1;
    }

    socklen_t sockaddr_int_len = sizeof(sockaddr_int);
    if (getsockname(socket_int, (struct sockaddr *)&sockaddr_int, &sockaddr_int_len) == -1)
    {
        std::cerr << "Failed to get socket name." << std::endl;
        return 1;
    }

    // Extract the port number from sockaddr_int
    unsigned short port_assigned = ntohs(sockaddr_int.sin_port);
    std::cout << "Assigned port: " << port_assigned << std::endl;

    GstElement *udpsink = gst_bin_get_by_name(GST_BIN(processing_pipeline), "udpsink");
    g_object_set(udpsink, "port", port_assigned, nullptr);

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

    init_udpsrcs();

    GstElement *udpsrc_0 = gst_bin_get_by_name(GST_BIN(processing_pipeline), "udpsrc_0");
    g_object_set(udpsrc_0, "socket", udpsrc_gsocks[1], nullptr);

    GstElement *udpsrc_1 = gst_bin_get_by_name(GST_BIN(processing_pipeline), "udpsrc_1");
    g_object_set(udpsrc_1, "socket", udpsrc_gsocks[0], nullptr);

    gst_element_set_state(processing_pipeline, GST_STATE_PLAYING);

    gint64 current_time = g_get_monotonic_time();
    gint64 client_activity_check = current_time;

    while (true)
    {
        // Block until a socket event occurs
        int poll_res = poll(fds, 2, -1);
        if (poll_res < 0)
        {
            std::cerr << "Poll error." << std::endl;
            return 1;
        }

        current_time = g_get_monotonic_time();

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

            // Add client to active clients, if client is not already there.
            if (client_sockaddrs.count(client_sockaddr) == 0)
            {
                client_sockaddrs.insert(client_sockaddr);

                if (udpsrc_sockaddrs_available.size() > 0)
                {
                    // FIXME port is not enough, the whole sockaddr is needed, host, port
                    client_routing[client_sockaddr.sin_port] = udpsrc_sockaddrs_available.back();
                    udpsrc_sockaddrs_available.pop_back();
                }
            }

            client_activity[client_sockaddr.sin_port] = current_time;

            // This is the way to go (maybe without copy), get the address to use with sendto.
            // FIXME Handle the case that no client exists, because of no unused above.
            sockaddr_in udpsrc_sockaddr = client_routing[client_sockaddr.sin_port];
            // FIXME Is it OK to use this socket?
            if (sendto(socket_ext, buffer, bytes_read, 0, (struct sockaddr *)&udpsrc_sockaddr, sizeof(udpsrc_sockaddr)) < 0)
            {
                std::cerr << "Failed to send to GStreamer." << std::endl;
                continue;
            }
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

        if (current_time - client_activity_check > 10000000)
        {
            client_activity_check = current_time;
            std::vector<sockaddr_in> client_sockaddrs_inactive;
            for (const auto &client_sockaddr : client_sockaddrs)
            {
                if (current_time - client_activity[client_sockaddr.sin_port] > 4000000)
                {
                    client_sockaddrs_inactive.push_back(client_sockaddr);
                }
            }
            for (const auto &client_sockaddr : client_sockaddrs_inactive)
            {
                udpsrc_sockaddrs_available.push_back(client_routing[client_sockaddr.sin_port]);
                client_sockaddrs.erase(client_sockaddr);
                client_routing.erase(client_sockaddr.sin_port);
                client_activity.erase(client_sockaddr.sin_port);
            }
            std::cout << "---- Activity check ----" << std::endl;
            std::cout << "------------------------" << std::endl;
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
