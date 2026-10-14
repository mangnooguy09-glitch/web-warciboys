#include <iostream>
#include <fstream>
#include <sstream>
#include "httplib.h" // Pastikan file header httplib.h ada di folder yang sama

// Fungsi untuk membaca file index.html
std::string readFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return "<h1>404 File Not Found</h1>";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main() {
    httplib::Server svr;

    // Menghubungkan endpoint root ("/") ke file index.html
    svr.Get("/", [](const httplib::Request& req, httplib::Response& res) {
        std::string htmlContent = readFile("warciboys.html"); // <- Diubah ke warciboys.html
        res.set_content(htmlContent, "text/html; charset=utf-8");
});

    int port = 8080;
    const char* env_port = std::getenv("PORT");
    if (env_port) {
        port = std::stoi(env_port);
    }

    std::cout << "Web Kenangan Warci Boys aktif di http://localhost:" << port << std::endl;
    svr.listen("0.0.0.0", port);

    return 0;
}