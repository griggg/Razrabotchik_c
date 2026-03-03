/// @file common.hpp
/// @brief Утилиты, общие для сервера и клиента.

#pragma once
#include <boost/asio.hpp>
#include <google/protobuf/message.h>
#include <memory>

/**
 * @brief Асинхронно отправляет Protobuf-сообщение через TCP-сокет.
 *
 * @param socket Указатель на TCP-сокет получателя.
 * @param msg    Protobuf-сообщение для отправки.
 */
inline void async_send_message(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
                               const google::protobuf::Message& msg)
{
    auto buf = std::make_shared<std::string>();
    std::string data;
    msg.SerializeToString(&data);

    uint32_t size = htonl(data.size());
    buf->resize(4 + data.size());
    std::memcpy(buf->data(), &size, 4);
    std::memcpy(buf->data() + 4, data.data(), data.size());

    boost::asio::async_write(*socket, boost::asio::buffer(*buf),
                             [buf, socket](boost::system::error_code ec, std::size_t) {
                                 if (ec) std::cerr << "Write error: " << ec.message() << "\n";
                             });
}
