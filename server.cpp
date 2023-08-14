#include <arpa/inet.h>
#include <gio/gio.h>
#include <gst/gst.h>
#include <poll.h>
#include <unistd.h>
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <thread>
#include <vector>

// TODO Check whether this should be higher
const int BUFFER_SIZE = 4096;
const int MAX_CLIENTS = 9;

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
std::map<sockaddr_in, sockaddr_in, sockaddr_in_cmp> client_routes;

// Maps client address to last activity time
std::map<sockaddr_in, gint64, sockaddr_in_cmp> client_activity;

// GStreamer pipeline unused udpsrc socket addresses
// Use as a stack? Initialize in reverse?
// TODO Initialize with specific size for efficiency, with reserve?
std::vector<sockaddr_in> udpsrc_sockaddrs_available;

std::vector<int> udpsrc_socks;

std::vector<GSocket *> udpsrc_gsocks;

// Maps udpsrc address to udpsrc index in pipeline
std::map<sockaddr_in, size_t, sockaddr_in_cmp> udpsrc_ixs;

// GStreamer pipeline compositor sink pads in order of creation
std::vector<GstPad *> compositor_pads;

void init_compositor_pads(GstElement *compositor)
{
    GstPad *sink;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        std::string sink_name = std::string("sink_") + std::to_string(i);
        sink = gst_element_get_static_pad(compositor, sink_name.c_str());
        compositor_pads.push_back(sink);
    }
}

// Sequence of {i, j} compositor cell row index and column index pair, in order of usage
std::vector<std::pair<uint8_t, uint8_t>> position_cells;

void init_position_cells(uint16_t cell_width, uint16_t cell_height, float target_aspect_ratio)
{
    uint8_t rows = 1;
    uint8_t cols = 1;
    position_cells.push_back({0, 0});
    while (true)
    {
        if (rows * cols >= MAX_CLIENTS)
            break;
        // Calculation of deviation from target aspect ratio when adding a column and when adding a row, respectively
        float horizontal_expansion_dev = std::abs((float)(cell_width * (cols + 1)) / (float)(cell_height * rows) - target_aspect_ratio);
        float vertical_expansion_dev = std::abs((float)(cell_width * cols) / (float)(cell_height * (rows + 1)) - target_aspect_ratio);
        // Adding a column yields a better aspect ratio
        if (horizontal_expansion_dev < vertical_expansion_dev)
        {
            cols++;
            const uint8_t j = cols - 1;
            for (uint8_t i = 0; i < rows; i++)
            {
                position_cells.push_back({i, j});
            }
        }
        // Adding a row yields a better aspect ratio
        else
        {
            rows++;
            const uint8_t i = rows - 1;
            for (uint8_t j = 0; j < cols; j++)
            {
                position_cells.push_back({i, j});
            }
        }
    }
}

// Sequence of {x, y} compositor cell top-left point x coordinate and y coordinate pair, in order of usage
std::vector<std::pair<uint16_t, uint16_t>> position_points;

void init_position_points()
{
    for (const auto &position_cell : position_cells)
    {
        position_points.push_back({320 * position_cell.second, 240 * position_cell.first});
    }
}

// Available positions for placing clients, as a stack. The next available position to use is taken from the back.
std::vector<size_t> positions_available;

void init_positions_available()
{
    for (int i = MAX_CLIENTS - 1; i >= 0; i--)
    {
        positions_available.push_back(i);
    }
}

// Maps udpsrc address to position
std::map<sockaddr_in, size_t, sockaddr_in_cmp> udpsrc_positions;

uint8_t rows = 0;
uint8_t cols = 0;

void update_grid_size()
{
    uint8_t max_i = 0;
    uint8_t max_j = 0;
    for (const auto &client_sockaddr : client_sockaddrs)
    {
        auto udpsrc_sockaddr = client_routes[client_sockaddr];
        auto udpsrc_position = udpsrc_positions[udpsrc_sockaddr];
        auto position_cell = position_cells[udpsrc_position];
        max_i = std::max(max_i, position_cell.first);
        max_j = std::max(max_j, position_cell.second);
    }
    rows = max_i + 1;
    cols = max_j + 1;
}

