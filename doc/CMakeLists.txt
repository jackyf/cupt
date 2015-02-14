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
	MAIN_DEPENDENCY "reference/Doxyfile"
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
		set(HTML_FILE_NAME ${_name}.html)
		set(MAN_FILE_NAME cupt_${_name}.7)
		add_custom_command(
			MAIN_DEPENDENCY ${T2T_SOURCE}
			OUTPUT ${_name}-stamp

			COMMAND txt2tags ARGS -t html -o ${HTML_FILE_NAME} ${T2T_SOURCE}
			COMMAND txt2tags ARGS -t man -o ${MAN_FILE_NAME} ${T2T_SOURCE}
			COMMAND touch ${_name}-stamp
		)
		install(FILES ${ODIR}/${HTML_FILE_NAME} ${ODIR}/${MAN_FILE_NAME} DESTINATION share/doc)
	ENDFOREACH(_name)
ENDMACRO(CUPT_PROCESS_T2T)

CUPT_PROCESS_T2T(tutorial functionalselectors)


function(process_pod _name _section)
	set(MAN_FILE "${_name}.${_section}")
	set(POD_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${MAN_FILE}.pod")
	string(TOUPPER ${_name} UC_NAME)

	add_custom_command(
		MAIN_DEPENDENCY ${POD_FILE}
		OUTPUT ${MAN_FILE}
		COMMAND pod2man --utf8 --name ${UC_NAME} --section ${_section} ${POD_FILE} ${MAN_FILE}
	)

	list(APPEND POD_MANS ${MAN_FILE})
	set(POD_MANS ${POD_MANS} PARENT_SCOPE)

	install(FILES ${MAN_FILE} DESTINATION share/doc)
endfunction()

process_pod(cupt 1)
process_pod(cupt.conf 5)
process_pod(cupt_vs_apt 5)


add_custom_target(doc ALL DEPENDS doxygen-stamp tutorial-stamp functionalselectors-stamp ${POD_MANS})

install(DIRECTORY examples DESTINATION share/doc/lib FILES_MATCHING PATTERN "*.cpp" PATTERN "*CMake*" EXCLUDE)
install(DIRECTORY ${ODIR}/reference/html DESTINATION share/doc/lib)
install(DIRECTORY ${ODIR}/reference/man DESTINATION share FILES_MATCHING PATTERN cupt*)

add_subdirectory(examples)

