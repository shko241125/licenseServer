#!/usr/bin/env bash
# 발급 서버 설치 패키지 만들기: 이미지(license-server, postgres) + scripts/server-release-files.txt 의 문서·배포 구성
#   → license-server-<버전>.tar.gz. 설명: docs/release/license-server.md
# 사용: scripts/make-server-release.sh [--ref <커밋>|--worktree] [--out <디렉터리>] [--verify]
#   --ref      이 커밋의 소스로 이미지를 빌드하고 문서를 담는다(기본 HEAD). 정식 발행은 태그를 지정한다.
#   --worktree 커밋 전 작업 트리로 만든다(버전에 -dirty 표시, 발행 금지 — 사전 점검용).
#   --out      산출 디렉터리(기본 ./build/server-release).
#   --verify   풀어낸 패키지만으로 새 스택을 설치·기동해 확인하고 지운다(127.0.0.3:8443, 기존 스택과 겹치지 않음).
# 필요: 리눅스, Docker(compose v2), 빌드 중 인터넷(apt·Gradle 의존성).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
REF=HEAD; WORKTREE=0; OUT="$ROOT/build/server-release"; VERIFY=0
while [ $# -gt 0 ]; do
  case "$1" in
    --ref) REF="$2"; shift 2 ;;
    --worktree) WORKTREE=1; shift ;;
    --out) OUT="$2"; shift 2 ;;
    --verify) VERIFY=1; shift ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

mapfile -t PATHS < <(sed -e 's/#.*//' -e 's/[[:space:]]*$//' scripts/server-release-files.txt | grep -v '^$')
# 이미지 빌드 컨텍스트(Dockerfile 이 COPY 하는 것 + .dockerignore)
BUILD_PATHS=(.dockerignore CMakeLists.txt cmake core tools license-server)

if [ "$WORKTREE" = 1 ]; then
  SRC_DESC="worktree"
  VERSION_SRC=$(cat license-server/build.gradle.kts)
else
  git rev-parse --verify -q "$REF^{commit}" >/dev/null || { echo "no such commit: $REF" >&2; exit 1; }
  SRC_DESC="$(git rev-parse "$REF^{commit}")"
  VERSION_SRC=$(git show "$REF:license-server/build.gradle.kts")
fi
VERSION=$(sed -n 's/^version = "\([0-9.]*\)"$/\1/p' <<<"$VERSION_SRC")
[ -n "$VERSION" ] || { echo "version not found in license-server/build.gradle.kts" >&2; exit 1; }
[ "$WORKTREE" = 1 ] && VERSION="$VERSION-dirty"
NAME="license-server-$VERSION"
IMAGE="license-server:$VERSION"
PG_IMAGE=$(sed -n 's/^ *image: *\(postgres:[^ ]*\).*/\1/p' license-server/deploy/docker-compose.yml)
[ -n "$PG_IMAGE" ] || { echo "postgres image not found in docker-compose.yml" >&2; exit 1; }

TMP=$(mktemp -d); V=""; trap 'rm -rf "$TMP"; [ -z "$V" ] || rm -rf "$V"' EXIT
STAGE="$TMP/$NAME"; CTX="$TMP/context"; mkdir -p "$STAGE/images" "$CTX"
copy_paths() {  # copy_paths <대상> <경로...>: 커밋(또는 작업 트리)의 내용만 복사
  local dst="$1"; shift
  if [ "$WORKTREE" = 1 ]; then
    git ls-files -z --cached --others --exclude-standard -- "$@" \
      | while IFS= read -r -d '' f; do [ -f "$f" ] && printf '%s\0' "$f"; done \
      | tar --null -T - -cf - | tar -xf - -C "$dst"
  else
    git archive --format=tar "$REF" -- "$@" | tar -xf - -C "$dst"
  fi
}
copy_paths "$STAGE" "${PATHS[@]}"
for p in "${PATHS[@]}"; do [ -e "$STAGE/$p" ] || { echo "listed path missing: $p" >&2; exit 1; }; done