void compact_positions()
{
    // Ensure that at least one position is available
    if (positions_available.size())
    {
        // Sort available positions in descending order
        sort(positions_available.begin(), positions_available.end(), std::greater<size_t>());

        auto lowest_position_available = positions_available.back();
        // If the lowest available position is less than the client count, then there is at least one client with a higher position
        if (lowest_position_available < client_sockaddrs.size())
        {
            for (const auto &client_sockaddr : client_sockaddrs)
            {
                auto udpsrc_sockaddr = client_routes[client_sockaddr];
                auto udpsrc_position = udpsrc_positions[udpsrc_sockaddr];

                // If the current client position is greater than the lowest available position, then the latter should be used for the client and the former should become available for later use
                if (udpsrc_position > lowest_position_available)
                {
                    auto udpsrc_ix = udpsrc_ixs[udpsrc_sockaddr];
                    auto pad = compositor_pads[udpsrc_ix];

                    udpsrc_positions[udpsrc_sockaddr] = lowest_position_available;
                    auto position_point = position_points[lowest_position_available];
                    g_object_set(pad, "xpos", position_point.first, "ypos", position_point.second, nullptr);

                    positions_available.pop_back();
                    positions_available.push_back(udpsrc_position);

                    sort(positions_available.begin(), positions_available.end(), std::greater<size_t>());
                    lowest_position_available = positions_available.back();
                }
            }
        }
    }
}

void crop_videobox(uint8_t rows, uint8_t cols, GstElement *capsfilter)
{
    uint16_t width = 320 * cols;
    uint16_t height = 240 * rows;
    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        "width", G_TYPE_INT, width,
                                        "height", G_TYPE_INT, height,
                                        nullptr);
    g_object_set(capsfilter, "caps", caps, nullptr);
}

/**
 * Initializes udpsrc elements.
 */
void init_udpsrcs(GstElement *pipeline)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
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

        std::string udpsrc_name = std::string("udpsrc_") + std::to_string(i);
        GstElement *udpsrc = gst_bin_get_by_name(GST_BIN(pipeline), udpsrc_name.c_str());
        g_object_set(udpsrc, "socket", udpsrc_gsock, nullptr);

        udpsrc_ixs[udpsrc_sockaddr] = i;
        udpsrc_socks.push_back(udpsrc_sock);
        udpsrc_sockaddrs_available.push_back(udpsrc_sockaddr);
        udpsrc_gsocks.push_back(udpsrc_gsock);
    }
}

/**
 * Make pipeline description string.
 */
std::string make_pipeline_desc_str()
{
    std::string compositor_to_udpsink("compositor name=compositor background=black zero-size-is-unscaled=false ! videobox autocrop=true ! capsfilter name=capsfilter caps=\"video/x-raw, width=320, height=240\" ! x264enc tune=zerolatency bitrate=500 speed-preset=superfast ! rtph264pay ! udpsink name=udpsink host=127.0.0.1");
    std::string udpsrc_to_compositor_part_1(" udpsrc name=udpsrc_");
    std::string udpsrc_to_compositor_part_2(" caps=\"application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264, payload=(int)96\" ! rtph264depay ! avdec_h264 ! videoscale ! videoconvert ! video/x-raw, framerate=30/1, width=320, height=240 ! compositor.");
    std::string result = compositor_to_udpsink;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        result += udpsrc_to_compositor_part_1 + std::to_string(i) + udpsrc_to_compositor_part_2;
    }
    return result;
}

