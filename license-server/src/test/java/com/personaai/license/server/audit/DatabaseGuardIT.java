package com.personaai.license.server.audit;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.util.Map;
import java.util.UUID;

import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.dao.DataAccessException;
import org.springframework.jdbc.core.simple.JdbcClient;

import com.personaai.license.server.IntegrationTestBase;

/** DB 수준 방어: 앱 계정 권한, 라이선스 상태 전이 트리거, 감사 로그 해시 체인. */
class DatabaseGuardIT extends IntegrationTestBase {
    @Autowired JdbcClient jdbc;
    @Autowired AuditService audit;

    long adminId() {
        return jdbc.sql("SELECT id FROM app_user WHERE username = 'root.admin'").query(Long.class).single();
    }

    String insertIssued() {
        String id = UUID.randomUUID().toString().replace("-", "");
        jdbc.sql("""
                INSERT INTO license (license_id, submission_token, status, project_name, license_type, site_id,
                  online_stt, offline_stt, issued_at, not_before, not_after, grace_period_days, warning_notice,
                  public_key_fp, issued_by)
                VALUES (?, ?, 'PENDING', 'p', 'poc', 's', 0, 1, now(), now(), now() + interval '1 day', 0, 'w', ?, ?)""")
                .params(id, UUID.randomUUID(), "0".repeat(64), adminId()).update();
        jdbc.sql("UPDATE license SET status='ISSUED', license_text='x', license_sha256=? WHERE license_id=?")
                .params("1".repeat(64), id).update();
        return id;
    }

    static void assertDenied(org.assertj.core.api.ThrowableAssert.ThrowingCallable sql) {
        assertThatThrownBy(sql).isInstanceOf(DataAccessException.class)
                .rootCause().hasMessageContaining("permission denied");
    }

    @Test
    void appAccountCannotRewriteHistory() {
        audit.record("t", "TEST", null, null, Map.of());
        assertDenied(() -> jdbc.sql("UPDATE audit_event SET actor = 'x'").update());
        assertDenied(() -> jdbc.sql("DELETE FROM audit_event").update());
        assertDenied(() -> jdbc.sql("TRUNCATE audit_event").update());
        String id = insertIssued();
        assertDenied(() -> jdbc.sql("DELETE FROM license WHERE license_id = ?").param(id).update());
        assertDenied(() -> jdbc.sql("UPDATE license SET offline_stt = 99 WHERE license_id = ?").param(id).update()); // 열 단위 권한
    }

    @Test
    void triggerEnforcesStateMachineEvenForOwner() throws Exception {
        String id = insertIssued();
        try (var c = ownerConnection(); var st = c.createStatement()) {
            assertThatThrownBy(() -> st.executeUpdate("UPDATE license SET status='PENDING' WHERE license_id='" + id + "'"))
                    .hasMessageContaining("not allowed");
            assertThatThrownBy(() -> st.executeUpdate("UPDATE license SET license_text='forged' WHERE license_id='" + id + "'"))
                    .hasMessageContaining("not allowed");
            assertThatThrownBy(() -> st.executeUpdate("UPDATE license SET offline_stt=50 WHERE license_id='" + id + "'"))
                    .hasMessageContaining("immutable");
            assertThatThrownBy(() -> st.executeUpdate("UPDATE license SET status='VOID' WHERE license_id='" + id + "'"))
                    .hasMessageContaining("not allowed"); // 사유 없는 무효 처리
        }
        // 정상 무효 처리(앱 계정)
        int n = jdbc.sql("UPDATE license SET status='VOID', void_reason='r', voided_by=?, voided_at=now() WHERE license_id=?")
                .params(adminId(), id).update();
        assertThat(n).isEqualTo(1);
    }

    @Test
    void hashChainDetectsTampering() throws Exception {
        for (int i = 0; i < 5; i++) audit.record("u" + i, "TEST", "t" + i, "10.0.0." + i, Map.of("i", i, "k", "한글\"q"));
        assertThat(audit.verifyChain().ok()).isTrue();
        long target = jdbc.sql("SELECT max(seq) - 2 FROM audit_event").query(Long.class).single();
        try (var c = ownerConnection(); var st = c.createStatement()) {
            String original;
            try (var rs = st.executeQuery("SELECT detail_json FROM audit_event WHERE seq = " + target)) {
                rs.next();
                original = rs.getString(1);
            }
            st.executeUpdate("UPDATE audit_event SET detail_json = '{\"i\":99}' WHERE seq = " + target);
            var r = audit.verifyChain();
            assertThat(r.ok()).isFalse();
            assertThat(r.brokenAtSeq()).isEqualTo(target);
            assertThat(r.reason()).contains("hash");
            // 내용 대신 행을 지워 흔적을 없애는 경우도 링크가 끊겨 탐지된다
            st.execute("CREATE TEMP TABLE saved AS SELECT * FROM audit_event WHERE seq = " + target);
            st.executeUpdate("DELETE FROM audit_event WHERE seq = " + target);
            var r2 = audit.verifyChain();
            assertThat(r2.ok()).isFalse();
            assertThat(r2.reason()).contains("prev_hash");
            // 공유 DB 이므로 원상 복구 후 체인이 다시 온전한지 확인
            st.executeUpdate("INSERT INTO audit_event SELECT * FROM saved");
            st.executeUpdate("UPDATE audit_event SET detail_json = '" + original.replace("'", "''") + "' WHERE seq = " + target);
        }
        assertThat(audit.verifyChain().ok()).isTrue();
    }
}
