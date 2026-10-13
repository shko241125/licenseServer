-- license_type 을 production / trial / dev 로 한정한다(2026-10-13, D40).
-- 허용 밖의 값을 가진 행이 있으면 적용을 멈춘다. 발급 기록은 바꾸거나 지우지 않는다(불변 트리거, D22).
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM license WHERE license_type NOT IN ('production', 'trial', 'dev')) THEN
    RAISE EXCEPTION 'V4: license rows with license_type other than production/trial/dev exist; resolve them before upgrading';
  END IF;
END $$;

ALTER TABLE license DROP CONSTRAINT license_license_type_check;
ALTER TABLE license ADD CONSTRAINT license_type_check
  CHECK (license_type IN ('production', 'trial', 'dev'));
