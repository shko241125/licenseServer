#!/bin/sh
# PostgreSQL 최초 초기화 때 한 번 실행된다(/docker-entrypoint-initdb.d).
# 마이그레이션 계정(license_owner)과 앱 계정(license_app)을 분리한다. 앱 계정 권한은 Flyway V1 이 최소로 부여한다.
set -eu
OWNER_PW=$(cat /run/secrets/db_owner_password)
APP_PW=$(cat /run/secrets/db_app_password)
# psql 변수(:'var')로 넘겨 비밀번호 안의 따옴표 등이 SQL 로 해석되지 않게 한다
psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" \
     -v owner_pw="$OWNER_PW" -v app_pw="$APP_PW" <<'SQL'
CREATE ROLE license_owner LOGIN PASSWORD :'owner_pw';
CREATE ROLE license_app LOGIN PASSWORD :'app_pw';
ALTER SCHEMA public OWNER TO license_owner;
REVOKE ALL ON SCHEMA public FROM PUBLIC;
REVOKE ALL ON DATABASE license FROM PUBLIC;
GRANT CONNECT ON DATABASE license TO license_owner, license_app;
SQL
