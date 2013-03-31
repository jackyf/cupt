set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=c++0x")
include_directories(../../../cpp/lib/include)
add_executable(show-upgradeable show-upgradeable.cpp)
target_link_libraries(show-upgradeable libcupt3)
