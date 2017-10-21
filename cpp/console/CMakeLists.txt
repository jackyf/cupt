include_directories(../lib/include)
add_executable(cupt.bin
	common.cpp
	common.hpp
	cupt.cpp
	cupt.hpp
	misc.cpp
	misc.hpp
	handlers.hpp
	handlers/search.cpp
	handlers/misc.cpp
	handlers/managepackages.cpp
	handlers/shell.cpp
	handlers/download.cpp
	handlers/snapshot.cpp
	handlers/why.cpp
	colorizer.cpp
	colorizer.hpp
	functionselectors.cpp
	functionselectors.hpp
	selectors.cpp
	selectors.hpp
)
set_property(TARGET cupt.bin PROPERTY OUTPUT_NAME cupt)
target_link_libraries(cupt.bin libcupt boost_program_options)

install(TARGETS cupt.bin DESTINATION bin)
