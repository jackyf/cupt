include_directories(../lib/include)

add_library(wget MODULE wget.cpp)
target_link_libraries(wget cupt2)

add_library(curl MODULE curl.cpp)
target_link_libraries(curl cupt2 curl-gnutls)

add_library(debdelta MODULE debdelta.cpp)
target_link_libraries(debdelta cupt2)

add_library(file MODULE file.cpp)
target_link_libraries(file cupt2)

install(TARGETS file wget curl debdelta DESTINATION lib/cupt2-${CUPT_SOVERSION}/downloadmethods)
