cmake_minimum_required(VERSION 2.8)

project(QtpThreadPool)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

add_executable(${PROJECT_NAME} "QtpThreadPoolSimple.c")
target_link_libraries(${PROJECT_NAME} -lpthread)
