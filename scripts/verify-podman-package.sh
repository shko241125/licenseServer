#!/usr/bin/env bash
# 발급 서버 podman 패키지 기동 시험: 풀어낸 패키지만으로 문서(docs/release/license-server-podman.md §3)대로
# 새 스택을 설치·기동해 확인하고 지운다. podman 이 있는 호스트(root)에서 실행한다.
#   sudo scripts/verify-podman-package.sh <license-server-<버전>-podman.tar.gz>
# 기존 스택과 겹치지 않는다: 이름 접두어 lsverify<pid>, 127.0.0.3:8443, 임시 디렉터리. 끝나면 DB 볼륨까지 지운다.
# make-server-release.sh --podman ... --verify 는 이 스크립트를 Rocky 8.10 + podman 4.9 컨테이너 안에서 실행한다.
set -euo pipefail
TAR="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
NAME="$(basename "$TAR" .tar.gz)"
fail() { echo "VERIFY FAILED: $*" >&2; exit 1; }
command -v podman >/dev/null || fail "podman not found"

V=$(mktemp -d); export LICENSE_STACK="lsverify$$"
PKG="$V/$NAME"; D="$V/opt"
trap '[ -x "$D/podman-stack.sh" ] && "$D/podman-stack.sh" down -v >/dev/null 2>&1; rm -rf "$V"' EXIT
tar -xzf "$TAR" -C "$V"
(cd "$PKG" && sha256sum --quiet -c SHA256SUMS)
IMAGE=$(sed -n 's/^image: \([^ ]*\) .*/\1/p' "$PKG/VERSION"); TAG="${IMAGE#license-server:}"
[ -n "$IMAGE" ] || fail "no image line in VERSION"
grep -qx 'flavor: podman' "$PKG/VERSION" || fail "not a podman package"
for t in "$PKG"/images/*.tar; do podman load -q -i "$t" >/dev/null; done
podman image exists "$IMAGE" || fail "image $IMAGE not in package"
REV=$(sed -n 's/^image-source: //p' "$PKG/VERSION")
[ "$(podman image inspect -f '{{index .Labels "org.opencontainers.image.revision"}}' "$IMAGE")" = "$REV" ] \
  || fail "image revision label does not match VERSION image-source"

mkdir -p "$D" && cp -r "$PKG/license-server/deploy/." "$D/"
(cd "$D" && ./init-secrets.sh verify.local >/dev/null)
U="$(id -u):$(id -g)"; mkdir -m 700 "$D/ceremony"
head -c 24 /dev/urandom | base64 > "$D/ceremony/pass"
podman run --rm --network none --user "$U" --entrypoint /opt/licensectl -v "$D/ceremony:/work:Z" "$IMAGE" \
  keygen -out /work/keys >/dev/null
FP=$(podman run --rm --network none --user "$U" --entrypoint java -v "$D/ceremony:/work:Z" "$IMAGE" \
  -jar /app/license-server.jar seal-key --in /work/keys/private.key --out /work/sealed-key.json \
  --licensectl /opt/licensectl --passphrase-file /work/pass | sed -n 's/^fingerprint: \([^ ]*\).*/\1/p')
[ -n "$FP" ] || fail "seal-key printed no fingerprint"
install -m 444 "$D/ceremony/sealed-key.json" "$D/data/key/sealed-key.json"
printf 'LICENSE_RUNTIME=podman\nLICENSE_PUBLIC_KEY_FP=%s\nLICENSE_BIND_IP=127.0.0.3\nLICENSE_SERVER_TAG=%s\n' \
  "$FP" "$TAG" > "$D/.env"
"$D/podman-stack.sh" up >/dev/null || fail "stack did not become healthy"

code=$(curl -sk -o /dev/null -w '%{http_code}' https://127.0.0.3:8443/login)
[ "$code" = 200 ] || fail "login page returned $code"
DB="$LICENSE_STACK-db"; WEB="$LICENSE_STACK-web"
migrated=$(podman exec -i "$DB" psql -U postgres -d license -At -c "SELECT count(*) FROM flyway_schema_history WHERE success")
podman logs "$WEB" 2>&1 | grep -E ' (WARN|ERROR) ' >&2 && fail "warnings or errors in web log"
if podman exec "$WEB" curl -s -m 4 -o /dev/null http://1.1.1.1; then
  [ "$(podman info -f '{{.Host.NetworkBackend}}')" = cni ] && fail "web container can reach outside"
  echo "warning: egress is open (not CNI) — add the host firewall rule (docs/release/license-server-podman.md)" >&2
fi
dbip=$(podman inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' "$LICENSE_STACK-infra")
timeout 3 bash -c "</dev/tcp/$dbip/5432" 2>/dev/null && fail "db port reachable from outside the pod"
(cd "$D" && ./backup.sh >/dev/null && ./verify-anchors.sh >/dev/null) || fail "backup.sh / verify-anchors.sh"
ls "$D"/backups/license-*.dump >/dev/null || fail "no dump written"
echo "verified: podman $(podman version -f '{{.Client.Version}}') ($(podman info -f '{{.Host.NetworkBackend}}')):" \
  "installs and starts (healthy, login 200, $migrated migrations, no WARN/ERROR, egress blocked, db pod-local," \
  "backup and anchors ok)"
