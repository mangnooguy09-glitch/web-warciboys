#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

// ===============================
// STATE GLOBAL (aman untuk multi-thread)
// ===============================
std::atomic<long long> pageViews{0};        // berapa kali halaman utama dibuka
std::atomic<long long> totalRequests{0};    // semua request yang masuk
std::atomic<int> activeConnections{0};      // koneksi yang sedang diproses
std::mutex logMutex;                        // supaya log tidak bercampur antar thread
const auto startTime = std::chrono::steady_clock::now();

// ===============================
// FUNGSI MENGIRIM DATA
// ===============================
bool sendAll(SOCKET socket, const char* data, int length) {
    int totalSent = 0;
    while (totalSent < length) {
        int sent = send(socket, data + totalSent, length - totalSent, 0);
        if (sent == SOCKET_ERROR) return false;
        totalSent += sent;
    }
    return true;
}

// ===============================
// BACA FILE DARI DISK
// ===============================
bool readFile(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream ss;
    ss << file.rdbuf();
    out = ss.str();
    return true;
}

// ===============================
// BUAT RESPON HTTP
// ===============================
std::string buildResponse(const std::string& status,
                          const std::string& contentType,
                          const std::string& body) {
    std::string res = "HTTP/1.1 " + status + "\r\n";
    res += "Content-Type: " + contentType + "\r\n";
    res += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    res += "Connection: close\r\n\r\n";
    res += body;
    return res;
}

// ===============================
// AMBIL METHOD DAN PATH DARI REQUEST
// contoh baris pertama: "GET /status?x=1 HTTP/1.1"
// ===============================
void parseRequestLine(const std::string& request, std::string& method, std::string& path) {
    method = "";
    path = "/";

    size_t lineEnd = request.find("\r\n");
    std::string line = request.substr(0, lineEnd);

    size_t sp1 = line.find(' ');
    if (sp1 == std::string::npos) return;
    size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return;

    method = line.substr(0, sp1);
    path = line.substr(sp1 + 1, sp2 - sp1 - 1);

    size_t q = path.find('?');       // buang query string
    if (q != std::string::npos) path = path.substr(0, q);
}

// ===============================
// ROUTING: tentukan respon berdasarkan path
// ===============================
std::string handleRoute(const std::string& method, const std::string& path) {
    if (method != "GET") {
        return buildResponse("405 Method Not Allowed", "text/plain; charset=UTF-8",
                             "405 - Method tidak diizinkan");
    }

    if (path == "/" || path == "/index.html") {
        std::string html;
        if (!readFile("index.html", html)) {
            return buildResponse("500 Internal Server Error", "text/plain; charset=UTF-8",
                                 "500 - File index.html tidak ditemukan. Taruh di folder yang sama dengan program.");
        }
        pageViews++;
        return buildResponse("200 OK", "text/html; charset=UTF-8", html);
    }

    if (path == "/status") {
        auto now = std::chrono::steady_clock::now();
        long long uptime = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();

        std::ostringstream json;
        json << "{\n"
             << "  \"status\": \"online\",\n"
             << "  \"server\": \"C++ Winsock\",\n"
             << "  \"uptime_seconds\": " << uptime << ",\n"
             << "  \"page_views\": " << pageViews.load() << ",\n"
             << "  \"total_requests\": " << totalRequests.load() << ",\n"
             << "  \"active_connections\": " << activeConnections.load() << "\n"
             << "}";
        return buildResponse("200 OK", "application/json; charset=UTF-8", json.str());
    }

    return buildResponse("404 Not Found", "text/plain; charset=UTF-8",
                         "404 - Halaman tidak ditemukan: " + path);
}

// ===============================
// PENANGANAN SATU KLIEN (jalan di thread sendiri)
// ===============================
void handleClient(SOCKET clientSocket, std::string clientIp) {
    activeConnections++;
    totalRequests++;

    char buffer[8192];
    int received = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

    if (received > 0) {
        buffer[received] = '\0';
        std::string request(buffer, received);

        std::string method, path;
        parseRequestLine(request, method, path);

        {
            std::lock_guard<std::mutex> lock(logMutex);
            std::cout << "[" << clientIp << "] " << method << " " << path << "\n";
        }

        std::string response = handleRoute(method, path);
        sendAll(clientSocket, response.c_str(), (int)response.size());
    }

    closesocket(clientSocket);
    activeConnections--;
}

// ===============================
// MAIN
// ===============================
int main() {
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Gagal inisialisasi Winsock.\n";
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Gagal membuat socket.\n";
        WSACleanup();
        return 1;
    }

    // Supaya port 8080 bisa dipakai lagi langsung setelah program ditutup
    BOOL reuse = TRUE;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(8080);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Gagal melakukan bind ke port 8080.\n";
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Gagal listening.\n";
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "===========================================\n";
    std::cout << " Server berjalan di http://localhost:8080\n";
    std::cout << " Route: /  dan  /status\n";
    std::cout << "===========================================\n";

    while (true) {
        sockaddr_in clientAddr;
        int clientAddrSize = sizeof(clientAddr);

        SOCKET clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientAddrSize);
        if (clientSocket == INVALID_SOCKET) continue;

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(clientAddr.sin_addr), ip, INET_ADDRSTRLEN);

        // Satu thread per koneksi, langsung dilepas (detach)
        std::thread(handleClient, clientSocket, std::string(ip)).detach();
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
