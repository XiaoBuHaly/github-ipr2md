cmake_minimum_required(VERSION 3.20)

project(github-ipr2md LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

find_package(Threads REQUIRED)

FetchContent_Declare(
  CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG v2.6.1
)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(CLI11 nlohmann_json)

set(GHX_SOURCES
  src/main.cpp
  src/process.h
  src/progress.cpp
  src/progress.h
  src/repo_detect.cpp
  src/repo_detect.h
  src/json_model.h
  src/json_convert.cpp
  src/json_convert.h
  src/gh_graphql.cpp
  src/gh_graphql.h
  src/md_render.cpp
  src/md_render.h
  src/stats.cpp
  src/stats.h
)

if(WIN32)
  list(APPEND GHX_SOURCES src/process_win.cpp)
else()
  list(APPEND GHX_SOURCES src/process_posix.cpp)
endif()

add_executable(github-ipr2md ${GHX_SOURCES})

target_link_libraries(github-ipr2md
  PRIVATE
    CLI11::CLI11
    nlohmann_json::nlohmann_json
    Threads::Threads
)

option(ENABLE_WERROR "Treat compiler warnings as errors (recommended for CI)" OFF)

target_compile_options(github-ipr2md PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
  "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>"
  $<$<AND:$<BOOL:${ENABLE_WERROR}>,$<CXX_COMPILER_ID:MSVC>>:/WX>
  $<$<AND:$<BOOL:${ENABLE_WERROR}>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-Werror>
)

enable_testing()

# Minimal smoke tests (no network / no gh dependency)
add_test(NAME help_smoke COMMAND $<TARGET_FILE:github-ipr2md> --help)

add_test(
  NAME in_mode_smoke
  COMMAND $<TARGET_FILE:github-ipr2md>
    --in ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/min_issue_list.json
    --out ${CMAKE_CURRENT_BINARY_DIR}/ctest-out.md
    --no-progress
    --no-stats
    --idempotent
)
# Avoid parallel tests clobbering the same output file.
set_tests_properties(in_mode_smoke PROPERTIES RUN_SERIAL TRUE)

# Optional smoke test for the subprocess runner (no network).
# Only enabled if git is available at configure time.
find_program(GIT_EXECUTABLE git)
if(GIT_EXECUTABLE)
  if(WIN32)
    set(GHX_PROCESS_SRC src/process_win.cpp)
  else()
    set(GHX_PROCESS_SRC src/process_posix.cpp)
  endif()

  add_executable(ghx_process_smoke
    tests/process_smoke.cpp
    src/process.h
    ${GHX_PROCESS_SRC}
  )
  target_link_libraries(ghx_process_smoke PRIVATE Threads::Threads)
  target_compile_options(ghx_process_smoke PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
  add_test(NAME process_smoke_git COMMAND $<TARGET_FILE:ghx_process_smoke> ${GIT_EXECUTABLE})
endif()


