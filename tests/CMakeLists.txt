add_executable(SwitchTests)

file(GLOB switch_test_sources CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx")

target_sources(
  SwitchTests
  PRIVATE ${switch_test_sources}
  PRIVATE FILE_SET
          switch_test_modules
          TYPE
          CXX_MODULES
          FILES
          Support.ixx)

target_compile_features(SwitchTests PRIVATE cxx_std_26)

set_target_properties(
  SwitchTests
  PROPERTIES CXX_EXTENSIONS OFF
             CXX_STANDARD_REQUIRED ON
             CXX_SCAN_FOR_MODULES ON
             CXX_MODULE_STD ON)

target_link_libraries(SwitchTests PRIVATE Switch::Switch)

add_test(NAME Switch.Tests COMMAND SwitchTests)
set_tests_properties(Switch.Tests PROPERTIES WORKING_DIRECTORY
                                             "${CMAKE_CURRENT_BINARY_DIR}")
