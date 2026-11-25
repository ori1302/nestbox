cmake_minimum_required(VERSION 3.20)
project(nestbox VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(NESTBOX_WERROR "Treat compiler warnings as errors" OFF)
set(NESTBOX_TEST_PETS "$ENV{NESTBOX_TEST_PETS}" CACHE PATH "A folder of extra .egg files for the tests to check")

add_library(nestbox_warnings INTERFACE)
target_compile_options(nestbox_warnings INTERFACE -Wall -Wextra $<$<BOOL:${NESTBOX_WERROR}>:-Werror>)

# The engine: portable C++17 without platform headers, built unchanged into the firmware and the simulator.
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS engine/*.cpp)
add_library(nestbox_engine STATIC ${ENGINE_SOURCES})
target_compile_definitions(nestbox_engine PUBLIC NESTBOX_VERSION="${PROJECT_VERSION}")
target_include_directories(nestbox_engine PUBLIC engine)
target_link_libraries(nestbox_engine PRIVATE nestbox_warnings)

# Tools: docgen prints what the engine serves (the API page, and the pet format's contract: signs, required
# animations, the look table); petrecord records a pet's looks and pastimes frame by frame, to check a pet before it
# ships.
add_executable(docgen tools/docgen.cpp)
add_executable(petrecord tools/petrecord.cpp)
foreach(tool docgen petrecord)
  target_link_libraries(${tool} PRIVATE nestbox_engine nestbox_warnings)
endforeach()

if(APPLE)
  # The simulator: the pet in a Mac window, with the same HTTP API as the device.
  find_package(SDL2 REQUIRED)
  add_executable(nestbox-sim
    simulator/main.cpp simulator/options.cpp simulator/sim_device.cpp simulator/window.cpp simulator/shot.cpp
    simulator/image.cpp simulator/files.cpp simulator/http_server.cpp simulator/bonjour.cpp simulator/log.cpp
    simulator/relay_link.cpp)
  target_link_libraries(nestbox-sim PRIVATE nestbox_engine nestbox_warnings SDL2::SDL2 "-framework ApplicationServices"
    "-framework Network")

  # The companion's command line: sends the Mac's activity to the pet, and single messages from scripts and hooks.
  add_executable(nestbox
    companion/mac/cli/main.cpp companion/mac/cli/activity.cpp companion/mac/cli/bonjour.cpp
    companion/mac/cli/commands.cpp companion/mac/cli/http.cpp companion/mac/cli/music.cpp
    companion/mac/cli/pairing_store.cpp companion/mac/cli/sensors.cpp)
  target_include_directories(nestbox PRIVATE engine)
  target_link_libraries(nestbox PRIVATE nestbox_warnings "-framework ApplicationServices" "-framework CoreFoundation")

  # macOS's QR reader, which the setup test points at the pet's screen.
  find_program(SWIFTC swiftc REQUIRED)
  set(QR_DECODE_SOURCE ${CMAKE_SOURCE_DIR}/tests/qr_decode.swift)
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/qr_decode
    COMMAND ${SWIFTC} -O ${QR_DECODE_SOURCE} -o ${CMAKE_BINARY_DIR}/qr_decode
    DEPENDS ${QR_DECODE_SOURCE}
    COMMENT "Building qr_decode")
  add_custom_target(qr_decode ALL DEPENDS ${CMAKE_BINARY_DIR}/qr_decode)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Eggs next to the binaries: pets/ for the simulator, with every egg in pets/ and pets/retro/; test-pets/ for the
# tests, with Nibble, Gander (Nibble packed under another name, so the tests see two pets) and any NESTBOX_TEST_PETS.
set(TEST_PETS_DIR ${CMAKE_BINARY_DIR}/test-pets)
file(GLOB PET_EGGS CONFIGURE_DEPENDS pets/*.egg pets/retro/*.egg)
set(EXTRA_EGGS "")
if(NESTBOX_TEST_PETS)
  file(GLOB EXTRA_EGGS CONFIGURE_DEPENDS ${NESTBOX_TEST_PETS}/*.egg)
endif()
set(TEST_EGGS "")
foreach(egg nibble.egg gander.egg ${EXTRA_EGGS})
  get_filename_component(name ${egg} NAME)
  list(APPEND TEST_EGGS ${TEST_PETS_DIR}/${name})
endforeach()

file(GLOB NIBBLE_FOLDER CONFIGURE_DEPENDS pets/retro/nibble/*)
file(GLOB EGGPACK_SOURCES CONFIGURE_DEPENDS tools/eggpack/*.py)
add_custom_command(OUTPUT ${TEST_PETS_DIR}/gander.egg
  COMMAND ${CMAKE_COMMAND} -E env NESTBOX_BUILD_DIR=${CMAKE_BINARY_DIR} PYTHONDONTWRITEBYTECODE=1
    ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/eggpack/eggpack.py ${CMAKE_SOURCE_DIR}/pets/retro/nibble
    -o ${TEST_PETS_DIR}/gander.egg --name Gander
  DEPENDS docgen ${NIBBLE_FOLDER} ${EGGPACK_SOURCES}
  COMMENT "Packing Nibble as Gander for the tests")
# Nibble with four known scene parts (tests/ambient_pet.py: a waterfall day and night, two parts at night only, and
# one shown on cue over a filled box), for egg_test's ambient checks.
set(AMBIENT_EGG ${CMAKE_BINARY_DIR}/ambient.egg)
add_custom_command(OUTPUT ${AMBIENT_EGG}
  COMMAND ${CMAKE_COMMAND} -E env NESTBOX_BUILD_DIR=${CMAKE_BINARY_DIR} PYTHONDONTWRITEBYTECODE=1
    ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tests/ambient_pet.py ${CMAKE_SOURCE_DIR}/pets/retro ${AMBIENT_EGG}
  DEPENDS docgen ${NIBBLE_FOLDER} ${EGGPACK_SOURCES} ${CMAKE_SOURCE_DIR}/tests/ambient_pet.py
  COMMENT "Packing Nibble with moving scene parts for the tests")
add_custom_target(eggs ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory pets ${TEST_PETS_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PET_EGGS} pets/
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/pets/retro/nibble.egg ${EXTRA_EGGS}
    ${TEST_PETS_DIR}/
  DEPENDS ${TEST_PETS_DIR}/gander.egg ${AMBIENT_EGG}
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Copying eggs for the simulator and the tests")

# Tests. Labels: quick (seconds, no network), network (a real simulator on a local port; macOS only).
enable_testing()
set(NIBBLE ${TEST_PETS_DIR}/nibble.egg)

foreach(t api_test brain_test egg_test guide_test host_test hud_test lap_test move_test path_test pets_test pond_test
          relay_test screens_test slices_test screen_palette_test)
  add_executable(${t} tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE nestbox_engine nestbox_warnings)
endforeach()

add_test(NAME api COMMAND api_test)
add_test(NAME brain COMMAND brain_test)
add_test(NAME egg COMMAND egg_test ${NIBBLE} ${AMBIENT_EGG})
add_test(NAME guide COMMAND guide_test ${TEST_EGGS})
add_test(NAME host COMMAND host_test)
add_test(NAME relay COMMAND relay_test)
add_test(NAME hud COMMAND hud_test ${TEST_EGGS})
add_test(NAME lap COMMAND lap_test ${TEST_EGGS})
add_test(NAME move COMMAND move_test ${TEST_EGGS})
add_test(NAME path COMMAND path_test ${TEST_EGGS})
add_test(NAME pets COMMAND pets_test ${NIBBLE} ${TEST_PETS_DIR}/gander.egg)
add_test(NAME pond COMMAND pond_test ${NIBBLE} ${CMAKE_SOURCE_DIR}/tests/fixtures/nibble_water.txt)
add_test(NAME screens COMMAND screens_test ${TEST_PETS_DIR} ${CMAKE_BINARY_DIR}/screens)
add_test(NAME slices COMMAND slices_test ${TEST_EGGS})
add_test(NAME screen_palette COMMAND screen_palette_test ${TEST_EGGS})
add_test(NAME layers COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/check_layers.py)
add_test(NAME pack COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/pack_test.py --build ${CMAKE_BINARY_DIR})
add_test(NAME studio COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/studio_test.py --build ${CMAKE_BINARY_DIR})
set_tests_properties(api brain egg guide host hud lap move path pets pond relay screens slices screen_palette layers pack
  studio
  PROPERTIES LABELS quick)

if(APPLE)
  foreach(display lcd-1.69 round-240)
    foreach(t e2e setup)
      add_test(NAME ${t}_${display}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/${t}_test.py --build ${CMAKE_BINARY_DIR})
      set_tests_properties(${t}_${display} PROPERTIES
        ENVIRONMENT NESTBOX_DISPLAY=${display} LABELS network TIMEOUT 900 RUN_SERIAL TRUE)
    endforeach()
  endforeach()
endif()
