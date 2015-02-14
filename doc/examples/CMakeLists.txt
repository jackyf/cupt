set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=c++0x")
include_directories(../../cpp/lib/include)

add_subdirectory(show-upgradeable)
add_subdirectory(show-available)

