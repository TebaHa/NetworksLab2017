cmake_minimum_required(VERSION 3.8)
project(EchoServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wl,--no-as-needed -ldl")

# libs
include_directories(libs)
set(THREAD_POOL_SRC libs/thread_pool/ThreadPool.h)
set(JSON_SRC libs/json/src/json.hpp)

# shared
include_directories(shared)
set(DEFINES shared/defines.h)

set(FINANCE_DB_SRC server/database/findb.h server/database/findb.cpp)

set(SERVER_SRC server/server.cpp server/server.h server/utils/sockutils.h)
set(SERVER_SRC ${SERVER_SRC} ${DEFINES} ${FINANCE_DB_SRC} ${LOGGER_SRC} ${JSON_SRC} ${THREAD_POOL_SRC})

add_executable(server server/server_main.cpp ${SERVER_SRC})

target_link_libraries(server /usr/local/lib/libSQLiteCpp.a)
target_link_libraries(server /usr/lib/x86_64-linux-gnu/libsqlite3.a)

add_executable(initdb server/initdb.cpp ${FINANCE_DB_SRC})

target_link_libraries(initdb /usr/local/lib/libSQLiteCpp.a)
target_link_libraries(initdb /usr/lib/x86_64-linux-gnu/libsqlite3.a)

set(CLIENT_SRC ${DEFINES} client/client_defs.h)
add_executable(client client/client.cpp ${CLIENT_SRC})



