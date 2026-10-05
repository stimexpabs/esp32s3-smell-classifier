cmake_minimum_required(VERSION 3.22)

# The buzzer driver is shared with the data logger, so it lives one level up.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Build only main and what it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

project(inference)
