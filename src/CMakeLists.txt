cmake_minimum_required(VERSION 3.16)
project(face_access_demo LANGUAGES CXX) 
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

# --- SISTEMA PRINCIPAL ---
add_executable(sistema_final sistema_final.cpp)
target_include_directories(sistema_final PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(sistema_final PRIVATE ${OpenCV_LIBS} Threads::Threads pam)
target_compile_definitions(sistema_final PRIVATE LOG_DIR=\"${CMAKE_SOURCE_DIR}/logs\")

# --- ENTRENADOR DE MODELO ---
add_executable(train_model train_model.cpp)
target_include_directories(train_model PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(train_model PRIVATE ${OpenCV_LIBS})