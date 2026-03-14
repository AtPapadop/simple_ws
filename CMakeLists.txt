cmake_minimum_required(VERSION 3.16)
project(simple_ws C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(simple_ws
  src/sha1.c
  src/base64.c
  src/wshandshake.c
  src/websocket.c
  src/wsserver.c
)

target_include_directories(simple_ws 
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(simple_ws
  PUBLIC
    pthread
)

set_target_properties(simple_ws
  PROPERTIES
    VERSION 0.1.0
    OUTPUT_NAME simple_ws
)

install(TARGETS simple_ws
  EXPORT simple_wsTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)

install(EXPORT simple_wsTargets
  FILE simple_wsTargets.cmake
  NAMESPACE simple_ws::
  DESTINATION lib/cmake/simple_ws
)