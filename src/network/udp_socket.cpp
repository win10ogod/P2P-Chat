#include "network/udp_socket.h"

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using socklen_t = int;
#else
  #include <arpa/inet.h>
  #include <cerrno>
  #include <fcntl.h>
  #include <ifaddrs.h>
  #include <netinet/in.h>
  #include <poll.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

#include <cstring>

namespace p2p {

// ─── Platform Helpers ────────────────────────────────────────────────────────

#ifdef _WIN32
namespace {
struct WinsockInit {
    WinsockInit()  { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); }
    ~WinsockInit() { WSACleanup(); }
};
static WinsockInit g_wsa;
} // anonymous namespace
#endif

static void platform_close(int fd) {
#ifdef _WIN32
    closesocket(static_cast<SOCKET>(fd));
#else
    ::close(fd);
#endif
}

static void platform_set_nonblocking(int fd) {
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

// ─── Lifecycle ───────────────────────────────────────────────────────────────

UdpSocket::UdpSocket() = default;

UdpSocket::~UdpSocket() {
    stop_receive();
    close();
}

UdpSocket::UdpSocket(UdpSocket&& o) noexcept
    : fd_(o.fd_), local_port_(o.local_port_), receiving_(o.receiving_.load()),
      handler_(std::move(o.handler_)) {
    o.fd_ = kInvalidSocket;
    o.local_port_ = 0;
    o.receiving_ = false;
    // Note: recv_thread_ is NOT moved. The source must have stopped receiving.
}

UdpSocket& UdpSocket::operator=(UdpSocket&& o) noexcept {
    if (this != &o) {
        stop_receive();
        close();
        fd_ = o.fd_;
        local_port_ = o.local_port_;
        receiving_ = o.receiving_.load();
        handler_ = std::move(o.handler_);
        o.fd_ = kInvalidSocket;
        o.local_port_ = 0;
        o.receiving_ = false;
    }
    return *this;
}

// ─── Bind ────────────────────────────────────────────────────────────────────

std::optional<Endpoint> UdpSocket::bind(uint16_t port) {
    if (fd_ != kInvalidSocket) return std::nullopt; // Already bound

    fd_ = static_cast<SocketHandle>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (fd_ == kInvalidSocket) return std::nullopt;

    int opt = 1;
    (void)setsockopt(static_cast<int>(fd_), SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(static_cast<int>(fd_), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close();
        return std::nullopt;
    }

    socklen_t len = sizeof(addr);
    if (getsockname(static_cast<int>(fd_), reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
        close();
        return std::nullopt;
    }
    local_port_ = ntohs(addr.sin_port);

    platform_set_nonblocking(static_cast<int>(fd_));
    return Endpoint{get_local_ip(), local_port_};
}

// ─── Send ────────────────────────────────────────────────────────────────────

int UdpSocket::send_to(const std::vector<uint8_t>& data, const Endpoint& target) {
    return send_to(data.data(), data.size(), target);
}

int UdpSocket::send_to(const uint8_t* data, size_t len, const Endpoint& target) {
    if (fd_ == kInvalidSocket || !target.is_valid()) return -1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(target.port);
    if (inet_pton(AF_INET, target.ip.c_str(), &addr.sin_addr) != 1) return -1;

    auto sent = ::sendto(static_cast<int>(fd_), reinterpret_cast<const char*>(data),
                         static_cast<int>(len), 0,
                         reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return static_cast<int>(sent);
}

// ─── Receive ─────────────────────────────────────────────────────────────────

void UdpSocket::start_receive(ReceiveHandler handler) {
    if (receiving_) return;
    handler_ = std::move(handler);
    receiving_ = true;
    recv_thread_ = std::thread(&UdpSocket::receive_loop, this);
}

void UdpSocket::stop_receive() {
    receiving_ = false;
    if (recv_thread_.joinable()) recv_thread_.join();
}

void UdpSocket::receive_loop() {
    std::vector<uint8_t> buf(config::kMaxPacketSize);

    while (receiving_) {
#ifdef _WIN32
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(static_cast<SOCKET>(fd_), &fds);
        timeval tv{0, 50000}; // 50ms
        int ready = select(0, &fds, nullptr, nullptr, &tv);
#else
        pollfd pfd{};
        pfd.fd = static_cast<int>(fd_);
        pfd.events = POLLIN;
        int ready = poll(&pfd, 1, 50); // 50ms
#endif
        if (ready <= 0) continue;

        sockaddr_in sa{};
        socklen_t sa_len = sizeof(sa);
        auto n = ::recvfrom(static_cast<int>(fd_), reinterpret_cast<char*>(buf.data()),
                            static_cast<int>(buf.size()), 0,
                            reinterpret_cast<sockaddr*>(&sa), &sa_len);
        if (n > 0 && handler_) {
            Datagram dg;
            dg.data.assign(buf.begin(), buf.begin() + n);
            char ip[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
            dg.sender = Endpoint{ip, ntohs(sa.sin_port)};
            handler_(std::move(dg));
        }
    }
}

// ─── Utility ─────────────────────────────────────────────────────────────────

bool UdpSocket::is_open() const noexcept { return fd_ != kInvalidSocket; }

void UdpSocket::close() {
    if (fd_ != kInvalidSocket) {
        platform_close(static_cast<int>(fd_));
        fd_ = kInvalidSocket;
        local_port_ = 0;
    }
}

std::string UdpSocket::get_local_ip() {
#ifdef _WIN32
    char hostname[256]{};
    gethostname(hostname, sizeof(hostname));
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(hostname, nullptr, &hints, &res) == 0 && res) {
        char ip[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET,
                  &reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr,
                  ip, sizeof(ip));
        freeaddrinfo(res);
        return ip;
    }
    return "127.0.0.1";
#else
    ifaddrs* addrs = nullptr;
    if (getifaddrs(&addrs) != 0) return "127.0.0.1";

    std::string best = "127.0.0.1";
    for (auto* ifa = addrs; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        char ip[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET,
                  &reinterpret_cast<sockaddr_in*>(ifa->ifa_addr)->sin_addr,
                  ip, sizeof(ip));
        std::string ip_str(ip);
        if (ip_str != "127.0.0.1") { best = ip_str; break; }
    }
    freeifaddrs(addrs);
    return best;
#endif
}

} // namespace p2p
