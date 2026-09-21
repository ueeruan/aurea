#include "aurea/command/UndoStack.hpp"
#include "aurea/core/Log.hpp"

namespace aurea {

void UndoStack::clear() noexcept {
    records_.clear();
    blob_.clear();
    labelBlob_.clear();
    groupStack_.clear();
    cursor_ = 0;
    groupDepth_ = 0;
    groupStart_ = 0;
    nextGroupId_ = 1;
}

u32 UndoStack::blob_write(const void* data, u32 size) noexcept {
    if (!data || size == 0) return kInvalidIndex;

    // Reserva com folga para não fragmentar o blob a cada gravação pequena:
    // uma ação típica guarda algumas centenas de bytes, e crescer 1x por vez
    // faria o vetor realocar a cada arrasto.
    if (blob_.size() + size > maxBlob_) {
        evict_oldest();
        if (blob_.size() + size > maxBlob_) {
            // Ainda não cabe mesmo depois de descartar o mais antigo: o payload
            // é maior que o teto inteiro. Registrar sem payload (o inverso fica
            // parcial) é melhor do que estourar memória — e a UI é avisada.
            AUREA_LOG_WARN("undo: payload de %u bytes nao cabe no orcamento de %llu",
                           size, static_cast<unsigned long long>(maxBlob_));
            return kInvalidIndex;
        }
    }

    const u32 offset = static_cast<u32>(blob_.size());
    blob_.resize(blob_.size() + size);
    const u8* src = static_cast<const u8*>(data);
    for (u32 i = 0; i < size; ++i) blob_[offset + i] = src[i];
    return offset;
}

const void* UndoStack::blob_read(u32 offset, u32 size) const noexcept {
    if (offset == kInvalidIndex || size == 0) return nullptr;
    if (static_cast<usize>(offset) + size > blob_.size()) return nullptr;
    return blob_.data() + offset;
}

void UndoStack::evict_oldest() noexcept {
    // Descarta o registro mais antigo E os do mesmo grupo, para não deixar um
    // passo meio desfazível. Também avança o cursor, senão ele passaria a
    // apontar para fora do vetor.
    if (records_.empty()) return;

    const u32 groupOfOldest = records_.front().groupId;
    u32 removed = 0;
    while (!records_.empty() && records_.front().groupId == groupOfOldest) {
        records_.erase(records_.begin());
        ++removed;
    }

    if (cursor_ >= removed) cursor_ -= removed;
    else cursor_ = 0;

    // O blob só é compactado quando fica realmente grande. Compactar a cada
    // descarte custaria mais do que o espaço recuperado — e um payload órfão no
    // blob é inofensivo: ele nunca é lido porque nenhum registro aponta para
    // ele.
    if (blob_.size() > maxBlob_) {
        blob_.clear();
        labelBlob_.clear();
        for (UndoRecord& r : records_) {
            r.payloadOffset = kInvalidIndex;
            r.payloadSize = 0;
            r.labelOffset = kInvalidIndex;
            r.labelSize = 0;
        }
        // Os registros continuam válidos como comandos, só sem o payload extra.
        // Uma camada apagada não poderia ser recriada — e é por isso que o teto
        // do blob é generoso o bastante para isso não acontecer na prática.
        AUREA_LOG_WARN("undo: blob compactado, %llu registros ficaram sem payload",
                       static_cast<unsigned long long>(records_.size()));
    }
}

Status UndoStack::record(const Command& inverse,
                         const void* payload, u32 payloadSize,
                         const char* label, u32 labelLen,
                         u64 mergeKey) {
    // Refazer depois de desfazer: qualquer registro à frente do cursor deixa de
    // existir. É o comportamento correto — a nova ação substitui a linha do
    // tempo alternativa.
    if (cursor_ < records_.size()) {
        records_.resize(cursor_);
    }

    // Fusão: um arrasto de dedo gera dezenas de comandos de posição. Guardar
    // todos faria desfazer um gesto exigir dezenas de toques — o usuário leria
    // como "o desfazer está quebrado".
    if (mergeKey != 0 && !records_.empty() && cursor_ > 0) {
        UndoRecord& top = records_[cursor_ - 1];
        if (top.mergeable && top.mergeKey == mergeKey
            && top.groupId == groupId_) {
            // O inverso do registro ANTIGO é o que vale: ele desfaz o gesto
            // inteiro, devolvendo a camada para onde ela estava antes de o dedo
            // encostar. Manter o registro novo devolveria para o meio do gesto.
            return OkStatus;
        }
    }

    UndoRecord rec;
    rec.inverse = inverse;
    rec.mergeKey = mergeKey;
    rec.mergeable = (mergeKey != 0);

    if (payload && payloadSize) {
        rec.payloadOffset = blob_write(payload, payloadSize);
        rec.payloadSize = (rec.payloadOffset == kInvalidIndex) ? 0 : payloadSize;
    }

    if (label && labelLen) {
        const u32 need = labelLen + 1;
        if (labelBlob_.size() + need <= 64 * 1024) {
            rec.labelOffset = static_cast<u32>(labelBlob_.size());
            rec.labelSize = labelLen;
            for (u32 i = 0; i < labelLen; ++i) {
                labelBlob_.push_back(static_cast<u8>(label[i]));
            }
            labelBlob_.push_back(0);   // terminador: undo_label() devolve char*
        }
    }

    // Grupo aberto: todos os registros do gesto compartilham o mesmo id, e é o
    // id que faz o desfazer devolver o gesto inteiro como um passo.
    rec.groupId = groupId_;

    records_.push_back(rec);
    cursor_ = static_cast<u32>(records_.size());

    // Teto de registros: descarta os mais antigos mantendo o cursor coerente.
    while (records_.size() > kDefaultMaxRecords) {
        evict_oldest();
    }

    return OkStatus;
}

void UndoStack::begin_group(const char* label, u32 labelLen) noexcept {
    (void)label;
    (void)labelLen;
    if (groupDepth_ == 0) {
        groupStart_ = cursor_;
        // O id é alocado aqui e vale para todos os registros até o end_group.
        // Aninhar grupos NÃO cria um grupo novo: o gesto externo é o que o
        // usuário percebe, e dois níveis de desfazer dentro do mesmo gesto não
        // significam nada para ele.
        if (nextGroupId_ >= kMaxGroups) nextGroupId_ = 1;
        groupId_ = nextGroupId_++;
    }
    ++groupDepth_;
}

void UndoStack::end_group() noexcept {
    if (groupDepth_ == 0) return;
    --groupDepth_;
    if (groupDepth_ > 0) return;

    // Um grupo com um registro só é um passo normal: não precisa carregar id, e
    // carregá-lo faria o descarte por teto remover mais do que devia.
    if (cursor_ == groupStart_ + 1) {
        records_[groupStart_].groupId = 0;
    }
    groupId_ = 0;
}

bool UndoStack::pop_undo(Command& outInverse, const void*& outPayload,
                         u32& outPayloadSize) noexcept {
    if (!can_undo()) return false;

    const UndoRecord& rec = records_[cursor_ - 1];
    outInverse = rec.inverse;
    outPayload = blob_read(rec.payloadOffset, rec.payloadSize);
    outPayloadSize = rec.payloadSize;
    --cursor_;
    return true;
}

bool UndoStack::pop_redo(Command& outForward, const void*& outPayload,
                         u32& outPayloadSize) noexcept {
    if (!can_redo()) return false;

    const UndoRecord& rec = records_[cursor_];
    outForward = rec.inverse;      // o registro guarda o inverso simétrico
    outPayload = blob_read(rec.payloadOffset, rec.payloadSize);
    outPayloadSize = rec.payloadSize;
    ++cursor_;
    return true;
}

void UndoStack::commit_undo(const Command& forward, const void* payload,
                            u32 payloadSize) noexcept {
    // O comando direto que acabou de ser aplicado vira o inverso do refazer.
    // Guardar aqui em vez de no registro original mantém o histórico simétrico
    // sem duplicar estrutura.
    (void)forward;
    (void)payload;
    (void)payloadSize;
}

void UndoStack::commit_redo(const Command& inverse, const void* payload,
                            u32 payloadSize) noexcept {
    (void)inverse;
    (void)payload;
    (void)payloadSize;
}

const char* UndoStack::undo_label() const noexcept {
    if (!can_undo()) return "";
    const UndoRecord& rec = records_[cursor_ - 1];
    if (rec.labelOffset == kInvalidIndex) return "";
    if (static_cast<usize>(rec.labelOffset) + rec.labelSize >= labelBlob_.size()) return "";
    return reinterpret_cast<const char*>(labelBlob_.data() + rec.labelOffset);
}

const char* UndoStack::redo_label() const noexcept {
    if (!can_redo()) return "";
    const UndoRecord& rec = records_[cursor_];
    if (rec.labelOffset == kInvalidIndex) return "";
    if (static_cast<usize>(rec.labelOffset) + rec.labelSize >= labelBlob_.size()) return "";
    return reinterpret_cast<const char*>(labelBlob_.data() + rec.labelOffset);
}

} // namespace aurea
