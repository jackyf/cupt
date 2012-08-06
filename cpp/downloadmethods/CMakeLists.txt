include_directories(../lib/include)

add_library(downloadmethod-wget MODULE wget.cpp)
set_property(TARGET downloadmethod-wget PROPERTY OUTPUT_NAME libwget.so)
target_link_libraries(downloadmethod-wget libcupt3)

add_library(downloadmethod-curl MODULE curl.cpp)
set_property(TARGET downloadmethod-curl PROPERTY OUTPUT_NAME libcurl.so)
target_link_libraries(downloadmethod-curl libcupt3 curl-gnutls)

add_library(downloadmethod-debdelta MODULE debdelta.cpp)
set_property(TARGET downloadmethod-debdelta PROPERTY OUTPUT_NAME libdebdelta.so)
target_link_libraries(downloadmethod-debdelta libcupt3)

add_library(downloadmethod-file MODULE file.cpp)
set_property(TARGET downloadmethod-file PROPERTY OUTPUT_NAME libfile.so)
target_link_libraries(downloadmethod-file libcupt3)

install(TARGETS downloadmethod-file downloadmethod-wget downloadmethod-curl downloadmethod-debdelta
		DESTINATION lib/cupt3-${CUPT_SOVERSION}/downloadmethods)
