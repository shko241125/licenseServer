package com.personaai.license.server.web;

import java.time.Duration;
import java.time.Instant;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;

import org.springframework.stereotype.Component;

import com.personaai.license.server.config.LicenseProperties;

/** 템플릿에서 ${@fmt.kst(t)} / ${@fmt.utc(t)} 로 시각을 표시한다. */
@Component("fmt")
public class ViewFormat {
    private static final DateTimeFormatter F = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss");
    private final LicenseProperties props;

    public ViewFormat(LicenseProperties props) {
        this.props = props;
    }

    public String kst(Instant t) {
        return t == null ? "" : F.format(t.atZone(props.zone())) + " KST";
    }

    public String utc(Instant t) {
        return t == null ? "" : F.format(t.atZone(ZoneOffset.UTC)) + "Z";
    }

    /** 운영 라이선스의 영구 표기(9999-12-31T23:59:59Z)인지. */
    public boolean permanent(Instant t) {
        return com.personaai.license.server.license.Contract.PERMANENT.equals(t);
    }

    /** 종료 시각 표시: 영구면 "영구", 아니면 KST. */
    public String end(Instant t) {
        return permanent(t) ? "영구" : kst(t);
    }

    /** now 부터 t 까지 남은 일수(내림). 이미 지났으면 음수. */
    public long daysUntil(Instant now, Instant t) {
        return Duration.between(now, t).toDays();
    }
}
