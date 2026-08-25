include_guard(GLOBAL)

function(switch_check_capabilities)
  file(SHA256 "${CMAKE_CURRENT_FUNCTION_LIST_FILE}" capability_contract_hash)

  set(capability_toolchain_hash "")
  if(CMAKE_TOOLCHAIN_FILE AND EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    file(SHA256 "${CMAKE_TOOLCHAIN_FILE}" capability_toolchain_hash)
  endif()

  string(
    CONCAT capability_signature_input
           "${CMAKE_CXX_COMPILER}|"
           "${CMAKE_CXX_COMPILER_ID}|"
           "${CMAKE_CXX_COMPILER_VERSION}|"
           "${CMAKE_CXX_FLAGS}|"
           "${CMAKE_BUILD_TYPE}|"
           "${CMAKE_TOOLCHAIN_FILE}|"
           "${capability_toolchain_hash}|"
           "${capability_contract_hash}")

  string(SHA256 capability_signature "${capability_signature_input}")

  set(capability_root
      "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/SwitchCapabilities/${capability_signature}"
  )
  set(capability_source_dir "${capability_root}/source")
  set(capability_build_dir "${capability_root}/build")
  set(capability_report "${CMAKE_CURRENT_BINARY_DIR}/SwitchCapabilities.json")

  file(MAKE_DIRECTORY "${capability_source_dir}")

  file(
    WRITE "${capability_source_dir}/direct_parameter_annotations.cxx"
    [=[
import std;

struct Marker final {
  int value;
};

auto annotated(int value [[= Marker{42}]]) -> void {
  (void)value;
}

consteval auto supportsDirectParameterAnnotations() -> bool {
  constexpr auto parameters =
      std::define_static_array(std::meta::parameters_of(^^annotated));

  if constexpr (parameters.size() != 1)
    return false;

  constexpr auto annotations =
      std::define_static_array(std::meta::annotations_of(parameters.front()));

  if constexpr (annotations.size() != 1)
    return false;

  constexpr Marker marker = std::meta::extract<Marker>(annotations.front());
  return marker.value == 42;
}

static_assert(supportsDirectParameterAnnotations());

auto main() -> int {
  return 0;
}
]=])

  file(
    WRITE "${capability_source_dir}/aggregate_member_synthesis.cxx"
    [=[
import std;

struct Storage;

consteval {
  std::meta::define_aggregate(
      ^^Storage,
      {
          std::meta::data_member_spec(
              ^^int,
              std::meta::data_member_options{.name = "_"}),
      });
}

auto main() -> int {
  Storage storage{42};
  (void)storage;
  return 0;
}
]=])

  file(
    WRITE "${capability_source_dir}/std_scope.cxx"
    [=[
import std;

auto main() -> int {
  bool invoked{};

  {
    std::scope_exit guard([&invoked] {
      invoked = true;
    });
  }

  return invoked ? 0 : 1;
}
]=])

  file(
    WRITE "${capability_source_dir}/CMakeLists.txt"
    [=[
cmake_minimum_required(VERSION 4.4 FATAL_ERROR)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_VERSION VERSION_LESS 4.5
   AND NOT DEFINED CMAKE_EXPERIMENTAL_CXX_IMPORT_STD)
  set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
      "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
endif()

project(SwitchCapabilities LANGUAGES CXX)

if(NOT 26 IN_LIST CMAKE_CXX_COMPILER_IMPORT_STD)
  message(FATAL_ERROR "Switch capability probes require C++26 import std support.")
endif()

foreach(capability IN ITEMS
        direct_parameter_annotations
        aggregate_member_synthesis
        std_scope)
  add_executable(
    "capability_${capability}"
    "${CMAKE_CURRENT_SOURCE_DIR}/${capability}.cxx")

  target_compile_features("capability_${capability}" PRIVATE cxx_std_26)

  set_target_properties(
    "capability_${capability}"
    PROPERTIES CXX_STANDARD 26
               CXX_STANDARD_REQUIRED ON
               CXX_EXTENSIONS OFF
               CXX_MODULE_STD ON)
endforeach()
]=])

  set(configure_command "${CMAKE_COMMAND}" -S "${capability_source_dir}" -B
                        "${capability_build_dir}" -G "${CMAKE_GENERATOR}")

  # A selected toolchain owns compiler/runtime mode completely. In particular,
  # Switch must not reconstruct implementation-specific compiler flags.
  if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND configure_command
         "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
  else()
    list(APPEND configure_command "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")

    if(CMAKE_CXX_FLAGS)
      list(APPEND configure_command "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}")
    endif()
  endif()

  if(CMAKE_MAKE_PROGRAM)
    list(APPEND configure_command "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
  endif()

  if(CMAKE_BUILD_TYPE)
    list(APPEND configure_command "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
  endif()

  execute_process(
    COMMAND ${configure_command}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_stdout
    ERROR_VARIABLE configure_stderr)

  file(WRITE "${capability_root}/configure.log"
       "${configure_stdout}\n${configure_stderr}")

  if(NOT configure_result EQUAL 0)
    string(
      CONCAT
        failure_message
        "Switch could not configure its C++26 capability probe project.\n\n"
        "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\n\n"
        "Log: ${capability_root}/configure.log")
    message(FATAL_ERROR "${failure_message}")
  endif()

  set(required_capability_names direct_parameter_annotations
                                aggregate_member_synthesis)
  set(compatibility_capability_names std_scope)
  set(capability_names ${required_capability_names}
                       ${compatibility_capability_names})

  set(capability_description_direct_parameter_annotations
      "C++26 direct annotations on reflected function parameters, including parameters_of, annotations_of, and extract"
  )
  set(capability_description_aggregate_member_synthesis
      "C++26 define_aggregate plus data_member_spec with explicit data_member_options"
  )
  set(capability_description_std_scope
      "std::scope_exit exported through the named standard-library module")

  set(missing_capabilities)

  foreach(capability IN LISTS capability_names)
    set(build_command "${CMAKE_COMMAND}" --build "${capability_build_dir}"
                      --target "capability_${capability}")

    if(CMAKE_BUILD_TYPE)
      list(APPEND build_command --config "${CMAKE_BUILD_TYPE}")
    endif()

    execute_process(
      COMMAND ${build_command}
      RESULT_VARIABLE build_result
      OUTPUT_VARIABLE build_stdout
      ERROR_VARIABLE build_stderr)

    set(capability_log "${capability_root}/${capability}.log")
    file(WRITE "${capability_log}" "${build_stdout}\n${build_stderr}")

    string(TOUPPER "${capability}" capability_upper)

    if(build_result EQUAL 0)
      set(capability_ok TRUE)
      set(capability_json true)
      message(STATUS "Switch capability ${capability}: yes")
    else()
      set(capability_ok FALSE)
      set(capability_json false)
      message(STATUS "Switch capability ${capability}: no")

      if("${capability}" IN_LIST required_capability_names)
        list(
          APPEND
          missing_capabilities
          "${capability}|${capability_description_${capability}}|${capability_log}"
        )
      endif()
    endif()

    set("SWITCH_CAPABILITY_${capability_upper}"
        "${capability_ok}"
        CACHE
          INTERNAL
          "Whether the selected toolchain provides Switch capability ${capability}"
          FORCE)

    set("capability_json_${capability}" "${capability_json}")
  endforeach()

  string(
    CONCAT
      capability_json
      "{\n"
      "  \"schemaVersion\": 1,\n"
      "  \"required\": {\n"
      "    \"direct_parameter_annotations\": ${capability_json_direct_parameter_annotations},\n"
      "    \"aggregate_member_synthesis\": ${capability_json_aggregate_member_synthesis}\n"
      "  },\n"
      "  \"compatibilityManaged\": {\n"
      "    \"std_scope\": ${capability_json_std_scope}\n"
      "  }\n"
      "}\n")

  file(WRITE "${capability_report}" "${capability_json}")

  if(NOT SWITCH_CAPABILITY_STD_SCOPE)
    message(STATUS "Switch scope compatibility: internal scope-exit guard")
  endif()

  if(missing_capabilities)
    string(
      CONCAT
        failure_message "Switch cannot build with the selected toolchain.\n\n"
        "Missing capabilities required by the current Switch source revision:\n"
    )

    foreach(item IN LISTS missing_capabilities)
      string(REPLACE "|" ";" fields "${item}")
      list(GET fields 0 name)
      list(GET fields 1 description)
      list(GET fields 2 log)

      string(APPEND failure_message "  - ${name}: ${description}\n"
             "    ${log}\n")
    endforeach()

    string(
      APPEND
      failure_message
      "\n"
      "Miracle validates the shared C++26 foundation. These are the additional "
      "capabilities required specifically by Switch's current source revision. "
      "Compatibility-managed capabilities may use internal implementations "
      "without changing Switch's public API. These probes are not an exhaustive "
      "C++26 conformance suite.\n\n"
      "Capability report: ${capability_report}")

    message(FATAL_ERROR "${failure_message}")
  endif()
endfunction()
