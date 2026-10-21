#!/usr/bin/env bash
# podman 배포(docker-compose.yml 과 같은 구성). Rocky/RHEL 8 의 podman 4.9(rootful, CNI 또는 netavark)에서 검증.
#   sudo ./podman-stack.sh up | down [-v] | ps | logs <web|db> [podman logs 옵션]
# compose 와 다른 점(docs/release/license-server-podman.md):
#  - web·db 를 pod 하나에 넣는다: 네트워크를 공유하므로 DNS 플러그인 없이 127.0.0.1 로 연결하고,
#    DB 는 127.0.0.1 에서만 받는다(pod 밖에서 접근 불가).
#  - pod 네트워크의 NAT(masquerade)를 끈다(compose 의 enable_ip_masquerade=false 와 같음): 게시 포트(수신)는
#    동작하고 컨테이너발 외부 연결은 응답을 받지 못한다. podman 은 internal 네트워크에 포트를 게시하지 못하고
#    (CNI·netavark 모두 확인), NAT 끄기 옵션도 없어 CNI 설정 파일의 ipMasq 를 바꾼다. netavark 는 바꿀 수단이
#    없으므로 경고만 하고, 호스트 방화벽 규칙으로 막는다(문서).
#  - 읽기 전용 루트에서 비밀값 파일을 걸 자리가 없으므로 /run/secrets 에 tmpfs 를 깐다.
#  - health 검사는 여기서 지정한다(podman load 한 이미지에는 HEALTHCHECK 가 남지 않음).
#  - 재부팅 후 자동 시작: systemctl enable podman-restart.service (restart=always 컨테이너를 시작).
set -euo pipefail
cd "$(dirname "$0")"
P="${LICENSE_STACK:-license-server}"   # 이름 접두어: pod·컨테이너·네트워크·볼륨
PG_IMAGE=postgres:16-alpine           # docker-compose.yml 의 db 이미지와 같게 유지(발행 스크립트가 검사)

die() { echo "error: $*" >&2; exit 1; }

load_env() {
  [ -r .env ] || die ".env not found (README §3 키 생성식)"
  set -a; . ./.env; set +a
  [ -n "${LICENSE_PUBLIC_KEY_FP:-}" ] || die "LICENSE_PUBLIC_KEY_FP is not set in .env"
  [ "${LICENSE_RUNTIME:-}" = podman ] || die "add LICENSE_RUNTIME=podman to .env (backup.sh·verify-anchors.sh 가 podman 을 쓰게)"
  TAG="${LICENSE_SERVER_TAG:-latest}"
  BIND="${LICENSE_BIND_IP:-127.0.0.1}"
  [ "$BIND" != 0.0.0.0 ] || die "LICENSE_BIND_IP must be the management IP, not 0.0.0.0"
  for f in db_superuser_password db_owner_password db_app_password data_key initial_admin_password \
           tls_keystore_password tls_keystore.p12; do
    [ -r "secrets/$f" ] || die "secrets/$f missing (./init-secrets.sh)"
  done
}

secret() { echo "-v" "$PWD/secrets/$1:/run/secrets/$1:ro,z"; }

wait_healthy() {  # wait_healthy <컨테이너> <초>
  local end=$((SECONDS + $2))
  until podman healthcheck run "$1" >/dev/null 2>&1; do
    [ "$(podman inspect -f '{{.State.Running}}' "$1")" = true ] || { podman logs --tail 50 "$1" >&2; die "$1 exited"; }
    [ $SECONDS -lt $end ] || { podman logs --tail 50 "$1" >&2; die "$1 did not become healthy in $2s"; }
    sleep 3
  done
}

create_net() {
  podman network create "$P-net" >/dev/null
  case "$(podman info --format '{{.Host.NetworkBackend}}')" in
    cni)
      local f="/etc/cni/net.d/$P-net.conflist"
      sed -i 's/"ipMasq": true/"ipMasq": false/' "$f"
      grep -q '"ipMasq": false' "$f" || die "could not disable NAT in $f"
      ;;
    *) echo "warning: network backend is not CNI; outbound NAT stays on. Block egress with a host firewall rule" \
            "(docs/release/license-server-podman.md)" >&2 ;;
  esac
}

