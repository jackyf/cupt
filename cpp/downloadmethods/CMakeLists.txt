include_directories(${Cupt_SOURCE_DIR}/lib/include)

add_library(wget MODULE wget.cpp)
add_precompiled_header(wget)
target_link_libraries(wget cupt2)

add_library(curl MODULE curl.cpp)
add_precompiled_header(curl)
target_link_libraries(curl cupt2 curl-gnutls)

add_library(debdelta MODULE debdelta.cpp)
add_precompiled_header(debdelta)
target_link_libraries(debdelta cupt2)

install(TARGETS wget curl debdelta DESTINATION lib/cupt2-${CUPT_SOVERSION}/downloadmethods)
