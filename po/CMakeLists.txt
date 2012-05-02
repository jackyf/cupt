include(FindGettext)

FOREACH(lang pl)
	GETTEXT_PROCESS_PO_FILES(${lang} ALL PO_FILES ${lang}.po)
	install(FILES ${lang}.gmo
			DESTINATION ${CMAKE_INSTALL_PREFIX}/share/locale/${lang}/LC_MESSAGES/
			RENAME cupt.mo)
ENDFOREACH(lang)
