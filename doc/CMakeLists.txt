IF(NOT EXISTS /usr/bin/doxygen)
	message(FATAL_ERROR "missing doxygen")
ENDIF(NOT EXISTS /usr/bin/doxygen)

add_custom_command(
	OUTPUT doc-stamp
	DEPENDS cupt2
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/reference"

	COMMAND pwd
	COMMAND doxygen
	COMMAND touch ../doc-stamp
)
add_custom_target(doc ALL DEPENDS doc-stamp)

install(DIRECTORY examples DESTINATION doc)
install(DIRECTORY reference DESTINATION doc)
