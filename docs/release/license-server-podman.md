# 발급 서버 배포 — podman 판 (Rocky/RHEL 8)

Docker가 없는 Rocky Linux 8.10(podman 4.9.4-rhel)에 발급 서버를 설치·업그레이드하는 방법입니다. docker 판([license-server.md](license-server.md))과 **같은 버전, 같은 서버 이미지**를 쓰고, 설치 방식만 다릅니다.
- 이 문서는 podman 판 패키지에 들어갑니다(`docs/release/license-server-podman.md`).
- 운영(발급·백업·복원·감사)의 내용은 [../../license-server/README.md](../../license-server/README.md)와 같습니다. docker 명령은 §4 대응표로 바꿔 씁니다.

## 1. 개요

| 항목 | 내용 |
|---|---|
| 산출물 | `license-server-<버전>-podman.tar.gz` + `.sha256` |
| 버전 | docker 판과 같음(`license-server/build.gradle.kts`). 이미지 파일은 같은 버전 docker 패키지의 것을 **바이트 그대로** 담음 → 서버 이미지 ID가 docker 판과 같음 |
| 대상 | Rocky/RHEL 8, podman 4.9(rootful), 네트워크 백엔드 CNI(기본) |
| 구성 도구 | `license-server/deploy/podman-stack.sh` (compose 대신. `podman-compose`는 EPEL에만 있고 기본 저장소에 없음) |
| 시험 도구 | `scripts/verify-podman-package.sh` (설치 서버에서 실물 확인용으로도 씀, §6) |
| 태그 | `server-v<버전>-podman` (podman 구성이 들어간 커밋) |

### docker 판과 다른 점과 이유 (Rocky 8.10 + podman 4.9.4 실측)
| 항목 | docker 판 | podman 판 | 이유 |
|---|---|---|---|
| 구성 | compose: web·db 컨테이너, 내부 네트워크 + 게시용 네트워크 | **pod 하나**에 web·db. web→db는 `127.0.0.1:5432`, DB는 `127.0.0.1`에서만 받음 | 기본 설치(CNI)에는 컨테이너 이름 DNS가 없음(netavark·aardvark-dns 미설치). pod는 네트워크를 공유해 DNS가 필요 없고, DB는 pod 밖에서 접근할 수 없음(시험으로 확인) |
| 외부 송신 차단 | 게시용 네트워크의 NAT를 끔 | pod 네트워크의 NAT를 끔(CNI 설정 `ipMasq: false`) | podman은 internal 네트워크에 포트를 게시하지 못하고(CNI·netavark 모두 확인), NAT 끄기 명령 옵션이 없음. 게시 포트는 동작, 컨테이너발 외부 연결은 막힘(확인) |
| health 검사 | 이미지의 HEALTHCHECK | 실행 명령에 지정(같은 명령·주기) | `podman load`한 이미지에는 HEALTHCHECK가 남지 않음(확인) |
| 비밀값 | compose file secret | 같은 파일을 `/run/secrets/<이름>`에 읽기 전용 bind mount(SELinux 라벨 `z`). web은 `/run/secrets`에 작은 tmpfs | podman secret은 비밀값 복사본을 저장소에 하나 더 만듦. 읽기 전용 루트에서는 mount 자리를 만들 tmpfs가 필요(확인) |
| 기동 순서 | `depends_on: service_healthy` | `podman-stack.sh up`이 db healthy를 기다린 뒤 web 기동 | |
| 재부팅 후 자동 시작 | Docker 데몬 + `restart: unless-stopped` | `restart=always` + `systemctl enable podman-restart.service` | podman은 데몬이 없음 |
| 실행 권한 | docker 그룹 | **root**(`sudo`) | rootless는 감사 로그의 접속 IP가 사라지고(사용자 공간 포워딩), 재부팅 시작에 linger·사용자 서비스가 필요 |
| 운영 스크립트 | `docker compose exec` | `backup.sh`·`verify-anchors.sh`가 `.env`의 `LICENSE_RUNTIME=podman`을 보고 `podman exec` | 같은 스크립트를 두 판이 공유 |

나머지(이미지, 사용자 uid 10001, 읽기 전용 루트, `cap_drop ALL`, `no-new-privileges`, 메모리 1 GiB·스왑 없음, 비밀값 생성, 키 봉인, DB 계정 분리, 마이그레이션)는 docker 판과 같습니다.

