cmake_minimum_required(VERSION 3.16)
project(bulwark VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(BULWARK_BUILD_GUI "Build the GTK4 GUI" ON)
option(BULWARK_BUILD_CLI "Build the command line tool" ON)
option(BULWARK_BUILD_TESTS "Build offline unit tests" ON)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0>=1.0.27)
find_package(nlohmann_json 3.10 REQUIRED)

# ---- core (static, used by the CLI, GUI, tests and the shared library) ----
add_library(bulwark_core STATIC
  src/core/codec.cpp
  src/core/controller.cpp
  src/core/devices.cpp
  src/core/gip.cpp
  src/core/hex.cpp
  src/core/names.cpp
  src/core/profile.cpp
  src/core/protocol.cpp
  src/core/sim_transport.cpp
  src/core/transport.cpp
  src/core/usb_transport.cpp)
target_include_directories(bulwark_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
target_compile_options(bulwark_core PRIVATE -Wall -Wextra)
set_target_properties(bulwark_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(bulwark_core PUBLIC PkgConfig::LIBUSB nlohmann_json::nlohmann_json)

# ---- public C library -----------------------------------------------------
add_library(bulwark SHARED src/lib/capi.cpp)
target_include_directories(bulwark PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_definitions(bulwark PRIVATE BULWARK_BUILDING_SHARED)
target_compile_options(bulwark PRIVATE -Wall -Wextra)
target_link_options(bulwark PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/lib/bulwark.map")
set_target_properties(bulwark PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/bulwark.map)
target_link_libraries(bulwark PRIVATE bulwark_core)

configure_file(data/bulwark.pc.in ${CMAKE_CURRENT_BINARY_DIR}/bulwark.pc @ONLY)
install(TARGETS bulwark LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/bulwark DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/bulwark.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
install(FILES udev/70-bulwark.rules DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d)

# ---- CLI ------------------------------------------------------------------
if(BULWARK_BUILD_CLI)
  add_executable(bulwark-cli src/cli/main.cpp)
  target_compile_options(bulwark-cli PRIVATE -Wall -Wextra)
  target_link_libraries(bulwark-cli PRIVATE bulwark_core)
  install(TARGETS bulwark-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ---- GUI ------------------------------------------------------------------
if(BULWARK_BUILD_GUI)
  pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.12)
  # Embed the stylesheet so the GUI also works from the build tree.
  file(READ ${CMAKE_CURRENT_SOURCE_DIR}/data/bulwark.css BULWARK_CSS)
  configure_file(src/gui/css.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/css.h @ONLY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS data/bulwark.css src/gui/css.h.in)
  add_executable(bulwark-gui
    src/gui/main.cpp
    src/gui/app.cpp
    src/gui/pages.cpp
    src/gui/art.cpp
    src/gui/widgets.cpp)
  target_compile_definitions(bulwark-gui PRIVATE BULWARK_VERSION="${PROJECT_VERSION}")
  target_include_directories(bulwark-gui PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
  target_compile_options(bulwark-gui PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
  target_link_libraries(bulwark-gui PRIVATE bulwark_core PkgConfig::GTK4)
  set_target_properties(bulwark-gui PROPERTIES OUTPUT_NAME bulwark)
  install(TARGETS bulwark-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES data/bulwark.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES data/bulwark.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
  # simplified drawing for small sizes (fewer details, heavier shapes)
  foreach(size 16 24 32)
    install(FILES data/bulwark-small.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
            RENAME bulwark.svg)
  endforeach()
endif()

# ---- tests ----------------------------------------------------------------
if(BULWARK_BUILD_TESTS)
  enable_testing()
  add_executable(bulwark-test tests/test_main.cpp)
  target_compile_options(bulwark-test PRIVATE -Wall -Wextra)
  target_compile_definitions(bulwark-test PRIVATE GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
  target_link_libraries(bulwark-test PRIVATE bulwark_core)
  add_test(NAME core COMMAND bulwark-test)

  add_executable(bulwark-test-capi tests/test_capi.c)
  target_link_libraries(bulwark-test-capi PRIVATE bulwark)
  add_test(NAME capi COMMAND bulwark-test-capi)

  if(BULWARK_BUILD_GUI)
    add_test(NAME gui-self-test COMMAND bulwark-gui --self-test)
    set_tests_properties(gui-self-test PROPERTIES SKIP_RETURN_CODE 77)
  endif()
endif()
