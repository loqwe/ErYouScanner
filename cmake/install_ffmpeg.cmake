set(FFMPEG_URL "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip")
set(FFMPEG_ARCHIVE "ffmpeg.zip")
set(FFMPEG_DIR "${CMAKE_BINARY_DIR}/ffmpeg-master-latest-win64-gpl-shared"
	CACHE PATH "FFmpeg SDK with matching bin, lib and include directories")

set(FFMPEG_ARCHIVE "${CMAKE_BINARY_DIR}/${FFMPEG_ARCHIVE}")

if(NOT EXISTS "${FFMPEG_DIR}")
	if(NOT FFMPEG_DIR STREQUAL "${CMAKE_BINARY_DIR}/ffmpeg-master-latest-win64-gpl-shared")
		message(FATAL_ERROR "The supplied FFMPEG_DIR does not exist: ${FFMPEG_DIR}")
	endif()
	message(STATUS "Downloading FFmpeg...")
	file(DOWNLOAD ${FFMPEG_URL} ${FFMPEG_ARCHIVE} SHOW_PROGRESS STATUS FFMPEG_DOWNLOAD_STATUS)
	list(GET FFMPEG_DOWNLOAD_STATUS 0 FFMPEG_DOWNLOAD_CODE)
	if(NOT FFMPEG_DOWNLOAD_CODE EQUAL 0)
		message(FATAL_ERROR "FFmpeg download failed: ${FFMPEG_DOWNLOAD_STATUS}")
	endif()
	file(ARCHIVE_EXTRACT INPUT ${FFMPEG_ARCHIVE} DESTINATION "${CMAKE_BINARY_DIR}")
endif()

if(NOT EXISTS "${FFMPEG_DIR}/include/libavcodec/avcodec.h")
	message(FATAL_ERROR "FFMPEG_DIR must contain a complete shared FFmpeg SDK")
endif()

set(FFMPEG_LIBS
	avcodec
	avdevice
	avfilter
	avformat
	avutil
	swscale
	swresample
)

set(FFMPEG_BIN_DIR ${FFMPEG_DIR}/bin)
set(FFMPEG_LIB_DIR ${FFMPEG_DIR}/lib)
set(FFMPEG_INCLUDE_DIR ${FFMPEG_DIR}/include)

link_directories(${FFMPEG_LIB_DIR})
include_directories(${FFMPEG_INCLUDE_DIR})