# ---- 이미지: 같은 커밋의 소스로 빌드(빌드 중 C++ 코어·서버 단위 테스트), postgres 는 compose 의 태그 ----
copy_paths "$CTX" "${BUILD_PATHS[@]}"
docker build -q -f "$CTX/license-server/Dockerfile" -t "$IMAGE" \
  --label org.opencontainers.image.title=license-server \
  --label org.opencontainers.image.version="$VERSION" \
  --label org.opencontainers.image.revision="$SRC_DESC" "$CTX" >/dev/null
docker image inspect "$PG_IMAGE" >/dev/null 2>&1 || docker pull -q "$PG_IMAGE" >/dev/null
IMAGE_ID=$(docker image inspect -f '{{.Id}}' "$IMAGE")
PG_ID=$(docker image inspect -f '{{.Id}}' "$PG_IMAGE")
PG_DIGEST=$(docker image inspect -f '{{join .RepoDigests " "}}' "$PG_IMAGE")

fail() { echo "RELEASE CHECK FAILED: $*" >&2; exit 1; }
KEY_RE='stt-license-ed25519-private-v1:[A-Za-z0-9+/]{43}='
PEM_RE='-----BEGIN [A-Z ]*PRIVATE KEY-----'
# 이미지 안 jar·licensectl 에 키·라이선스·비밀값이 섞이지 않았는지
CID=$(docker create "$IMAGE")
docker cp -q "$CID:/app/license-server.jar" "$TMP/app.jar"; docker cp -q "$CID:/opt/licensectl" "$TMP/licensectl"
docker rm "$CID" >/dev/null
bad=$(python3 -c 'import sys,zipfile; [print(n) for n in zipfile.ZipFile(sys.argv[1]).namelist() if n.endswith((".key",".lic",".env",".pdf")) or "secrets/" in n]' "$TMP/app.jar")
[ -z "$bad" ] || fail "forbidden entries in jar: $bad"
python3 -c 'import sys,zipfile; z=zipfile.ZipFile(sys.argv[1]); sys.stdout.buffer.write(b"".join(z.read(n) for n in z.namelist()))' "$TMP/app.jar" \
  | grep -aqE -- "$KEY_RE|$PEM_RE" && fail "private key material in jar"
