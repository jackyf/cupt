IF(NOT EXISTS /usr/bin/doxygen)
	message(FATAL_ERROR "missing doxygen")
ENDIF(NOT EXISTS /usr/bin/doxygen)

IF(NOT EXISTS /usr/bin/txt2tags)
	message(FATAL_ERROR "missing txt2tags")
ENDIF(NOT EXISTS /usr/bin/txt2tags)

add_custom_command(
	OUTPUT doxygen-stamp
	DEPENDS cupt2
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/reference"

	COMMAND pwd
	COMMAND doxygen
	COMMAND touch ../doxygen-stamp
)
add_custom_command(
	MAIN_DEPENDENCY tutorial.t2t
	OUTPUT tutorial-stamp

	COMMAND txt2tags ARGS -t html -o tutorial.html tutorial.t2t
	COMMAND txt2tags ARGS -t man -o cupt_tutorial.7 tutorial.t2t
	COMMAND touch tutorial-stamp
)
add_custom_target(doc ALL DEPENDS doxygen-stamp tutorial-stamp)

install(DIRECTORY examples DESTINATION share/doc/lib)
install(DIRECTORY reference/html DESTINATION share/doc/lib)
install(DIRECTORY reference/man DESTINATION share FILES_MATCHING PATTERN cupt*)
install(FILES tutorial.html cupt_tutorial.7 DESTINATION share/doc)

