// sock_reader.hpp
#pragma once

#include "stream_reader.hpp"
#include <string>
#include <cstring>
#include <stdexcept>
#include <iostream>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <linux/if_packet.h>
    #include <linux/if_ether.h>
    #include <linux/ip.h>
    #include <linux/udp.h>
    #include <linux/filter.h>
    #include <net/if.h>
    #include <sys/ioctl.h>
#endif

// Socket receive buffer size
constexpr int SOCKET_RCVBUF_SIZE = 4 * 1024 * 1024;  // 4 MiB

// Exception types
class ReadTimeout : public std::runtime_error {
public:
    explicit ReadTimeout(const std::string& msg) : std::runtime_error(msg) {}
};

class SocketError : public std::runtime_error {
public:
    explicit SocketError(const std::string& msg) : std::runtime_error(msg) {}
};

#ifdef _WIN32
// Windows-specific: WSA initialization helper
class WSAInitializer {
public:
    WSAInitializer() {
        WSADATA wsa_data;
        int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (result != 0) {
            throw SocketError("WSAStartup failed: " + std::to_string(result));
        }
    }
    
    ~WSAInitializer() {
        WSACleanup();
    }
    
    // Singleton instance
    static WSAInitializer& instance() {
        static WSAInitializer inst;
        return inst;
    }
};

inline std::string get_last_socket_error() {
    int err = WSAGetLastError();
    return "WSA error " + std::to_string(err);
}

inline void close_socket(SOCKET sock) {
    closesocket(sock);
}

constexpr SOCKET INVALID_SOCKET_FD = INVALID_SOCKET;

#else

inline std::string get_last_socket_error() {
    return std::string(strerror(errno));
}

inline void close_socket(int sock) {
    close(sock);
}

constexpr int INVALID_SOCKET_FD = -1;

#endif

// Template socket reader implementation
template<bool IS_RAW>
class SocketReaderImpl : public I_STREAM_READER {
private:
#ifdef _WIN32
    SOCKET sock_fd_;
#else
    int sock_fd_;
#endif
    
    std::string ip_;
    uint16_t port_;
    std::string dev_;
    int32_t timeout_ms_;
    size_t chunk_size_;
    
    // Temporary buffer for frame reading
    static constexpr size_t MAX_FRAME_SIZE = 65536;
    uint8_t frame_buffer_[MAX_FRAME_SIZE];
    
    void setup_socket() {
#ifdef _WIN32
        // Initialize WSA (singleton, only once)
        WSAInitializer::instance();
#endif
        
        if constexpr (IS_RAW) {
#ifdef _WIN32
            // Raw sockets not supported on Windows
            throw SocketError("Raw sockets (IS_RAW=true) are not supported on Windows");
#else
            // Raw socket (AF_PACKET) - Linux only
            sock_fd_ = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
#endif
        } else {
            // Regular UDP socket
#ifdef _WIN32
            sock_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
            sock_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
#endif
        }
        
        if (sock_fd_ == INVALID_SOCKET_FD) {
            throw SocketError("Failed to create socket: " + get_last_socket_error());
        }
    }
    
