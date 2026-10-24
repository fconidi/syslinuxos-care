cmake_minimum_required(VERSION 3.20)
project(syslinuxos_care VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Concurrent LinguistTools)

add_library(system_metrics src/metrics.cpp)
target_include_directories(system_metrics PUBLIC src)
target_link_libraries(system_metrics PUBLIC Qt6::Core)

add_library(file_scanner src/scanner.cpp)
target_include_directories(file_scanner PUBLIC src)
target_link_libraries(file_scanner PUBLIC Qt6::Core)

add_library(autostart_entries src/autostart.cpp)
target_include_directories(autostart_entries PUBLIC src)
target_link_libraries(autostart_entries PUBLIC Qt6::Core)

add_library(process_entries src/processes.cpp)
target_include_directories(process_entries PUBLIC src)
target_link_libraries(process_entries PUBLIC Qt6::Core)

add_library(service_entries src/services.cpp)
target_include_directories(service_entries PUBLIC src)
target_link_libraries(service_entries PUBLIC Qt6::Core)

add_library(package_entries src/packages.cpp)
target_include_directories(package_entries PUBLIC src)
target_link_libraries(package_entries PUBLIC Qt6::Core)

add_library(apt_archive_entries src/aptcache.cpp)
target_include_directories(apt_archive_entries PUBLIC src)
target_link_libraries(apt_archive_entries PUBLIC file_scanner Qt6::Core)

add_library(trash_entries src/trash.cpp)
target_include_directories(trash_entries PUBLIC src)
target_link_libraries(trash_entries PUBLIC Qt6::Core)

add_library(browser_caches src/browsercache.cpp)
target_include_directories(browser_caches PUBLIC src)
target_link_libraries(browser_caches PUBLIC process_entries file_scanner Qt6::Core)

add_library(history_chart src/historychart.cpp)
target_include_directories(history_chart PUBLIC src)
target_link_libraries(history_chart PUBLIC Qt6::Widgets)

add_executable(syslinuxos-care src/main.cpp)
target_link_libraries(syslinuxos-care PRIVATE system_metrics file_scanner autostart_entries process_entries service_entries package_entries apt_archive_entries trash_entries browser_caches history_chart Qt6::Widgets Qt6::Concurrent)
add_executable(syslinuxos-care-helper src/privileged.cpp)
target_link_libraries(syslinuxos-care-helper PRIVATE package_entries service_entries apt_archive_entries Qt6::Core)
qt_add_translations(syslinuxos-care TS_FILES
    translations/syslinuxos-care_it.ts
    translations/syslinuxos-care_fr.ts
    translations/syslinuxos-care_de.ts
    translations/syslinuxos-care_es.ts)

enable_testing()
add_executable(metrics_test tests/metrics_test.cpp)
target_link_libraries(metrics_test PRIVATE system_metrics Qt6::Core)
add_test(NAME metrics_test COMMAND metrics_test)
add_executable(scanner_test tests/scanner_test.cpp)
target_link_libraries(scanner_test PRIVATE file_scanner Qt6::Core)
add_test(NAME scanner_test COMMAND scanner_test)
add_executable(autostart_test tests/autostart_test.cpp)
target_link_libraries(autostart_test PRIVATE autostart_entries Qt6::Core)
add_test(NAME autostart_test COMMAND autostart_test)
add_executable(processes_test tests/processes_test.cpp)
target_link_libraries(processes_test PRIVATE process_entries Qt6::Core)
add_test(NAME processes_test COMMAND processes_test)
add_executable(services_test tests/services_test.cpp)
target_link_libraries(services_test PRIVATE service_entries Qt6::Core)
add_test(NAME services_test COMMAND services_test)
add_executable(packages_test tests/packages_test.cpp)
target_link_libraries(packages_test PRIVATE package_entries Qt6::Core)
add_test(NAME packages_test COMMAND packages_test)
add_executable(translations_test tests/translations_test.cpp)
target_link_libraries(translations_test PRIVATE Qt6::Core)
add_dependencies(translations_test syslinuxos-care)
add_test(NAME translations_test COMMAND translations_test
    ${CMAKE_CURRENT_BINARY_DIR}/syslinuxos-care_it.qm
    ${CMAKE_CURRENT_BINARY_DIR}/syslinuxos-care_fr.qm
    ${CMAKE_CURRENT_BINARY_DIR}/syslinuxos-care_de.qm
    ${CMAKE_CURRENT_BINARY_DIR}/syslinuxos-care_es.qm)
add_executable(trash_test tests/trash_test.cpp)
target_link_libraries(trash_test PRIVATE trash_entries Qt6::Core)
add_test(NAME trash_test COMMAND trash_test)
add_executable(browsercache_test tests/browsercache_test.cpp)
target_link_libraries(browsercache_test PRIVATE browser_caches Qt6::Core)
add_test(NAME browsercache_test COMMAND browsercache_test)
add_executable(aptcache_test tests/aptcache_test.cpp)
target_link_libraries(aptcache_test PRIVATE apt_archive_entries Qt6::Core)
add_test(NAME aptcache_test COMMAND aptcache_test)

foreach(test_target IN ITEMS metrics_test scanner_test autostart_test processes_test
        services_test packages_test translations_test trash_test browsercache_test aptcache_test)
    target_compile_options(${test_target} PRIVATE -UNDEBUG)
endforeach()

install(TARGETS syslinuxos-care RUNTIME DESTINATION bin)
install(TARGETS syslinuxos-care-helper RUNTIME DESTINATION libexec)
install(FILES packaging/syslinuxos-care.desktop DESTINATION share/applications)
install(FILES packaging/org.syslinuxos.care.policy DESTINATION share/polkit-1/actions)
install(FILES README.md LICENSE packaging/copyright DESTINATION share/doc/syslinuxos-care)

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME syslinuxos-care)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Desktop maintenance and monitoring for SysLinuxOS 13 and Debian 13")
set(CPACK_PACKAGE_CONTACT "Franco Conidi")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Franco Conidi")
set(CPACK_DEBIAN_PACKAGE_SECTION admin)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "apt, dpkg, pkexec")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
include(CPack)
