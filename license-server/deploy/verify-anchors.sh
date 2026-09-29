#!/usr/bin/env bash
# 감사 로그 앵커 대조: backup.sh 가 남긴 각 앵커(seq, hash)가 현재 DB 에 그대로 있는지 확인한다.
#   ./verify-anchors.sh [앵커 파일]   (기본: ./backups/audit-anchor.log)
# 해시 체인 검사(화면)가 잡지 못하는 "꼬리 삭제"와 "체인 전체 재계산(바꿔치기)"을 잡는다.
# 종료 코드: 모두 일치 0, 하나라도 불일치 1.
set -uo pipefail
cd "$(dirname "$0")"
ANCHORS="${1:-./backups/audit-anchor.log}"
[ -r "$ANCHORS" ] || { echo "anchor file not found: $ANCHORS" >&2; exit 2; }
fail=0
while read -r ts seq hash _dump; do
  [[ "$seq" =~ ^[0-9]+$ ]] || continue   # 'empty' 등 앵커가 없던 백업은 건너뜀
  # < /dev/null: exec 가 반복문의 stdin(앵커 파일)을 읽어 버리지 않게 한다
  cur=$(docker compose exec -T db psql -U postgres -d license -At \
        -c "SELECT hash FROM audit_event WHERE seq = $seq" < /dev/null)
  if [ "$cur" = "$hash" ]; then
    echo "OK    seq $seq  ($ts)"
  else
    echo "FAIL  seq $seq  ($ts)  anchor=$hash  db=${cur:-<행 없음>}"
    fail=1
  fi
done < "$ANCHORS"
[ $fail -eq 0 ] && echo "all anchors match" || echo "MISMATCH: 감사 로그가 앵커 기록 이후 삭제·교체되었을 수 있음"
exit $fail
