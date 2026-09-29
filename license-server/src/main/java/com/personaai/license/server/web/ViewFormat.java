package com.personaai.license.server.web;

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
}
