-- license_ver 1 단일 형식으로 정리한다(2026-10-13, D42). SDK 적용 전이라 이전 버전 호환을 두지 않는다.
-- license_ver 가 없는 행이 있으면 적용을 멈춘다. 발급 기록은 지우거나 바꾸지 않는다(D22).
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM license WHERE license_ver IS NULL) THEN
    RAISE EXCEPTION 'V5: license rows without license_ver exist; resolve them before upgrading';
  END IF;
END $$;

-- 불변 트리거: 삭제할 열을 비교 대상에서 뺀다. 함수 본문은 열 의존성이 추적되지 않으므로 열 삭제보다 먼저 바꾼다.
CREATE OR REPLACE FUNCTION license_guard() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  IF (NEW.license_id, NEW.submission_token, NEW.project_name, NEW.license_type, NEW.site_id,
      NEW.issued_at, NEW.not_before, NEW.not_after, NEW.warning_notice, NEW.public_key_fp, NEW.renewed_from,
      NEW.issued_by, NEW.created_at,
      NEW.license_ver, NEW.solution, NEW.channels_total, NEW.channels_json)
     IS DISTINCT FROM
     (OLD.license_id, OLD.submission_token, OLD.project_name, OLD.license_type, OLD.site_id,
      OLD.issued_at, OLD.not_before, OLD.not_after, OLD.warning_notice, OLD.public_key_fp, OLD.renewed_from,
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

ALTER TABLE license DROP CONSTRAINT license_format_shape;
-- 옛 열에 걸린 CHECK(범위, online_stt + offline_stt > 0)는 열과 함께 삭제된다.
ALTER TABLE license
  DROP COLUMN online_stt,
  DROP COLUMN offline_stt,
  DROP COLUMN grace_period_days;
ALTER TABLE license
  ALTER COLUMN license_ver SET NOT NULL,
  ALTER COLUMN solution SET NOT NULL,
  ALTER COLUMN channels_total SET NOT NULL,
  ALTER COLUMN channels_json SET NOT NULL;
