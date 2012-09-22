include_directories(../lib/include)
add_executable(cupt.bin
	common.cpp
	cupt.cpp
	misc.cpp
	handlers/search.cpp
	handlers/misc.cpp
	handlers/managepackages.cpp
	handlers/shell.cpp
	handlers/download.cpp
	handlers/snapshot.cpp
	colorizer.cpp
	functionselectors.cpp
	selectors.cpp)
set_property(TARGET cupt.bin PROPERTY OUTPUT_NAME cupt)
target_link_libraries(cupt.bin libcupt3 boost_program_options)

