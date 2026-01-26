#include "graia.hpp"
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
    #include <cerrno>
    #include <cstring>
#endif

#include <memory>
#include <chrono>
#include <iostream>
#include <vector>

// 'Dummy' capture function: all in user space for now. recv() → copy to userspace buffer → RotatingWriter::write()
int capture(const CapturePrm prm, CaptureResult& result)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return -1;
    }
#endif

    // Create UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "socket() failed\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return -1;
    }

    // Bind to port (INADDR_ANY - ignore interface/ip params for now)
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(prm.port);

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        std::cerr << "bind() failed on port " << prm.port << "\n";
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return -1;
    }

    // Create writer
    IPacketWriter* writer = create_rotating_writer(prm);
    if (!writer) {
        std::cerr << "create_rotating_writer() failed\n";
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return -1;
    }

    std::cout << "Listening on port " << prm.port << ", packet size " << PACKET_SZ << " bytes\n";

    // Capture loop
    std::vector<char> pkt_buf(PACKET_SZ);
    auto t_start = std::chrono::steady_clock::now();

    while (true) {
        int recvd = recv(sock, pkt_buf.data(), PACKET_SZ, 0);
        
        if (recvd < 0) {
            std::cerr << "recv() error\n";
            break;
        }

        if (recvd != static_cast<int>(PACKET_SZ)) {
            std::cerr << "recv() returned " << recvd << " bytes, expected " << PACKET_SZ << "\n";
            continue; // skip malformed packet
        }

        writer->write(pkt_buf.data());

        // Check limit
        if (prm.capture_limit > 0 && writer->total_written_u64() >= prm.capture_limit) {
            std::cout << "\nCapture limit reached: " << prm.capture_limit << " bytes\n";
            break;
        }
    }
    writer->flush();
    auto t_end = std::chrono::steady_clock::now();
    // Fill result
    result.total_captured = writer->total_written_u64();
    result.elapsed_seconds = std::chrono::duration<double>(t_end - t_start).count();
    result.capture_rate_mbps = (result.total_captured * 8.0) / (result.elapsed_seconds * 1'000'000.0);
    result.files_created = writer->files_count();
    // Flush and cleanup

    delete writer;

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
    return 0;
}


// Capture function: (AF_PACKET + PACKET_MMAP): Ring buffer in kernel space, userspace reads via mmap without copy → RotatingWriter::write()
// NOT IMPLEMENTED YET - placeholder for future AF_PACKET + PACKET_MMAP implementation
// Requirements:
// - Linux-only (AF_PACKET with PACKET_MMAP)
// - Zero-copy packet capture from kernel ring buffer
// - mmap() interface for userspace access
// - Direct write to RotatingWriter without intermediate copy
//
// Implementation notes:
// - Must setup ring buffer with tp_block_size aligned to page size
// - tp_frame_size should match or exceed PACKET_SZ
// - Poll/wait for packets in ring buffer
// - Process packets directly from mapped memory
// - Call writer->write() for each packet
int capture_mk1(const CapturePrm prm, CaptureResult& result)
{
    (void)prm;     // suppress unused parameter warning
    (void)result;  // suppress unused parameter warning
    
    // TODO: Implement AF_PACKET + PACKET_MMAP capture
    // Return error code indicating not implemented
    std::cerr << "capture_mk1: AF_PACKET+PACKET_MMAP not implemented yet\n";
    return -1;
}

// Capture function: Socket fd → pipe → file fd via splice() syscall (Linux)
// NOT IMPLEMENTED YET - placeholder for future splice()-based zero-copy implementation
// Requirements:
// - Linux-only (splice() syscall)
// - Zero-copy data transfer from socket → pipe → file
// - No userspace buffer copies
// - Direct kernel-to-kernel data movement
//
// Implementation notes:
// - Create pipe with pipe2()
// - splice() from socket fd to pipe write end
// - splice() from pipe read end to file fd
// - Handle splice() partial transfers and EAGAIN
// - File rotation will require closing/opening file fd between splices
// - May need fallback to regular recv() for packet boundaries if splice doesn't preserve them
int capture_mk2(const CapturePrm prm, CaptureResult& result)
{
    (void)prm;     // suppress unused parameter warning
    (void)result;  // suppress unused parameter warning
    
    // TODO: Implement splice()-based zero-copy capture
    // Return error code indicating not implemented
    std::cerr << "capture_mk2: splice()-based capture not implemented yet\n";
    return -1;
}
