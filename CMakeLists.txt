cmake_minimum_required(VERSION 3.16)

set(SIMPLE_WS_VERSION 0.1.0)
project(simple_ws VERSION ${SIMPLE_WS_VERSION} LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(SIMPLE_WS_BUILD_STATIC "Build static library" ON)
option(SIMPLE_WS_BUILD_SHARED "Build shared library" OFF)

set(SIMPLE_WS_SOURCES
  src/sha1.c
  src/base64.c
  src/wshandshake.c
  src/websocket.c
  src/wsserver.c
)

if(SIMPLE_WS_BUILD_STATIC)
  add_library(simple_ws STATIC ${SIMPLE_WS_SOURCES})
endif()

if(SIMPLE_WS_BUILD_SHARED)
  add_library(simple_ws_shared SHARED ${SIMPLE_WS_SOURCES})
endif()

if(SIMPLE_WS_BUILD_STATIC)
  target_include_directories(simple_ws
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
  )
endif()

if(SIMPLE_WS_BUILD_SHARED)
  target_include_directories(simple_ws_shared
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
  )
endif()

if(SIMPLE_WS_BUILD_STATIC)
  target_link_libraries(simple_ws
    PUBLIC
      pthread
  )
endif()

if(SIMPLE_WS_BUILD_SHARED)
  target_link_libraries(simple_ws_shared
    PUBLIC
      pthread
  )
endif()

if(SIMPLE_WS_BUILD_STATIC)
  set_target_properties(simple_ws
    PROPERTIES
      VERSION ${SIMPLE_WS_VERSION}
      OUTPUT_NAME simple_ws
  )
endif()

if(SIMPLE_WS_BUILD_SHARED)
  set_target_properties(simple_ws_shared
    PROPERTIES
      VERSION ${SIMPLE_WS_VERSION}
      OUTPUT_NAME simple_ws
  )
endif()

set(SIMPLE_WS_INSTALL_TARGETS)
if(SIMPLE_WS_BUILD_STATIC)
  list(APPEND SIMPLE_WS_INSTALL_TARGETS simple_ws)
endif()
if(SIMPLE_WS_BUILD_SHARED)
  list(APPEND SIMPLE_WS_INSTALL_TARGETS simple_ws_shared)
endif()

install(TARGETS ${SIMPLE_WS_INSTALL_TARGETS}
  EXPORT simple_wsTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)

configure_file(simple_ws.pc.in simple_ws.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/simple_ws.pc DESTINATION lib/pkgconfig)

install(EXPORT simple_wsTargets
  FILE simple_wsTargets.cmake
  NAMESPACE simple_ws::
  DESTINATION lib/cmake/simple_ws
)