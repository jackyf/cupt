string(REPLACE " " ";" CompileFlags ${CMAKE_CXX_FLAGS})
add_custom_command(
	OUTPUT precompiled.hpp.gch
	DEPENDS precompiled.hpp

	COMMAND ${CMAKE_CXX_COMPILER} -I${Cupt_SOURCE_DIR}/lib/include ${CompileFlags} -x c++-header -c precompiled.hpp -o precompiled.hpp.gch
)
add_custom_target(precompiled DEPENDS precompiled.hpp.gch)

