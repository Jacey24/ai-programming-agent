#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
void close_socket(int fd) {
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

bool initialize_sockets() {
#ifdef _WIN32
    WSADATA wsa_data;
    return WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
#else
    return true;
#endif
}

void cleanup_sockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

std::string http_get(const std::string& host, const std::string& port, const std::string& path) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0) {
        return "Failed to resolve backend host";
    }

    int socket_fd = -1;
    for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        socket_fd = static_cast<int>(socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol));
        if (socket_fd < 0) {
            continue;
        }

        if (connect(socket_fd, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == 0) {
            break;
        }

        close_socket(socket_fd);
        socket_fd = -1;
    }

    freeaddrinfo(result);

    if (socket_fd < 0) {
        return "Failed to connect to backend";
    }

    std::ostringstream request;
    request << "GET " << path << " HTTP/1.1\r\n"
            << "Host: " << host << "\r\n"
            << "Connection: close\r\n\r\n";

    const std::string request_text = request.str();
    send(socket_fd, request_text.data(), static_cast<int>(request_text.size()), 0);

    std::string response;
    char buffer[4096] = {};
    for (;;) {
        const int received = static_cast<int>(recv(socket_fd, buffer, sizeof(buffer), 0));
        if (received <= 0) {
            break;
        }
        response.append(buffer, static_cast<std::size_t>(received));
    }

    close_socket(socket_fd);
    return response;
}
}

int run_tui_application() {
    const std::string host = "codepilot-server";
    const std::string fallback_host = "127.0.0.1";
    const std::string port = "8080";
    const std::string path = "/api/v1/health";

    if (!initialize_sockets()) {
        std::cerr << "Failed to initialize sockets\n";
        return 1;
    }

    std::cout << "CodePilot TUI client\n";
    std::cout << "Checking backend: http://" << host << ":" << port << path << "\n";

    std::string response = http_get(host, port, path);
    if (response == "Failed to resolve backend host" || response == "Failed to connect to backend") {
        std::cout << "Docker service name unavailable, trying localhost\n";
        response = http_get(fallback_host, port, path);
    }

    cleanup_sockets();

    std::cout << response << "\n";
    return response.find("\"status\":\"ok\"") != std::string::npos ? 0 : 1;
}
