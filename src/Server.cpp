/// @file Server.cpp
/// @brief Точка входа серверного приложения.

#include <boost/asio.hpp>
#include "Server.hpp"
#include <iostream>
#include <thread>
#include <string>
#include "msg.pb.h"

/**
 * @brief Цикл обработки команд оператора из stdin.
 *
 * Поддерживаемые команды:
 * - @c makeTask @c a @c b @c h — запустить интегрирование на отрезке [a, b] с шагом h.
 *
 * @param server Ссылка на экземпляр сервера.
 */
void worker(Server& server) {
    std::string s;
    while (1) {
        std::cin >> s;
        if (s == "makeTask") {
            double a, b, c;
            std::cin >> a >> b >> c;
            server.sendIntegrationTask(a, b, c);
        }
    }
}

/**
 * @brief Точка входа серверного приложения.
 *
 * Создаёт io_context, запускает сервер и event loop в отдельном потоке,
 * затем передаёт управление циклу команд оператора.
 */
int main() {
    boost::asio::io_context io;
    Server s(io);

    std::thread server_thread([&io]() {
        io.run();
    });

    worker(s);
    server_thread.join();
}
