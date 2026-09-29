-- 임시 비밀번호 만료 (계획서 §13). 임시 비밀번호를 설정할 때 now + TTL, 본인이 변경하면 NULL.
-- 초기 관리자(부트스트랩)와 이 마이그레이션 이전에 만든 임시 비밀번호는 NULL(만료 없음).
-- 앱 계정은 app_user 에 테이블 단위 UPDATE 권한이 있어 새 열도 갱신할 수 있다.
ALTER TABLE app_user ADD COLUMN password_expires_at TIMESTAMPTZ;
ALTER TABLE app_user ADD CONSTRAINT app_user_expiry_only_for_temp
  CHECK (password_expires_at IS NULL OR must_change_password);
