#include "TestFramework.hpp"
#include "aurea/export/ExportPacketLog.hpp"
#include "aurea/project/FileIO.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace aurea;
namespace plog = aurea::export_log;

namespace {
struct LogFile {
    std::string path;
    explicit LogFile(const char* name) : path(std::string("aurea_test_pktlog_") + name) { fileio::remove_file(path); }
    ~LogFile() { fileio::remove_file(path); }
};
plog::StreamInfo stream(u64 key = 77) {
    plog::StreamInfo info;
    info.codec = 0; info.width = 64; info.height = 36;
    info.rateNum = 30; info.rateDen = 1; info.timebaseNum = 1; info.timebaseDen = 30000;
    info.sessionKey = key;
    info.extradata = {1, 0x64, 0, 0x1f, 0xff, 0xe1, 0, 4, 9, 9, 9, 9};
    return info;
}
std::vector<u8> payload(u32 frame) {
    std::vector<u8> data(100 + frame * 7 % 50);
    for (usize i = 0; i < data.size(); ++i) data[i] = static_cast<u8>(frame * 31 + i);
    return data;
}
// GOP de 10 quadros: 0, 10, 20... são keyframes.
bool key(u32 frame) { return frame % 10 == 0; }
void write_frames(plog::Writer& writer, u32 from, u32 to) {
    for (u32 f = from; f < to; ++f) {
        const auto data = payload(f);
        AUREA_CHECK(writer.append(f, data.data(), data.size(), key(f)).ok());
    }
}
}

AUREA_TEST(ExportPacketLog, RoundTripKeepsEveryPacketAndTheEncoderParameters) {
    LogFile file("roundtrip");
    plog::Writer writer;
    AUREA_CHECK(writer.create(file.path, stream()).ok());
    write_frames(writer, 0, 35);
    AUREA_CHECK_EQ(writer.durable_frames(), 30u); // fsync antes do keyframe 30
    AUREA_CHECK(writer.close().ok());
    plog::ScanResult scan;
    AUREA_CHECK(plog::scan(file.path, scan).ok());
    AUREA_CHECK_EQ(scan.validFrames, 35u);
    AUREA_CHECK_EQ(scan.resumeFrame, 30u);
    AUREA_CHECK_EQ(scan.keyframes, 4u);
    AUREA_CHECK(scan.info.same_stream(stream()));
    plog::Reader reader;
    AUREA_CHECK(reader.open(file.path).ok());
    AUREA_CHECK(reader.info().extradata == stream().extradata);
    plog::Packet packet; bool end = false; u32 count = 0;
    while (reader.next(packet, end).ok() && !end) {
        AUREA_CHECK_EQ(packet.frame, count);
        AUREA_CHECK_EQ(packet.key, key(count));
        AUREA_CHECK(packet.data == payload(count));
        ++count;
    }
    AUREA_CHECK(end);
    AUREA_CHECK_EQ(count, 35u);
}

AUREA_TEST(ExportPacketLog, TruncatedTailResumesAtTheLastCompleteGop) {
    LogFile file("truncated");
    {
        plog::Writer writer;
        AUREA_CHECK(writer.create(file.path, stream()).ok());
        write_frames(writer, 0, 27);
        AUREA_CHECK(writer.close().ok());
    }
    // O app morreu no meio da escrita do quadro 26: o arquivo termina no meio dele.
    std::vector<u8> bytes;
    AUREA_CHECK(fileio::read_all(file.path, bytes, 1u << 20));
    bytes.resize(bytes.size() - 40);
    AUREA_CHECK(fileio::write_atomic(file.path, bytes.data(), bytes.size()).ok());
    plog::ScanResult scan;
    AUREA_CHECK(plog::scan(file.path, scan).ok());
    AUREA_CHECK_EQ(scan.validFrames, 26u);
    AUREA_CHECK_EQ(scan.resumeFrame, 20u);
    // Continua com uma sessão nova: o quadro 20 precisa ser keyframe.
    plog::Writer writer;
    AUREA_CHECK(writer.resume(file.path, stream(), scan).ok());
    AUREA_CHECK_EQ(writer.frames(), 20u);
    const auto notKey = payload(20);
    AUREA_CHECK_EQ(writer.append(20, notKey.data(), notKey.size(), false).code(), Errc::CorruptData);
    AUREA_CHECK(writer.resume(file.path, stream(), scan).ok());
    write_frames(writer, 20, 40);
    AUREA_CHECK(writer.close().ok());
    AUREA_CHECK(plog::scan(file.path, scan).ok());
    AUREA_CHECK_EQ(scan.validFrames, 40u);
    plog::Reader reader;
    AUREA_CHECK(reader.open(file.path).ok());
    plog::Packet packet; bool end = false; u32 count = 0;
    while (reader.next(packet, end).ok() && !end) { AUREA_CHECK(packet.data == payload(count)); ++count; }
    AUREA_CHECK_EQ(count, 40u);
}

