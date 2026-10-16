cmake_minimum_required(VERSION 3.20)
project(forumer_image_pipeline_tests LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)

add_executable(image_pipeline_tests
    test_image_pipeline.cpp
    ../../src/image_prep.cpp
)

target_compile_features(image_pipeline_tests PRIVATE cxx_std_20)

target_include_directories(image_pipeline_tests PRIVATE
    ../../src
    ../../lib/forumer_core/include
)

target_link_libraries(image_pipeline_tests PRIVATE
    Qt6::Core
    Qt6::Gui
)

enable_testing()
add_test(NAME image_pipeline COMMAND image_pipeline_tests)
