cmake_minimum_required(VERSION 3.24)
project(WowTypeWindows LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The Windows Text Services Framework target must be built on Windows")
endif()
if(NOT DEFINED WOWTYPE_CORE_INCLUDE OR NOT DEFINED WOWTYPE_CORE_IMPORT_LIB)
    message(FATAL_ERROR "Set WOWTYPE_CORE_INCLUDE and WOWTYPE_CORE_IMPORT_LIB")
endif()

add_library(wowtype_text_service SHARED
    src/CandidateWindow.cpp
    src/DllMain.cpp
    src/Registration.cpp
    src/TextService.cpp
    src/WowTypeTextService.def)
target_compile_features(wowtype_text_service PRIVATE cxx_std_20)
target_compile_definitions(wowtype_text_service PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(wowtype_text_service PRIVATE
    "${WOWTYPE_CORE_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(wowtype_text_service PRIVATE
    "${WOWTYPE_CORE_IMPORT_LIB}"
    ole32 uuid user32 gdi32 advapi32)
set_target_properties(wowtype_text_service PROPERTIES
    OUTPUT_NAME "WowTypeTextService"
    PREFIX "")
