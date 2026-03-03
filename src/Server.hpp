#ifndef SERVER_HPP
#define SERVER_HPP

#include <boost/asio.hpp>
#include <iostream>
#include <mutex>
#include <algorithm>
#include "msg.pb.h"
#include "common.hpp"

using boost::asio::ip::tcp;

/**
 * @brief TCP-сервер, распределяющий задачи интегрирования между подключёнными клиентами
 *        и агрегирующий результаты.
 *
 * Принимает входящие TCP-соединения. При поступлении команды оператора формирует
 * задачу интегрирования, разбивает отрезок равномерно по числу клиентов, рассылает
 * подзадачи и суммирует возвращённые результаты.
 */
class Server {
public:
    /**
     * @brief Конструктор сервера. Сразу начинает принимать входящие соединения.
     * @param io Ссылка на контекст ввода-вывода Boost.Asio.
     */
    Server(boost::asio::io_context& io)
        : io(io), acceptor(io, tcp::endpoint(tcp::v4(), 9000))
    {
        accept();
    }

    /**
     * @brief Отправляет задачу интегрирования всем подключённым клиентам.
     *
     * Делит отрезок [left, right] поровну между клиентами и рассылает каждому
     * свой подотрезок с шагом h. Если предыдущая задача ещё не завершена — отклоняет
     * запрос. Вызов потокобезопасен — работа делегируется через boost::asio::post.
     *
     * @param left  Нижний предел интегрирования (должен быть > 1.0).
     * @param right Верхний предел интегрирования.
     * @param h     Шаг интегрирования.
     */
    void sendIntegrationTask(double left, double right, double h) {
        if (left <= 1.0) {
            std::cout << "Ошибка: нижняя граница должна быть > 1 (ln(1) = 0)\n";
            return;
        }
        if (left >= right) {
            std::cout << "Ошибка: левая граница должна быть меньше правой\n";
            return;
        }
        if (h <= 0) {
            std::cout << "Ошибка: шаг должен быть положительным\n";
            return;
        }

        boost::asio::post(io, [this, left, right, h]() {
            std::lock_guard<std::mutex> lock(serverMtx);

            if (clients.empty()) {
                std::cout << "Нет клиентов\n";
                return;
            }

            // Защита от повторного запуска пока предыдущая задача не завершена
            if (resultsReceived < expectedResults) {
                std::cout << "Ошибка: предыдущая задача ещё не завершена ("
                          << resultsReceived << "/" << expectedResults << ")\n";
                return;
            }

            resultsReceived = 0;
            totalResult = 0;
            expectedResults = clients.size();

            double segmentSize = (right - left) / clients.size();

            for (size_t i = 0; i < clients.size(); i++) {
                IntegrationTask task;
                task.set_left(left + i * segmentSize);
                task.set_right(left + (i + 1) * segmentSize);
                task.set_step(h);

                Envelope env;
                env.set_type(INTEGRATION_TASK);
                if (!task.SerializeToString(env.mutable_payload())) {
                    std::cerr << "Failed to serialize IntegrationTask for client " << i << "\n";
                    continue;
                }

                std::cout << "Клиент " << i << " получает: ["
                          << task.left() << ", " << task.right() << "]\n";

                async_send_message(clients[i], env);
            }
        });
    }

    /**
     * @brief Принимает частичный результат от одного из клиентов и накапливает итог.
     *
     * Потокобезопасно суммирует результаты. Когда получены все ожидаемые ответы,
     * выводит итоговое значение интеграла на экран.
     *
     * @param res Частичный результат интегрирования, присланный клиентом.
     */
    void onResult(double res) {
        std::lock_guard<std::mutex> lock(serverMtx);
        totalResult += res;
        resultsReceived++;
        std::cout << "Получен результат: " << res
                  << " (" << resultsReceived << "/" << expectedResults << ")\n";
        if (resultsReceived == expectedResults) {
            std::cout << "=== ИТОГ: " << totalResult << " ===\n";
        }
    }

private:
    /**
     * @brief Удаляет отключившийся клиентский сокет из списка.
     *
     * Вызывается при ошибке чтения, чтобы не отправлять задачи мёртвым соединениям.
     * Если задача ещё выполнялась — корректирует ожидаемое число результатов.
     *
     * @param sock Сокет отключившегося клиента.
     */
    void removeClient(std::shared_ptr<tcp::socket> sock) {
        std::lock_guard<std::mutex> lock(serverMtx);
        auto it = std::find(clients.begin(), clients.end(), sock);
        if (it != clients.end()) {
            clients.erase(it);
            std::cout << "Клиент отключился, осталось: " << clients.size() << "\n";
        }

        if (expectedResults > 0 && resultsReceived < expectedResults) {
            expectedResults--;
            std::cout << "Ожидаемых результатов скорректировано до: " << expectedResults << "\n";
            if (resultsReceived == expectedResults) {
                std::cout << "=== ИТОГ (без отключившегося клиента): " << totalResult << " ===\n";
            }
        }
    }

