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
// TODO Rename this to PORT, after the other one below it is discarded
const int PORT_EXT = 62000;

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

// Maps client port to GStreamer pipeline udpsrc address
std::map<u_int, sockaddr_in> map_gst;

std::vector<guint16> ports;

// FIXME This is problematic, do not go at it this way. Also take care of removing idle stuff.
int i = 0;

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

    // Create a new GSocket using the GInetSocketAddress with UDP protocol
    GSocket *socket = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, NULL);

    // Create a GInetSocketAddress with any IP and port 0 to let the system assign a port
    GSocketAddress *address = G_SOCKET_ADDRESS(g_inet_socket_address_new(g_inet_address_new_any(G_SOCKET_FAMILY_IPV4), 0));

    // Bind the socket to the address
    if (g_socket_bind(socket, address, TRUE, NULL) == FALSE)
    {
        g_object_unref(socket);
        g_object_unref(address);
        std::cerr << "Failed to bind the socket to the address." << std::endl;
        return 1;
    }

    // Get the assigned port number
    GSocketAddress *bound_address = g_socket_get_local_address(socket, NULL);
    GInetSocketAddress *bound_inet_address = G_INET_SOCKET_ADDRESS(bound_address);
    guint16 port = g_inet_socket_address_get_port(bound_inet_address);

    std::cout << "Socket bound to port: " << port << std::endl;

    GstElement *udpsrc_0 = gst_bin_get_by_name(GST_BIN(processing_pipeline), "udpsrc_0");
    g_object_set(udpsrc_0, "socket", socket, nullptr);

    ports.push_back(port);

    // Create a new GSocket using the GInetSocketAddress with UDP protocol
    GSocket *socket2 = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, NULL);

    // Create a GInetSocketAddress with any IP and port 0 to let the system assign a port
    GSocketAddress *address2 = G_SOCKET_ADDRESS(g_inet_socket_address_new(g_inet_address_new_any(G_SOCKET_FAMILY_IPV4), 0));

    // Bind the socket to the address
    if (g_socket_bind(socket2, address2, TRUE, NULL) == FALSE)
    {
        g_object_unref(socket2);
        g_object_unref(address2);
        std::cerr << "Failed to bind the socket to the address." << std::endl;
        return 1;
    }

    // Get the assigned port number
    GSocketAddress *bound_address2 = g_socket_get_local_address(socket2, NULL);
    GInetSocketAddress *bound_inet_address2 = G_INET_SOCKET_ADDRESS(bound_address2);
    guint16 port2 = g_inet_socket_address_get_port(bound_inet_address2);

    std::cout << "Socket bound to port: " << port2 << std::endl;

    GstElement *udpsrc_1 = gst_bin_get_by_name(GST_BIN(processing_pipeline), "udpsrc_1");
    g_object_set(udpsrc_1, "socket", socket2, nullptr);

    ports.push_back(port2);

    // Your UDP socket is now ready for use.

    // Don't forget to clean up
    // g_object_unref(socket);
    // g_object_unref(bound_address);
    // g_object_unref(address);

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

            // Add client to active clients, if client is not already there.
            if (client_sockaddrs.count(client_sockaddr) == 0)
            {
                client_sockaddrs.insert(client_sockaddr);

                sockaddr_in sockaddr_udpsrc{};
                sockaddr_udpsrc.sin_family = AF_INET;
                inet_pton(AF_INET, "127.0.0.1", &(sockaddr_udpsrc.sin_addr));
                sockaddr_udpsrc.sin_port = htons(ports[i]);
                // FIXME port is not enough, the whole sockaddr is needed, host, port
                map_gst[client_sockaddr.sin_port] = sockaddr_udpsrc;
                i++;
            }

            // This is the way to go (maybe without copy), get the address to use with sendto.
            sockaddr_in sockaddr_udpsrc_gst = map_gst[client_sockaddr.sin_port];

            if (sendto(socket_ext, buffer, bytes_read, 0, (struct sockaddr *)&sockaddr_udpsrc_gst, sizeof(sockaddr_udpsrc_gst)) < 0)
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
