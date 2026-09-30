# SDK 빌드를 흉내 낸 소비자 프로젝트: stt_license 를 add_subdirectory 로 넣어 JNI 형태의 공유 라이브러리를 만든다.
# scripts/check.sh 가 여러 구성(C++14/20 부모, OpenSSL 재사용, 사용자 지정 crypto 타깃)으로 빌드해 본다.
cmake_minimum_required(VERSION 3.16)
project(mock_sdk LANGUAGES CXX)

set(MOCK_CXX_STANDARD 14 CACHE STRING "부모(SDK) 프로젝트의 C++ 표준")
set(CMAKE_CXX_STANDARD ${MOCK_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(MOCK_CRYPTO_MODE "none" CACHE STRING "none | parent_find | custom_target")

if(MOCK_CRYPTO_MODE STREQUAL "parent_find")
  # SDK가 먼저 OpenSSL 을 찾아 둔 경우 → stt_license 는 같은 OpenSSL::Crypto 를 재사용해야 한다
  set(OPENSSL_USE_STATIC_LIBS TRUE)
  find_package(OpenSSL REQUIRED COMPONENTS Crypto)
elseif(MOCK_CRYPTO_MODE STREQUAL "custom_target")
  # SDK가 OpenSSL 을 자체 빌드해 다른 이름의 타깃으로 갖고 있는 경우
  add_library(sdk_crypto STATIC IMPORTED)
  set_target_properties(sdk_crypto PROPERTIES
    IMPORTED_LOCATION "${MOCK_LIBCRYPTO_A}"
    INTERFACE_INCLUDE_DIRECTORIES "${MOCK_OPENSSL_INCLUDE}"
    INTERFACE_LINK_LIBRARIES "dl;pthread")
  set(STT_LICENSE_CRYPTO_TARGET sdk_crypto CACHE STRING "" FORCE)
endif()

# SDK 가 키를 캐시가 아닌 일반 변수로 지정하는 경우(회귀 시험: 이 값이 서브프로젝트에서 지워지면 안 됨)
if(MOCK_PUBLIC_KEY)
  set(STT_LICENSE_PUBLIC_KEY_FILE ${MOCK_PUBLIC_KEY})
endif()

add_subdirectory(${STT_LICENSE_SOURCE_DIR} stt_license)

add_library(mock_sdk_jni SHARED mock_sdk.cpp)
target_compile_options(mock_sdk_jni PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(mock_sdk_jni PRIVATE stt_license::embedded)
stt_license_harden_shared_library(mock_sdk_jni EXPORTS "mock_*")

add_executable(mock_sdk_smoke smoke.cpp)
target_link_libraries(mock_sdk_smoke PRIVATE mock_sdk_jni)
