cmake_minimum_required(VERSION 4.4 FATAL_ERROR)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_CXX_MODULE_STD ON)

if(CMAKE_VERSION VERSION_LESS 4.5 AND NOT DEFINED
                                      CMAKE_EXPERIMENTAL_CXX_IMPORT_STD)
  set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
endif()

project(SwitchConsumer LANGUAGES CXX)

if(NOT SWITCH_SOURCE_DIR)
  message(FATAL_ERROR "SWITCH_SOURCE_DIR must point at a Switch source tree")
endif()

add_subdirectory("${SWITCH_SOURCE_DIR}" switch)

add_executable(switch_consumer "${CMAKE_CURRENT_LIST_DIR}/../main.cxx")
target_compile_features(switch_consumer PRIVATE cxx_std_26)
target_link_libraries(switch_consumer PRIVATE Switch::Switch)

set_target_properties(
  switch_consumer
  PROPERTIES CXX_STANDARD 26
             CXX_STANDARD_REQUIRED ON
             CXX_EXTENSIONS OFF
             CXX_SCAN_FOR_MODULES ON
             CXX_MODULE_STD ON)