    void set_buffer_size() {
#ifdef _WIN32
        // Windows: use SO_RCVBUF directly
        if (setsockopt(sock_fd_, SOL_SOCKET, SO_RCVBUF,
                       (const char*)&SOCKET_RCVBUF_SIZE, sizeof(SOCKET_RCVBUF_SIZE)) == SOCKET_ERROR) {
            std::cerr << "Warning: Failed to set socket receive buffer to " 
                      << SOCKET_RCVBUF_SIZE << " bytes: " << get_last_socket_error() << "\n";
        }
#else
        // Linux: try SO_RCVBUFFORCE first, fallback to SO_RCVBUF
        if (setsockopt(sock_fd_, SOL_SOCKET, SO_RCVBUFFORCE, 
                       &SOCKET_RCVBUF_SIZE, sizeof(SOCKET_RCVBUF_SIZE)) == -1) {
            if (setsockopt(sock_fd_, SOL_SOCKET, SO_RCVBUF,
                           &SOCKET_RCVBUF_SIZE, sizeof(SOCKET_RCVBUF_SIZE)) == -1) {
                std::cerr << "Warning: Failed to set socket receive buffer to " 
                          << SOCKET_RCVBUF_SIZE << " bytes. "
                          << "Using system default. Performance may be degraded.\n";
            } else {
                std::cerr << "Warning: SO_RCVBUFFORCE failed (CAP_NET_ADMIN required). "
                          << "Buffer size limited by net.core.rmem_max.\n";
            }
        }
#endif
    }
    
void setup_bpf_filter() {
    if constexpr (IS_RAW) {
#ifndef _WIN32
        // BPF filter: IPv4 + UDP only (port filtering in userspace)
        struct sock_filter bpf_code[] = {
            // [0] EtherType == IPv4?
            BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 12),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x0800, 0, 3),
            
            // [2] IP protocol == UDP?
            BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 23),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, IPPROTO_UDP, 0, 1),
            
            // [4] Accept
            BPF_STMT(BPF_RET | BPF_K, 65535),
            
            // [5] Reject
            BPF_STMT(BPF_RET | BPF_K, 0),
        };
        
        struct sock_fprog bpf = {
            .len = sizeof(bpf_code) / sizeof(bpf_code[0]),
            .filter = bpf_code,
        };
        
        if (setsockopt(sock_fd_, SOL_SOCKET, SO_ATTACH_FILTER, &bpf, sizeof(bpf)) == -1) {
            close_socket(sock_fd_);
            throw SocketError("Failed to attach BPF filter: " + get_last_socket_error());
        }
