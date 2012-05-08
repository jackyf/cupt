IF(NOT EXISTS /usr/bin/doxygen)
	message(FATAL_ERROR "missing doxygen")
ENDIF(NOT EXISTS /usr/bin/doxygen)

IF(NOT EXISTS /usr/bin/txt2tags)
	message(FATAL_ERROR "missing txt2tags")
ENDIF(NOT EXISTS /usr/bin/txt2tags)

set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/reference")
add_custom_command(
	OUTPUT doxygen-stamp
	DEPENDS cupt3
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/reference"

	COMMAND mkdir -p ${DOXYGEN_OUTPUT_DIRECTORY}
	COMMAND sh -c "( cat Doxyfile; echo \"OUTPUT_DIRECTORY=${DOXYGEN_OUTPUT_DIRECTORY}\" ) | doxygen -"
	COMMAND touch ${CMAKE_CURRENT_BINARY_DIR}/doxygen-stamp

	VERBATIM
)

set(TUTORIAL_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tutorial.t2t")
add_custom_command(
	MAIN_DEPENDENCY ${TUTORIAL_SOURCE}
	OUTPUT tutorial-stamp

	COMMAND txt2tags ARGS -t html -o tutorial.html ${TUTORIAL_SOURCE}
	COMMAND txt2tags ARGS -t man -o cupt_tutorial.7 ${TUTORIAL_SOURCE}
	COMMAND touch tutorial-stamp
)
add_custom_target(doc ALL DEPENDS doxygen-stamp tutorial-stamp)

install(DIRECTORY examples DESTINATION share/doc/lib)
install(DIRECTORY reference/html DESTINATION share/doc/lib)
install(DIRECTORY reference/man DESTINATION share FILES_MATCHING PATTERN cupt*)
install(FILES tutorial.html cupt_tutorial.7 DESTINATION share/doc)

