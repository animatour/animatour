#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <set>
#include <thread>

const int BUFFER_SIZE = 1024;
const int PORT_EXT = 62000;
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

void dispatch()
{
    // External socket that clients send to
    int socket_ext = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ext < 0)
    {
        std::cerr << "Failed to create socket_ext." << std::endl;
        return;
    }

    sockaddr_in sockaddr_ext{};
    sockaddr_ext.sin_family = AF_INET;
    sockaddr_ext.sin_addr.s_addr = INADDR_ANY;
    sockaddr_ext.sin_port = htons(PORT_EXT);

    if (bind(socket_ext, (struct sockaddr *)&sockaddr_ext, sizeof(sockaddr_ext)) < 0)
    {
        std::cerr << "Failed to bind socket_ext." << std::endl;
        return;
    }

    // Internal socket that GStreamer sends to
    int socket_int = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_int < 0)
    {
        std::cerr << "Failed to create socket_int." << std::endl;
        return;
    }

    sockaddr_in sockaddr_int{};
    sockaddr_int.sin_family = AF_INET;
    sockaddr_int.sin_addr.s_addr = INADDR_LOOPBACK;
    sockaddr_int.sin_port = htons(PORT_INT);

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    sockaddr_in client_sockaddr{};
    socklen_t client_sockaddr_len = sizeof(client_sockaddr);

    while (true)
    {
        // Receive from client
        bytes_read = recvfrom(socket_ext, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_sockaddr, &client_sockaddr_len);
        if (bytes_read < 0)
        {
            std::cerr << "Failed to receive." << std::endl;
            continue;
        }

        // Print sending client info
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_sockaddr.sin_addr), client_ip, INET_ADDRSTRLEN);
        std::cout << "Received from client: " << client_ip << ":" << ntohs(client_sockaddr.sin_port) << std::endl;

        // Add client to active clients, if client is not already there.
        if (client_sockaddrs.count(client_sockaddr) == 0)
        {
            client_sockaddrs.insert(client_sockaddr);
        }

        // Send received data to all active clients
        // FIXME Do not use socket_int
        for (const auto &client_sockaddr : client_sockaddrs)
        {
            if (sendto(socket_int, buffer, bytes_read, 0, (struct sockaddr *)&client_sockaddr, sizeof(client_sockaddr)) < 0)
            {
                std::cerr << "Failed to send." << std::endl;
                continue;
            }
        }

        // Print active clients
        std::cout << "-------- Active clients --------" << std::endl;
        for (const auto &client_sockaddr : client_sockaddrs)
        {
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(client_sockaddr.sin_addr), client_ip, INET_ADDRSTRLEN);
            std::cout << client_ip << ":" << ntohs(client_sockaddr.sin_port) << std::endl;
        }
        std::cout << "--------------------------------" << std::endl;
    }

    close(socket_ext);
    close(socket_int);
}

int main()
{
    std::thread dispatch_thread(dispatch);
    dispatch_thread.join();

    return 0;
}
