
cmake_minimum_required(VERSION 3.22)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
endif()
message("Build type: " ${CMAKE_BUILD_TYPE})

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(logic-analyzer LANGUAGES C CXX ASM)
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

add_executable(${PROJECT_NAME})

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.c")

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_sources(${PROJECT_NAME} PRIVATE ${SOURCES})
target_compile_definitions(${PROJECT_NAME} PRIVATE)
target_compile_options(${PROJECT_NAME} PRIVATE -save-temps)

add_subdirectory(cmake/stm32cubemx)
target_link_libraries(${PROJECT_NAME} stm32cubemx)

include(FetchContent)

FetchContent_Declare(
  blola
  GIT_REPOSITORY https://github.com/Chamber6821/blola.git
  GIT_TAG v0.0.2
)
FetchContent_Declare(
  RTT
  GIT_REPOSITORY https://github.com/Chamber6821/segger-rtt.git
  GIT_TAG 8.56a
)

FetchContent_MakeAvailable(blola RTT)
target_link_libraries(${PROJECT_NAME} blola RTT)
target_include_directories(RTT PUBLIC config)

file(CREATE_LINK
  "${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json"
  "${CMAKE_CURRENT_BINARY_DIR}/../compile_commands.json"
  SYMBOLIC)

file(CREATE_LINK
  "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.elf"
  "${CMAKE_CURRENT_BINARY_DIR}/../firmware.elf"
  SYMBOLIC)

file(CREATE_LINK
  "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${PROJECT_NAME}.dir"
  "${CMAKE_CURRENT_BINARY_DIR}/../preprocessed"
  SYMBOLIC)

get_target_property(BLOLA_SCRIP_PATH blola SCRIPT_PATH)
file(CREATE_LINK
  "${BLOLA_SCRIP_PATH}"
  "${CMAKE_CURRENT_BINARY_DIR}/../blola.py"
  SYMBOLIC)


