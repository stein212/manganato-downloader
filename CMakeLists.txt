cmake_minimum_required(VERSION 3.17)

project(manganato-downloader)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "-g -Wall -Werror -pedantic -fopenmp")
set(CMAKE_LD_FLAGS "-lpthreads")

find_package(OpenSSL REQUIRED)
find_package(tbb REQUIRED)

# https://stackoverflow.com/questions/66372317/use-find-package-on-external-project
include(FetchContent)
include(ExternalProject)

FetchContent_Declare(cpp-httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.10.9
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(cpp-httplib)

FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 9.0.0
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(fmt)

FetchContent_Declare(lexbor
    GIT_REPOSITORY https://github.com/lexbor/lexbor.git
    GIT_TAG v2.1.0
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(lexbor)

file(GLOB SOURCES *.cxx)
add_executable(manganato-downloader ${SOURCES})

# target_precompile_headers(httplib INTERFACE "${HTTPLIB_HEADER_PATH}")
target_link_libraries(manganato-downloader
    PRIVATE
    httplib::httplib
    fmt::fmt
    lexbor
)

target_include_directories(manganato-downloader
    PRIVATE
    ${LEXBOR_INCLUDES}
)

# macOS homebrew llvm
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
target_include_directories(manganato-downloader
    PRIVATE
    /opt/homebrew/opt/llvm/lib
)

target_link_directories(manganato-downloader
    PRIVATE
    /opt/homebrew/opt/llvm/include
)

endif()
