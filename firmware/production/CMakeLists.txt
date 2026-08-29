cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.2.18-dev")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(mcl02m_custom)
