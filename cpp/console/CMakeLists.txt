include_directories(${Cupt_SOURCE_DIR}/lib/include)
add_executable(cupt.bin
	cupt.cpp
	misc.cpp
	handlers/search.cpp
	handlers/misc.cpp
	handlers/managepackages.cpp
	handlers/shell.cpp
	handlers/download.cpp
	selectors.cpp)
target_link_libraries(cupt.bin cupt2 boost_program_options)
add_precompiled_header(cupt.bin)

install(PROGRAMS cupt.bin DESTINATION bin RENAME cupt-experimental)