int main(int argc, char *argv[])
{
    int server_port = 27884;

    int opt;

    while ((opt = getopt(argc, argv, "p:h")) != -1)
    {
        switch (opt)
        {
        case 'p':
            server_port = atoi(optarg);
            break;
        case 'h':
            fprintf(stderr, "Usage: %s [-p port]\n", argv[0]);
            exit(EXIT_SUCCESS);
        default:
            fprintf(stderr, "Usage: %s [-p port]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    // Initialize GStreamer
    gst_init(nullptr, nullptr);

    std::string pipeline_desc_str = make_pipeline_desc_str();

    // Create pipeline
    GstElement *pipeline = gst_parse_launch(pipeline_desc_str.c_str(), nullptr);

    // Socket for client to server and server to client (two-way) communication
    int server_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_sock < 0)
    {
        std::cerr << "Failed to create server_sock." << std::endl;
        return 1;
    }

    sockaddr_in server_sockaddr{};
    server_sockaddr.sin_family = AF_INET;
    inet_pton(AF_INET, "0.0.0.0", &(server_sockaddr.sin_addr));
    server_sockaddr.sin_port = htons(server_port);

    if (bind(server_sock, (struct sockaddr *)&server_sockaddr, sizeof(server_sockaddr)) < 0)
    {
        std::cerr << "Failed to bind server_sock." << std::endl;
        return 1;
    }

    // Socket for GStreamer pipeline udpsink to server (one-way) communication
    int udpsink_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpsink_sock < 0)
    {
        std::cerr << "Failed to create udpsink_sock." << std::endl;
        return 1;
    }

    sockaddr_in udpsink_sockaddr{};
    udpsink_sockaddr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &(udpsink_sockaddr.sin_addr));
    udpsink_sockaddr.sin_port = htons(0); // Assign any available port

    if (bind(udpsink_sock, (struct sockaddr *)&udpsink_sockaddr, sizeof(udpsink_sockaddr)) < 0)
    {
        std::cerr << "Failed to bind udpsink_sock." << std::endl;
        return 1;
    }

    socklen_t sockaddr_int_len = sizeof(udpsink_sockaddr);
    if (getsockname(udpsink_sock, (struct sockaddr *)&udpsink_sockaddr, &sockaddr_int_len) == -1)
    {
        std::cerr << "Failed to get socket name for udpsink_sock." << std::endl;
        return 1;
    }

    auto udpsink_port = ntohs(udpsink_sockaddr.sin_port);

    GstElement *udpsink = gst_bin_get_by_name(GST_BIN(pipeline), "udpsink");
    g_object_set(udpsink, "port", udpsink_port, nullptr);

    struct pollfd fds[2];

    // TODO Check whether using the same buffer for both sockets is OK and whether it should be outside of the loop
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    // TODO Same check for these
    sockaddr_in client_sockaddr{};
    socklen_t client_sockaddr_len = sizeof(client_sockaddr);

    fds[0].fd = server_sock;
    fds[0].events = POLLIN;
    fds[1].fd = udpsink_sock;
    fds[1].events = POLLIN;

    init_udpsrcs(pipeline);

    GstElement *capsfilter = gst_bin_get_by_name(GST_BIN(pipeline), "capsfilter");

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    init_position_cells(320, 240, 16.0 / 9.0);

    init_position_points();

    init_positions_available();

    GstElement *compositor = gst_bin_get_by_name(GST_BIN(pipeline), "compositor");

    init_compositor_pads(compositor);

    gint64 current_time = g_get_monotonic_time();
    gint64 client_activity_check = current_time;

    std::vector<sockaddr_in> client_sockaddrs_inactive;

    bool has_addition_occurred;
    bool has_removal_occurred;

    while (true)
    {
        // Block until a socket event occurs
        int poll_res = poll(fds, 2, -1);
        if (poll_res == -1)
        {
            std::cerr << "Poll error." << std::endl;
            return 1;
        }

        current_time = g_get_monotonic_time();
        has_addition_occurred = false;
        has_removal_occurred = false;

        // Check whether server_sock has data
        if (fds[0].revents & POLLIN)
        {
            // Receive from client
            bytes_read = recvfrom(server_sock, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_sockaddr, &client_sockaddr_len);
            if (bytes_read < 0)
            {
                std::cerr << "Failed to receive from client." << std::endl;
                continue;
            }

            // Add client to active clients, if client is not already there
            if (client_sockaddrs.count(client_sockaddr) == 0)
            {
                // TODO Handle the opposite case gracefully and do not fail silently
                if (udpsrc_sockaddrs_available.size() > 0)
                {
                    client_sockaddrs.insert(client_sockaddr);

                    auto udpsrc_sockaddr = udpsrc_sockaddrs_available.back();
                    client_routes[client_sockaddr] = udpsrc_sockaddr;

                    auto udpsrc_ix = udpsrc_ixs[udpsrc_sockaddr];
                    auto pad = compositor_pads[udpsrc_ix];

                    auto position = positions_available.back();
                    udpsrc_positions[udpsrc_sockaddr] = position;

                    auto position_cell = position_cells[position];
                    rows = std::max(rows, (uint8_t)(position_cell.first + 1));
                    cols = std::max(cols, (uint8_t)(position_cell.second + 1));

                    auto position_point = position_points[position];

                    g_object_set(pad, "alpha", 1.0, "xpos", position_point.first, "ypos", position_point.second, "width", 320, "height", 240, nullptr);

                    positions_available.pop_back();
                    udpsrc_sockaddrs_available.pop_back();

                    has_addition_occurred = true;
                }
            }

            // If a client route exists, store the client activity time and route to the associated udpsrc_sockaddr
            if (auto client_route = client_routes.find(client_sockaddr); client_route != client_routes.end())
            {
                client_activity[client_sockaddr] = current_time;

                // TODO Check whether it is OK to use server_sock to send
                if (sendto(server_sock, buffer, bytes_read, 0, (struct sockaddr *)&(client_route->second), sizeof(client_route->second)) < 0)
                {
                    std::cerr << "Failed to send to GStreamer." << std::endl;
                    continue;
                }
            }
        }

        // Check whether udpsink_sock has data
        if (fds[1].revents & POLLIN)
        {
            // Receive from GStreamer
            // TODO Check whether the same buffer and struct should be used for GStreamer
            bytes_read = recvfrom(udpsink_sock, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_sockaddr, &client_sockaddr_len);
            if (bytes_read < 0)
            {
                std::cerr << "Failed to receive from GStreamer." << std::endl;
                continue;
            }

            // Send received data from GStreamer to all active clients
            for (const auto &client_sockaddr : client_sockaddrs)
            {
                if (sendto(server_sock, buffer, bytes_read, 0, (struct sockaddr *)&client_sockaddr, sizeof(client_sockaddr)) < 0)
                {
                    std::cerr << "Failed to send to client." << std::endl;
                    continue;
                }
            }
        }

        if (current_time - client_activity_check > 8000000)
        {
            client_activity_check = current_time;
            client_sockaddrs_inactive.clear();

            for (const auto &client_sockaddr : client_sockaddrs)
            {
                if (current_time - client_activity[client_sockaddr] > 2000000)
                {
                    client_sockaddrs_inactive.push_back(client_sockaddr);
                }
            }

            for (const auto &client_sockaddr : client_sockaddrs_inactive)
            {
                auto udpsrc_sockaddr = client_routes[client_sockaddr];
                auto udpsrc_ix = udpsrc_ixs[udpsrc_sockaddr];
                auto pad = compositor_pads[udpsrc_ix];

                g_object_set(pad, "alpha", 0.0, "xpos", 0, "ypos", 0, "width", 0, "height", 0, nullptr);

                auto udpsrc_position = udpsrc_positions[udpsrc_sockaddr];

                positions_available.push_back(udpsrc_position);
                udpsrc_sockaddrs_available.push_back(udpsrc_sockaddr);

                udpsrc_positions.erase(udpsrc_sockaddr);
                client_sockaddrs.erase(client_sockaddr);
                client_routes.erase(client_sockaddr);
                client_activity.erase(client_sockaddr);

                has_removal_occurred = true;
            }

            if (has_removal_occurred)
            {
                compact_positions();
            }
        }

        if (has_addition_occurred || has_removal_occurred)
        {
            update_grid_size();
            crop_videobox(rows, cols, capsfilter);
            std::cout << "---- Active clients ----" << std::endl;
            for (const auto &client_sockaddr : client_sockaddrs)
            {
                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(client_sockaddr.sin_addr), client_ip, INET_ADDRSTRLEN);
                std::cout << client_ip << ":" << ntohs(client_sockaddr.sin_port) << std::endl;
            }
            std::cout << "------------------------" << std::endl;
        }
    }

    close(server_sock);
    close(udpsink_sock);

    return 0;
}
