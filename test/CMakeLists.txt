set(SDIR ${CMAKE_CURRENT_SOURCE_DIR})
add_custom_target(
	test
	COMMAND cmake -E create_symlink ${SDIR}/t t
	COMMAND prove -r --timer -I${SDIR} t :: $<TARGET_FILE:cupt.bin>
)

