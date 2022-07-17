cmake_minimum_required(VERSION 3.10)

project(manganato-downloader)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "-g -Wall")

# https://www.scivision.dev/cmake-fetchcontent-vs-external-project/
include(FetchContent)
include(ExternalProject)

ExternalProject_Add(cpp-httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.10.9
)

find_package(openssl REQUIRED)
find_package(httplib REQUIRED)

file(GLOB SOURCES *.cxx)

add_executable(manganato-downloader ${SOURCES})

target_link_libraries(manganato-downloader PRIVATE httplib::httplib)
