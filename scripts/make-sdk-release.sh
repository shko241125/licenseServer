#!/usr/bin/env bash
# SDK 적용용 패키지 만들기: scripts/sdk-release-files.txt 에 있는 경로만 담아 stt-license-sdk-<버전>.tar.gz 생성.
# 사용: scripts/make-sdk-release.sh [--ref <커밋>|--worktree] [--out <디렉터리>] [--verify]
#   --ref      이 커밋의 내용으로 만든다(기본 HEAD). 정식 발행은 태그를 지정한다.
#   --worktree 커밋 전 작업 트리로 만든다(버전에 -dirty 표시, 발행 금지 — 사전 점검용).
#   --out      산출 디렉터리(기본 ./build/sdk-release).
#   --verify   풀어낸 패키지만으로 scripts/check.sh 실행(리눅스 파일시스템의 임시 디렉터리).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
REF=HEAD; WORKTREE=0; OUT="$ROOT/build/sdk-release"; VERIFY=0
while [ $# -gt 0 ]; do
  case "$1" in
    --ref) REF="$2"; shift 2 ;;
    --worktree) WORKTREE=1; shift ;;
    --out) OUT="$2"; shift 2 ;;
    --verify) VERIFY=1; shift ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

mapfile -t PATHS < <(sed -e 's/#.*//' -e 's/[[:space:]]*$//' scripts/sdk-release-files.txt | grep -v '^$')

if [ "$WORKTREE" = 1 ]; then
  SRC_DESC="worktree"
  VERSION_SRC=$(cat CMakeLists.txt)
else
  git rev-parse --verify -q "$REF^{commit}" >/dev/null || { echo "no such commit: $REF" >&2; exit 1; }
  SRC_DESC="$(git rev-parse "$REF^{commit}")"
  VERSION_SRC=$(git show "$REF:CMakeLists.txt")
fi
VERSION=$(sed -n 's/^project(stt_license VERSION \([0-9.]*\).*/\1/p' <<<"$VERSION_SRC")
[ -n "$VERSION" ] || { echo "version not found in CMakeLists.txt" >&2; exit 1; }
[ "$WORKTREE" = 1 ] && VERSION="$VERSION-dirty"
NAME="stt-license-sdk-$VERSION"

TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
STAGE="$TMP/$NAME"; mkdir -p "$STAGE"
if [ "$WORKTREE" = 1 ]; then
  # 추적 중 + 새로 만든(무시되지 않은) 파일만. build/, 편집기 임시 파일 등 무시 대상은 들어가지 않는다.
  git ls-files -z --cached --others --exclude-standard -- "${PATHS[@]}" \
    | while IFS= read -r -d '' f; do [ -f "$f" ] && printf '%s\0' "$f"; done \
    | tar --null -T - -cf - | tar -xf - -C "$STAGE"
else
  git archive --format=tar "$REF" -- "${PATHS[@]}" | tar -xf - -C "$STAGE"
fi
for p in "${PATHS[@]}"; do [ -e "$STAGE/$p" ] || { echo "listed path missing: $p" >&2; exit 1; }; done

# 패키지 루트 README (docs/sdk/README.md 로 안내) 와 버전 정보
cat > "$STAGE/README.md" <<EOF
# STT 라이선스 SDK 모듈 $VERSION

적용 안내: [docs/sdk/README.md](docs/sdk/README.md)
EOF
printf '%s\nsource: %s\n' "$VERSION" "$SRC_DESC" > "$STAGE/VERSION"

# ---- 발행 금지 내용 검사 ----
fail() { echo "RELEASE CHECK FAILED: $*" >&2; exit 1; }
cd "$STAGE"
bad=$(find . \( -name 'private.key' -o -name '*.pdf' -o -name '*.swp' -o -name '.env' -o -name 'sonaLicense*' \
        -o -path './license-server*' -o -path './work_*' \) -print)
[ -z "$bad" ] || fail "forbidden files: $bad"
bad=$(find . \( -name '*.key' -o -name '*.lic' \) ! -path './core/testdata/golden_*' -print)
[ -z "$bad" ] || fail "key/license files other than core/testdata/golden_*: $bad"
bad=$(grep -rlE 'stt-license-ed25519-private-v1:[A-Za-z0-9+/]{43}=' . || true)
[ -z "$bad" ] || fail "embedded private key material in: $bad"
bad=$(grep -rlE -- '-----BEGIN [A-Z ]*PRIVATE KEY-----' . || true)
[ -z "$bad" ] || fail "PEM private key in: $bad"
# 문서의 상대 링크가 패키지 안에서 모두 풀리는지 (발급 서버 문서 등 빠진 파일을 가리키지 않게)
python3 - <<'PY' || fail "broken links in docs"
import os, re, sys
bad = []
for dp, _, fs in os.walk("."):
    for f in fs:
        if not f.endswith(".md"): continue
        p = os.path.join(dp, f)
        for link in re.findall(r"\]\(([^)#\s]+)(?:#[^)]*)?\)", open(p, encoding="utf-8").read()):
            if re.match(r"[a-z]+:", link): continue
            if not os.path.exists(os.path.normpath(os.path.join(dp, link))): bad.append(f"{p} -> {link}")
for b in bad: print("  " + b, file=sys.stderr)
sys.exit(1 if bad else 0)
PY
find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
cd "$ROOT"

mkdir -p "$OUT"
TAR="$OUT/$NAME.tar.gz"
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime='2000-01-01 00:00:00Z' \
    -C "$TMP" -cf - "$NAME" | gzip -n > "$TAR"
(cd "$OUT" && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256")
echo "package: $TAR ($(tar -tzf "$TAR" | grep -vc '/$') files, source $SRC_DESC)"
cat "$TAR.sha256"

if [ "$VERIFY" = 1 ]; then
  V=$(mktemp -d); trap 'rm -rf "$TMP" "$V"' EXIT
  tar -xzf "$TAR" -C "$V"
  (cd "$V/$NAME" && sha256sum --quiet -c SHA256SUMS)
  "$V/$NAME/scripts/check.sh" "$V/build-check"
  echo "verified: extracted package passes scripts/check.sh"
fi
