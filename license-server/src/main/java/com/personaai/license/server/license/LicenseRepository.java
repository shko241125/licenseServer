package com.personaai.license.server.license;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.time.Instant;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.util.List;
import java.util.Optional;
import java.util.UUID;

import org.springframework.jdbc.core.simple.JdbcClient;
import org.springframework.stereotype.Repository;

@Repository
public class LicenseRepository {
    // 텍스트 블록은 줄 끝 공백을 지우므로 뒤에 이어 붙일 SQL 조각은 일반 문자열로 둔다.
    private static final String SELECT =
            "SELECT l.*, u.username AS issued_by_name FROM license l JOIN app_user u ON u.id = l.issued_by ";

    private final JdbcClient jdbc;

    public LicenseRepository(JdbcClient jdbc) {
        this.jdbc = jdbc;
    }

    private static Instant ts(ResultSet rs, String col) throws SQLException {
        OffsetDateTime t = rs.getObject(col, OffsetDateTime.class);
        return t == null ? null : t.toInstant();
    }

    static OffsetDateTime odt(Instant i) {
        return i == null ? null : OffsetDateTime.ofInstant(i, ZoneOffset.UTC);
    }

    static LicenseRecord map(ResultSet rs, int i) throws SQLException {
        return new LicenseRecord(rs.getLong("id"), rs.getString("license_id"),
                rs.getObject("submission_token", UUID.class), LicenseRecord.Status.valueOf(rs.getString("status")),
                rs.getString("project_name"), rs.getString("license_type"), rs.getString("site_id"),
                rs.getInt("online_stt"), rs.getInt("offline_stt"), ts(rs, "issued_at"), ts(rs, "not_before"),
                ts(rs, "not_after"), rs.getInt("grace_period_days"), rs.getString("warning_notice"),
                rs.getString("license_text"), rs.getString("license_sha256"), rs.getString("public_key_fp"),
                rs.getString("renewed_from"), rs.getLong("issued_by"), rs.getString("issued_by_name"),
                rs.getString("error"), rs.getString("void_reason"), ts(rs, "voided_at"), ts(rs, "created_at"));
    }

    /** 새 PENDING 행. 같은 제출 토큰이 이미 있으면 false(이중 제출). */
    public boolean insertPending(String licenseId, UUID token, Contract c, String publicKeyFp, long issuedBy) {
        return jdbc.sql("""
                INSERT INTO license (license_id, submission_token, status, project_name, license_type, site_id,
                  online_stt, offline_stt, issued_at, not_before, not_after, grace_period_days, warning_notice,
                  public_key_fp, renewed_from, issued_by)
                VALUES (?, ?, 'PENDING', ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT (submission_token) DO NOTHING""")
                .params(licenseId, token, c.projectName(), c.licenseType(), c.siteId(), c.onlineStt(),
                        c.offlineStt(), odt(c.issuedAt()), odt(c.notBefore()), odt(c.notAfter()),
                        c.gracePeriodDays(), c.warningNotice(), publicKeyFp, c.renewedFrom(), issuedBy)
                .update() == 1;
    }

    public void markIssued(String licenseId, String text, String sha256) {
        jdbc.sql("UPDATE license SET status = 'ISSUED', license_text = ?, license_sha256 = ? WHERE license_id = ? AND status = 'PENDING'")
                .params(text, sha256, licenseId).update();
    }

    public void markFailed(String licenseId, String error) {
        jdbc.sql("UPDATE license SET status = 'FAILED', error = ? WHERE license_id = ? AND status = 'PENDING'")
                .params(error, licenseId).update();
    }

    public boolean markVoid(String licenseId, String reason, long by, Instant at) {
        return jdbc.sql("""
                UPDATE license SET status = 'VOID', void_reason = ?, voided_by = ?, voided_at = ?
                WHERE license_id = ? AND status = 'ISSUED'""")
                .params(reason, by, odt(at), licenseId).update() == 1;
    }

    public Optional<LicenseRecord> findByLicenseId(String licenseId) {
        return jdbc.sql(SELECT + "WHERE l.license_id = ?").param(licenseId).query(LicenseRepository::map).optional();
    }

    public Optional<LicenseRecord> findByToken(UUID token) {
        return jdbc.sql(SELECT + "WHERE l.submission_token = ?").param(token).query(LicenseRepository::map).optional();
    }

    public List<LicenseRecord> search(String q, String status, int limit, int offset) {
        String like = q == null || q.isBlank() ? null : "%" + q.strip().replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_") + "%";
        return jdbc.sql(SELECT + """
                WHERE (CAST(:q AS TEXT) IS NULL OR l.project_name ILIKE :q OR l.site_id ILIKE :q OR l.license_id ILIKE :q)
                  AND (CAST(:status AS TEXT) IS NULL OR l.status = :status)
                ORDER BY l.created_at DESC, l.id DESC LIMIT :limit OFFSET :offset""")
                .param("q", like).param("status", status == null || status.isBlank() ? null : status)
                .param("limit", limit).param("offset", offset)
                .query(LicenseRepository::map).list();
    }

    public List<LicenseRecord> expiringBetween(Instant from, Instant to) {
        return jdbc.sql(SELECT + "WHERE l.status = 'ISSUED' AND l.not_after BETWEEN ? AND ? ORDER BY l.not_after")
                .params(odt(from), odt(to)).query(LicenseRepository::map).list();
    }

    public List<String> distinctValues(String column) {
        if (!column.equals("project_name") && !column.equals("site_id")) throw new IllegalArgumentException(column);
        return jdbc.sql("SELECT DISTINCT " + column + " FROM license ORDER BY 1 LIMIT 500").query(String.class).list();
    }
}
