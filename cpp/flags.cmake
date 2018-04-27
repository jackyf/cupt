try_run(LAMBDA_CAPTURE_OPT_RESULT LAMBDA_CAPTURE_COMPILES
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/platform-tests/std-function-lambda-capture.cpp
	COMPILE_DEFINITIONS "-O2"
	COMPILE_OUTPUT_VARIABLE LAMBDA_COMPILE_OUTPUT)
if (NOT LAMBDA_CAPTURE_COMPILES)
	message(FATAL_ERROR "Lambda platform test didn't compile: ${LAMBDA_COMPILE_OUTPUT}")
endif()
if (${LAMBDA_CAPTURE_OPT_RESULT} EQUAL 77)
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O1")
	message("Detected lambda capture misoptimisation, switching to -O1. See #838438.")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wall -Wextra -std=gnu++14 -fPIC -include common/common.hpp")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} $ENV{CXXFLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-format-security") # yes, we should move to type-safe alternative eventually

set(OUR_LINKER_FLAGS "-pthread -Wl,--as-needed")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OUR_LINKER_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${OUR_LINKER_FLAGS}")

