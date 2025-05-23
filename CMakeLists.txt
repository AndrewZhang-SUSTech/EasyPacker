cmake_minimum_required(VERSION 3.10.0)
project(EasyPacker VERSION 0.1.0 LANGUAGES C CXX)

add_executable(EasyPacker main.cpp)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/PicoSHA2)

target_link_libraries(EasyPacker PRIVATE 
    picosha2
)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
