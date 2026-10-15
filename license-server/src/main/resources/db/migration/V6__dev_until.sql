-- 개발(dev)의 총 개발기한을 발급 기록에 둔다(2026-10-14, D43). 서명 대상이 아니라 서버 기록 전용이다.
-- 1회 발급 기간은 1년 이내(코어 SignContract)이고, 총 개발기한 안에서 갱신 발급한다.
-- 이 마이그레이션 전에 발급된 개발 라이선스는 값이 없다(NULL). 새 발급의 필수 입력은 앱이 검사한다.
ALTER TABLE license ADD COLUMN dev_until TIMESTAMPTZ;
ALTER TABLE license ADD CONSTRAINT license_dev_until_check
  CHECK (dev_until IS NULL OR (license_type = 'dev' AND not_after <= dev_until));

-- 불변 트리거: 비교 대상에 dev_until 을 더한다(상태 전이 규칙은 V5 와 같음).
CREATE OR REPLACE FUNCTION license_guard() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  IF (NEW.license_id, NEW.submission_token, NEW.project_name, NEW.license_type, NEW.site_id,
      NEW.issued_at, NEW.not_before, NEW.not_after, NEW.warning_notice, NEW.public_key_fp, NEW.renewed_from,
      NEW.issued_by, NEW.created_at,
      NEW.license_ver, NEW.solution, NEW.channels_total, NEW.channels_json, NEW.dev_until)
     IS DISTINCT FROM
     (OLD.license_id, OLD.submission_token, OLD.project_name, OLD.license_type, OLD.site_id,
      OLD.issued_at, OLD.not_before, OLD.not_after, OLD.warning_notice, OLD.public_key_fp, OLD.renewed_from,
      OLD.issued_by, OLD.created_at,
      OLD.license_ver, OLD.solution, OLD.channels_total, OLD.channels_json, OLD.dev_until) THEN
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