## 2. 사전 준비 (설치 서버)

```bash
cat /etc/rocky-release            # Rocky Linux release 8.10
sudo dnf install -y podman openssl   # 인터넷이 없으면 설치 DVD의 AppStream 등 내부 저장소에서
podman version                    # 4.9.4-rhel
sudo podman info --format '{{.Host.NetworkBackend}}'   # cni (기본). netavark 이면 §5 방화벽 규칙 필수
getenforce                        # Enforcing 그대로 둠(SELinux 라벨은 스크립트가 붙임)
```
- 네트워크 백엔드를 바꾸지 않습니다(바꾸려면 `podman system reset`이 필요해 기존 컨테이너·볼륨이 지워짐).

## 3. 배포 방법

### 발행하는 쪽 (발행 담당)
1. 같은 버전의 docker 패키지가 먼저 발행되어 있어야 합니다(`build/server-release/license-server-<버전>.tar.gz` + `.sha256`).
2. podman 구성을 커밋하고 태그를 답니다: `git tag -a server-v<버전>-podman -m "license-server <버전> podman"`
3. 패키지를 만들고 검증합니다(리눅스, Docker. 검증은 Rocky 8.10 + podman 4.9 컨테이너 안에서 하며 처음 한 번 dnf 인터넷이 필요).
   ```bash
   scripts/make-server-release.sh --ref server-v<버전>-podman \
     --podman build/server-release/license-server-<버전>.tar.gz --verify
   # → build/server-release/license-server-<버전>-podman.tar.gz (+ .sha256), 마지막에 "verified: podman 4.9.4-rhel (cni): ..."
   ```
4. 태그를 원격에 올리고, `.tar.gz`와 `.sha256`을 운영자에게 전달합니다. `docs/dev/STATUS.md`에 발행 기록을 남깁니다.

### 받는 쪽 — 새로 설치 (모두 root)
```bash
sha256sum -c license-server-<버전>-podman.tar.gz.sha256
tar -xzf license-server-<버전>-podman.tar.gz && cd license-server-<버전>-podman
sha256sum --quiet -c SHA256SUMS && cat VERSION
sudo scripts/verify-podman-package.sh ../license-server-<버전>-podman.tar.gz   # 선택(권장): 이 서버에서 시험 설치·기동 후 자동 삭제, 마지막 줄 "verified: ..."
sudo podman load -i images/license-server-<버전>.tar
sudo podman load -i images/postgres-16-alpine.tar
sudo install -d -m 755 /opt/license-server
sudo cp -r license-server/deploy/. /opt/license-server/ && cd /opt/license-server
sudo ./init-secrets.sh <관리망 호스트명>
```
키 생성식(최초 1회, 네트워크 없는 컨테이너):
```bash
TAG=<버전>
sudo mkdir -m 700 ceremony
sudo podman run --rm --network none --user 0:0 --entrypoint /opt/licensectl -v $PWD/ceremony:/work:Z \
  license-server:$TAG keygen -out /work/keys
sudo podman run --rm -it --network none --user 0:0 --entrypoint java -v $PWD/ceremony:/work:Z \
  license-server:$TAG -jar /app/license-server.jar seal-key \
  --in /work/keys/private.key --out /work/sealed-key.json --licensectl /opt/licensectl
#  → 패스프레이즈(20자 이상)를 두 번 입력. 출력된 fingerprint 를 기록
sudo install -m 444 ceremony/sealed-key.json data/key/sealed-key.json
sudo cp ceremony/keys/public.key ./public.key       # SDK 빌드 담당자에게 전달(지문도 함께)
sudo shred -u ceremony/keys/private.key
sudo tee .env >/dev/null <<EOF
LICENSE_RUNTIME=podman
LICENSE_PUBLIC_KEY_FP=<fingerprint>
LICENSE_BIND_IP=<관리망 IP>
LICENSE_SERVER_TAG=$TAG
EOF
sudo chmod 600 .env
```
- `--user 0:0`: 이미지의 기본 사용자(uid 10001)는 root 소유 `ceremony/`에 쓸 수 없습니다. 네트워크 없는 일회용 컨테이너라 root로 실행합니다. 만든 파일은 root 소유이고, `/opt/license-server`는 root만 다룹니다.
- 패스프레이즈 보관, `sealed-key.json` 2부 백업, 분실 시 조치는 서버 README §3과 같습니다.

