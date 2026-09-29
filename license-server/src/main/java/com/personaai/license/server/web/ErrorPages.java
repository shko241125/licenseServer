package com.personaai.license.server.web;

import org.springframework.stereotype.Controller;
import org.springframework.web.bind.annotation.GetMapping;

@Controller
public class ErrorPages {
    @GetMapping("/error/403")
    public String forbidden(jakarta.servlet.http.HttpServletResponse res) {
        res.setStatus(403);
        return "error/403";
    }
}
