#!/usr/bin/env bash
# 새 Ubuntu(클라우드 세션, 새 PC)에서 검증 도구를 준비한다. 없는 것만 설치하므로 여러 번 실행해도 된다.
# claude.ai/code 환경의 setup script 로 등록: bash scripts/cloud-setup.sh
# 준비 대상: scripts/check.sh (C++·JNI·Java·Python), license-server 의 ./gradlew test / integrationTest
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

missing=()
need_cmd() { command -v "$1" >/dev/null 2>&1 || missing+=("$2"); }   # need_cmd <명령> <apt 패키지>
need_pkg() { dpkg -s "$1" >/dev/null 2>&1 || missing+=("$1"); }     # 헤더·라이브러리 패키지
need_cmd git git
need_cmd cmake cmake
need_cmd ninja ninja-build
need_cmd g++ g++
need_cmd cc gcc
need_cmd pkg-config pkg-config
need_cmd python3 python3
need_cmd curl curl
need_pkg libssl-dev        # libcrypto 헤더 + 정적 libcrypto.a (check.sh 4·6단계)
need_pkg libgtest-dev      # GoogleTest
# JDK: check.sh 는 17 이상, license-server 는 Gradle 툴체인이 21 을 요구(자동 내려받기 없음)
jdk_major() { javac -version 2>&1 | sed -n 's/^javac \([0-9]*\).*/\1/p'; }
v=$(jdk_major || true); [ "${v:-0}" -ge 21 ] || missing+=(openjdk-21-jdk-headless)

if [ ${#missing[@]} -gt 0 ]; then
  SUDO=; [ "$(id -u)" = 0 ] || SUDO=sudo
  echo "installing: ${missing[*]}"
  $SUDO apt-get update -q
  DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y -q --no-install-recommends "${missing[@]}"
else
  echo "tools: all present"
fi
echo "javac $(jdk_major), $(cmake --version | head -1), $(openssl version 2>/dev/null || echo 'openssl CLI 없음(무관)')"

# Gradle 배포판·의존성을 미리 받는다(setup script 결과 캐시에 포함되게).
(cd "$ROOT/license-server" && ./gradlew --no-daemon -q testClasses >/dev/null)
echo "gradle: dependencies ready"

# 통합 테스트(Testcontainers)용 이미지. Docker 가 없거나 데몬에 접근할 수 없으면 건너뛴다.
if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
  docker pull -q postgres:16-alpine >/dev/null && echo "docker: postgres:16-alpine ready"
else
  echo "docker: 사용 불가 — integrationTest 는 실행할 수 없음"
fi