up() {
  load_env
  podman image exists "license-server:$TAG" || die "image license-server:$TAG not loaded (podman load -i images/...)"
  podman image exists "$PG_IMAGE" || die "image $PG_IMAGE not loaded"
  podman network exists "$P-net" || create_net
  podman volume exists "$P-pgdata" || podman volume create "$P-pgdata" >/dev/null
  # 컨테이너는 매번 새로 만든다(이미지 태그 변경 = 업그레이드). DB 볼륨·비밀값은 유지된다.
  podman pod exists "$P" && podman pod rm -f "$P" >/dev/null
  podman pod create --name "$P" --infra-name "$P-infra" --network "$P-net" -p "$BIND:8443:8443" >/dev/null

  podman run -d --pod "$P" --name "$P-db" --restart always \
    -e POSTGRES_DB=license -e POSTGRES_USER=postgres -e POSTGRES_PASSWORD_FILE=/run/secrets/db_superuser_password \
    -v "$P-pgdata:/var/lib/postgresql/data" -v "$PWD/db-init:/docker-entrypoint-initdb.d:ro,z" \
    $(secret db_superuser_password) $(secret db_owner_password) $(secret db_app_password) \
    --health-cmd 'pg_isready -U postgres -d license' --health-interval 10s --health-timeout 5s --health-retries 10 \
    "$PG_IMAGE" postgres -c listen_addresses=127.0.0.1 >/dev/null
  wait_healthy "$P-db" 120

  podman run -d --pod "$P" --name "$P-web" --restart always \
    --user 10001:10001 --read-only --tmpfs /tmp:size=64m,mode=1777 \
    --tmpfs /run/secrets:size=1m,mode=0755 \
    --security-opt no-new-privileges --cap-drop ALL \
    --memory 1g --memory-swap 1g \
    -e DB_URL=jdbc:postgresql://127.0.0.1:5432/license \
    -e LICENSE_PUBLIC_KEY_FP="$LICENSE_PUBLIC_KEY_FP" \
    -e LICENSE_BOOTSTRAP_ADMIN="${LICENSE_BOOTSTRAP_ADMIN:-admin}" -e TZ=Asia/Seoul \
    -v "$PWD/data/key/sealed-key.json:/data/key/sealed-key.json:ro,z" \
    -v "$PWD/secrets/tls_keystore.p12:/data/tls/keystore.p12:ro,z" \
    $(secret db_app_password) $(secret db_owner_password) $(secret data_key) \
    $(secret initial_admin_password) $(secret tls_keystore_password) \
    --health-cmd "curl -fsS http://127.0.0.1:8081/actuator/health | grep -q '\"UP\"' || exit 1" \
    --health-interval 30s --health-timeout 5s --health-start-period 60s --health-retries 3 \
    "license-server:$TAG" >/dev/null
  wait_healthy "$P-web" 180
  echo "up: https://$BIND:8443 (license-server:$TAG)"
}

case "${1:-}" in
  up) up ;;
  down)
    podman pod exists "$P" && podman pod rm -f "$P" >/dev/null
    if [ "${2:-}" = -v ]; then  # DB 까지 지운다: 계정·발급 기록·감사 로그가 모두 사라짐
      podman volume exists "$P-pgdata" && podman volume rm "$P-pgdata" >/dev/null
      podman network exists "$P-net" && podman network rm "$P-net" >/dev/null
    fi
    echo "down" ;;
  ps) podman ps -a --filter "pod=$P" --format '{{.Names}}\t{{.Status}}\t{{.Ports}}' ;;
  logs) [ $# -ge 2 ] || die "usage: $0 logs <web|db> [options]"; s="$2"; shift 2; podman logs "$@" "$P-$s" ;;
  *) echo "usage: $0 up | down [-v] | ps | logs <web|db> [options]" >&2; exit 2 ;;
esac
