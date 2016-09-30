IF(NOT EXISTS /usr/include/boost/xpressive/xpressive_dynamic.hpp)
	message(FATAL_ERROR "missing Boost.Xpressive library")
ENDIF()

IF(NOT EXISTS /usr/include/boost/program_options.hpp)
	message(FATAL_ERROR "missing Boost.ProgramOptions library")
ENDIF()

find_package(Boost 1.42.0)
IF(Boost_FOUND)
	IF(Boost_VERSION LESS 104200)
		message(FATAL_ERROR "need Boost of version 1.42.0")
	ENDIF(Boost_VERSION LESS 104200)
ELSE()
	message(FATAL_ERROR "missing Boost")
ENDIF()

IF(NOT EXISTS /usr/include/readline/readline.h)
	message(FATAL_ERROR "missing GNU Readline library")
ENDIF()

OPTION(LOCAL "is build local" ON)

include(flags.cmake)
include(version.cmake)

include_directories(.)

add_subdirectory(console)
add_subdirectory(lib)
add_subdirectory(downloadmethods)

