set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSOVERSION=${CUPT_SOVERSION} -fvisibility=hidden")

include_directories(include)
include_directories(src)
add_library(libcupt3 SHARED
	./src/internal/common.cpp
	./src/internal/configparser.cpp
	./src/internal/nativeresolver/impl.cpp
	./src/internal/nativeresolver/solution.cpp
	./src/internal/nativeresolver/score.cpp
	./src/internal/nativeresolver/dependencygraph.cpp
	./src/internal/nativeresolver/decisionfailtree.cpp
	./src/internal/nativeresolver/autoremovalpossibility.cpp
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
	./src/internal/regex.cpp
	./src/internal/cachefiles.cpp
	./src/internal/logger.cpp
	./src/internal/pipe.cpp
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
	./src/system/resolver.cpp
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
	./src/hashsums.cpp
	./src/versionstring.cpp
)
set_target_properties(libcupt3 PROPERTIES VERSION 3 SOVERSION ${CUPT_SOVERSION} OUTPUT_NAME libcupt3.so)
target_link_libraries(libcupt3 dl rt gcrypt)

install(TARGETS libcupt3 DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
