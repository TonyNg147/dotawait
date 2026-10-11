# Wraps the generate-then-compile pattern for `.async.cpp` sources, the same
# spirit as qt_wrap_cpp/protobuf_generate_cpp: each source is run through the
# asynctp CLI tool as a build step, producing a plain .cpp that is then
# compiled normally by the real C++ compiler.
#
# Usage -- mix .async.cpp and ordinary sources freely; asyncrt is linked
# for you:
#   add_async_executable(my_app main.async.cpp util.cpp)
#   add_async_library(my_lib STATIC net.async.cpp)
#
#   # or on a target you created yourself:
#   add_executable(my_app main.cpp)
#   target_async_sources(my_app PRIVATE worker.async.cpp)
#
# Generated files keep `#include "..."` working relative to the original
# .async.cpp, and carry #line directives so compiler errors point at it.

# Transpiles each `.async.cpp` in ARGN into ${dir}/generated/; any other
# source is passed through unchanged. Low-level building block for the
# helpers below.
function(async_transpile_sources OUT_VAR)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "OUTPUT_DIR" "")
  if(NOT arg_OUTPUT_DIR)
    set(arg_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
  endif()

  set(result "")
  foreach(src IN LISTS arg_UNPARSED_ARGUMENTS)
    get_filename_component(abs_src "${src}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT abs_src MATCHES "\\.async\\.cpp$")
      list(APPEND result "${abs_src}")
      continue()
    endif()

    # Mirror the source's path relative to this directory so a/x.async.cpp
    # and b/x.async.cpp don't collide.
    file(RELATIVE_PATH rel_src "${CMAKE_CURRENT_SOURCE_DIR}" "${abs_src}")
    string(REPLACE "../" "__/" rel_src "${rel_src}")
    string(REGEX REPLACE "\\.async\\.cpp$" ".cpp" rel_out "${rel_src}")
    set(out_path "${arg_OUTPUT_DIR}/${rel_out}")
    get_filename_component(out_dir "${out_path}" DIRECTORY)

    add_custom_command(
      OUTPUT "${out_path}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${out_dir}"
      COMMAND asynctp "${abs_src}" -o "${out_path}"
      DEPENDS "${abs_src}" asynctp
      COMMENT "Transpiling async source ${src}"
      VERBATIM
    )
    list(APPEND result "${out_path}")
  endforeach()
  set(${OUT_VAR} "${result}" PARENT_SCOPE)
endfunction()

# target_async_sources(<target> [PRIVATE|PUBLIC|INTERFACE] <sources>...)
# Adds sources to an existing target, transpiling any `.async.cpp` among
# them, and links the target against asyncrt.
function(target_async_sources target)
  cmake_parse_arguments(PARSE_ARGV 1 arg "PRIVATE;PUBLIC;INTERFACE" "" "")
  set(scope PRIVATE)
  if(arg_PUBLIC)
    set(scope PUBLIC)
  elseif(arg_INTERFACE)
    set(scope INTERFACE)
  endif()

  async_transpile_sources(generated ${arg_UNPARSED_ARGUMENTS}
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/asynctp_generated/${target}")
  target_sources(${target} ${scope} ${generated})
  target_link_libraries(${target} ${scope} asyncrt)

  # The generated .cpp lives in the build tree, so give it the original
  # source directories to resolve quoted includes against.
  foreach(src IN LISTS arg_UNPARSED_ARGUMENTS)
    if(src MATCHES "\\.async\\.cpp$")
      get_filename_component(abs_src "${src}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
      get_filename_component(src_dir "${abs_src}" DIRECTORY)
      target_include_directories(${target} PRIVATE "${src_dir}")
    endif()
  endforeach()
endfunction()

# add_async_executable(<name> <sources>...)
function(add_async_executable name)
  add_executable(${name})
  target_async_sources(${name} PRIVATE ${ARGN})
endfunction()

# add_async_library(<name> [STATIC|SHARED|OBJECT] <sources>...)
function(add_async_library name)
  set(kind "")
  if(ARGV1 MATCHES "^(STATIC|SHARED|OBJECT|MODULE)$")
    set(kind ${ARGV1})
    list(REMOVE_AT ARGN 0)
  endif()
  add_library(${name} ${kind})
  target_async_sources(${name} PRIVATE ${ARGN})
  # Consumers of the library get asyncrt too, since its headers almost
  # certainly expose asyncrt::Task.
  target_link_libraries(${name} PUBLIC asyncrt)
endfunction()
