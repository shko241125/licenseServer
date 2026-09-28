package licenseharness;

/** 실제 SonaSttAPI 와 같은 호출 모델의 모의 SDK. 라이선스 코어와의 결합 지점만 실제 코드다. */
public class SttHarness {
  static {
    System.loadLibrary("stt_harness_jni");
  }

  /** 0 = 성공, 1000+ = 라이선스 오류(stt::license::Error 순번), 그 외 = SDK 오류. */
  public native int connect(String licensePath, int sonasttSessions, int whisperSessions);

  /** {"result":0,"session_id":"..."} 또는 {"result":code,"error":"...","message":"..."} */
  public native String getIdleSession(String engine);

  /** 작업 종료(onResult/onError/abortTask) → 세션·채널 반환. */
  public native int finishTask(String sessionId);

  public native String getSTTSessionInfo();

  public native int reloadLicense();

  public native void disconnect();
}
