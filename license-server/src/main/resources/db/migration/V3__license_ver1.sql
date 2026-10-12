-- 라이선스 형식 license_ver 1 (계획서 §15, 2026-10-06).
-- 새 형식: solution, allowed_channels.total + 엔진별 offline/online, 유예 없음, 운영은 영구(9999-12-31T23:59:59Z).
-- 이전 형식(format_version 1)으로 발급한 행은 지우지 않고 그대로 보존한다: 옛 열은 NULL 허용으로만 바꾼다.
-- 권한: 앱 계정의 license 테이블 단위 SELECT·INSERT 는 새 열에도 적용된다. 열 단위 UPDATE 권한은 바꾸지 않는다.

ALTER TABLE license ADD COLUMN license_ver INT CHECK (license_ver = 1);
ALTER TABLE license ADD COLUMN solution TEXT;
ALTER TABLE license ADD COLUMN channels_total INT CHECK (channels_total BETWEEN 1 AND 100000);
-- {"offline":{"엔진":n,...},"online":{...}} — 입력 순서를 보존한 JSON 텍스트(화면 표시용). 서명 대상은 license_text.
ALTER TABLE license ADD COLUMN channels_json TEXT;

ALTER TABLE license ALTER COLUMN online_stt DROP NOT NULL;
ALTER TABLE license ALTER COLUMN offline_stt DROP NOT NULL;
ALTER TABLE license ALTER COLUMN grace_period_days DROP NOT NULL;

-- 한 행은 정확히 한 형식만: 이전 형식(옛 열 채움, 새 열 NULL) 또는 license_ver 1(새 열 채움, 옛 열 NULL).
-- 기존 CHECK (online_stt + offline_stt > 0) 는 NULL 이면 통과하므로 그대로 둔다(이전 형식 행에는 계속 적용).
ALTER TABLE license ADD CONSTRAINT license_format_shape CHECK (
  (license_ver IS NULL
     AND online_stt IS NOT NULL AND offline_stt IS NOT NULL AND grace_period_days IS NOT NULL
     AND solution IS NULL AND channels_total IS NULL AND channels_json IS NULL)
  OR
  (license_ver = 1
     AND online_stt IS NULL AND offline_stt IS NULL AND grace_period_days IS NULL
     AND solution IS NOT NULL AND channels_total IS NOT NULL AND channels_json IS NOT NULL)
);

-- 불변 트리거: 새 계약 열도 변경 금지 대상에 넣는다(상태 전이 규칙은 V1 과 동일).
CREATE OR REPLACE FUNCTION license_guard() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  IF (NEW.license_id, NEW.submission_token, NEW.project_name, NEW.license_type, NEW.site_id,
      NEW.online_stt, NEW.offline_stt, NEW.issued_at, NEW.not_before, NEW.not_after,
      NEW.grace_period_days, NEW.warning_notice, NEW.public_key_fp, NEW.renewed_from,
      NEW.issued_by, NEW.created_at,
      NEW.license_ver, NEW.solution, NEW.channels_total, NEW.channels_json)
     IS DISTINCT FROM
     (OLD.license_id, OLD.submission_token, OLD.project_name, OLD.license_type, OLD.site_id,
      OLD.online_stt, OLD.offline_stt, OLD.issued_at, OLD.not_before, OLD.not_after,
      OLD.grace_period_days, OLD.warning_notice, OLD.public_key_fp, OLD.renewed_from,
      OLD.issued_by, OLD.created_at,
      OLD.license_ver, OLD.solution, OLD.channels_total, OLD.channels_json) THEN
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
