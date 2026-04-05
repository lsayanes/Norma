cmake_minimum_required(VERSION 3.16)
project(Norma)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configuration for local development on Mac (for testing)
# For cross-compilation, use: -DCMAKE_TOOLCHAIN_FILE=toolchain-aarch64.cmake

# Configure Qt automoc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt version selection:
# - You can force with: -DQT_MAJOR=6 (or 5), or environment QT_MAJOR=6|5
# - If Qt6_DIR/Qt5_DIR is provided, it takes precedence
set(QT_REQUESTED_MAJOR "" CACHE STRING "Force Qt major version (5 or 6). Empty = auto")
if(DEFINED QT_MAJOR AND NOT "${QT_MAJOR}" STREQUAL "")
  set(QT_REQUESTED_MAJOR "${QT_MAJOR}")
elseif(DEFINED ENV{QT_MAJOR} AND NOT "$ENV{QT_MAJOR}" STREQUAL "")
  set(QT_REQUESTED_MAJOR "$ENV{QT_MAJOR}")
endif()

# Selection priority:
# 1) Explicit QT_MAJOR=6|5 (or env) wins
# 2) If not set, honor explicit DIR hints
# 3) Else auto-detect (Qt6 then Qt5)
if(QT_REQUESTED_MAJOR STREQUAL "6")
  set(QT_VERSION_MAJOR 6)
  find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
elseif(QT_REQUESTED_MAJOR STREQUAL "5")
  set(QT_VERSION_MAJOR 5)
  find_package(Qt5 REQUIRED COMPONENTS Core Widgets Network)
elseif(DEFINED Qt6_DIR)
  set(QT_VERSION_MAJOR 6)
  find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
elseif(DEFINED Qt5_DIR)
  set(QT_VERSION_MAJOR 5)
  find_package(Qt5 REQUIRED COMPONENTS Core Widgets Network)
else()
  # Auto-detect, try Qt6 first then Qt5
  find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Network)
  find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Network)
endif()

set(SOURCES
    main.cpp
    FilePanel.cpp
    Norma.cpp
)

set(HEADERS
    FilePanel.h 
    Norma.h
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Link with Qt
target_link_libraries(${PROJECT_NAME} Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)

# Copy resources folder next to the built binary
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/resources
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/resources
)

# Copy img folder next to the built binary
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/img
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/img
)

# Install rules (for packaging / cross-deploy)
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/img
        DESTINATION share/${PROJECT_NAME})

install(DIRECTORY ${CMAKE_SOURCE_DIR}/resources
        DESTINATION share/${PROJECT_NAME})




find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE GIT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE GIT_DESCRIBE OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE GIT_SHA OUTPUT_STRIP_TRAILING_WHITESPACE)
else()
  set(GIT_BRANCH "unknown")
  set(GIT_DESCRIBE "unknown")
  set(GIT_SHA "unknown")
endif()
string(TIMESTAMP BUILD_TIME "%Y-%m-%d %H:%M:%S")

set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})
# En lugar de generar header, definir macros de compilación (el header git_info.h provee defaults)
target_compile_definitions(${PROJECT_NAME} PRIVATE
  NORMA_GIT_BRANCH="${GIT_BRANCH}"
  NORMA_GIT_DESCRIBE="${GIT_DESCRIBE}"
  NORMA_GIT_SHA="${GIT_SHA}"
  NORMA_BUILD_TIME="${BUILD_TIME}"
)