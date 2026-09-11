cmake_minimum_required(VERSION 3.24)
project(PathWeave VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(WIN32)
  set(CMAKE_INSTALL_BINDIR ".")
endif()

include(GNUInstallDirs)
include(CTest)

if(MSVC)
  set(CMAKE_DISABLE_FIND_PACKAGE_WrapVulkanHeaders TRUE)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Sql Test PrintSupport)

if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8> $<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
else()
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall> $<$<COMPILE_LANGUAGE:CXX>:-Wextra> $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>)
endif()

add_library(pathweave_core
  src/domain/Schema.cpp src/database/Database.cpp src/matching/MatchingEngine.cpp
  src/services/CareerService.cpp src/services/SecretStore.cpp src/reports/Reports.cpp
  src/services/ResumeBuilder.cpp src/services/DiscoveryPolicy.cpp src/services/BackupCrypto.cpp
  src/networking/NetworkService.cpp src/sources/Sources.cpp
  src/sources/AdzunaSource.cpp src/sources/RemotiveSource.cpp src/sources/GreenhouseSource.cpp
  src/sources/LeverSource.cpp src/sources/GenericJsonSource.cpp src/sources/GenericRssSource.cpp
  src/sources/UserProvidedUrlSource.cpp)

target_include_directories(pathweave_core PUBLIC src)
target_link_libraries(pathweave_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql)
if(WIN32)
  target_link_libraries(pathweave_core PRIVATE crypt32 bcrypt advapi32)
elseif(APPLE)
  find_library(SECURITY_FRAMEWORK Security REQUIRED)
  target_link_libraries(pathweave_core PRIVATE ${SECURITY_FRAMEWORK})
endif()

add_library(pathweave_ui src/ui/MainWindow.cpp src/ui/EntityEditor.cpp src/ui/SurveyDialog.cpp src/ui/ResumeDialog.cpp)
target_link_libraries(pathweave_ui PUBLIC pathweave_core Qt6::Widgets Qt6::PrintSupport)

if(APPLE)
  set(PATHWEAVE_EXEC_KIND MACOSX_BUNDLE)
elseif(WIN32)
  set(PATHWEAVE_EXEC_KIND WIN32)
else()
  set(PATHWEAVE_EXEC_KIND "")
endif()
qt_add_executable(PathWeave ${PATHWEAVE_EXEC_KIND} src/main.cpp resources/resources.qrc)
if(WIN32)
  target_sources(PathWeave PRIVATE resources/PathWeave.rc)
endif()

target_link_libraries(PathWeave PRIVATE pathweave_ui)
target_compile_definitions(PathWeave PRIVATE PATHWEAVE_VERSION="${PROJECT_VERSION}")

if(BUILD_TESTING)
  qt_add_executable(pathweave_enhancement_tests tests/TestEnhancements.cpp)
  target_link_libraries(pathweave_enhancement_tests PRIVATE pathweave_ui Qt6::Test)
  add_test(NAME PathWeave.CVAndEnhancements COMMAND pathweave_enhancement_tests -o enhancement-results.xml,junitxml -o -,txt)
  if(WIN32)
    set_tests_properties(PathWeave.CVAndEnhancements PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows" TIMEOUT 120)
  else()
    set_tests_properties(PathWeave.CVAndEnhancements PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
  endif()

  qt_add_executable(pathweave_source_probe tests/SourceProbe.cpp)
  target_link_libraries(pathweave_source_probe PRIVATE pathweave_core Qt6::Widgets)

  qt_add_executable(pathweave_tests tests/TestPathWeave.cpp tests/TestPathWeave.h)
  target_link_libraries(pathweave_tests PRIVATE pathweave_ui Qt6::Test)
  add_test(NAME PathWeave.UnitAndWorkflow COMMAND pathweave_tests -o test-results.xml,junitxml -o -,txt)
  if(WIN32)
    set_tests_properties(PathWeave.UnitAndWorkflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows" TIMEOUT 90)
  else()
    set_tests_properties(PathWeave.UnitAndWorkflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
  endif()
endif()

if(APPLE)
  install(TARGETS PathWeave BUNDLE DESTINATION .)
else()
  install(TARGETS PathWeave RUNTIME DESTINATION .)
endif()

if(MSVC)
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
  include(InstallRequiredSystemLibraries)
endif()

install(FILES LICENSE docs/THIRD_PARTY_NOTICES.md DESTINATION licenses)
install(DIRECTORY docs/licenses/ DESTINATION licenses/Qt)

qt_generate_deploy_app_script(TARGET PathWeave OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${deploy_script})

set(CPACK_PACKAGE_NAME "PathWeave")
set(CPACK_PACKAGE_VENDOR "PathWeave")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Turn today's work into tomorrow's opportunity.")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "PathWeave")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_SOURCE_DIR}/release")
if(APPLE)
  set(CPACK_PACKAGE_FILE_NAME "PathWeave-Setup-macOS-${CMAKE_SYSTEM_PROCESSOR}")
  set(CPACK_DMG_VOLUME_NAME "PathWeave")
  set(CPACK_GENERATOR DragNDrop)
  set(CPACK_DMG_FORMAT "ULFO")
  set(CPACK_BUNDLE_NAME "PathWeave")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
elseif(WIN32)
  set(CPACK_PACKAGE_FILE_NAME "PathWeave-Setup-x64")
  set(CPACK_GENERATOR NSIS)
  set(CPACK_NSIS_DISPLAY_NAME "PathWeave")
  set(CPACK_NSIS_PACKAGE_NAME "PathWeave")
  set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
  set(CPACK_PACKAGE_EXECUTABLES "PathWeave;PathWeave")
  set(CPACK_NSIS_INSTALLED_ICON_NAME "PathWeave.exe")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  set(CPACK_NSIS_MODIFY_PATH OFF)
  set(CPACK_NSIS_DEFINES "RequestExecutionLevel user")
  set(CPACK_NSIS_INSTALL_ROOT "$LOCALAPPDATA")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
endif()

set(CPACK_POST_BUILD_SCRIPTS "${CMAKE_SOURCE_DIR}/cmake/Artifact.cmake")
include(CPack)