#endif
    }
}

    
    void bind_socket() {
        if constexpr (IS_RAW) {
#ifndef _WIN32
            // Bind to device for raw socket (Linux only)
            if (setsockopt(sock_fd_, SOL_SOCKET, SO_BINDTODEVICE, 
                           dev_.c_str(), dev_.length()) == -1) {
                close_socket(sock_fd_);
                throw SocketError("Failed to bind to device " + dev_ + ": " + 
                                  get_last_socket_error());
            }
            
            // Get interface index
            struct ifreq ifr;
            std::memset(&ifr, 0, sizeof(ifr));
            std::strncpy(ifr.ifr_name, dev_.c_str(), IFNAMSIZ - 1);
            
            if (ioctl(sock_fd_, SIOCGIFINDEX, &ifr) == -1) {
                close_socket(sock_fd_);
                throw SocketError("Failed to get interface index for " + dev_ + ": " + 
                                  get_last_socket_error());
            }
            
            // Bind to interface
            struct sockaddr_ll sll;
            std::memset(&sll, 0, sizeof(sll));
            sll.sll_family = AF_PACKET;
            sll.sll_ifindex = ifr.ifr_ifindex;
            sll.sll_protocol = htons(ETH_P_ALL);
            
            if (bind(sock_fd_, (struct sockaddr*)&sll, sizeof(sll)) == -1) {
                close_socket(sock_fd_);
                throw SocketError("Failed to bind raw socket: " + get_last_socket_error());
            }
#endif
        } else {
            // Bind by IP for regular socket (Windows + Linux)
            struct sockaddr_in addr;
            std::memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port_);
            
            if (inet_pton(AF_INET, ip_.c_str(), &addr.sin_addr) != 1) {
                close_socket(sock_fd_);
                throw SocketError("Invalid IP address: " + ip_);
            }
            
            if (bind(sock_fd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
                close_socket(sock_fd_);
                throw SocketError("Failed to bind socket to " + ip_ + ":" + 
                                  std::to_string(port_) + ": " + get_last_socket_error());
            }
        }
    }
    
    void set_timeout() {
        if (timeout_ms_ > 0) {
#ifdef _WIN32
            // Windows uses milliseconds directly
            DWORD timeout = static_cast<DWORD>(timeout_ms_);
            if (setsockopt(sock_fd_, SOL_SOCKET, SO_RCVTIMEO, 
                           (const char*)&timeout, sizeof(timeout)) == SOCKET_ERROR) {
                close_socket(sock_fd_);
                throw SocketError("Failed to set socket timeout: " + get_last_socket_error());
            }
#else
            // Linux uses struct timeval
            struct timeval tv;
            tv.tv_sec = timeout_ms_ / 1000;
            tv.tv_usec = (timeout_ms_ % 1000) * 1000;
            
            if (setsockopt(sock_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
                close_socket(sock_fd_);
                throw SocketError("Failed to set socket timeout: " + get_last_socket_error());
            }
#endif
        }
    }

public:
    SocketReaderImpl(const std::string& ip, 
                     uint16_t port,
                     const std::string& dev,
                     int32_t timeout_ms,
                     size_t chunk_size)
        : sock_fd_(INVALID_SOCKET_FD)
        , ip_(ip)
        , port_(port)
        , dev_(dev)
        , timeout_ms_(timeout_ms)
        , chunk_size_(chunk_size)
    {
        setup_socket();
        set_buffer_size();
        setup_bpf_filter();
        bind_socket();
        set_timeout();
    }
    
    ~SocketReaderImpl() {
        if (sock_fd_ != INVALID_SOCKET_FD) {
            close_socket(sock_fd_);
        }
    }
    
    // Delete copy/move (socket ownership)
    SocketReaderImpl(const SocketReaderImpl&) = delete;
    SocketReaderImpl& operator=(const SocketReaderImpl&) = delete;
    SocketReaderImpl(SocketReaderImpl&&) = delete;
    SocketReaderImpl& operator=(SocketReaderImpl&&) = delete;
    
    size_t get_chunk_size() const noexcept override {
        return chunk_size_;
    }
    
    std::string get_type() const noexcept override {
        if constexpr (IS_RAW) {
            return "SocketReader<RAW>";
        } else {
            return "SocketReader<UDP>";
        }
    }
    
    size_t read_into(uint8_t* buff) override {
        while (true) {  // ← Loop until correct port packet
            ssize_t recv_bytes;
            
            if constexpr (IS_RAW) {
    #ifndef _WIN32
                // Raw socket: receive full Ethernet frame
                recv_bytes = recvfrom(sock_fd_, frame_buffer_, MAX_FRAME_SIZE, 0, nullptr, nullptr);
                
                if (recv_bytes == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        throw ReadTimeout("Socket receive timeout expired");
                    } else if (errno == EINTR) {
                        return 0;  // Interrupted (Ctrl+C)
                    } else {
                        throw SocketError("recvfrom() failed: " + get_last_socket_error());
                    }
                }
                
                // Parse Ethernet frame
                if (static_cast<size_t>(recv_bytes) < 14 + 20 + 8) {
                    throw SocketError("Received frame too small for UDP packet");
                }
                
                // Skip Ethernet header (14 bytes)
                uint8_t* ip_header = frame_buffer_ + 14;
                
                // Get IP header length (IHL field)
                uint8_t ip_header_len = (ip_header[0] & 0x0F) * 4;
                
                if (ip_header_len < 20) {
                    throw SocketError("Invalid IP header length: " + std::to_string(ip_header_len));
                }
                
                // UDP header at: ip_header + ip_header_len
                uint8_t* udp_header = ip_header + ip_header_len;
                
                // ====== USERSPACE PORT CHECK ======
                // UDP header: [0-1] src port, [2-3] dest port
                uint16_t udp_dest_port = (udp_header[2] << 8) | udp_header[3];  // Big-endian
                
                if (udp_dest_port != port_) {
                    // Wrong port - skip this packet and read next
                    continue;  // ← Loop back to recvfrom
                }
                // ==================================
                
                // Skip UDP header (8 bytes)
                uint8_t* udp_payload = udp_header + 8;
                
                // Calculate payload size
                size_t total_headers = 14 + ip_header_len + 8;
                if (static_cast<size_t>(recv_bytes) < total_headers) {
                    throw SocketError("Frame size mismatch in header parsing");
                }
                
                size_t payload_len = recv_bytes - total_headers;
                
                // Copy UDP payload to caller's buffer
                std::memcpy(buff, udp_payload, payload_len);
                return payload_len;  // ← Exit loop with correct packet
    #else
                throw SocketError("Raw socket not supported on Windows");
    #endif
                
            } else {
                // Regular UDP socket branch (unchanged)
                // ...
            }
        }  // ← End of while(true) loop
    }

};

// Wrapper function for factory
inline I_STREAM_READER* create_socket_reader(
    const std::string& ip,
    uint16_t port,
    const std::string& dev,
    int32_t timeout_ms,
    size_t chunk_size,
    bool is_raw)
{
    if (is_raw) {
        return new SocketReaderImpl<true>(ip, port, dev, timeout_ms, chunk_size);
    } else {
        return new SocketReaderImpl<false>(ip, port, dev, timeout_ms, chunk_size);
    }
}