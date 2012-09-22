IF(NOT EXISTS /usr/bin/doxygen)
	message(FATAL_ERROR "missing doxygen")
ENDIF(NOT EXISTS /usr/bin/doxygen)

IF(NOT EXISTS /usr/bin/txt2tags)
	message(FATAL_ERROR "missing txt2tags")
ENDIF(NOT EXISTS /usr/bin/txt2tags)

set(ODIR "${CMAKE_CURRENT_BINARY_DIR}")
set(DOXYGEN_OUTPUT_DIRECTORY "${ODIR}/reference")
add_custom_command(
	OUTPUT doxygen-stamp
	DEPENDS libcupt3
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/reference"

	COMMAND mkdir -p ${DOXYGEN_OUTPUT_DIRECTORY}
	COMMAND sh -c "( cat Doxyfile; echo \"OUTPUT_DIRECTORY=${DOXYGEN_OUTPUT_DIRECTORY}\" ) | doxygen -"
	COMMAND touch ${ODIR}/doxygen-stamp

	VERBATIM
)

MACRO(CUPT_PROCESS_T2T)
	FOREACH(_name ${ARGN})
		set(T2T_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/${_name}.t2t")
		add_custom_command(
			MAIN_DEPENDENCY ${T2T_SOURCE}
			OUTPUT ${_name}-stamp

			COMMAND txt2tags ARGS -t html -o ${_name}.html ${T2T_SOURCE}
			COMMAND txt2tags ARGS -t man -o cupt_${_name}.7 ${T2T_SOURCE}
			COMMAND touch ${_name}-stamp
		)
	ENDFOREACH(_name)
ENDMACRO(CUPT_PROCESS_T2T)

CUPT_PROCESS_T2T(tutorial functionalselectors)

add_custom_target(doc ALL DEPENDS doxygen-stamp tutorial-stamp functionalselectors-stamp)

install(DIRECTORY examples DESTINATION share/doc/lib)
install(DIRECTORY ${ODIR}/reference/html DESTINATION share/doc/lib)
install(DIRECTORY ${ODIR}/reference/man DESTINATION share FILES_MATCHING PATTERN cupt*)
install(FILES ${ODIR}/tutorial.html ${ODIR}/cupt_tutorial.7 DESTINATION share/doc)

