cmake_minimum_required(VERSION 3.26)
project(scan_latency_benchmark LANGUAGES CXX)
find_package(OpenCV REQUIRED COMPONENTS opencv_world)

set(bench_include_dirs ../../src/Core ../../3rdparty/QR-Code-generator/cpp)

add_executable(bench_scan_latency bench_scan_latency.cpp ../../src/Core/QRScanner.cpp
    ../../3rdparty/QR-Code-generator/cpp/qrcodegen.cpp)
target_compile_features(bench_scan_latency PRIVATE cxx_std_20)
target_include_directories(bench_scan_latency PRIVATE ${bench_include_dirs})
target_include_directories(bench_scan_latency SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(bench_scan_latency PRIVATE ${OpenCV_LIBS})
if(MSVC)
    target_compile_options(bench_scan_latency PRIVATE /W4 /EHsc /utf-8)
endif()
add_custom_command(TARGET bench_scan_latency POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../../ScanModel"
            "$<TARGET_FILE_DIR:bench_scan_latency>/ScanModel")

add_executable(bench_scale_floor bench_scale_floor.cpp ../../3rdparty/QR-Code-generator/cpp/qrcodegen.cpp)
target_compile_features(bench_scale_floor PRIVATE cxx_std_20)
target_include_directories(bench_scale_floor PRIVATE ${bench_include_dirs})
target_include_directories(bench_scale_floor SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(bench_scale_floor PRIVATE ${OpenCV_LIBS})
if(MSVC)
    target_compile_options(bench_scale_floor PRIVATE /W4 /EHsc /utf-8)
endif()
add_custom_command(TARGET bench_scale_floor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../../ScanModel"
            "$<TARGET_FILE_DIR:bench_scale_floor>/ScanModel")

add_executable(verify_shipped_scanner verify_shipped_scanner.cpp ../../src/Core/QRScanner.cpp)
target_compile_features(verify_shipped_scanner PRIVATE cxx_std_20)
target_include_directories(verify_shipped_scanner PRIVATE ${bench_include_dirs})
target_include_directories(verify_shipped_scanner SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(verify_shipped_scanner PRIVATE ${OpenCV_LIBS})
if(MSVC)
    target_compile_options(verify_shipped_scanner PRIVATE /W4 /EHsc /utf-8)
endif()
add_custom_command(TARGET verify_shipped_scanner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../../ScanModel"
            "$<TARGET_FILE_DIR:verify_shipped_scanner>/ScanModel")
