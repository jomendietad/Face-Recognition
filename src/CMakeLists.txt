cmake_minimum_required(VERSION 3.16)
project(face_access LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

add_executable(sistema_final sistema_final.cpp)

target_include_directories(sistema_final PRIVATE ${OpenCV_INCLUDE_DIRS})

# Link OpenCV, Threads, and PAM (for authentication)
target_link_libraries(sistema_final PRIVATE ${OpenCV_LIBS} Threads::Threads pam)

target_compile_definitions(sistema_final PRIVATE LOG_DIR=\"${CMAKE_SOURCE_DIR}/logs\")