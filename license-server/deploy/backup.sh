#!/usr/bin/env bash
# 일일 백업: DB 덤프 + 감사 로그 해시 앵커(마지막 seq·hash 를 DB 밖에 누적 기록).
#   ./backup.sh [백업 디렉터리]   (기본: ./backups)
# 앵커가 있으면 DB 를 통째로 바꿔치기한 조작도, 복원 후 해시 체인 검사 결과와 대조해 드러난다.
# DB 에는 비밀키가 없다(봉인 파일은 별도). 사용자 비밀번호는 BCrypt, OTP 비밀값은 AES-GCM 으로 저장된다.
set -euo pipefail
cd "$(dirname "$0")"
OUT="${1:-./backups}"
# podman 배포(podman-stack.sh)는 .env 에 LICENSE_RUNTIME=podman 을 둔다. 없으면 docker compose.
if grep -qx 'LICENSE_RUNTIME=podman' .env 2>/dev/null; then
  dbx() { podman exec -i "${LICENSE_STACK:-license-server}-db" "$@"; }
else
  dbx() { docker compose exec -T db "$@"; }
fi
umask 077
mkdir -p "$OUT"
TS=$(date -u +%Y%m%dT%H%M%SZ)
dbx pg_dump -U postgres --format=custom license > "$OUT/license-$TS.dump"
ANCHOR=$(dbx psql -U postgres -d license -At -c \
  "SELECT seq || ' ' || hash FROM audit_event ORDER BY seq DESC LIMIT 1")
echo "$TS ${ANCHOR:-empty} $(sha256sum "$OUT/license-$TS.dump" | cut -d' ' -f1)" >> "$OUT/audit-anchor.log"
echo "backup: $OUT/license-$TS.dump"
echo "anchor: $(tail -1 "$OUT/audit-anchor.log")"
