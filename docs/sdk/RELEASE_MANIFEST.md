# SDK 적용용 발행 목록 (RELEASE MANIFEST)

SDK 패키지는 라이선스 **발급 서버와 분리해서** 발행합니다. 이 문서는 무엇을 담고, 무엇을 빼고, 어떻게 만들고 검증하는지를 정리합니다.
- 기계가 읽는 목록: 원 저장소의 `scripts/sdk-release-files.txt`
- 생성 도구: 원 저장소의 `scripts/make-sdk-release.sh`
- 두 파일은 발행 담당용이라 패키지에는 들어가지 않습니다.

## 1. 포함 목록

| 경로 | 용도 | SDK 빌드에 필요 |
|---|---|---|
| `CMakeLists.txt` | 빌드 진입점, 버전(`project(... VERSION)`), 옵션 | ● |
| `cmake/SttLicenseHardening.cmake` | `stt_license_harden_shared_library()` 심볼 은닉 | ● |
| `core/include/stt_license/license.h` | 공개 헤더(C++11 이상) — SDK가 include하는 유일한 파일 | ● |
| `core/src/` | 구현(JSON·정규화·Ed25519·검증·Manager), `embedded_key.cpp.in` 공개키 내장 템플릿 | ● |
| `core/tests/` | GoogleTest 단위 테스트, `crosscheck/GoldenCheck.java`(JDK 교차검증) | |
| `core/testdata/` | 골든 벡터(`golden_*` — 정규화 호환성의 증거), 계약 예시 JSON | |
| `core/fuzz/` | libFuzzer 대상 | |
| `tools/licensectl/` | 개발 키 생성·시험 라이선스 발급·반입 전 `verify` CLI | |
| `integration/harness/` | JNI 참조 구현(★ = SDK로 옮길 부분) + Java 결합 테스트 | |
| `integration/cmake-consumer/` | SDK 형태 CMake 소비자(연동 인수 테스트) | |
| `scripts/check.sh` | 전체 검증 7단계 | |
| `docs/sdk/` | 이 문서 묶음(README, 01~06, 이 목록) | |
| `.gitattributes` | `*.sh` LF 줄바꿈 유지 | |
| `README.md` *(생성)* | 패키지 루트 안내 → `docs/sdk/README.md` | |
| `VERSION` *(생성)* | 버전과 원 저장소 커밋 | |
| `SHA256SUMS` *(생성)* | 패키지 안 모든 파일의 SHA-256 | |

"SDK 빌드에 필요"만 있으면 `stt_license::embedded`가 빌드됩니다(최소 복사, 01-build.md §2). 나머지는 검증과 참조용이지만, **발행 패키지는 전체를 담습니다.** 수용 기준(05-testing.md)을 패키지만으로 재현하기 위해서입니다.

## 2. 제외 목록 (패키지에 절대 들어가면 안 되는 것)

| 대상 | 이유 |
|---|---|
| `license-server/` 전체(코드, `docs/`, 배포 스크립트, `deploy/secrets`, DB 데이터) | 발급 서버는 별도 발행·사내 전용. 운영 절차와 감사 방법론이 들어 있음 |
| 비밀키(`private.key`, `stt-license-ed25519-private-v1:` + 키 값), PEM 비밀키 | 유출 시 누구나 라이선스 발급 가능 → 전 고객 재배포 |
| 운영 공개키 파일 | 패키지는 키와 무관한 소스. 공개키는 SDK 저장소가 따로 관리(01-build.md §6) |
| 실제 고객 라이선스, 기존 `sonaLicense.*.key` | 고객 데이터 |
| `work_claude/`, `work_codex/`, `docs/*.pdf` | 내부 계획·원문 자료 |
| 원 저장소 루트 `README.md`, `.gitignore`, `.dockerignore` | 발급 서버를 포함한 저장소 안내·설정 |
| `build*/`, 편집기 임시 파일(`*.swp`) | 산출물·잡파일 |

목록 방식(허용 목록)으로 담으므로 새 파일은 기본적으로 빠집니다. 추가하려면 `sdk-release-files.txt`에 적고 이 표도 고칩니다.

## 3. 발행 절차

1. **버전 결정**: `CMakeLists.txt`의 `project(stt_license VERSION x.y.z)`와 `docs/sdk/README.md` §6 변경 이력을 같이 고칩니다.
   - 공개 헤더 API·오류 코드 순번·라이선스 규격의 호환이 깨지면 x를 올립니다.
   - API 추가는 y, 수정만 있으면 z를 올립니다.
2. 커밋하고 태그를 답니다: `git tag sdk-v1.1.0`
3. 패키지를 만들고 검증합니다(리눅스, 필요 도구는 05-testing.md §2).
   ```bash
   scripts/make-sdk-release.sh --ref sdk-v1.1.0 --verify
   # → build/sdk-release/stt-license-sdk-1.1.0.tar.gz (+ .sha256)
   ```
   커밋 전 사전 점검은 `--worktree`로 합니다. 결과 버전에 `-dirty`가 붙으며, 이 결과물은 발행하지 않습니다.
4. `.tar.gz`와 `.sha256`을 SDK 팀에 전달합니다. **운영 공개키 `public.key`는 패키지와 별도로**, 그 SHA-256 지문은 **또 다른 경로로** 알립니다(01-build.md §6.2). 공개키가 바뀌지 않았다면 다시 보낼 필요는 없습니다(SDK 저장소에 이미 커밋됨).

## 4. 스크립트가 자동 확인하는 것

- 목록의 모든 경로가 존재합니다(오타·삭제 감지).
- 금지 파일이 없습니다: `private.key`, `*.pdf`, `*.swp`, `.env`, `sonaLicense*`, `license-server*`, `work_*`.
- `*.key`/`*.lic`는 `core/testdata/golden_*`만 허용합니다.
- 비밀키 값 패턴(`stt-license-ed25519-private-v1:` + Base64 44자), PEM 비밀키가 없습니다.
- 문서(`*.md`)의 상대 링크가 모두 패키지 안에서 풀립니다(빠진 문서를 가리키지 않음).
- 결과를 재현할 수 있습니다: `git archive`(커밋 내용만), 파일 순서·소유자·시각을 고정하고, `gzip -n`으로 압축합니다.
- `--verify`: 풀어낸 패키지의 `SHA256SUMS`를 대조하고, **패키지만으로** `scripts/check.sh` 7단계를 통과해야 합니다.

## 5. 발행 수용 기준

- [ ] `make-sdk-release.sh --ref <태그> --verify` 성공(`ALL CHECKS PASSED`)
- [ ] `VERSION`의 커밋이 태그와 일치
- [ ] `docs/sdk/README.md` 변경 이력에 이번 버전이 있음
- [ ] 이전 버전 대비 `core/testdata/golden_*` 변경 없음(바뀌었다면 기존 라이선스 호환 영향 검토 기록)
- [ ] 오류 코드 순번 변경 없음(헤더 `static_assert`)
