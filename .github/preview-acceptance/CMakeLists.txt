cmake_minimum_required(VERSION 3.24)
project(GswRealCdnReadback LANGUAGES C CXX)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
add_executable(preview_real_cdn CdnProbe.cpp
  "${GSW_PRODUCT_NATIVE}/src/NativeGithubPreviewFeed.cpp"
  "${GSW_PRODUCT_NATIVE}/third_party/miniz/miniz.c")
target_compile_features(preview_real_cdn PRIVATE cxx_std_20)
target_compile_options(preview_real_cdn PRIVATE /utf-8 /Zc:__cplusplus)
target_compile_definitions(preview_real_cdn PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
target_include_directories(preview_real_cdn PRIVATE "${GSW_PRODUCT_NATIVE}/src"
  "${GSW_PRODUCT_NATIVE}/third_party/miniz" "${GSW_PRODUCT_NATIVE}/third_party/nlohmann")
target_link_libraries(preview_real_cdn PRIVATE Qt6::Core winhttp)
