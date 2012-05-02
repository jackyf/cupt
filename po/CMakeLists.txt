include(FindGettext)

FOREACH(lang pl)
	GETTEXT_PROCESS_PO_FILES(${lang} ALL
			INSTALL_DESTINATION ${CMAKE_INSTALL_PREFIX}/share/locale/
			PO_FILES ${lang}.po)
ENDFOREACH(lang)
