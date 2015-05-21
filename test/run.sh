#!/bin/sh

set -e

TESTS_PATH=$1
RUNNER="perl -Mwarnings -Mstrict -I${TESTS_PATH} -MTestCupt"
BINARY_UNDER_TEST_PATH=${PROVE_BINARY:-$2}
prove --exec "$RUNNER" -r --timer $PROVE_PARAMS tt/$PROVE_FILTER :: "$BINARY_UNDER_TEST_PATH"

