cmake_minimum_required(VERSION 3.16)
project(stt_license VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)  # SDK의 JNI 공유 라이브러리에 정적 링크되므로 PIC 필수

option(STT_LICENSE_BUILD_TESTS "Build unit tests (GoogleTest)" ON)
option(STT_LICENSE_BUILD_FUZZ "Build libFuzzer targets (clang only)" OFF)
option(STT_LICENSE_WERROR "Treat warnings as errors" ON)
set(STT_LICENSE_PUBLIC_KEY_FILE "" CACHE FILEPATH
    "public.key from 'licensectl keygen'. Required for stt_license_key (Manager::Open)")

option(STT_LICENSE_OPENSSL_STATIC "Link OpenSSL libcrypto statically" OFF)
if(STT_LICENSE_OPENSSL_STATIC)
  set(OPENSSL_USE_STATIC_LIBS TRUE)
endif()
find_package(OpenSSL 1.1.1 REQUIRED COMPONENTS Crypto)
message(STATUS "OpenSSL ${OPENSSL_VERSION}: ${OPENSSL_CRYPTO_LIBRARY}")

set(STT_WARN -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
if(STT_LICENSE_WERROR)
  list(APPEND STT_WARN -Werror)
endif()

# ---- 코어 (공개키 비포함) ----
add_library(stt_license STATIC
  core/src/json.cpp
  core/src/crypto.cpp
  core/src/license.cpp
  core/src/manager.cpp)
target_include_directories(stt_license
  PUBLIC core/include
  PRIVATE core/src)
target_compile_options(stt_license PRIVATE ${STT_WARN})
set_target_properties(stt_license PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
target_link_libraries(stt_license PUBLIC OpenSSL::Crypto)

# ---- 내장 공개키 + Manager::Open ----
if(STT_LICENSE_PUBLIC_KEY_FILE)
  file(READ "${STT_LICENSE_PUBLIC_KEY_FILE}" STT_LICENSE_PUBLIC_KEY_B64)
  string(STRIP "${STT_LICENSE_PUBLIC_KEY_B64}" STT_LICENSE_PUBLIC_KEY_B64)
  string(LENGTH "${STT_LICENSE_PUBLIC_KEY_B64}" _key_len)
  if(NOT _key_len EQUAL 44 OR NOT STT_LICENSE_PUBLIC_KEY_B64 MATCHES "^[A-Za-z0-9+/]+=$")
    message(FATAL_ERROR "${STT_LICENSE_PUBLIC_KEY_FILE}: not a Base64 Ed25519 public key (44 chars)")
  endif()
  configure_file(core/src/embedded_key.cpp.in ${CMAKE_BINARY_DIR}/generated/embedded_key.cpp @ONLY)
  add_library(stt_license_key STATIC ${CMAKE_BINARY_DIR}/generated/embedded_key.cpp)
  target_include_directories(stt_license_key PRIVATE core/src)
  target_compile_options(stt_license_key PRIVATE ${STT_WARN})
  set_target_properties(stt_license_key PROPERTIES CXX_VISIBILITY_PRESET hidden)
  target_link_libraries(stt_license_key PUBLIC stt_license)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${STT_LICENSE_PUBLIC_KEY_FILE}")
endif()

# ---- 발급기 ----
add_executable(licensectl tools/licensectl/main.cpp)
target_include_directories(licensectl PRIVATE core/src)
target_compile_options(licensectl PRIVATE ${STT_WARN})
target_link_libraries(licensectl PRIVATE stt_license)

# ---- 테스트 ----
if(STT_LICENSE_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)
  add_executable(stt_license_tests
    core/tests/json_test.cpp
    core/tests/license_test.cpp
    core/tests/manager_test.cpp)
  target_include_directories(stt_license_tests PRIVATE core/src)
  target_compile_definitions(stt_license_tests PRIVATE
    STT_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/core/testdata")
  target_link_libraries(stt_license_tests PRIVATE stt_license GTest::gtest_main)
  add_test(NAME stt_license_tests COMMAND stt_license_tests)
endif()

# ---- 퍼저 ----
if(STT_LICENSE_BUILD_FUZZ)
  add_executable(fuzz_parse core/fuzz/fuzz_parse.cpp)
  target_include_directories(fuzz_parse PRIVATE core/src)
  target_compile_options(fuzz_parse PRIVATE -fsanitize=fuzzer)
  target_link_options(fuzz_parse PRIVATE -fsanitize=fuzzer)
  target_link_libraries(fuzz_parse PRIVATE stt_license)
endif()
