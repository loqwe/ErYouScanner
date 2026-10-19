cmake_minimum_required(VERSION 3.26)
project(ErYouScanner
	VERSION 2.0.1
	LANGUAGES CXX
)
set(ERYOU_DISPLAY_NAME "ErYouScanner")

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

option(UNIT_TESTS "Unit Tests" OFF)
option(DEV "DEV" OFF)

add_definitions(-DMHY_Scanner_VERSION="${PROJECT_VERSION}")

if(MSVC)
	add_compile_options(/Zi /sdl /arch:AVX2 /utf-8)
	#add_compile_options("/analyze:external-")
	add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF,ICF,LBR>)
endif()

include(FetchContent)
include(ExternalProject)
include(cmake/install_ffmpeg.cmake)
include(cmake/build_QR-Code-generator.cmake)

FetchContent_Declare(
	nlohmann_json
	GIT_REPOSITORY https://github.com/nlohmann/json.git
	GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(nlohmann_json)

find_package(unofficial-webview2 CONFIG REQUIRED)

find_package(cpr CONFIG REQUIRED)

find_package(OpenCV REQUIRED opencv_world)
include_directories(${OpenCV_INCLUDE_DIRS})

set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)

find_package(Qt6 COMPONENTS Widgets Concurrent REQUIRED)

#boost
include_directories(${CMAKE_SOURCE_DIR}/3rdparty/boost)

#include_directories(./3rdparty/abogus_cpp)
#link_directories(./3rdparty/abogus_cpp)

if(DEV)
	# Developer builds only. The scan paths show an OpenCV preview window under
	# this macro; release builds must not link that window in at all.
	add_definitions(-DERYOU_SCAN_PREVIEW)
endif()

include_directories(src/Core)

add_executable(${PROJECT_NAME}
	"${CMAKE_SOURCE_DIR}/src/Resources/MHY_Scanner.rc"
	"${CMAKE_SOURCE_DIR}/src/Resources/MHY_Scanner.qrc" 
	"${CMAKE_SOURCE_DIR}/src/UI/Main.cpp" 
	"${CMAKE_SOURCE_DIR}/src/UI/QRCodeForStream.cpp"
	"${CMAKE_SOURCE_DIR}/src/UI/QRCodeForStream.h"
	"${CMAKE_SOURCE_DIR}/src/UI/QRCodeForScreen.cpp" 
	"${CMAKE_SOURCE_DIR}/src/UI/QRCodeForScreen.h" 
	"${CMAKE_SOURCE_DIR}/src/UI/WindowAbout.cpp" 
	"${CMAKE_SOURCE_DIR}/src/UI/WindowAbout.h" 
	"${CMAKE_SOURCE_DIR}/src/UI/WindowMain.cpp" 
	"${CMAKE_SOURCE_DIR}/src/UI/WindowMain.h" 
	"${CMAKE_SOURCE_DIR}/src/UI/LiveRoomItemDelegate.h"
	"${CMAKE_SOURCE_DIR}/src/UI/LiveRoomItemDelegate.cpp"
	"${CMAKE_SOURCE_DIR}/src/UI/WindowLogin.h"
	"${CMAKE_SOURCE_DIR}/src/UI/WindowLogin.cpp" 
	"${CMAKE_SOURCE_DIR}/src/UI/WindowLoginKuro.h"
	"${CMAKE_SOURCE_DIR}/src/UI/WindowLoginKuro.cpp"
	"${CMAKE_SOURCE_DIR}/src/UI/KuroCaptchaDialog.hpp"
	"${CMAKE_SOURCE_DIR}/src/UI/KuroScanSmsDialog.hpp"
	"${CMAKE_SOURCE_DIR}/src/UI/WindowGeeTest.h" 
	"${CMAKE_SOURCE_DIR}/src/UI/WindowGeeTest.cpp"
	"${CMAKE_SOURCE_DIR}/src/Core/CryptoKit.cpp"
	"${CMAKE_SOURCE_DIR}/src/Core/CryptoKit.h"
	"${CMAKE_SOURCE_DIR}/src/Core/LiveStreamLink.cpp"
	"${CMAKE_SOURCE_DIR}/src/Core/LiveStreamLink.h"
	"${CMAKE_SOURCE_DIR}/src/Core/QRScanner.cpp"
	"${CMAKE_SOURCE_DIR}/src/Core/QRScanner.h"
	"${CMAKE_SOURCE_DIR}/src/Core/ScreenScan.cpp"
	"${CMAKE_SOURCE_DIR}/src/Core/ScreenScan.h"
	"${CMAKE_SOURCE_DIR}/src/Core/ConfigDate.cpp"
	"${CMAKE_SOURCE_DIR}/src/Core/ConfigDate.h"
	"${CMAKE_SOURCE_DIR}/src/Core/CookieParser.hpp"
	"${CMAKE_SOURCE_DIR}/src/Core/CreateUUID.hpp"
	"${CMAKE_SOURCE_DIR}/src/Core/ScannerBase.hpp"
	"${CMAKE_SOURCE_DIR}/src/Core/compile_string.hpp"
	"${CMAKE_SOURCE_DIR}/src/Core/ApiDefs.hpp"
	"${CMAKE_SOURCE_DIR}/src/Core/UrlQuery.hpp"
)

