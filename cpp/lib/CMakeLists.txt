set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSOVERSION=${CUPT_SOVERSION}")

include_directories(include)
include_directories(src)
add_library(cupt2 SHARED
	./src/internal/common.cpp
	./src/internal/configparser.cpp
	./src/internal/nativeresolverimpl.cpp
	./src/internal/solution.cpp
	./src/internal/lock.cpp
	./src/internal/cacheimpl.cpp
	./src/internal/pininfo.cpp
	./src/internal/filesystem.cpp
	./src/internal/debdeltahelper.cpp
	./src/internal/tagparser.cpp
	./src/internal/worker/base.cpp
	./src/internal/worker/archives.cpp
	./src/internal/worker/metadata.cpp
	./src/internal/worker/setupandpreview.cpp
	./src/internal/worker/packages.cpp
	./src/internal/worker/snapshots.cpp
	./src/config.cpp
	./src/cache.cpp
	./src/cache/relation.cpp
	./src/cache/binarypackage.cpp
	./src/cache/binaryversion.cpp
	./src/cache/sourcepackage.cpp
	./src/cache/sourceversion.cpp
	./src/cache/package.cpp
	./src/cache/version.cpp
	./src/system/state.cpp
	./src/system/resolvers/native.cpp
	./src/system/worker.cpp
	./src/system/snapshots.cpp
	./src/download/uri.cpp
	./src/download/manager.cpp
	./src/download/method.cpp
	./src/download/methodfactory.cpp
	./src/download/progress.cpp
	./src/download/progresses/console.cpp
	./src/common.cpp
	./src/common/compareversions.cpp
	./src/common/consumers.cpp
	./src/file.cpp
	./src/regex.cpp
	./src/hashsums.cpp
	./src/pipe.cpp
)
set_target_properties(cupt2 PROPERTIES VERSION 2.0.0 SOVERSION ${CUPT_SOVERSION})
target_link_libraries(cupt2 dl rt gcrypt)
add_precompiled_header(cupt2)

install(TARGETS cupt2 DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
