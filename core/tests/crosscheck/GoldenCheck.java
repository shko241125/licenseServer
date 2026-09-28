// 골든 벡터를 C++ 코어와 독립된 구현(JDK 표준 Ed25519, Java 15+)으로 검증한다.
// 사용: java GoldenCheck.java <testdata_dir>
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.KeyFactory;
import java.security.Signature;
import java.security.spec.X509EncodedKeySpec;
import java.util.Base64;
import java.util.HexFormat;

public class GoldenCheck {
  public static void main(String[] args) throws Exception {
    Path dir = Path.of(args[0]);
    byte[] raw = Base64.getDecoder().decode(Files.readString(dir.resolve("golden_public.key")).trim());
    // Ed25519 SubjectPublicKeyInfo = 고정 헤더 12바이트 + 원시 공개키 32바이트 (RFC 8410)
    byte[] spki = HexFormat.of().parseHex("302a300506032b6570032100" + HexFormat.of().formatHex(raw));
    var pub = KeyFactory.getInstance("Ed25519").generatePublic(new X509EncodedKeySpec(spki));

    String lic = Files.readString(dir.resolve("golden_license.lic"));
    var m = java.util.regex.Pattern.compile("\"signature\": \"([A-Za-z0-9+/=]+)\"").matcher(lic);
    if (!m.find()) throw new AssertionError("signature not found");
    byte[] sig = Base64.getDecoder().decode(m.group(1));
    byte[] canonical = Files.readAllBytes(dir.resolve("golden_canonical.json"));

    Signature v = Signature.getInstance("Ed25519");
    v.initVerify(pub);
    v.update(canonical);
    if (!v.verify(sig)) throw new AssertionError("JDK Ed25519 verification FAILED");
    canonical[canonical.length - 3] ^= 1;
    v.initVerify(pub);
    v.update(canonical);
    if (v.verify(sig)) throw new AssertionError("tampered canonical bytes verified");
    System.out.println("JDK Ed25519: golden signature OK, tamper rejected");
  }
}
