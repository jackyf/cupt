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

set(CMAKE_CXX_FLAGS "-pthread -ggdb -Wall -Wextra -std=gnu++0x -fPIC -I${CMAKE_SOURCE_DIR}/cpp/ -include common/common.hpp")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-format-security") # yes, we should move type-safe alternative eventually
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} $ENV{CXXFLAGS}")

OPTION(VERBOSE "is build verbose")
IF(VERBOSE)
	set(CMAKE_VERBOSE_MAKEFILE TRUE)
ENDIF()

OPTION(LOCAL "is build local" ON)
IF(LOCAL)
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DCUPT_LOCAL_BUILD")
ENDIF()

OPTION(OPTIMIZE "is build optimized")
IF(OPTIMIZE GREATER 0)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNDEBUG -O${OPTIMIZE}")
	IF(OPTIMIZE GREATER 1)
		# set(CMAKE_LD_FLAGS "${CMAKE_LD_FLAGS} -flto")
		# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto")
	ENDIF(OPTIMIZE GREATER 1)
ENDIF(OPTIMIZE GREATER 0)

set(OUR_LINKER_FLAGS "-pthread -Wl,--as-needed")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OUR_LINKER_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${OUR_LINKER_FLAGS}")

set(CUPT_SOVERSION 0)

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

