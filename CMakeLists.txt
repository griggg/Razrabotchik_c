cmake_minimum_required(VERSION 4.2)

# Политики CMake для совместимости
cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0167 NEW)

# Проект
project(NetworkApp LANGUAGES CXX)

# C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------------
# Boost (если нужен)
# -------------------------------
find_package(Boost REQUIRED COMPONENTS system)

find_package(Protobuf REQUIRED)  # ← добавь это

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS src/msg.proto)  # ← и это

# Основные бинарники
add_executable(Server src/Server.cpp src/common.cpp ${PROTO_SRCS})
add_executable(Client src/Client.cpp src/common.cpp src/Integrator.cpp ${PROTO_SRCS})

target_link_libraries(Server protobuf::libprotobuf Boost::system)
target_link_libraries(Client protobuf::libprotobuf Boost::system)

# -------------------------------
# Google Test через FetchContent
# -------------------------------
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)

# Загружаем и добавляем поддиректорию
FetchContent_MakeAvailable(googletest)

# Включаем тестирование
enable_testing()

# -------------------------------
# Тестовый исполняемый файл
# -------------------------------
add_executable(test_integrator
    tests/test_integrator.cpp   # тестовый файл
    src/Integrator.cpp          # исходник функций, используемых в тестах
)

# Линкуем с GoogleTest
target_link_libraries(test_integrator PRIVATE gtest_main)

target_compile_options(Server PRIVATE -fsanitize=address -g)
target_link_options(Server PRIVATE -fsanitize=address)

# Регистрируем тесты для CTest
include(GoogleTest)
gtest_discover_tests(test_integrator)
