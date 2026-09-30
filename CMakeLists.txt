cmake_minimum_required(VERSION 3.16)
project(stt_license VERSION 1.1.0 LANGUAGES CXX)

# 단독 빌드인지, SDK 등에 add_subdirectory/FetchContent 로 포함됐는지 (PROJECT_IS_TOP_LEVEL 은 3.21+ 라 직접 비교)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(_stt_top_level ON)
else()
  set(_stt_top_level OFF)
endif()

# 포함될 때는 라이브러리만 만든다. 테스트·도구·-Werror 는 단독 빌드에서만 기본으로 켠다.
option(STT_LICENSE_BUILD_TESTS "Build unit tests (GoogleTest)" ${_stt_top_level})
option(STT_LICENSE_BUILD_TOOLS "Build licensectl issuer CLI" ${_stt_top_level})
option(STT_LICENSE_BUILD_FUZZ "Build libFuzzer target (clang only)" OFF)
option(STT_LICENSE_BUILD_HARNESS "Build JNI integration harness" OFF)
option(STT_LICENSE_WERROR "Treat warnings as errors" ${_stt_top_level})
option(STT_LICENSE_OPENSSL_STATIC "Prefer static libcrypto when this project runs find_package(OpenSSL)" OFF)
set(STT_LICENSE_PUBLIC_KEY_FILE "" CACHE FILEPATH
    "public.key from 'licensectl keygen'. Required by stt_license::embedded (Manager::Open)")
set(STT_LICENSE_CRYPTO_TARGET "" CACHE STRING
    "Existing CMake target providing libcrypto (OpenSSL >= 1.1.1). Empty: reuse OpenSSL::Crypto or find_package")

include(${CMAKE_CURRENT_LIST_DIR}/cmake/SttLicenseHardening.cmake)

# ---- libcrypto 선택 ----
# 한 공유 라이브러리 안에 OpenSSL 이 두 벌 들어가면 심볼이 충돌하므로 SDK 가 쓰는 것을 그대로 써야 한다.
#  1) STT_LICENSE_CRYPTO_TARGET 지정 → 그 타깃
#  2) 상위 디렉터리에서 이미 find_package(OpenSSL) 해 OpenSSL::Crypto 가 보이면 → 재사용
#  3) 그 외 → find_package(OpenSSL 1.1.1)
if(STT_LICENSE_CRYPTO_TARGET)
  if(NOT TARGET ${STT_LICENSE_CRYPTO_TARGET})
    message(FATAL_ERROR "STT_LICENSE_CRYPTO_TARGET='${STT_LICENSE_CRYPTO_TARGET}' is not a target visible here. "
                        "Define it before add_subdirectory(stt_license).")
  endif()
  set(_stt_crypto ${STT_LICENSE_CRYPTO_TARGET})
  message(STATUS "stt_license: libcrypto from target ${STT_LICENSE_CRYPTO_TARGET}")
elseif(TARGET OpenSSL::Crypto)
  set(_stt_crypto OpenSSL::Crypto)
  message(STATUS "stt_license: reusing OpenSSL::Crypto from parent project (${OPENSSL_VERSION} ${OPENSSL_CRYPTO_LIBRARY})")
else()
  if(STT_LICENSE_OPENSSL_STATIC)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
  endif()
  find_package(OpenSSL 1.1.1 REQUIRED COMPONENTS Crypto)
  set(_stt_crypto OpenSSL::Crypto)
  message(STATUS "stt_license: OpenSSL ${OPENSSL_VERSION}: ${OPENSSL_CRYPTO_LIBRARY}")
endif()

# 이 프로젝트의 모든 타깃 공통 설정. 부모의 CMAKE_CXX_STANDARD(예: 14)와 무관하게 최소 C++17 로 빌드된다.
function(_stt_license_setup target)
  target_compile_features(${target} PRIVATE cxx_std_17)
  set_target_properties(${target} PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON        # SDK 의 JNI 공유 라이브러리에 정적 링크되므로 필수
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
      $<$<BOOL:${STT_LICENSE_WERROR}>:-Werror>)
  endif()
endfunction()

# ---- 코어 (공개키 비포함) ----
add_library(stt_license STATIC
  core/src/json.cpp
  core/src/crypto.cpp
  core/src/license.cpp
  core/src/manager.cpp)
add_library(stt_license::core ALIAS stt_license)
_stt_license_setup(stt_license)
target_include_directories(stt_license
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core/src)
target_link_libraries(stt_license PUBLIC ${_stt_crypto})

