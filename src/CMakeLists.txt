cmake_minimum_required(VERSION 3.16)
project(picam-app VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCAMERA REQUIRED libcamera)
pkg_check_modules(LIBJPEG   REQUIRED libturbojpeg)

add_executable(picam-app
    main.cpp
    camera.cpp
    encoder.cpp
    uvc_gadget.cpp
)

target_compile_options(picam-app PRIVATE
    ${LIBCAMERA_CFLAGS}
    ${LIBJPEG_CFLAGS}
    -Wall -Wextra -O2
)

target_include_directories(picam-app PRIVATE
    ${LIBCAMERA_INCLUDE_DIRS}
    ${LIBJPEG_INCLUDE_DIRS}
)

target_link_libraries(picam-app PRIVATE
    ${LIBCAMERA_LIBRARIES}
    ${LIBJPEG_LIBRARIES}
)

install(TARGETS picam-app DESTINATION bin)
