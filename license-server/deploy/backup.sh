#!/usr/bin/env bash
# 일일 백업: DB 덤프 + 감사 로그 해시 앵커(마지막 seq·hash 를 DB 밖에 누적 기록).
#   ./backup.sh [백업 디렉터리]   (기본: ./backups)
# 앵커가 있으면 DB 를 통째로 바꿔치기한 조작도, 복원 후 해시 체인 검사 결과와 대조해 드러난다.
# DB 에는 비밀키가 없다(봉인 파일은 별도). 사용자 비밀번호는 BCrypt, OTP 비밀값은 AES-GCM 으로 저장된다.
set -euo pipefail
cd "$(dirname "$0")"
OUT="${1:-./backups}"
umask 077
mkdir -p "$OUT"
TS=$(date -u +%Y%m%dT%H%M%SZ)
docker compose exec -T db pg_dump -U postgres --format=custom license > "$OUT/license-$TS.dump"
ANCHOR=$(docker compose exec -T db psql -U postgres -d license -At -c \
  "SELECT seq || ' ' || hash FROM audit_event ORDER BY seq DESC LIMIT 1")
echo "$TS ${ANCHOR:-empty} $(sha256sum "$OUT/license-$TS.dump" | cut -d' ' -f1)" >> "$OUT/audit-anchor.log"
echo "backup: $OUT/license-$TS.dump"
echo "anchor: $(tail -1 "$OUT/audit-anchor.log")"
