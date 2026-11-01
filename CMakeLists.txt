cmake_minimum_required(VERSION 3.25)

project(ECloudAssistant LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ECloudAssistant uses Windows screen/audio capture and input APIs.")
endif()

if(DEFINED ENV{QTDIR} AND NOT CMAKE_PREFIX_PATH)
    list(APPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia OpenGL)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Network Multimedia OpenGL)
if(QT_VERSION_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS OpenGLWidgets)
endif()

set(FFMPEG_ROOT "" CACHE PATH "FFmpeg installation root")
set(FFMPEG_INCLUDE_HINTS "$ENV{FFMPEG_INCLUDE}")
set(FFMPEG_LIBRARY_HINTS "$ENV{FFMPEG_LIB}")
foreach(root IN ITEMS "${FFMPEG_ROOT}" "$ENV{FFMPEG_HOME}")
    if(root)
        list(APPEND FFMPEG_INCLUDE_HINTS "${root}/include")
        list(APPEND FFMPEG_LIBRARY_HINTS "${root}/lib")
    endif()
endforeach()

find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
    HINTS ${FFMPEG_INCLUDE_HINTS} REQUIRED)

foreach(component IN ITEMS avcodec avdevice avformat avutil swresample swscale)
    string(TOUPPER "${component}" upper_component)
    find_library(FFMPEG_${upper_component}_LIBRARY NAMES ${component}
        HINTS ${FFMPEG_LIBRARY_HINTS} REQUIRED)
    list(APPEND FFMPEG_LIBRARIES "${FFMPEG_${upper_component}_LIBRARY}")
endforeach()

file(GLOB PROJECT_ROOT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
file(GLOB_RECURSE PROJECT_MODULE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/UI/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Net/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Net/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Codec/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Codec/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Pusher/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Pusher/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Puller/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Puller/*.h")

# The standalone Net demo and its unused server implementation are separate from this app.
foreach(unused IN ITEMS
    Net/main.cpp
    Net/Acceptor.cpp Net/Acceptor.h
    Net/EpoolTaskScheduler.cpp Net/EpoolTaskScheduler.h
    Net/TcpServer.cpp Net/TcpServer.h)
    list(REMOVE_ITEM PROJECT_MODULE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${unused}")
endforeach()

set(PROJECT_SOURCES ${PROJECT_ROOT_SOURCES} ${PROJECT_MODULE_SOURCES} res.qrc)

add_executable(ECloudAssistant WIN32 ${PROJECT_SOURCES})
set_target_properties(ECloudAssistant PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    AUTOMOC ON
    AUTORCC ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_include_directories(ECloudAssistant PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    UI/title UI/center UI/list UI/tool Net Codec Pusher Pusher/capture Pusher/rtmp
    Puller/UI Puller/Render
    "${FFMPEG_INCLUDE_DIR}")
target_link_libraries(ECloudAssistant PRIVATE
    Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Multimedia Qt${QT_VERSION_MAJOR}::OpenGL
    ${FFMPEG_LIBRARIES}
    ws2_32 gdi32 user32 ole32 ksuser)
if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(ECloudAssistant PRIVATE Qt6::OpenGLWidgets)
endif()
