set(SDIR ${CMAKE_CURRENT_SOURCE_DIR})
add_custom_target(
	test
	COMMAND ${SDIR}/run.sh ${SDIR} $<TARGET_FILE:cupt.bin>
)

