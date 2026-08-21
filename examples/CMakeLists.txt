add_executable(SwitchQuickstart quickstart.cxx)

target_compile_features(SwitchQuickstart PRIVATE cxx_std_26)

set_target_properties(
  SwitchQuickstart
  PROPERTIES CXX_EXTENSIONS OFF
             CXX_STANDARD_REQUIRED ON
             CXX_SCAN_FOR_MODULES ON
             CXX_MODULE_STD ON)

target_link_libraries(SwitchQuickstart PRIVATE Switch::Switch)
