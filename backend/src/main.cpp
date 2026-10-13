#include "service.hpp"
#include <iostream>
#include <thread>
int main() {
    try {
        if (mysql_library_init(0, nullptr, nullptr)) throw std::runtime_error("MySQL client init failed");
        bootstrap_passwords();
        httplib::Server server;
        server.new_task_queue = [] { return new httplib::ThreadPool(16); };
        server.set_payload_max_length(1024 * 128);
        server.set_read_timeout(5, 0);
        server.set_write_timeout(5, 0);
        server.set_keep_alive_max_count(50);
        configure_routes(server);
        if (!server.set_mount_point("/", env("FRONTEND_DIR", "frontend"))) throw std::runtime_error("frontend directory missing");
        std::atomic<bool> stop{false};
        std::thread worker([&] { billing_loop(stop); });
        int port = std::stoi(env("PORT", "8080"));
        std::cout << "Course registration listening on port " << port << std::endl;
        bool ok = server.listen(env("BIND_HOST", "127.0.0.1"), port);
        stop = true; worker.join();
        mysql_library_end();
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Startup error: " << e.what() << std::endl; return 1;
    }
}