AUREA_TEST(ExportPacketLog, CorruptPacketDiscardsItsGopAndEverythingAfter) {
    LogFile file("corrupt");
    {
        plog::Writer writer;
        AUREA_CHECK(writer.create(file.path, stream()).ok());
        write_frames(writer, 0, 30);
        AUREA_CHECK(writer.close().ok());
    }
    std::vector<u8> bytes;
    AUREA_CHECK(fileio::read_all(file.path, bytes, 1u << 20));
    // Um byte trocado no fim do arquivo (dentro do quadro 29... ou antes):
    // o pacote atingido e tudo depois dele saem; volta ao keyframe anterior.
    bytes[bytes.size() - 500] ^= 0x5a;
    AUREA_CHECK(fileio::write_atomic(file.path, bytes.data(), bytes.size()).ok());
    plog::ScanResult scan;
    AUREA_CHECK(plog::scan(file.path, scan).ok());
    AUREA_CHECK(scan.validFrames < 30u);
    AUREA_CHECK(scan.resumeFrame <= scan.validFrames);
    AUREA_CHECK_EQ(scan.resumeFrame % 10, 0u);
    plog::Reader reader;
    AUREA_CHECK(reader.open(file.path).ok());
    plog::Packet packet; bool end = false; Status last = OkStatus; u32 good = 0;
    for (;;) { last = reader.next(packet, end); if (!last.ok() || end) break; ++good; }
    AUREA_CHECK_EQ(last.code(), Errc::CorruptData); // a leitura nunca entrega o pacote ruim
    AUREA_CHECK_EQ(good, scan.validFrames);
}

AUREA_TEST(ExportPacketLog, AnotherExportOrEncoderNeverContinuesTheLog) {
    LogFile file("mismatch");
    {
        plog::Writer writer;
        AUREA_CHECK(writer.create(file.path, stream(1)).ok());
        write_frames(writer, 0, 25);
        AUREA_CHECK(writer.close().ok());
    }
    plog::ScanResult scan;
    AUREA_CHECK(plog::scan(file.path, scan).ok());
    plog::Writer writer;
    AUREA_CHECK_EQ(writer.resume(file.path, stream(2), scan).code(), Errc::UnsupportedFormat);
    auto otherSps = stream(1); otherSps.extradata.back() ^= 1;
    AUREA_CHECK_EQ(writer.resume(file.path, otherSps, scan).code(), Errc::UnsupportedFormat);
    // Nada foi cortado pelas tentativas recusadas.
    plog::ScanResult again;
    AUREA_CHECK(plog::scan(file.path, again).ok());
    AUREA_CHECK_EQ(again.validFrames, 25u);
}

AUREA_TEST(ExportPacketLog, LogWithoutACompleteGopRestartsFromZero) {
    LogFile file("short");
    {
        plog::Writer writer;
        AUREA_CHECK(writer.create(file.path, stream()).ok());
        write_frames(writer, 0, 6);
        AUREA_CHECK(writer.close().ok());
    }
    plog::ScanResult scan;
    AUREA_CHECK(plog::scan(file.path, scan).ok());
    AUREA_CHECK_EQ(scan.validFrames, 6u);
    AUREA_CHECK_EQ(scan.resumeFrame, 0u);
    plog::Writer writer;
    AUREA_CHECK_EQ(writer.resume(file.path, stream(), scan).code(), Errc::InvalidState);
    // Cabeçalho corrompido: não é um registro.
    std::vector<u8> bytes;
    AUREA_CHECK(fileio::read_all(file.path, bytes, 1u << 20));
    bytes[20] ^= 0xff;
    AUREA_CHECK(fileio::write_atomic(file.path, bytes.data(), bytes.size()).ok());
    AUREA_CHECK_EQ(plog::scan(file.path, scan).code(), Errc::CorruptData);
    AUREA_CHECK_EQ(plog::scan(file.path + ".nao-existe", scan).code(), Errc::NotFound);
}
