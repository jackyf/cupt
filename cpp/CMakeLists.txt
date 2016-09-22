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

set(CUPT_API_VERSION 4)
set(CUPT_SOVERSION 1)
set(CUPT_RELATIVE_DOWNLOADMETHODS_DIR "lib/cupt${CUPT_API_VERSION}-${CUPT_SOVERSION}/downloadmethods")

# detect version from debian/changelog
execute_process(
	COMMAND "dpkg-parsechangelog" "-l${PROJECT_SOURCE_DIR}/debian/changelog"
	COMMAND "grep" "^Version"
	COMMAND "cut" "-d " "-f" "2"
	OUTPUT_VARIABLE CUPT_VERSION
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

IF(CUPT_VERSION)
	message(STATUS "Detected Cupt version: ${CUPT_VERSION}")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DCUPT_VERSION=${CUPT_VERSION}")
ELSE()
	message(FATAL_ERROR "Unable to detect Cupt version.")
ENDIF()

include_directories(.)

add_subdirectory(console)
add_subdirectory(lib)
add_subdirectory(downloadmethods)

