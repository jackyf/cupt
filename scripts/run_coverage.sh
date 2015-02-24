#!/bin/sh

OUTPUT=lcov.output.info

set -e
set -x

ABSOLUTE_SOURCE_ROOT_PATH=`git rev-parse --show-toplevel`/cpp

CAPTURE_OPTIONS="--directory . --base-directory "$ABSOLUTE_SOURCE_ROOT_PATH" --no-external"

if [ -z "$ONLY_CAPTURE" ]; then
	# recompile
	CXXFLAGS=-coverage cmake ../..
	make

	# clean leftovers from previous tests
	lcov --directory . --zerocounters

	# gather all source files
	lcov $CAPTURE_OPTIONS --capture --initial --output-file ${OUTPUT}.initial.base
	lcov --remove ${OUTPUT}.initial.base '*CMake*Compiler*' --output-file ${OUTPUT}.initial

	# test
	make test
fi

# capture
lcov $CAPTURE_OPTIONS --capture --output-file ${OUTPUT}.tests

# combine
lcov -a ${OUTPUT}.initial -a ${OUTPUT}.tests -o $OUTPUT

# visualise
genhtml --output-directory html --prefix $ABSOLUTE_SOURCE_ROOT_PATH $OUTPUT --no-function-coverage

