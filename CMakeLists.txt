cmake_minimum_required(VERSION 3.0)
project(webserver)

# set(SRC_LIB
#     CGImysql/sql_connection_pool.cpp
#     http/http_conn.cpp
#     log/log.cpp
#     timer/lst_timer.cpp
#     config.cpp
#     webserver.cpp
#     main.cpp)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

aux_source_directory(CGImysql CGI)
aux_source_directory(http HTTP)
aux_source_directory(log LOG)
aux_source_directory(timer TIMER)

#设置二进制可执行文件输出路径
set (EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin) 

add_executable(server main.cpp config.cpp webserver.cpp ${CGI} ${HTTP} ${LOG} ${TIMER})

target_link_libraries(server /usr/lib/x86_64-linux-gnu/libmysqlclient.so)

