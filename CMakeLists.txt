cmake_minimum_required(VERSION 2.6)
project(Cupt)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_USE_CXX11_ABI=0")

add_subdirectory(cpp)
add_subdirectory(doc)
add_subdirectory(po)
add_subdirectory(test)

