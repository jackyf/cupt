include_directories(../lib/include)
add_executable(cupt.bin
	cupt.cpp
	misc.cpp
	handlers/search.cpp
	handlers/misc.cpp
	handlers/managepackages.cpp
	handlers/shell.cpp
	handlers/download.cpp
	handlers/snapshot.cpp
	colorizer.cpp
	selectors.cpp)
target_link_libraries(cupt.bin cupt3 boost_program_options)

install(PROGRAMS cupt.bin DESTINATION bin RENAME cupt)
