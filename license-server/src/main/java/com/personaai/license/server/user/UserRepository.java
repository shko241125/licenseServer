package com.personaai.license.server.user;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.time.Instant;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.util.List;
import java.util.Optional;

import org.springframework.jdbc.core.simple.JdbcClient;
import org.springframework.stereotype.Repository;

@Repository
public class UserRepository {
    private final JdbcClient jdbc;

    public UserRepository(JdbcClient jdbc) {
        this.jdbc = jdbc;
    }

    static AppUser map(ResultSet rs, int i) throws SQLException {
        OffsetDateTime locked = rs.getObject("locked_until", OffsetDateTime.class);
        OffsetDateTime expires = rs.getObject("password_expires_at", OffsetDateTime.class);
        return new AppUser(rs.getLong("id"), rs.getString("username"), rs.getString("password_hash"),
                AppUser.Role.valueOf(rs.getString("role")), rs.getString("totp_secret_enc"),
                rs.getLong("totp_last_step"), rs.getBoolean("enabled"), rs.getBoolean("must_change_password"),
                rs.getInt("failed_count"), locked == null ? null : locked.toInstant(),
                rs.getObject("created_at", OffsetDateTime.class).toInstant(),
                expires == null ? null : expires.toInstant());
    }

    public Optional<AppUser> findByUsername(String username) {
        return jdbc.sql("SELECT * FROM app_user WHERE username = ?").param(username).query(UserRepository::map).optional();
    }

    public Optional<AppUser> findById(long id) {
        return jdbc.sql("SELECT * FROM app_user WHERE id = ?").param(id).query(UserRepository::map).optional();
    }

    public List<AppUser> findAll() {
        return jdbc.sql("SELECT * FROM app_user ORDER BY username").query(UserRepository::map).list();
    }

    public long count() {
        return jdbc.sql("SELECT count(*) FROM app_user").query(Long.class).single();
    }

    public long countEnabledAdmins() {
        return jdbc.sql("SELECT count(*) FROM app_user WHERE role = 'ADMIN' AND enabled").query(Long.class).single();
    }

    public long insert(String username, String passwordHash, AppUser.Role role) {
        return jdbc.sql("""
                INSERT INTO app_user (username, password_hash, role, must_change_password)
                VALUES (?, ?, ?, TRUE) RETURNING id""")
                .params(username, passwordHash, role.name()).query(Long.class).single();
    }

    /** expiresAt: 임시 비밀번호 만료 시각(mustChange=true 일 때만 의미, null 이면 만료 없음). */
    public void updatePassword(long id, String passwordHash, boolean mustChange, Instant expiresAt) {
        jdbc.sql("""
                UPDATE app_user SET password_hash = ?, must_change_password = ?, password_expires_at = ?,
                  failed_count = 0, locked_until = NULL WHERE id = ?""")
                .params(passwordHash, mustChange,
                        mustChange && expiresAt != null ? OffsetDateTime.ofInstant(expiresAt, ZoneOffset.UTC) : null, id)
                .update();
        jdbc.sql("INSERT INTO password_history (user_id, password_hash) VALUES (?, ?)").params(id, passwordHash).update();
    }

    public List<String> recentPasswordHashes(long id, int n) {
        return jdbc.sql("SELECT password_hash FROM password_history WHERE user_id = ? ORDER BY created_at DESC, id DESC LIMIT ?")
                .params(id, n).query(String.class).list();
    }

    /** 실패 1회 기록. 한도에 도달하면 잠그고 카운터를 0 으로. 잠금 여부 반환. */
    public boolean recordFailure(String username, int maxFailures, Instant lockUntil) {
        Boolean locked = jdbc.sql("""
                UPDATE app_user SET
                  failed_count = CASE WHEN failed_count + 1 >= ? THEN 0 ELSE failed_count + 1 END,
                  locked_until = CASE WHEN failed_count + 1 >= ? THEN ? ELSE locked_until END
                WHERE username = ? RETURNING failed_count = 0""")
                .params(maxFailures, maxFailures, OffsetDateTime.ofInstant(lockUntil, ZoneOffset.UTC), username)
                .query(Boolean.class).optional().orElse(false);
        return locked;
    }

    public void resetFailures(long id) {
        jdbc.sql("UPDATE app_user SET failed_count = 0, locked_until = NULL WHERE id = ?").param(id).update();
    }

    public void setEnabled(long id, boolean enabled) {
        jdbc.sql("UPDATE app_user SET enabled = ? WHERE id = ?").params(enabled, id).update();
    }

    public void setTotp(long id, String secretEnc, long lastStep) {
        jdbc.sql("UPDATE app_user SET totp_secret_enc = ?, totp_last_step = ? WHERE id = ?")
                .params(secretEnc, lastStep, id).update();
    }

    /** step 이 이전에 쓴 step 보다 클 때만 갱신 → 같은 코드 재사용(재전송) 차단, 동시 요청에도 1회만 성공. */
    public boolean consumeTotpStep(long id, long step) {
        return jdbc.sql("UPDATE app_user SET totp_last_step = ? WHERE id = ? AND totp_last_step < ?")
                .params(step, id, step).update() == 1;
    }
}