set_target_properties(${PROJECT_NAME} PROPERTIES
	OUTPUT_NAME "${ERYOU_DISPLAY_NAME}"
	PDB_NAME "${ERYOU_DISPLAY_NAME}_v${PROJECT_VERSION}"
)

if(NOT DEV)
	#target_link_options(${PROJECT_NAME} PRIVATE "/SUBSYSTEM:WINDOWS")
	set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

set(PROJECT_LIBRARIES
	# windows
	Ws2_32
	wldap32
	Normaliz
	Dbghelp
	Crypt32

	# Qt6
	Qt6::Core
	Qt6::Widgets
	Qt6::Concurrent

	cpr::cpr
	QR_Code_generator
	unofficial::webview2::webview2
	nlohmann_json::nlohmann_json
	${OpenCV_LIBS}
	${FFMPEG_LIBS}
	OpenSSL::Crypto
)

target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_LIBRARIES})

file(GLOB FFMPEG_DLL_FILES
	"${FFMPEG_BIN_DIR}/avcodec-*.dll"
	"${FFMPEG_BIN_DIR}/avformat-*.dll"
	"${FFMPEG_BIN_DIR}/avutil-*.dll"
	"${FFMPEG_BIN_DIR}/swscale-*.dll"
	"${FFMPEG_BIN_DIR}/swresample-*.dll"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
	${FFMPEG_DLL_FILES}
	"$<TARGET_FILE_DIR:${PROJECT_NAME}>"
)

# unit tests
if(UNIT_TESTS)
	enable_testing()
	add_subdirectory(tests)
	target_link_libraries(test_core PRIVATE ${PROJECT_LIBRARIES})
	target_link_libraries(test_gui PRIVATE ${PROJECT_LIBRARIES})
	target_link_libraries(test_video PRIVATE ${PROJECT_LIBRARIES})
	target_link_libraries(test_video_hw PRIVATE ${PROJECT_LIBRARIES})
	target_link_libraries(test_streamlink PRIVATE ${PROJECT_LIBRARIES})
endif()

# Relative destinations allow the packager to stage runtime and symbols separately.
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION . COMPONENT Runtime)
if(MSVC)
	install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}>
		DESTINATION . COMPONENT Symbols)
endif()

set(QT_DLL_FILES
	${QT6_INSTALL_PREFIX}/bin/Qt6Core.dll
	${QT6_INSTALL_PREFIX}/bin/Qt6Widgets.dll
	${QT6_INSTALL_PREFIX}/bin/Qt6Gui.dll
	${QT6_INSTALL_PREFIX}/bin/Qt6Concurrent.dll
)

install(
	FILES 
	${QT_DLL_FILES}
	${FFMPEG_DLL_FILES}
	DESTINATION 
	. COMPONENT Runtime
)

install(
	FILES
	${QT6_INSTALL_PREFIX}/plugins/platforms/qdirect2d.dll
	${QT6_INSTALL_PREFIX}/plugins/platforms/qminimal.dll
	${QT6_INSTALL_PREFIX}/plugins/platforms/qoffscreen.dll
	${QT6_INSTALL_PREFIX}/plugins/platforms/qwindows.dll
	DESTINATION
	plugins/platforms COMPONENT Runtime
)

install(
	FILES
	${QT6_INSTALL_PREFIX}/plugins/styles/qmodernwindowsstyle.dll
	DESTINATION
	plugins/styles COMPONENT Runtime
)

install(
	DIRECTORY
	ScanModel
	DESTINATION
	. COMPONENT Runtime
)

install(FILES doc/RELEASE_README.md LICENSE DESTINATION . COMPONENT Runtime)
install(FILES doc/THIRD_PARTY.md DESTINATION . COMPONENT Runtime)
install(DIRECTORY licenses DESTINATION . COMPONENT Runtime)

option(ERYOU_UI_TESTS "Build isolated application UI regression" OFF)
if(ERYOU_UI_TESTS)
	enable_testing()
	get_target_property(ERYOU_APP_SOURCES ${PROJECT_NAME} SOURCES)
	list(FILTER ERYOU_APP_SOURCES EXCLUDE REGEX "/[Mm]ain\\.cpp$")
	add_executable(test_app_ui tests/app_ui/test_app_ui.cpp ${ERYOU_APP_SOURCES})
	target_include_directories(test_app_ui PRIVATE src/UI)
	target_link_libraries(test_app_ui PRIVATE ${PROJECT_LIBRARIES})
	add_custom_command(TARGET test_app_ui POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${QT_DLL_FILES} ${FFMPEG_DLL_FILES}
		"$<TARGET_FILE_DIR:test_app_ui>")
	add_test(NAME app_ui_regression COMMAND test_app_ui -platform windows)
	set_tests_properties(app_ui_regression PROPERTIES
		ENVIRONMENT "QT_PLUGIN_PATH=${QT6_INSTALL_PREFIX}/plugins;QT_SCREEN_SCALE_FACTORS=1.25"
		TIMEOUT 120)
endif()
