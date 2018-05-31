#  minimum required cmake version: 3.1.0
cmake_minimum_required(VERSION 3.1.0)

project(RealSenseImShowExample)

# Save the command line compile commands in the build output
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
if(COMPILER_SUPPORTS_CXX11)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
elseif(COMPILER_SUPPORTS_CXX0X)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
endif()


# find ffmpeg libraries
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})
find_package(FFMPEG)

include_directories(
  include
  ${FFMPEG_INCLUDE_DIR}
)


set(SOURCES rs-decoder.cpp rs-retrieve.cpp)
add_executable(rs-retrieve ${SOURCES})

target_link_libraries(rs-retrieve ${DEPENDENCIES} ${FFMPEG_LIBRARIES})
set_target_properties (rs-retrieve PROPERTIES
	FOLDER "Examples/OpenCV"
)

install(
	TARGETS

	rs-retrieve

	RUNTIME DESTINATION
	${CMAKE_INSTALL_PREFIX}/bin
)
