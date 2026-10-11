cmake_minimum_required(VERSION 3.14)
project(ForumerPlugin LANGUAGES CXX)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

# Derive the module name + version from metadata.json - single source of truth.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json" METADATA_JSON)
string(JSON MODULE_NAME GET ${METADATA_JSON} name)
string(JSON MODULE_VERSION GET ${METADATA_JSON} version)

# forumer_core: Forumer's own engine (identity, crypto, signed posts, the post
# log, the sync protocol). Qt- and SDK-free so it is unit-testable standalone
# (scripts/test-core.sh).
add_subdirectory(lib/forumer_core)

# Universal UI module: you write the .rep + the *Backend class. REP_FILE runs
# repc; the generated *Plugin glue in generated_code/ is compiled automatically.
logos_module(
    NAME ${MODULE_NAME}
    REP_FILE src/forumer.rep
    SOURCES
        src/forumer_backend.h
        src/forumer_backend.cpp
        src/base64.h
        src/base64.cpp
        src/delivery_module_transport.h
        src/delivery_module_transport.cpp
        src/image_prep.h
        src/image_prep.cpp
    INCLUDE_DIRS
        src
    LINK_LIBRARIES
        forumer_core
)

# Images attached to posts are decoded and re-encoded in the backend
# (src/image_prep.cpp), which needs QtGui's QImage - not linked by default.
find_package(Qt6 REQUIRED COMPONENTS Gui)
target_link_libraries(${MODULE_NAME}_module_plugin PRIVATE Qt6::Gui)

# Surface the metadata.json version to the backend, which publishes it to the
# QML view via the appVersion PROP (so the title's version stays in sync here).
target_compile_definitions(${MODULE_NAME}_module_plugin PRIVATE
    FORUMER_VERSION="${MODULE_VERSION}")