cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/version.txt PROJECT_VER LIMIT_COUNT 1)
project(pocket-games)
