include(FindGettext)

# existing targets don't suit:
# GETTEXT_CREATE_TRANSLATIONS modifies .po-files during the build
# GETTEXT_PROCESS_POT_FILE deletes .po-files in the 'clean' target
# GETTEXT_PROCESS_PO_FILES is limited to a single language and cannot be issued
#   several times due to 'duplicate targets error'

MACRO(CUPT_PROCESS_PO)
	FOREACH(_lang ${ARGN})
		GET_FILENAME_COMPONENT(_absPoFile ${_lang}.po ABSOLUTE)
		SET(_moFile ${CMAKE_CURRENT_BINARY_DIR}/${_lang}.mo)

		ADD_CUSTOM_COMMAND(
			OUTPUT ${_moFile}
			COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} -o ${_moFile} ${_absPoFile}
			DEPENDS ${_absPoFile}
		)

		INSTALL(FILES ${_moFile} DESTINATION share/locale/${_lang}/LC_MESSAGES RENAME cupt.mo)
		SET(_moFiles ${_moFiles} ${_moFile})
	ENDFOREACH(_lang)

	ADD_CUSTOM_TARGET(translations ALL DEPENDS ${_moFiles})
ENDMACRO(CUPT_PROCESS_PO)


CUPT_PROCESS_PO(pl da sk fr de)
