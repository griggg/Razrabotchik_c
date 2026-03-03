#include <boost/asio.hpp>
#include <iostream>
#include <thread>
#include <future>
#include "msg.pb.h"
#include "Integrator.hpp"
#include "common.hpp"

using boost::asio::ip::tcp;

/**
 * @brief TCP-клиент, принимающий задачи интегрирования от сервера и возвращающий результаты.
 *
 * Подключается к серверу по TCP, получает задачу (IntegrationTask),
 * разбивает её на подзадачи по числу доступных ядер CPU,
 * выполняет параллельное интегрирование и отправляет суммарный результат обратно.
 */
class Client {
public:
    unsigned int VALTHREADS = std::max(1u, std::thread::hardware_concurrency());

    /**
     * @brief Конструктор клиента.
     * @param io Ссылка на контекст ввода-вывода Boost.Asio.
     */
    Client(boost::asio::io_context& io)
        : resolver(io), socket(std::make_shared<tcp::socket>(io)) {}

    /**
     * @brief Инициирует асинхронное подключение к серверу.
     * @param host Адрес сервера (IP или hostname).
     * @param port Порт сервера.
     */
    void connect(const std::string& host, const std::string& port) {
        resolver.async_resolve(host, port,
                               [this](boost::system::error_code ec, tcp::resolver::results_type endpoints) {
                                   if (!ec) {
                                       boost::asio::async_connect(*socket, endpoints,
                                                                  [this](boost::system::error_code ec, tcp::endpoint) {
                                                                      if (!ec) {
                                                                          std::cout << "Connected!\n";
                                                                          read();
                                                                      }
                                                                  });
                                   }
                               });
    }

private:
    /**
     * @brief Выполняет параллельное численное интегрирование и отправляет результат серверу.
     *
     * Делит отрезок [left, right] на VALTHREADS подотрезков, запускает интегрирование
     * функции 1/ln(x) в отдельных потоках через std::async, суммирует частичные результаты
     * в фоновом потоке (чтобы не блокировать io_context) и отправляет итог серверу.
     *
     * @param task Задача интегрирования, полученная от сервера.
     */
    void startIntegration(IntegrationTask& task) {
        if (task.step() <= 0 || task.left() >= task.right()) {
            std::cerr << "Invalid task params!\n";
            return;
        }

        double left = task.left();
        double right = task.right();
        double segmentSize = (right - left) / VALTHREADS;

        std::vector<IntegrationTask> tasks;
        for (size_t i = 0; i < VALTHREADS; i++) {
            IntegrationTask nt;
            nt.set_left(left + i * segmentSize);
            nt.set_right(left + (i + 1) * segmentSize);
            nt.set_step(task.step());
            tasks.push_back(nt);
        }

        /// Подынтегральная функция 1/ln(x).
        auto f = [](double x) { return 1.0 / std::log(x); };

        std::vector<std::future<double>> futures;
        for (size_t i = 0; i < VALTHREADS; i++) {
            futures.push_back(std::async(std::launch::async, [f, t = tasks[i]]() {
                Integrator integrator;
                return integrator.integrate(f, t.left(), t.right(), t.step());
            }));
        }

        std::thread([this, futures = std::move(futures), sock = socket]() mutable {
            double total = 0;
            for (auto& fut : futures)
                total += fut.get();

            std::cout << "Local result: " << total << ", sending to server...\n";

            IntegrationTaskResult result;
            result.set_res(total);

            Envelope env;
            env.set_type(INTEGRATION_RESULT);
            if (!result.SerializeToString(env.mutable_payload())) {
                std::cerr << "Failed to serialize IntegrationTaskResult\n";
                return;
            }

            async_send_message(sock, env);
        }).detach();
    }

    /**
     * @brief Обрабатывает входящий запрос от сервера после чтения заголовка.
     *
     * Читает тело сообщения по размеру из заголовка, десериализует Envelope
     * и диспетчеризует по типу сообщения.
     *
     * @param ec   Код ошибки Boost.Asio.
     * @param      Число прочитанных байт (не используется).
     */
    void handleServerReq(boost::system::error_code ec, std::size_t) {
        if (ec) { std::cerr << "Read error: " << ec.message() << "\n"; return; }

        uint32_t size = ntohl(*reinterpret_cast<uint32_t*>(header));
        body.resize(size);

        boost::asio::async_read(*socket, boost::asio::buffer(body),
                                [this](boost::system::error_code ec, std::size_t) {
                                    if (ec) { std::cerr << "Read error: " << ec.message() << "\n"; return; }

                                    Envelope env;
                                    if (!env.ParseFromString(std::string(body.begin(), body.end()))) {
                                        std::cerr << "Failed to parse Envelope\n";
                                        read();
                                        return;
                                    }

                                    switch (env.type()) {
                                    case INTEGRATION_TASK: {
                                        IntegrationTask task;
                                        if (!task.ParseFromString(env.payload())) {
                                            std::cerr << "Failed to parse IntegrationTask\n";
                                            break;
                                        }
                                        startIntegration(task);
                                        break;
                                    }
                                    default:
                                        std::cout << "Unknown message type\n";
                                    }

                                    read();
                                });
    }

    /**
     * @brief Запускает асинхронное чтение 4-байтного заголовка (длина следующего сообщения).
     */
    void read() {
        boost::asio::async_read(*socket, boost::asio::buffer(header, 4),
                                [this](boost::system::error_code ec, std::size_t size) {
                                    handleServerReq(ec, size);
                                });
    }

    tcp::resolver resolver;                   ///< Резолвер DNS для поиска эндпоинтов.
    std::shared_ptr<tcp::socket> socket;      ///< TCP-сокет соединения с сервером (shared, т.к. захватывается в потоках).
    char header[4];                           ///< Буфер для 4-байтного заголовка (big-endian размер сообщения).
    std::vector<char> body;                   ///< Буфер для тела сообщения.
};

/**
 * @brief Точка входа клиентского приложения.
 *
 * Создаёт io_context, подключается к серверу на 127.0.0.1:9000
 * и запускает event loop в отдельном потоке.
 */
int main() {
    boost::asio::io_context io;
    Client c(io);
    c.connect("127.0.0.1", "9000");

    std::thread ioThread([&io]() {
        io.run();
    });
    ioThread.join();
}