# ---- 내장 공개키 + Manager::Open (SDK 가 링크하는 타깃: stt_license::embedded) ----
set(_stt_key_src ${CMAKE_CURRENT_BINARY_DIR}/generated/stt_license_embedded_key.cpp)
if(STT_LICENSE_PUBLIC_KEY_FILE)
  if(NOT EXISTS "${STT_LICENSE_PUBLIC_KEY_FILE}")
    message(FATAL_ERROR "STT_LICENSE_PUBLIC_KEY_FILE not found: ${STT_LICENSE_PUBLIC_KEY_FILE}")
  endif()
  file(READ "${STT_LICENSE_PUBLIC_KEY_FILE}" STT_LICENSE_PUBLIC_KEY_B64)
  string(STRIP "${STT_LICENSE_PUBLIC_KEY_B64}" STT_LICENSE_PUBLIC_KEY_B64)
  if(STT_LICENSE_PUBLIC_KEY_B64 MATCHES "^stt-license-ed25519-private")
    # 비밀키가 SDK 바이너리에 내장되어 고객에게 배포되는 사고를 막는다
    message(FATAL_ERROR "${STT_LICENSE_PUBLIC_KEY_FILE} is a PRIVATE key. "
                        "Use public.key; the private key must never leave the issuing host.")
  endif()
  string(LENGTH "${STT_LICENSE_PUBLIC_KEY_B64}" _key_len)
  if(NOT _key_len EQUAL 44 OR NOT STT_LICENSE_PUBLIC_KEY_B64 MATCHES "^[A-Za-z0-9+/]+=$")
    message(FATAL_ERROR "${STT_LICENSE_PUBLIC_KEY_FILE}: not a Base64 Ed25519 public key (44 chars)")
  endif()
  configure_file(core/src/embedded_key.cpp.in ${_stt_key_src} @ONLY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${STT_LICENSE_PUBLIC_KEY_FILE}")
  add_library(stt_license_key STATIC ${_stt_key_src})
else()
  # 키 없이 구성해도 설정 단계는 통과시키되, 이 타깃을 실제로 빌드(링크)하려 하면 명확한 메시지로 실패한다.
  file(WRITE ${_stt_key_src}
    "#error \"stt_license: STT_LICENSE_PUBLIC_KEY_FILE is not set. Configure with -DSTT_LICENSE_PUBLIC_KEY_FILE=<public.key>\"\n")
  add_library(stt_license_key STATIC EXCLUDE_FROM_ALL ${_stt_key_src})
  message(STATUS "stt_license: STT_LICENSE_PUBLIC_KEY_FILE not set; stt_license::embedded cannot be built")
endif()
add_library(stt_license::embedded ALIAS stt_license_key)
_stt_license_setup(stt_license_key)
target_include_directories(stt_license_key PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core/src)
target_link_libraries(stt_license_key PUBLIC stt_license)

# ---- 발급기 ----
if(STT_LICENSE_BUILD_TOOLS)
  add_executable(licensectl tools/licensectl/main.cpp)
  _stt_license_setup(licensectl)
  target_include_directories(licensectl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core/src)
  target_link_libraries(licensectl PRIVATE stt_license)
endif()

# ---- 테스트 ----
if(STT_LICENSE_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)
  if(TARGET GTest::gtest_main)
    set(_stt_gtest GTest::gtest_main)
  else()
    set(_stt_gtest GTest::Main)  # CMake < 3.20 의 FindGTest
  endif()
  add_executable(stt_license_tests
    core/tests/json_test.cpp
    core/tests/license_test.cpp
    core/tests/manager_test.cpp
    core/tests/verify_test.cpp)
  _stt_license_setup(stt_license_tests)
  target_include_directories(stt_license_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core/src)
  target_compile_definitions(stt_license_tests PRIVATE
    STT_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/core/testdata")
  target_link_libraries(stt_license_tests PRIVATE stt_license ${_stt_gtest})
  add_test(NAME stt_license_tests COMMAND stt_license_tests)
  set_tests_properties(stt_license_tests PROPERTIES TIMEOUT 120)
endif()

# ---- 퍼저 ----
if(STT_LICENSE_BUILD_FUZZ)
  add_executable(stt_license_fuzz core/fuzz/fuzz_parse.cpp)
  _stt_license_setup(stt_license_fuzz)
  set_target_properties(stt_license_fuzz PROPERTIES OUTPUT_NAME fuzz_parse)
  target_include_directories(stt_license_fuzz PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core/src)
  target_compile_options(stt_license_fuzz PRIVATE -fsanitize=fuzzer)
  target_link_options(stt_license_fuzz PRIVATE -fsanitize=fuzzer)
  target_link_libraries(stt_license_fuzz PRIVATE stt_license)
endif()

# ---- JNI 통합 하네스 ----
if(STT_LICENSE_BUILD_HARNESS)
  if(NOT STT_LICENSE_PUBLIC_KEY_FILE)
    message(FATAL_ERROR "STT_LICENSE_BUILD_HARNESS requires STT_LICENSE_PUBLIC_KEY_FILE")
  endif()
  # jni.h 헤더만 쓴다(libjvm·AWT 링크 없음). 기본 find_package(JNI)는 AWT 까지 요구해서
  # 헤드리스 JDK(서버·클라우드 이미지의 openjdk-*-jdk-headless)에서 실패한다. FindJNI 에는 "헤더만" 컴포넌트가
  # 없으므로(3.28 기준 AWT/JVM/NativeHelper) 두 라이브러리를 불필요로 표시한다. 헤더 경로 검사는 그대로 남는다.
  set(JAVA_AWT_LIBRARY NotNeeded)
  set(JAVA_JVM_LIBRARY NotNeeded)
  set(JAVA_AWT_INCLUDE_PATH NotNeeded)
  find_package(JNI REQUIRED)
  add_library(stt_harness_jni SHARED integration/harness/harness_jni.cpp)
  _stt_license_setup(stt_harness_jni)
  target_include_directories(stt_harness_jni PRIVATE ${JAVA_INCLUDE_PATH} ${JAVA_INCLUDE_PATH2})
  target_link_options(stt_harness_jni PRIVATE "LINKER:--no-undefined")
  target_link_libraries(stt_harness_jni PRIVATE stt_license::embedded)
  stt_license_harden_shared_library(stt_harness_jni)  # SDK 에 권장하는 것과 같은 방식
endif()
