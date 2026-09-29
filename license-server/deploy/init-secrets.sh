#!/usr/bin/env bash
# 비밀값 생성(최초 1회). 기존 파일은 덮어쓰지 않는다.
#   ./init-secrets.sh [TLS 인증서 CN/호스트명]   (사내 CA 인증서가 있으면 secrets/tls_keystore.p12 를 그것으로 교체)
# 파일은 0444(컨테이너 사용자 uid 10001·postgres 가 읽을 수 있게), 디렉터리는 0700(호스트 사용자만 접근).
set -euo pipefail
cd "$(dirname "$0")"
CN="${1:-license-server.local}"
umask 077
mkdir -p secrets data/key
chmod 700 secrets data

gen() {  # gen <name> <command>
  local f="secrets/$1"
  if [ -e "$f" ]; then echo "keep  $f"; return; fi
  bash -c "$2" > "$f"
  chmod 444 "$f"
  echo "made  $f"
}
gen db_superuser_password "openssl rand -base64 33 | tr -d '\n'"
gen db_owner_password     "openssl rand -base64 33 | tr -d '\n'"
gen db_app_password       "openssl rand -base64 33 | tr -d '\n'"
gen data_key              "openssl rand -base64 32 | tr -d '\n'"
gen tls_keystore_password "openssl rand -base64 24 | tr -d '\n'"
gen initial_admin_password "printf 'Init-%s' \"\$(openssl rand -base64 18 | tr -d '\n/+=')\""

if [ ! -e secrets/tls_keystore.p12 ]; then
  # 개발·임시용 자체 서명 인증서. 운영은 사내 CA 인증서로 교체한다(README).
  openssl req -x509 -newkey rsa:3072 -sha256 -days 825 -nodes -subj "/CN=$CN" \
    -addext "subjectAltName=DNS:$CN,DNS:localhost,IP:127.0.0.1" \
    -keyout secrets/tls.key -out secrets/tls.crt 2>/dev/null
  openssl pkcs12 -export -inkey secrets/tls.key -in secrets/tls.crt -name license-server \
    -passout file:secrets/tls_keystore_password -out secrets/tls_keystore.p12
  rm -f secrets/tls.key
  chmod 444 secrets/tls_keystore.p12 secrets/tls.crt
  echo "made  secrets/tls_keystore.p12 (self-signed, CN=$CN; trust secrets/tls.crt on admin PCs)"
fi
echo
echo "initial admin password: secrets/initial_admin_password (첫 로그인 때 변경)"
