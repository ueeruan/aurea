# =============================================================================
#  Regras do R8 para o Aurea (Fase 8I).
#
#  O R8 só enxerga chamadas feitas em Kotlin/Java. O que o motor C++ chama por
#  JNI é invisível para ele e precisa ficar preso aqui — nome e assinatura.
#
#  Motor → Kotlin (engine/platform/android/aurea_jni.cpp, JNI_OnLoad):
#    FindClass("com/aurea/aurea/engine/AureaEngine")
#    GetStaticMethodID(..., "openContentFd", "(Ljava/lang/String;)I")
#    GetStaticMethodID(..., "decodeImage",   "(Ljava/lang/String;)[B")
#
#  Kotlin → motor: as funções `external` (Java_com_aurea_aurea_engine_
#  AureaEngine_native*). A regra padrão do Android já preserva métodos nativos
#  e o nome da classe; está repetida aqui para não depender dela.
#
#  Mudou um nome do lado do C++? Mude aqui junto — um nome errado só aparece
#  no aparelho, como NoSuchMethodError na abertura ou "falha ao abrir mídia".
# =============================================================================

-keep class com.aurea.aurea.engine.AureaEngine {
    native <methods>;
    public static int openContentFd(java.lang.String);
    public static byte[] decodeImage(java.lang.String);
}
-keepclasseswithmembernames,includedescriptorclasses class * {
    native <methods>;
}

# Plugins 0.3.4: o motor acha `PluginHost` por FindClass/GetStaticMethodID
# (aurea_jni.cpp, nativeInstallPluginHost). Sem a regra o R8 renomeia a classe
# e apaga os métodos (só a JNI os chama): no release a rede/área de
# transferência/seletor dos plugins ficava desligada sem aviso.
-keep class com.aurea.aurea.effects.PluginHost {
    public static java.lang.String[] fetch(java.lang.String, java.lang.String, java.lang.String[], java.lang.String, int, int);
    public static java.lang.String clipboardRead();
    public static boolean clipboardWrite(java.lang.String);
    public static java.lang.String[] pickFile(java.lang.String, java.lang.String[]);
}

# 0.4.1 login com Google (Credential Manager): o provedor do Play Services é
# carregado por reflexão; a regra recomendada pela documentação do androidx.
-if class androidx.credentials.CredentialManager
-keep class androidx.credentials.playservices.** {
  *;
}

# Os números de linha do stack trace de um crash de release continuam
# legíveis com o mapping.txt do build (guardar junto com o APK publicado).
-keepattributes SourceFile,LineNumberTable
-renamesourcefileattribute SourceFile
