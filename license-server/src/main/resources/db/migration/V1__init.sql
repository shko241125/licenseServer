-- 소유자: 마이그레이션 계정(license_owner). 앱 계정(license_app)에는 필요한 최소 권한만 부여한다.
-- 두 역할은 DB 초기화 스크립트(deploy/db-init) 또는 테스트 초기화에서 미리 만든다.

CREATE TABLE app_user (
  id                   BIGSERIAL PRIMARY KEY,
  username             TEXT UNIQUE NOT NULL CHECK (username ~ '^[a-z0-9._-]{3,32}$'),
  password_hash        TEXT NOT NULL,
  role                 TEXT NOT NULL CHECK (role IN ('ADMIN', 'ISSUER', 'VIEWER')),
  totp_secret_enc      TEXT,                              -- AES-GCM(data key) 로 암호화, Base64
  totp_last_step       BIGINT NOT NULL DEFAULT 0,         -- 같은 코드 재사용 방지
  enabled              BOOLEAN NOT NULL DEFAULT TRUE,
  must_change_password BOOLEAN NOT NULL DEFAULT TRUE,
  failed_count         INT NOT NULL DEFAULT 0,
  locked_until         TIMESTAMPTZ,
  created_at           TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE password_history (
  id            BIGSERIAL PRIMARY KEY,
  user_id       BIGINT NOT NULL REFERENCES app_user(id),
  password_hash TEXT NOT NULL,
  created_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX password_history_user ON password_history (user_id, created_at DESC);

CREATE TABLE license (
  id                BIGSERIAL PRIMARY KEY,
  license_id        CHAR(32) UNIQUE NOT NULL CHECK (license_id ~ '^[0-9a-f]{32}$'),
  submission_token  UUID UNIQUE NOT NULL,
  status            TEXT NOT NULL CHECK (status IN ('PENDING', 'ISSUED', 'FAILED', 'VOID')),
  project_name      TEXT NOT NULL,
  license_type      TEXT NOT NULL CHECK (license_type IN ('production', 'trial', 'poc')),
  site_id           TEXT NOT NULL,
  online_stt        INT NOT NULL CHECK (online_stt BETWEEN 0 AND 100000),
  offline_stt       INT NOT NULL CHECK (offline_stt BETWEEN 0 AND 100000),
  issued_at         TIMESTAMPTZ NOT NULL,
  not_before        TIMESTAMPTZ NOT NULL,
  not_after         TIMESTAMPTZ NOT NULL,
  grace_period_days INT NOT NULL CHECK (grace_period_days BETWEEN 0 AND 90),
  warning_notice    TEXT NOT NULL,
  license_text      TEXT,
  license_sha256    CHAR(64),
  public_key_fp     CHAR(64) NOT NULL,
  renewed_from      CHAR(32) REFERENCES license(license_id),
  issued_by         BIGINT NOT NULL REFERENCES app_user(id),
  error             TEXT,
  void_reason       TEXT,
  voided_by         BIGINT REFERENCES app_user(id),
  voided_at         TIMESTAMPTZ,
  created_at        TIMESTAMPTZ NOT NULL DEFAULT now(),
  CHECK (not_before < not_after),
  CHECK (online_stt + offline_stt > 0),
  CHECK (status <> 'ISSUED' OR (license_text IS NOT NULL AND license_sha256 IS NOT NULL))
);
CREATE INDEX license_site ON license (site_id);
CREATE INDEX license_project ON license (project_name);
CREATE INDEX license_not_after ON license (not_after);
CREATE INDEX license_created ON license (created_at DESC);

-- 상태 전이 강제: PENDING→ISSUED|FAILED (결과 열만), ISSUED→VOID (사유 필수). 그 외 변경 금지.
CREATE FUNCTION license_guard() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  IF (NEW.license_id, NEW.submission_token, NEW.project_name, NEW.license_type, NEW.site_id,
      NEW.online_stt, NEW.offline_stt, NEW.issued_at, NEW.not_before, NEW.not_after,
      NEW.grace_period_days, NEW.warning_notice, NEW.public_key_fp, NEW.renewed_from,
      NEW.issued_by, NEW.created_at)
     IS DISTINCT FROM
     (OLD.license_id, OLD.submission_token, OLD.project_name, OLD.license_type, OLD.site_id,
      OLD.online_stt, OLD.offline_stt, OLD.issued_at, OLD.not_before, OLD.not_after,
      OLD.grace_period_days, OLD.warning_notice, OLD.public_key_fp, OLD.renewed_from,
      OLD.issued_by, OLD.created_at) THEN
    RAISE EXCEPTION 'license % : contract fields are immutable', OLD.license_id;
  END IF;
  IF OLD.status = 'PENDING' AND NEW.status IN ('ISSUED', 'FAILED')
     AND NEW.void_reason IS NULL AND NEW.voided_by IS NULL THEN
    RETURN NEW;
  END IF;
  IF OLD.status = 'ISSUED' AND NEW.status = 'VOID'
     AND NEW.license_text IS NOT DISTINCT FROM OLD.license_text
     AND NEW.license_sha256 IS NOT DISTINCT FROM OLD.license_sha256
     AND NEW.error IS NOT DISTINCT FROM OLD.error
     AND NEW.void_reason IS NOT NULL AND NEW.voided_by IS NOT NULL AND NEW.voided_at IS NOT NULL THEN
    RETURN NEW;
  END IF;
  RAISE EXCEPTION 'license % : transition % -> % not allowed', OLD.license_id, OLD.status, NEW.status;
END $$;
CREATE TRIGGER license_guard BEFORE UPDATE ON license FOR EACH ROW EXECUTE FUNCTION license_guard();

-- 감사 로그: 추가만 허용(권한) + 해시 체인. detail 은 해시 계산에 쓴 정규화 문자열을 그대로(TEXT) 저장한다.
CREATE TABLE audit_event (
  seq         BIGINT PRIMARY KEY,
  at          TIMESTAMPTZ NOT NULL,
  actor       TEXT NOT NULL,
  action      TEXT NOT NULL,
  target      TEXT,
  client_ip   TEXT,
  detail_json TEXT NOT NULL,
  prev_hash   CHAR(64) NOT NULL,
  hash        CHAR(64) NOT NULL UNIQUE
);
CREATE SEQUENCE audit_event_seq;
CREATE INDEX audit_event_at ON audit_event (at DESC);
CREATE INDEX audit_event_target ON audit_event (target);

-- 앱 계정 권한 (DELETE·TRUNCATE 는 어떤 테이블에도 없음)
GRANT USAGE ON SCHEMA public TO license_app;
GRANT SELECT, INSERT, UPDATE ON app_user TO license_app;
GRANT SELECT, INSERT ON password_history TO license_app;
GRANT SELECT, INSERT ON license TO license_app;
GRANT UPDATE (status, license_text, license_sha256, error, void_reason, voided_by, voided_at) ON license TO license_app;
GRANT SELECT, INSERT ON audit_event TO license_app;
GRANT USAGE ON SEQUENCE app_user_id_seq, password_history_id_seq, license_id_seq, audit_event_seq TO license_app;
