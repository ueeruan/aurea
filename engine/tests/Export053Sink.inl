// =============================================================================
//  0.5.3 (P0-5): o encoder que rearredonda o tempo de um quadro ou descarta um
//  quadro (controle de taxa de encoder de aparelho) não derruba mais a tentativa
//  inteira. Aqui com o sink FFmpeg real e o encoder de software do host, com o
//  defeito injetado no pacote (set_export_encoder_test_quirk). Incluído por
//  test_ffmpeg_export.cpp (só com FFmpeg).
// =============================================================================

/// Pacote com o tempo rearredondado (meia tolerância): é o mesmo quadro. O
/// arquivo sai com todos os quadros, na ordem, no relógio exato do export.
AUREA_TEST(FFmpegExport, RetimedEncoderPacketKeepsEveryPictureOnTheExactClock) {
    configure_export_rung_memory("", "");
    auto sink = make_ffmpeg_export_sink(nullptr); AUREA_CHECK(sink != nullptr); if (!sink) return;
    set_export_encoder_test_quirk(1, 5);
    const bool exported = exportNumbered(*sink, output("retimed-packet"), 30, 30, true);
    set_export_encoder_test_quirk(0, 0);
    AUREA_CHECK(exported);
}

/// O encoder descarta o 1o quadro (ainda no período de teste do degrau): o
/// próximo degrau recebe os quadros guardados e o arquivo sai inteiro.
AUREA_TEST(FFmpegExport, DroppedFirstPictureFallsBackToTheNextEncoderWithEveryPicture) {
    configure_export_rung_memory("", "");
    if (!avcodec_find_encoder_by_name("libopenh264") || !avcodec_find_encoder_by_name("libx264")) {
        std::printf("\n    sem libopenh264 e libx264 neste FFmpeg: escada de um degrau so\n");
        return;
    }
    auto sink = make_ffmpeg_export_sink(nullptr); AUREA_CHECK(sink != nullptr); if (!sink) return;
    set_export_encoder_test_quirk(2, 0);
    const bool exported = exportNumbered(*sink, output("dropped-first-packet"), 30, 30, true);
    set_export_encoder_test_quirk(0, 0);
    AUREA_CHECK(exported);
    AUREA_CHECK(std::strcmp(sink->encoder_info().name, "libopenh264") != 0);
}

/// O encoder descarta um quadro DEPOIS do período de teste, em taxa variável:
/// a sessão pede "refazer" (Busy — a mesma tentativa continua do último GOP do
/// checkpoint, sem descer a escada) e o aparelho aprende a taxa constante para
/// aquele encoder. Antes era EncodeFailed e a escada descia para o software.
AUREA_TEST(FFmpegExport, DroppedPictureAfterTheStartRedoesTheSameAttemptAndLearnsCbr) {
    configure_export_rung_memory("", "");
    auto sink = make_ffmpeg_export_sink(nullptr); AUREA_CHECK(sink != nullptr); if (!sink) return;
    const auto path = output("dropped-mid-video");
    auto video = configuration(30);
    video.rateMode = 1;   // VBR
    if (!good(sink->open(path.c_str(), video, nullptr))) return;
    const std::string encoder = sink->encoder_info().name;
    set_export_encoder_test_quirk(2, 10);
    Picture picture;
    Status failed = OkStatus;
    u32 failedAt = 0;
    for (u32 n = 0; n < 30 && failed.ok(); ++n) {
        picture.numbered(n);
        failed = sink->write_video(picture.y.data(), kStride, picture.uv.data(), kStride, std::llround(n * 1'000'000.0 / 30));
        failedAt = n;
    }
    set_export_encoder_test_quirk(0, 0);
    sink->abort();
    AUREA_CHECK_MSG(failed.code() == Errc::Busy, detailOf(failed).c_str());
    AUREA_CHECK(failedAt >= 10 && failedAt <= 12);
    AUREA_CHECK_EQ(export_rung_memory_count('C', ("cbr:" + encoder + "|avc").c_str(), 0), 1u);
    AUREA_CHECK(!std::filesystem::exists(path));
    configure_export_rung_memory("", "");
}