기동과 자동 시작:
```bash
sudo ./podman-stack.sh up          # db healthy → web healthy 까지 기다림. "up: https://<관리망 IP>:8443"
sudo ./podman-stack.sh ps
sudo systemctl enable podman-restart.service   # 재부팅 후 restart=always 컨테이너 시작
sudo firewall-cmd --permanent --add-port=8443/tcp && sudo firewall-cmd --reload   # firewalld 사용 시(관리망 영역으로 한정 권장)
```
- `https://<관리망 IP>:8443`에서 `admin`과 `secrets/initial_admin_password`로 로그인합니다. 첫 로그인 때 비밀번호 변경과 OTP 등록.
- 재부팅 직후에는 db와 web이 함께 시작됩니다. web이 db보다 먼저 뜨면 DB 연결 실패로 종료했다가 `restart=always`로 다시 시작합니다(1~2회 재시작 정상).

### 받는 쪽 — 업그레이드
1. 백업: `sudo ./backup.sh`. 덤프 파일 이름과 이전 `LICENSE_SERVER_TAG`를 적어 둡니다. 이전 이미지는 지우지 않습니다.
2. 새 패키지를 대조하고 이미지를 `podman load` 합니다.
3. 배포 구성을 덮어씁니다: `sudo cp -r license-server/deploy/. /opt/license-server/` (`secrets/`·`data/`·`.env`·`backups/`는 패키지에 없어 그대로)
4. `.env`의 `LICENSE_SERVER_TAG`를 새 버전으로 바꾸고 `sudo ./podman-stack.sh up` (컨테이너를 새로 만듦, DB 볼륨 유지, 마이그레이션 자동 적용)
5. 확인: `ps`(healthy), 로그인, 키 해제, 감사 로그 "해시 체인 무결성 검사".

**되돌리기:** 마이그레이션이 적용됐다면 이전 이미지만 띄우면 안 됩니다. §4 "복원"으로 1단계 덤프를 되돌린 뒤 `.env`를 이전 버전으로 바꾸고 `up`.

## 4. docker 명령 대응표 (서버 README를 읽을 때)

모두 `/opt/license-server`에서 root로 실행합니다. 이름 접두어는 `license-server`(바꾸려면 환경변수 `LICENSE_STACK`).

| 서버 README의 docker 명령 | podman 판 |
|---|---|
| `docker compose up -d` | `./podman-stack.sh up` |
| `docker compose ps` | `./podman-stack.sh ps` |
| `docker compose logs web` | `./podman-stack.sh logs web` (`--tail 100`, `-f` 등 podman logs 옵션) |
| `docker compose stop web` / `start web` | `podman stop license-server-web` / `podman start license-server-web` |
| `docker compose exec -T db <명령>` | `podman exec -i license-server-db <명령>` |
| `docker compose down` | `./podman-stack.sh down` (DB 유지) |
| `docker compose down -v` (처음 상태로 초기화) | `./podman-stack.sh down -v` (**계정·발급 기록·감사 로그 삭제**) |
| `docker load -i …` / `docker run …` | `podman load -i …` / `podman run …` (bind mount에 `:Z`) |
| `./backup.sh`, `./verify-anchors.sh` | 그대로(`.env`의 `LICENSE_RUNTIME=podman`) |

복원:
```bash
sudo podman stop license-server-web
sudo podman exec -i license-server-db pg_restore -U postgres -d license --clean < backups/license-<시각>.dump
sudo podman start license-server-web
```
이어서 감사 로그 화면에서 "해시 체인 무결성 검사"를 하고 마지막 해시를 `audit-anchor.log`와 대조합니다.

백업 일정(root crontab): `0 2 * * * cd /opt/license-server && ./backup.sh >> backups/backup.log 2>&1`

## 5. 네트워크·보안 참고

