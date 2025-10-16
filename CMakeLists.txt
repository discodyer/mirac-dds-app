cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/modules/libmicroxrcedds)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(MiracDDS)

# zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

if(CONFIG_MICROXRCEDDSCLIENT)
  target_link_libraries(app PRIVATE microxrceddsclient)
endif()

target_sources(app PRIVATE src/main.cpp)
