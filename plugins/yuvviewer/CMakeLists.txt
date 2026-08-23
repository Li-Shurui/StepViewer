# Copyright (C) 2026
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

find_package(OpenCV QUIET COMPONENTS core imgproc)

if(OpenCV_FOUND)
    set(yuvviewer_opencv_include_dirs ${OpenCV_INCLUDE_DIRS})
    set(yuvviewer_opencv_libraries ${OpenCV_LIBS})
else()
    message(STATUS "OpenCV core/imgproc not found; building a minimal bundled copy.")

    set(BUILD_LIST "core,imgproc" CACHE STRING "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_apps OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_java_bindings_generator OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_js_bindings_generator OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_objc_bindings_generator OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_python_bindings_generator OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_python_tests OFF CACHE BOOL "" FORCE)
    set(WITH_FFMPEG OFF CACHE BOOL "" FORCE)
    set(WITH_LAPACK OFF CACHE BOOL "" FORCE)
    set(WITH_OPENCL OFF CACHE BOOL "" FORCE)
    set(WITH_OPENEXR OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        opencv
        GIT_REPOSITORY https://github.com/opencv/opencv.git
        GIT_TAG 4.12.0
        GIT_SHALLOW TRUE
    )

    # qt_standard_project_setup() enables these globally. OpenCV is not a Qt
    # project, and AUTOMOC incorrectly treats its generated OpenCL headers as
    # moc inputs before they exist.
    set(_yuvviewer_saved_automoc "${CMAKE_AUTOMOC}")
    set(_yuvviewer_saved_autorcc "${CMAKE_AUTORCC}")
    set(_yuvviewer_saved_autouic "${CMAKE_AUTOUIC}")
    set(CMAKE_AUTOMOC OFF)
    set(CMAKE_AUTORCC OFF)
    set(CMAKE_AUTOUIC OFF)
    FetchContent_MakeAvailable(opencv)
    set(CMAKE_AUTOMOC "${_yuvviewer_saved_automoc}")
    set(CMAKE_AUTORCC "${_yuvviewer_saved_autorcc}")
    set(CMAKE_AUTOUIC "${_yuvviewer_saved_autouic}")

    set(yuvviewer_opencv_include_dirs
        "${opencv_SOURCE_DIR}/modules/core/include"
        "${opencv_SOURCE_DIR}/modules/imgproc/include"
        "${opencv_SOURCE_DIR}/include"
        "${OPENCV_CONFIG_FILE_INCLUDE_DIR}"
        "${opencv_BINARY_DIR}"
    )
    set(yuvviewer_opencv_libraries opencv_core opencv_imgproc)
endif()

qt_add_plugin(yuvviewer
    CLASS_NAME YuvViewer
    yuvviewer.cpp yuvviewer.h
    rawimagedecoder.cpp rawimagedecoder.h
)

set_target_properties(yuvviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/.."
)

target_compile_features(yuvviewer PRIVATE cxx_std_23)

target_include_directories(yuvviewer SYSTEM PRIVATE
    ${yuvviewer_opencv_include_dirs}
)

target_link_libraries(yuvviewer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    abstractviewer
    ${yuvviewer_opencv_libraries}
)

qt_add_translations(yuvviewer
    SOURCE_TARGETS yuvviewer
    TS_FILE_BASE yuvviewer
)

if(WIN32 AND TARGET opencv_core AND TARGET opencv_imgproc)
    get_target_property(_yuvviewer_opencv_type opencv_core TYPE)
    if(_yuvviewer_opencv_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET yuvviewer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:opencv_core>
                $<TARGET_FILE:opencv_imgproc>
                "${CMAKE_BINARY_DIR}"
            COMMENT "Copying OpenCV runtime libraries"
        )
    endif()
endif()