grep -aqE -- "$KEY_RE|$PEM_RE" "$TMP/licensectl" && fail "private key material in licensectl"
rm -f "$TMP/app.jar" "$TMP/licensectl"
docker save -o "$STAGE/images/$NAME.tar" "$IMAGE"
docker save -o "$STAGE/images/${PG_IMAGE//:/-}.tar" "$PG_IMAGE"
chmod 644 "$STAGE"/images/*.tar   # docker save -o 는 0600 — root 로 풀면 docker 그룹 사용자가 load 하지 못한다

# ---- 루트 안내와 버전 정보 ----
cat > "$STAGE/README.md" <<EOF
# 사내 라이선스 발급 서버 $VERSION

1. 대조: \`sha256sum --quiet -c SHA256SUMS\`, 버전·원 커밋: \`VERSION\`
2. 이미지: \`docker load -i images/$NAME.tar\`, \`docker load -i images/${PG_IMAGE//:/-}.tar\`
3. 설치·업그레이드 순서: [docs/release/license-server.md](docs/release/license-server.md) §3
4. 설치 후 운영: [license-server/README.md](license-server/README.md)
EOF
printf '%s\nsource: %s\nimage: %s %s\npostgres: %s %s %s\n' "$VERSION" "$SRC_DESC" \
  "$IMAGE" "$IMAGE_ID" "$PG_IMAGE" "$PG_ID" "$PG_DIGEST" > "$STAGE/VERSION"

# ---- 발행 금지 내용 검사 (이미지 tar 는 위에서 내용 검사함) ----
cd "$STAGE"
bad=$(find . \( -name secrets -o -name data -o -name backups -o -name '.env' -o -name 'private.key' \
        -o -name 'sealed-key.json' -o -name '*.key' -o -name '*.lic' -o -name '*.dump' -o -name '*.pdf' \
        -o -name '*.swp' -o -path './work_*' \) -print)
[ -z "$bad" ] || fail "forbidden files: $bad"
bad=$(grep -rlE --exclude-dir=images -- "$KEY_RE|$PEM_RE" . || true)
[ -z "$bad" ] || fail "private key material in: $bad"
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
for s in license-server/deploy/*.sh license-server/deploy/db-init/*.sh; do bash -n "$s" || fail "syntax: $s"; done
find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
cd "$ROOT"

mkdir -p "$OUT"
TAR="$OUT/$NAME.tar.gz"
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime='2000-01-01 00:00:00Z' \
    -C "$TMP" -cf - "$NAME" | gzip -n > "$TAR"
(cd "$OUT" && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256")
echo "package: $TAR ($(du -h "$TAR" | cut -f1), $(tar -tzf "$TAR" | grep -vc '/$') files, source $SRC_DESC)"
cat "$TAR.sha256"

[ "$VERIFY" = 1 ] || exit 0
# ---- 기동 시험: 문서(docs/release/license-server.md §3, README §3)대로 새 스택을 설치·기동하고 지운다 ----
V=$(mktemp -d); P="lsverify$$"
tar -xzf "$TAR" -C "$V"
PKG="$V/$NAME"; D="$V/opt"
(cd "$PKG" && sha256sum --quiet -c SHA256SUMS)
grep -qx "source: $SRC_DESC" "$PKG/VERSION" || fail "VERSION source mismatch"
LOADED=$(docker load -q -i "$PKG/images/$NAME.tar" | sed -n 's/^Loaded image: //p')
[ "$LOADED" = "$IMAGE" ] || fail "image tar holds '$LOADED', expected $IMAGE"
[ "$(docker image inspect -f '{{index .Config.Labels "org.opencontainers.image.revision"}}' "$IMAGE")" = "$SRC_DESC" ] \
  || fail "image revision label mismatch"
mkdir -p "$D" && cp -r "$PKG/license-server/deploy/." "$D/"
trap '(cd "$D" && docker compose -p "$P" down -v --remove-orphans >/dev/null 2>&1) || true; rm -rf "$TMP" "$V"' EXIT
(cd "$D" && ./init-secrets.sh verify.local >/dev/null)
U="$(id -u):$(id -g)"; mkdir -m 700 "$D/ceremony"
head -c 24 /dev/urandom | base64 > "$D/ceremony/pass"
docker run --rm --network none --user "$U" --entrypoint /opt/licensectl -v "$D/ceremony:/work" "$IMAGE" \
  keygen -out /work/keys >/dev/null
FP=$(docker run --rm --network none --user "$U" --entrypoint java -v "$D/ceremony:/work" "$IMAGE" \
  -jar /app/license-server.jar seal-key --in /work/keys/private.key --out /work/sealed-key.json \
  --licensectl /opt/licensectl --passphrase-file /work/pass | sed -n 's/^fingerprint: \([^ ]*\).*/\1/p')
[ -n "$FP" ] || fail "seal-key printed no fingerprint"
install -m 444 "$D/ceremony/sealed-key.json" "$D/data/key/sealed-key.json"
printf 'LICENSE_PUBLIC_KEY_FP=%s\nLICENSE_BIND_IP=127.0.0.3\nLICENSE_SERVER_TAG=%s\n' "$FP" "$VERSION" > "$D/.env"
(cd "$D" && docker compose --progress quiet -p "$P" up -d --wait --wait-timeout 180) || {
  (cd "$D" && docker compose -p "$P" logs --tail 50 web) >&2; fail "stack did not become healthy"; }
code=$(curl -sk -o /dev/null -w '%{http_code}' https://127.0.0.3:8443/login)
[ "$code" = 200 ] || fail "login page returned $code"
migrated=$(cd "$D" && docker compose -p "$P" exec -T db psql -U postgres -d license -At \
  -c "SELECT count(*) FROM flyway_schema_history WHERE success")
logs=$(cd "$D" && docker compose -p "$P" logs web)
grep -E ' (WARN|ERROR) ' <<<"$logs" >&2 && fail "warnings or errors in web log"
echo "verified: extracted package installs and starts (healthy, login 200, $migrated migrations, no WARN/ERROR)"
