-- 운영의 deploy/db-init 과 같은 역할 구성을 테스트용 고정 비밀번호로 만든다.
CREATE ROLE license_owner LOGIN PASSWORD 'owner-pw';
CREATE ROLE license_app LOGIN PASSWORD 'app-pw';
ALTER SCHEMA public OWNER TO license_owner;
REVOKE ALL ON SCHEMA public FROM PUBLIC;
