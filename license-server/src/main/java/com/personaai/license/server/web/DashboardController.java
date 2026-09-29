package com.personaai.license.server.web;

import java.time.Clock;
import java.time.Duration;

import org.springframework.stereotype.Controller;
import org.springframework.ui.Model;
import org.springframework.web.bind.annotation.GetMapping;

import com.personaai.license.server.key.KeyVault;
import com.personaai.license.server.license.LicenseRepository;

@Controller
public class DashboardController {
    private final KeyVault vault;
    private final LicenseRepository licenses;
    private final Clock clock;

    public DashboardController(KeyVault vault, LicenseRepository licenses, Clock clock) {
        this.vault = vault;
        this.licenses = licenses;
        this.clock = clock;
    }

    @GetMapping("/")
    public String dashboard(Model model) {
        var now = clock.instant();
        model.addAttribute("keyState", vault.state());
        model.addAttribute("keyError", vault.loadError());
        model.addAttribute("fingerprint", vault.publicKeyFingerprint());
        model.addAttribute("now", now);
        model.addAttribute("recent", licenses.search(null, null, 10, 0));
        model.addAttribute("expiring", licenses.expiringBetween(now, now.plus(Duration.ofDays(30))));
        return "dashboard";
    }
}