    /**
     * @brief Запускает асинхронное чтение сообщений от конкретного клиента.
     *
     * Читает 4-байтный заголовок (размер), затем тело сообщения, десериализует
     * Envelope и обрабатывает результат интегрирования. После обработки рекурсивно
     * ожидает следующее сообщение.
     *
     * @param sock Сокет клиента, с которым ведётся обмен.
     */
    void readFromClient(std::shared_ptr<tcp::socket> sock) {
        auto header = std::make_shared<std::array<char, 4>>();
        boost::asio::async_read(*sock, boost::asio::buffer(*header),
                                [this, sock, header](boost::system::error_code ec, std::size_t) {
                                    if (ec) {
                                        std::cerr << "Client read error: " << ec.message() << "\n";
                                        removeClient(sock);
                                        return;
                                    }

                                    uint32_t size = ntohl(*reinterpret_cast<uint32_t*>(header->data()));
                                    auto body = std::make_shared<std::vector<char>>(size);

                                    boost::asio::async_read(*sock, boost::asio::buffer(*body),
                                                            [this, sock, body](boost::system::error_code ec, std::size_t) {
                                                                if (ec) {
                                                                    std::cerr << "Client body read error: " << ec.message() << "\n";
                                                                    removeClient(sock);
                                                                    return;
                                                                }

                                                                Envelope env;
                                                                if (!env.ParseFromString(std::string(body->begin(), body->end()))) {
                                                                    std::cerr << "Failed to parse Envelope from client\n";
                                                                    readFromClient(sock);
                                                                    return;
                                                                }

                                                                if (env.type() == INTEGRATION_RESULT) {
                                                                    IntegrationTaskResult result;
                                                                    if (!result.ParseFromString(env.payload())) {
                                                                        std::cerr << "Failed to parse IntegrationTaskResult\n";
                                                                        readFromClient(sock);
                                                                        return;
                                                                    }
                                                                    onResult(result.res());
                                                                }

                                                                readFromClient(sock);
                                                            });
                                });
    }

    /**
     * @brief Асинхронно принимает новое входящее TCP-соединение.
     *
     * После успешного принятия добавляет клиентский сокет в список и запускает
     * для него цикл чтения. Сразу же запускает ожидание следующего соединения.
     */
    void accept() {
        auto sock = std::make_shared<tcp::socket>(io);
        acceptor.async_accept(*sock, [this, sock](boost::system::error_code ec) {
            if (!ec) {
                std::lock_guard<std::mutex> lock(serverMtx);
                clients.push_back(sock);
                std::cout << "Клиент добавлен, всего: " << clients.size() << "\n";
                readFromClient(sock);
            }
            accept();
        });
    }

    boost::asio::io_context& io;         ///< Контекст ввода-вывода Boost.Asio.
    tcp::acceptor acceptor;              ///< Акцептор входящих TCP-соединений на порту 9000.
    std::vector<std::shared_ptr<tcp::socket>> clients; ///< Список подключённых клиентских сокетов.
    std::mutex serverMtx;               ///< Мьютекс для защиты разделяемых данных.

    size_t resultsReceived = 0; ///< Число уже полученных частичных результатов.
    size_t expectedResults = 0; ///< Ожидаемое число результатов (= число клиентов).
    double totalResult = 0;     ///< Накопленная сумма результатов интегрирования.
};

#endif
