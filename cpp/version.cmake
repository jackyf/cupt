set(CUPT_API_VERSION 4)
set(CUPT_SOVERSION 2)

set(CUPT_RELATIVE_DOWNLOADMETHODS_DIR "lib/cupt${CUPT_API_VERSION}-${CUPT_SOVERSION}/downloadmethods")
if (LOCAL)
	set(DOWNLOADMETHODS_DIR "${CMAKE_CURRENT_BINARY_DIR}/downloadmethods")
else()
	set(DOWNLOADMETHODS_DIR "/usr/${CUPT_RELATIVE_DOWNLOADMETHODS_DIR}")
endif()

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
ELSE()
	message(FATAL_ERROR "Unable to detect Cupt version.")
ENDIF()

configure_file(version.hpp.in version.hpp)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

