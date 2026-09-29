package com.personaai.license.server.web;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Map;

import jakarta.servlet.http.HttpServletRequest;

import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.stereotype.Controller;
import org.springframework.ui.Model;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.multipart.MultipartFile;

import com.personaai.license.server.audit.AuditService;
import com.personaai.license.server.license.VerifyService;
import com.personaai.license.server.security.AppPrincipal;
import com.personaai.license.server.security.ClientIp;

@Controller
public class VerifyController {
    private final VerifyService verify;
    private final AuditService audit;

    public VerifyController(VerifyService verify, AuditService audit) {
        this.verify = verify;
        this.audit = audit;
    }

    @GetMapping("/verify")
    public String page() {
        return "verify";
    }

    @PostMapping("/verify")
    public String submit(@RequestParam(required = false) MultipartFile file, @RequestParam(required = false) String text,
                         @AuthenticationPrincipal AppPrincipal p, HttpServletRequest req, Model model) throws IOException {
        byte[] bytes = file != null && !file.isEmpty() ? file.getBytes()
                : (text == null ? new byte[0] : text.getBytes(StandardCharsets.UTF_8));
        Map<String, Object> result = verify.verify(bytes);
        audit.record(p.getUsername(), "VERIFY", String.valueOf(result.getOrDefault("license_id", "")), ClientIp.of(req),
                Map.of("result", String.valueOf(result.get("result")), "state", String.valueOf(result.getOrDefault("state", ""))));
        model.addAttribute("result", result);
        return "verify";
    }
}