- **외부 송신 차단:** CNI면 `podman-stack.sh`가 pod 네트워크(`license-server-net`)의 NAT를 끕니다(`/etc/cni/net.d/license-server-net.conflist`의 `ipMasq: false`). 사설 IP 패킷 자체까지 막으려면 호스트 방화벽 규칙을 더합니다.
  ```bash
  SUBNET=$(sudo podman network inspect license-server-net --format '{{range .Subnets}}{{.Subnet}}{{end}}')
  sudo firewall-cmd --permanent --direct --add-rule ipv4 filter FORWARD 0 -s "$SUBNET" -m conntrack --ctstate NEW -j DROP
  sudo firewall-cmd --reload
  ```
  netavark 백엔드는 NAT를 끌 수단이 없어 `up`이 경고합니다. 이 규칙이 **필수**입니다.
- **감사 로그의 접속 IP:** rootful podman의 포트 게시는 DNAT라 관리자 PC의 실제 IP가 기록됩니다(docker의 userland-proxy 설정은 해당 없음).
- **pod 안 공유:** web과 db가 네트워크를 공유하므로 db 컨테이너도 web의 관리 포트(127.0.0.1:8081)에 닿습니다. 관리 포트는 상세 정보 없는 health만 노출합니다(`application.yml`).
- **TLS:** 사내 CA 인증서로 바꾸는 방법은 서버 README §3과 같습니다(`secrets/tls_keystore.p12`, `secrets/tls_keystore_password`).

## 6. 검증 범위

| 항목 | 상태 |
|---|---|
| Rocky 8.10 + podman 4.9.4-rhel + CNI(컨테이너 안, `--privileged`): 이미지 load, 비밀값·키 생성식, 기동, healthy, 로그인 200, 마이그레이션, 경고·오류 로그 0, 외부 송신 차단, pod 밖 DB 차단, 재기동 시 데이터 유지, 백업·앵커 대조 | 발행 때 `--verify`로 확인 |
| SELinux Enforcing에서 bind mount 라벨(`z`/`Z`) | **실물 서버에서 확인**(시험 환경은 SELinux 없음) |
| cgroup v1(Rocky 8 기본)에서 메모리 제한 | **실물 서버에서 확인**(시험 환경은 cgroup v2) |
| 재부팅 후 `podman-restart.service` 자동 시작, health 검사 타이머(systemd) | **실물 서버에서 확인**(시험 환경에 systemd 없음) |
| firewalld 사용 중 포트 게시 | **실물 서버에서 확인** |
| netavark 백엔드 | 포트 게시만 확인. 외부 송신 차단은 방화벽 규칙에 의존 |

실물 서버에서는 설치 전에 `sudo scripts/verify-podman-package.sh <패키지>`를 실행하면 앞의 다섯 항목 중 SELinux·cgroup·firewalld 조건에서 설치·기동이 되는지 바로 확인할 수 있습니다. 재부팅 자동 시작은 실제 설치 후 `sudo reboot` → `sudo ./podman-stack.sh ps`로 확인합니다.

## 7. 패키지 안

docker 판 패키지 목록([license-server.md](license-server.md) §4)과 같고, 다음이 다릅니다.
| 경로 | 내용 |
|---|---|
| `README.md`, `VERSION` *(생성)* | podman 안내. `VERSION`: 버전, `flavor: podman`, `source`(podman 구성 커밋), `image-source`(이미지를 빌드한 커밋), `images-from`(이미지를 가져온 docker 패키지와 그 SHA-256), 이미지 ID·postgres 다이제스트 |
| `images/*.tar` | 같은 버전 docker 패키지의 이미지 파일 그대로 |

`podman-stack.sh`, 이 문서, `scripts/verify-podman-package.sh`는 docker 판 패키지에도 함께 들어갑니다(같은 허용 목록).

## 8. 발행 이력

| 버전 | 태그 → 커밋 | 이미지 출처 | 패키지 SHA-256 |
|---|---|---|---|
| 1.8.0 | `server-v1.8.0` → `2a11774`(docker 판과 같은 태그. podman 구성이 이미 그 커밋에 있어 별도 태그 없음) | `license-server-1.8.0.tar.gz`(`d5cd1d73…a7e0`)의 이미지 그대로 | `6c9bdf5e98a6ad45efcf53323e7b36087fe28973d55050b2b3fac37404c5c5fb` |
| 1.7.0 | `server-v1.7.0-podman` → `69eb452` | `license-server-1.7.0.tar.gz`(`783f6be6…b67b4`)의 이미지 그대로, 원 커밋 `6b9fc33` | `969061628ef42f377066d39e499f1bc5e046f6a88594ee89b55e7f6dfe32167e` |
