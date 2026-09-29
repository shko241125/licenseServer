package com.personaai.license.server.web;

import java.util.Map;

import jakarta.servlet.http.HttpServletRequest;

import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.stereotype.Controller;
import org.springframework.ui.Model;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestParam;

import com.personaai.license.server.audit.AuditService;
import com.personaai.license.server.security.AppPrincipal;
import com.personaai.license.server.security.ClientIp;

@Controller
public class AuditController {
    private final AuditService audit;

    public AuditController(AuditService audit) {
        this.audit = audit;
    }

    @GetMapping("/audit")
    public String page(@RequestParam(required = false) String action, Model model) {
        if (action != null && !action.matches("[A-Z_]{1,40}")) action = null;
        model.addAttribute("items", audit.recent(300, action));
        model.addAttribute("action", action);
        return "audit";
    }

    @PostMapping("/audit/verify")
    public String verify(@AuthenticationPrincipal AppPrincipal p, HttpServletRequest req, Model model) {
        AuditService.VerifyResult r = audit.verifyChain();
        audit.record(p.getUsername(), "AUDIT_VERIFY", null, ClientIp.of(req),
                Map.of("ok", r.ok(), "checked", r.checked(), "last_hash", r.lastHash()));
        model.addAttribute("chain", r);
        return page(null, model);
    }
}
