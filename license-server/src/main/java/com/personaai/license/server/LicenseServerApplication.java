package com.personaai.license.server;

import java.time.Clock;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.context.properties.ConfigurationPropertiesScan;
import org.springframework.context.annotation.Bean;
import org.springframework.scheduling.annotation.EnableScheduling;

import com.personaai.license.server.key.SealKeyCli;

@SpringBootApplication
@ConfigurationPropertiesScan
@EnableScheduling
public class LicenseServerApplication {

    public static void main(String[] args) throws Exception {
        if (args.length > 0 && "seal-key".equals(args[0])) {
            System.exit(SealKeyCli.run(args)); // 키 생성식용 CLI 모드 (웹 서버를 띄우지 않음)
        }
        SpringApplication.run(LicenseServerApplication.class, args);
    }

    @Bean
    Clock clock() {
        return Clock.systemUTC();
    }
}
