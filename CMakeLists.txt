cmake_minimum_required(VERSION 3.20)
project(IREngine LANGUAGES C CXX)

# Keep generated build files separate from the project source files.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
            "Use a separate build folder, for example: cmake -S . -B build")
endif()

# Build all programs with the same supported language and compiler settings.
if(CMAKE_SIZEOF_VOID_P LESS 8)
    message(FATAL_ERROR "Use a 64-bit compiler for this project.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static dependencies" FORCE)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)
find_package(Threads REQUIRED)

# Check the required stoplist before downloading or building any dependencies.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/stoplist.txt")
    message(FATAL_ERROR
            "Missing resources/stoplist.txt. Rename resources/stopwordlist.txt "
            "to stoplist.txt, then reload CMake.")
endif()

# Find each program once without depending on one editor's file layout.
function(find_program_source output_variable filename)
    set(matches)

    foreach(candidate "src/${filename}" "${filename}")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${candidate}")
            list(APPEND matches "${candidate}")
        endif()
    endforeach()

    list(LENGTH matches match_count)

    if(match_count EQUAL 0)
        message(FATAL_ERROR
                "Missing ${filename}. Keep it in src or the project root.")
    elseif(match_count GREATER 1)
        message(FATAL_ERROR
                "Found both src/${filename} and ${filename}. "
                "Keep one complete updated copy so the wrong version is not built.")
    endif()

    list(GET matches 0 source_file)
    set(${output_variable} "${source_file}" PARENT_SCOPE)

    message(STATUS "Using ${source_file}")
endfunction()

find_program_source(engine_source main.cpp)
find_program_source(input_source input_main.cpp)
find_program_source(parser_tests_source parser_tests.cpp)
find_program_source(application_tests_source application_tests.cpp)

# Download the existing dependencies and the graphical file chooser header.
FetchContent_Declare(json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)

FetchContent_Declare(sqlite
        URL https://sqlite.org/2025/sqlite-amalgamation-3500400.zip)

FetchContent_Declare(miniz
        GIT_REPOSITORY https://github.com/richgel999/miniz.git
        GIT_TAG 3.0.2)

set(EXPAT_LARGE_SIZE ON CACHE BOOL "" FORCE)
set(EXPAT_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(EXPAT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(EXPAT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(EXPAT_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(EXPAT_BUILD_DOCS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(expat
        URL https://github.com/libexpat/libexpat/releases/download/R_2_7_1/expat-2.7.1.tar.xz)

FetchContent_Declare(portable_dialogs
        URL https://github.com/samhocevar/portable-file-dialogs/archive/refs/tags/0.1.0.tar.gz
        SOURCE_SUBDIR unused_header_only_directory)

FetchContent_MakeAvailable(
        json
        sqlite
        miniz
        expat
        portable_dialogs)

# Keep build discovered dependency locations outside application runtime path settings.
add_library(ir_sqlite STATIC
        "${sqlite_SOURCE_DIR}/sqlite3.c")

target_include_directories(ir_sqlite SYSTEM PUBLIC
        "${sqlite_SOURCE_DIR}")

target_link_libraries(ir_sqlite PUBLIC
        Threads::Threads
        ${CMAKE_DL_LIBS})

if(UNIX)
    target_link_libraries(ir_sqlite PUBLIC m)
endif()

add_library(ir_core STATIC
        src/TextParser.cpp
        src/PorterStemmer.cpp
        src/TrecReader.cpp
        src/OutputWriter.cpp
        src/CorpusReader.cpp
        src/ParserApplication.cpp
        src/StreamingWorkbook.cpp)

target_include_directories(ir_core PUBLIC src)

target_include_directories(ir_core SYSTEM PRIVATE
        "${portable_dialogs_SOURCE_DIR}")

target_include_directories(ir_core SYSTEM PUBLIC
        "${miniz_SOURCE_DIR}")

target_compile_definitions(ir_core PUBLIC
        _FILE_OFFSET_BITS=64)

target_link_libraries(ir_core PUBLIC
        Threads::Threads
        nlohmann_json::nlohmann_json
        ir_sqlite
        miniz
        expat)

if(WIN32)
    target_compile_definitions(ir_core PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN)

    target_link_libraries(ir_core PUBLIC
            comdlg32
            ole32
            shell32
            user32
            advapi32
            uuid)
endif()

if(MSVC)
    target_compile_options(ir_core PRIVATE
            /W4
            /utf-8)
else()
    target_compile_options(ir_core PRIVATE
            -Wall
            -Wextra
            -Wpedantic)
endif()

# Copy the required stoplist so build folders support relative runtime paths.
configure_file(
        resources/stoplist.txt
        resources/stoplist.txt
        COPYONLY)

# Allow graphical file selection even when the bundled sample is missing.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/corpus/sample.trec")
    configure_file(
            resources/corpus/sample.trec
            resources/corpus/sample.trec
            COPYONLY)
else()
    message(STATUS
            "No bundled sample was found. Use InputParser to choose a corpus, "
            "or restore resources/corpus/sample.trec for automatic sample runs.")
endif()

add_executable(IREngine
        "${engine_source}")

add_executable(InputParser
        "${input_source}")

add_executable(ParserTests
        "${parser_tests_source}")

add_executable(ApplicationTests
        "${application_tests_source}")

foreach(target IREngine InputParser ParserTests ApplicationTests)
    target_link_libraries(${target} PRIVATE ir_core)

    set_target_properties(${target} PROPERTIES
            XCODE_GENERATE_SCHEME TRUE
            XCODE_SCHEME_WORKING_DIRECTORY "$(PROJECT_DIR)"
            VS_DEBUGGER_WORKING_DIRECTORY "$(ProjectDir)")
endforeach()

# Run isolated tests without opening dialogs or replacing the normal outputs.
enable_testing()

add_test(
        NAME parser_tests
        COMMAND ParserTests --self-test)

add_test(
        NAME application_tests
        COMMAND ApplicationTests --self-test)

add_custom_target(run_parser
        COMMAND $<TARGET_FILE:IREngine>
        DEPENDS IREngine
        USES_TERMINAL
        VERBATIM)

add_custom_target(run_input
        COMMAND $<TARGET_FILE:InputParser>
        DEPENDS InputParser
        USES_TERMINAL
        VERBATIM)