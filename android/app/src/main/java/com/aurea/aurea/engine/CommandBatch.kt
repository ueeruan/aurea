package com.aurea.aurea.engine

import java.nio.ByteBuffer

/**
 * Escrita de comandos na fila do motor.
 *
 * POR QUE OS OFFSETS ESTÃO AQUI E NÃO ESPALHADOS PELA UI:
 *
 * Cada comando tem o seu próprio arranjo de campos dentro do payload de 64
 * bytes, e escrever no offset errado não dá erro — dá um comando com campos
 * trocados. Uma posição X que vira opacidade não trava o app: ela move a
 * camada para o lugar errado e o usuário acha que o editor está maluco.
 *
 * Concentrar a escrita neste arquivo faz com que exista UM lugar onde os
 * offsets podem estar errados, e cada um deles tem um `static_assert` do lado
 * C++ (`cmd_layout` em command/Command.hpp) que quebra a compilação do motor se
 * os dois lados divergirem.
 *
 * REGRA DE USO: monte o lote com os métodos nomeados. Não escreva num offset
 * solto — se falta um método, é sinal de que falta um comando no motor.
 */
class CommandBatch(private val engine: AureaEngine) {

    /**
     * Monta um comando: reserva o slot, escreve o tipo, executa `fill` no
     * payload e fecha.
     *
     * Devolve `false` quando o lote encheu — a UI deve então reenviar o resto no
     * próximo frame. Perder o comando aqui seria perder uma ação do usuário.
     */
    private inline fun emit(type: Int, fill: (ByteBuffer) -> Unit): Boolean {
        val slot = engine.reserveCommandSlot() ?: return false
        slot.putShort(0, type.toShort())
        slot.putInt(4, 0)          // stringOffset — quem precisa chama emitString
        slot.putInt(8, 0)          // stringLength
        slot.putLong(80, 0)        // correlationId
        // Zera o payload: um comando que só preenche parte dos campos deixaria
        // lixo do comando anterior nos outros. O motor leria esse lixo como
        // valor válido.
        for (i in 0 until PodLayout.CMD_PAYLOAD_BYTES step 8) {
            slot.putLong(PodLayout.CMD_OFF_PAYLOAD + i, 0L)
        }
        fill(slot)
        engine.endCommand()
        return true
    }

    /** Igual a [emit], mas com uma string anexada ao blob do lote. */
    private inline fun emitString(type: Int, text: String, fill: (ByteBuffer) -> Unit): Boolean {
        val offset = engine.writeString(text)
        if (offset < 0) return false
        val bytes = text.toByteArray(Charsets.UTF_8)
        val slot = engine.reserveCommandSlot() ?: return false
        slot.putShort(0, type.toShort())
        slot.putInt(4, offset)
        slot.putInt(8, bytes.size)
        slot.putLong(80, 0)
        for (i in 0 until PodLayout.CMD_PAYLOAD_BYTES step 8) {
            slot.putLong(PodLayout.CMD_OFF_PAYLOAD + i, 0L)
        }
        fill(slot)
        engine.endCommand()
        return true
    }

    private fun ByteBuffer.putHandle(offset: Int, handle: Long) = putLong(offset, handle)

    // =========================================================================
    // Camadas
    // =========================================================================

    fun createLayer(kind: Int, name: String, correlationId: Long): Boolean =
        emitString(CommandType.LAYER_CREATE, name) { b ->
            b.putShort(Off.LAYER_CREATE_KIND, kind.toShort())
            b.putLong(80, correlationId)
        }

    fun deleteLayer(layer: Long) =
        emit(CommandType.LAYER_DELETE) { b -> b.putHandle(Off.LAYER, layer) }

    fun duplicateLayer(layer: Long) =
        emit(CommandType.LAYER_DUPLICATE) { b -> b.putHandle(Off.LAYER, layer) }

    fun reorderLayer(layer: Long, newIndex: Int) = emit(CommandType.LAYER_REORDER) { b ->
        b.putHandle(Off.LAYER, layer)
        b.putInt(Off.SECOND_U32, newIndex)
    }

    fun setLayerVisible(layer: Long, visible: Boolean) = emit(CommandType.LAYER_SET_VISIBLE) { b ->
        b.putHandle(Off.LAYER, layer)
        b.put(Off.SECOND_U32, if (visible) 1 else 0)
    }

    fun setLayerLocked(layer: Long, locked: Boolean) = emit(CommandType.LAYER_SET_LOCKED) { b ->
        b.putHandle(Off.LAYER, layer)
        b.put(Off.SECOND_U32, if (locked) 1 else 0)
    }

    fun setLayerName(layer: Long, name: String) =
        emitString(CommandType.LAYER_SET_NAME, name) { b -> b.putHandle(Off.LAYER, layer) }

    fun setLayerBlendMode(layer: Long, mode: Int) = emit(CommandType.LAYER_SET_BLEND_MODE) { b ->
        b.putHandle(Off.LAYER, layer)
        b.putShort(Off.SECOND_U32, mode.toShort())
    }

    fun setLayerParent(layer: Long, parent: Long) = emit(CommandType.LAYER_SET_PARENT) { b ->
        b.putHandle(Off.LAYER, layer)
        b.putHandle(Off.SECOND_U32, parent)
    }

    /**
     * Trim: move a borda de entrada e/ou saída. É o gesto de arrastar a ponta da
     * barra na timeline.
     */
    fun setLayerTimeRange(layer: Long, startFrame: Int, endFrame: Int, offsetFrames: Int? = null) =
        emit(CommandType.LAYER_SET_TIME_RANGE) { b ->
            b.putHandle(Off.LAYER, layer)
            b.putLong(Off.RANGE_START, startFrame.toLong())
            b.putLong(Off.RANGE_END, endFrame.toLong())
            // Trim do INÍCIO: o conteúdo fica parado e só a borda anda.
            if (offsetFrames != null) {
                b.putLong(Off.RANGE_OFFSET, offsetFrames.toLong())
                b.putInt(Off.RANGE_SET_OFFSET, 1)
            }
        }

    /** Divide a camada no playhead. */
    fun splitLayer(layer: Long, atFrame: Int) = emit(CommandType.LAYER_SPLIT) { b ->
        b.putHandle(Off.LAYER, layer)
        b.putLong(Off.SPLIT_AT, atFrame.toLong())
    }

    // =========================================================================
    // Transform
    // =========================================================================

    /**
     * Escreve posição, escala, rotação, âncora e opacidade de uma vez.
     *
     * UM comando, não treze. Arrastar uma camada com o dedo mexe em X e Y; se
     * cada eixo fosse um comando separado, o motor aplicaria um estado
     * intermediário em que a camada está no X novo e no Y velho — e durante um
     * arrasto diagonal isso aparece como tremor.
     */
    fun setTransform(
        layer: Long,
        x: Float, y: Float, z: Float,
        scaleX: Float, scaleY: Float, scaleZ: Float,
        rotX: Float, rotY: Float, rotZ: Float,
        anchorX: Float, anchorY: Float, anchorZ: Float,
        opacity: Float,
    ) = emit(CommandType.LAYER_SET_TRANSFORM) { b ->
        b.putHandle(Off.LAYER, layer)
        b.putFloat(Off.TRANSFORM_X, x)
        b.putFloat(Off.TRANSFORM_X + 4, y)
        b.putFloat(Off.TRANSFORM_X + 8, z)
        b.putFloat(Off.TRANSFORM_SCALE_X, scaleX)
        b.putFloat(Off.TRANSFORM_SCALE_X + 4, scaleY)
        b.putFloat(Off.TRANSFORM_SCALE_X + 8, scaleZ)
        b.putFloat(Off.TRANSFORM_ROT_X, rotX)
        b.putFloat(Off.TRANSFORM_ROT_X + 4, rotY)
        b.putFloat(Off.TRANSFORM_ROT_X + 8, rotZ)
        b.putFloat(Off.TRANSFORM_ANCHOR_X, anchorX)
        b.putFloat(Off.TRANSFORM_ANCHOR_X + 4, anchorY)
        b.putFloat(Off.TRANSFORM_ANCHOR_X + 8, anchorZ)
        b.putFloat(Off.TRANSFORM_OPACITY, opacity)
    }

    fun setPosition(layer: Long, x: Float, y: Float, z: Float) =
        emit(CommandType.LAYER_SET_POSITION) { b ->
            b.putHandle(Off.LAYER, layer)
            b.putFloat(Off.POSITION_X, x)
            b.putFloat(Off.POSITION_Y, y)
            b.putFloat(Off.POSITION_Z, z)
        }

    fun setScale(layer: Long, sx: Float, sy: Float, sz: Float) =
        emit(CommandType.LAYER_SET_SCALE) { b ->
            b.putHandle(Off.LAYER, layer)
            b.putFloat(Off.SCALE_X, sx)
            b.putFloat(Off.SCALE_Y, sy)
            b.putFloat(Off.SCALE_Z, sz)
        }

    fun setRotation(layer: Long, rx: Float, ry: Float, rz: Float) =
        emit(CommandType.LAYER_SET_ROTATION) { b ->
            b.putHandle(Off.LAYER, layer)
            b.putFloat(Off.ROTATION_X, rx)
            b.putFloat(Off.ROTATION_Y, ry)
            b.putFloat(Off.ROTATION_Z, rz)
        }

    fun setOpacity(layer: Long, value: Float) = emit(CommandType.LAYER_SET_OPACITY) { b ->
        b.putHandle(Off.LAYER, layer)
        b.putFloat(Off.OPACITY_VALUE, value)
    }

    fun setAnchor(layer: Long, x: Float, y: Float, z: Float) =
        emit(CommandType.LAYER_SET_ANCHOR) { b ->
            b.putHandle(Off.LAYER, layer)
            b.putFloat(Off.ANCHOR_X, x)
            b.putFloat(Off.ANCHOR_Y, y)
            b.putFloat(Off.ANCHOR_Z, z)
        }

    // =========================================================================
    // Keyframes
    // =========================================================================

    fun insertKeyframe(
        layer: Long, property: Int, effectIndex: Int, effectParam: Int,
        timeFrame: Int, value: Float,
    ) = emit(CommandType.KEYFRAME_INSERT) { b -> writeTrackRef(b, layer, property, effectIndex, effectParam); b.putLong(Off.KEYFRAME_TIME, timeFrame.toLong()); b.putFloat(Off.KEYFRAME_VALUE, value) }

    fun deleteKeyframe(
        layer: Long, property: Int, effectIndex: Int, effectParam: Int, timeFrame: Int,
    ) = emit(CommandType.KEYFRAME_DELETE) { b -> writeTrackRef(b, layer, property, effectIndex, effectParam); b.putLong(Off.KEYFRAME_TIME, timeFrame.toLong()) }

    fun moveKeyframe(
        layer: Long, property: Int, effectIndex: Int, effectParam: Int,
        fromFrame: Int, toFrame: Int,
    ) = emit(CommandType.KEYFRAME_MOVE) { b ->
        writeTrackRef(b, layer, property, effectIndex, effectParam)
        b.putLong(Off.KEYFRAME_TIME, fromFrame.toLong())
        b.putLong(Off.KEYFRAME_TIME + 8, toFrame.toLong())
    }

    fun setKeyframeValue(
        layer: Long, property: Int, effectIndex: Int, effectParam: Int,
        timeFrame: Int, value: Float,
    ) = emit(CommandType.KEYFRAME_SET_VALUE) { b -> writeTrackRef(b, layer, property, effectIndex, effectParam); b.putLong(Off.KEYFRAME_TIME, timeFrame.toLong()); b.putFloat(Off.KEYFRAME_VALUE, value) }

    /**
     * Muda a interpolação do keyframe. `interp` é o valor de `Interpolation` do
     * C++; os quatro floats são os control points do bezier (ignorados nos
     * outros modos).
     */
    fun setKeyframeInterpolation(
        layer: Long, property: Int, effectIndex: Int, effectParam: Int,
        timeFrame: Int, interp: Int,
        bx1: Float, by1: Float, bx2: Float, by2: Float,
    ) = emit(CommandType.KEYFRAME_SET_INTERPOLATION) { b ->
        writeTrackRef(b, layer, property, effectIndex, effectParam)
        b.putLong(Off.KEYFRAME_TIME, timeFrame.toLong())
        b.put(Off.KEYFRAME_TIME + 8, interp.toByte())
        // Os control points vêm logo depois do enum, alinhados em 4.
        b.putFloat(Off.KEYFRAME_TIME + 12, bx1)
        b.putFloat(Off.KEYFRAME_TIME + 16, by1)
        b.putFloat(Off.KEYFRAME_TIME + 20, bx2)
        b.putFloat(Off.KEYFRAME_TIME + 24, by2)
    }

    private fun writeTrackRef(b: ByteBuffer, layer: Long, property: Int, effectIndex: Int, effectParam: Int) {
        b.putLong(Off.KEYFRAME_TRACK, layer)
        b.putShort(Off.KEYFRAME_TRACK + 8, property.toShort())
        b.putInt(Off.KEYFRAME_TRACK + 12, effectIndex)
        b.putInt(Off.KEYFRAME_TRACK + 16, effectParam)
    }

    // =========================================================================
    // Reprodução
    // =========================================================================

    fun play() = emit(CommandType.PLAYBACK_PLAY) { }
    fun pause() = emit(CommandType.PLAYBACK_PAUSE) { }

    fun seek(timeNs: Long) = emit(CommandType.PLAYBACK_SEEK) { b ->
        b.putLong(Off.ABSOLUTE, timeNs)
    }

    fun setLoop(loop: Boolean) = emit(CommandType.PLAYBACK_SET_LOOP) { b ->
        b.put(Off.ABSOLUTE, if (loop) 1 else 0)
    }

    // =========================================================================
    // Visualização
    // =========================================================================

    fun setViewportZoom(zoom: Float) = emit(CommandType.VIEWPORT_SET_ZOOM) { b ->
        b.putFloat(Off.ABSOLUTE, zoom)
    }

    fun setViewportPan(x: Float, y: Float) = emit(CommandType.VIEWPORT_SET_PAN) { b ->
        b.putFloat(Off.ABSOLUTE, x)
        b.putFloat(Off.ABSOLUTE + 4, y)
    }

    /**
     * Escala do preview. `automatic` devolve o controle ao adaptativo; caso
     * contrário vale `numerator/denominator` (1/1, 1/2, 1/4, 1/8).
     */
    fun setPreviewScale(automatic: Boolean, numerator: Int = 1, denominator: Int = 1) =
        emit(CommandType.VIEWPORT_SET_PREVIEW_SCALE) { b ->
            b.putInt(Off.PREVIEW_NUMERATOR, numerator)
            b.putInt(Off.PREVIEW_DENOMINATOR, denominator)
            b.put(Off.PREVIEW_AUTOMATIC, if (automatic) 1 else 0)
        }

    // =========================================================================
    // Composição
    // =========================================================================

    fun setCompositionSize(comp: Long, width: Int, height: Int) =
        emit(CommandType.COMPOSITION_SET_SIZE) { b ->
            b.putHandle(Off.LAYER, comp)
            b.putInt(Off.COMP_WIDTH, width)
            b.putInt(Off.COMP_HEIGHT, height)
        }

    fun setCompositionFps(comp: Long, fps: Double) = emit(CommandType.COMPOSITION_SET_FPS) { b ->
        b.putHandle(Off.LAYER, comp)
        b.putDouble(Off.COMP_FPS, fps)
    }

    fun setCompositionDuration(comp: Long, frames: Int) = emit(CommandType.COMPOSITION_SET_DURATION) { b ->
        b.putHandle(Off.LAYER, comp)
        b.putLong(Off.COMP_DURATION, frames.toLong())
    }

    /** Fundo da composição em RGBA linear. */
    fun setCompositionBackground(comp: Long, r: Float, g: Float, b: Float, a: Float) =
        emit(CommandType.COMPOSITION_SET_BACKGROUND) { buf ->
            buf.putHandle(Off.LAYER, comp)
            buf.putFloat(Off.COMP_BACKGROUND, r)
            buf.putFloat(Off.COMP_BACKGROUND + 4, g)
            buf.putFloat(Off.COMP_BACKGROUND + 8, b)
            buf.putFloat(Off.COMP_BACKGROUND + 12, a)
        }

    fun setCurrentComposition(comp: Long) = emit(CommandType.PROJECT_SET_CURRENT_COMPOSITION) { b ->
        b.putHandle(Off.LAYER, comp)
    }

    // =========================================================================
    // Texto
    // =========================================================================

    fun setTextContent(layer: Long, text: String) =
        emitString(CommandType.TEXT_SET_CONTENT, text) { b -> b.putHandle(Off.LAYER, layer) }

    fun setTextSize(layer: Long, size: Float) = emit(CommandType.TEXT_SET_SIZE) { b ->
        b.putHandle(Off.LAYER, layer)
        b.putFloat(Off.TEXT_SIZE_VALUE, size)
    }

    fun setTextColor(layer: Long, r: Float, g: Float, b: Float, a: Float) =
        emit(CommandType.TEXT_SET_COLOR) { buf ->
            buf.putHandle(Off.LAYER, layer)
            buf.putFloat(Off.TEXT_COLOR_R, r)
            buf.putFloat(Off.TEXT_COLOR_G, g)
            buf.putFloat(Off.TEXT_COLOR_B, b)
            buf.putFloat(Off.TEXT_COLOR_A, a)
        }

    fun setTextAlignment(layer: Long, alignment: Int) = emit(CommandType.TEXT_SET_ALIGNMENT) { b ->
        b.putHandle(Off.LAYER, layer)
        b.putInt(Off.TEXT_ALIGNMENT, alignment)
    }

    // =========================================================================
    // Áudio
    // =========================================================================

    fun setAudioGain(layer: Long, gain: Float) = emit(CommandType.AUDIO_SET_GAIN) { b ->
        b.putHandle(Off.LAYER, layer)
        b.putFloat(Off.GAIN_VALUE, gain)
    }

    fun setAudioMuted(layer: Long, muted: Boolean) = emit(CommandType.AUDIO_SET_MUTED) { b ->
        b.putHandle(Off.LAYER, layer)
        b.put(Off.SECOND_U32, if (muted) 1 else 0)
    }

    fun setAudioSolo(layer: Long, solo: Boolean) = emit(CommandType.AUDIO_SET_SOLO) { b ->
        b.putHandle(Off.LAYER, layer)
        b.put(Off.SECOND_U32, if (solo) 1 else 0)
    }

    /** AudioFadePayload { LayerId; FrameIndex duration } (i64 em +8). */
    fun setAudioFadeIn(layer: Long, frames: Int) = emit(CommandType.AUDIO_SET_FADE_IN) { b ->
        b.putHandle(Off.LAYER, layer)
        b.putLong(Off.GAIN_VALUE, frames.toLong())
    }

    fun setAudioFadeOut(layer: Long, frames: Int) = emit(CommandType.AUDIO_SET_FADE_OUT) { b ->
        b.putHandle(Off.LAYER, layer)
        b.putLong(Off.GAIN_VALUE, frames.toLong())
    }

    /** Volume parado (linear, 1 = 100%). Com keyframes, use os de AUDIO_VOLUME. */
    fun setAudioVolume(layer: Long, volume: Float) = emit(CommandType.AUDIO_SET_VOLUME) { b ->
        b.putHandle(Off.LAYER, layer)
        b.putFloat(Off.GAIN_VALUE, volume)
    }

    fun setAudioPan(layer: Long, pan: Float) = emit(CommandType.AUDIO_SET_PAN) { b ->
        b.putHandle(Off.LAYER, layer)
        b.putFloat(Off.GAIN_VALUE, pan)
    }

    // =========================================================================
    // Histórico
    // =========================================================================

    /**
     * Abre um grupo de desfazer. Tudo que for emitido até [endUndoGroup] desfaz
     * junto — um gesto do dedo é UM passo, não sessenta.
     */
    fun beginUndoGroup(label: String) =
        emitString(CommandType.UNDO_BEGIN_GROUP, label) { }

    fun endUndoGroup() = emit(CommandType.UNDO_END_GROUP) { }

    fun undo() = emit(CommandType.UNDO) { }
    fun redo() = emit(CommandType.REDO) { }

    // =========================================================================
    // Efeitos
    //
    // `effectId` é o id LOCAL da layer (estável ao reordenar), o mesmo que
    // `LayerEffectRow.effectId` devolve. Vai no campo `index` do EffectId.
    // =========================================================================

    /** Adiciona no fim da pilha (ou em `index`). `typeId` vem do catálogo. */
    fun addEffect(layer: Long, typeId: Int, index: Int = -1) = emit(CommandType.EFFECT_ADD) { b ->
        b.putHandle(Off.LAYER, layer)
        b.putInt(Off.EFFECT_TYPE, typeId)
        b.putInt(Off.EFFECT_ADD_INDEX, index)   // -1 = 0xFFFFFFFF = no fim
    }

    fun removeEffect(layer: Long, effectId: Int) = emit(CommandType.EFFECT_REMOVE) { b ->
        b.putHandle(Off.LAYER, layer)
        b.putInt(Off.EFFECT_ID, effectId)
    }

    fun reorderEffect(layer: Long, effectId: Int, newIndex: Int) = emit(CommandType.EFFECT_REORDER) { b ->
        b.putHandle(Off.LAYER, layer)
        b.putInt(Off.EFFECT_ID, effectId)
        b.putInt(Off.EFFECT_PARAM_INDEX, newIndex)   // EffectReorderPayload.newIndex (+32)
    }

    fun setEffectEnabled(layer: Long, effectId: Int, enabled: Boolean) = emit(CommandType.EFFECT_SET_ENABLED) { b ->
        b.putHandle(Off.LAYER, layer)
        b.putInt(Off.EFFECT_ID, effectId)
        b.put(Off.EFFECT_PARAM_INDEX, if (enabled) 1 else 0)
    }

    fun setEffectParam(layer: Long, effectId: Int, param: Int, value: Float) = emit(CommandType.EFFECT_SET_PARAM) { b ->
        b.putHandle(Off.LAYER, layer)
        b.putInt(Off.EFFECT_ID, effectId)
        b.putInt(Off.EFFECT_PARAM_INDEX, param)
        b.putFloat(Off.EFFECT_VALUE, value)
    }

    /** Parâmetro de até 4 componentes: cor RGBA, ponto 2D/3D. */
    fun setEffectVector(layer: Long, effectId: Int, param: Int, v0: Float, v1: Float, v2: Float, v3: Float) =
        emit(CommandType.EFFECT_SET_COLOR_PARAM) { b ->
            b.putHandle(Off.LAYER, layer)
            b.putInt(Off.EFFECT_ID, effectId)
            b.putInt(Off.EFFECT_PARAM_INDEX, param)
            b.putFloat(Off.EFFECT_VALUE, v0)
            b.putFloat(Off.EFFECT_VALUE + 4, v1)
            b.putFloat(Off.EFFECT_VALUE + 8, v2)
            b.putFloat(Off.EFFECT_VALUE + 12, v3)
        }

    // =========================================================================
    // Reprodução: scrub e passo
    // =========================================================================

    fun togglePlayback() = emit(CommandType.PLAYBACK_TOGGLE) { }

    /** O dedo encostou na régua: o decode entra em modo scrub (coalescido). */
    fun scrubBegin() = emit(CommandType.PLAYBACK_SCRUB_BEGIN) { }

    fun scrub(timeNs: Long) = emit(CommandType.PLAYBACK_SCRUB) { b ->
        b.putLong(Off.ABSOLUTE, timeNs)
    }

    /** Soltou: o frame exato do ponto final é decodificado. */
    fun scrubEnd() = emit(CommandType.PLAYBACK_SCRUB_END) { }

    fun step(frames: Int) = emit(CommandType.PLAYBACK_STEP) { b ->
        b.putInt(Off.ABSOLUTE, frames)
    }

    fun setSpeed(speed: Float) = emit(CommandType.PLAYBACK_SET_SPEED) { b ->
        b.putFloat(Off.ABSOLUTE, speed)
    }

    /**
     * Offsets dentro do `Command`.
     *
     * Espelho de `aurea::cmd_layout` em command/Command.hpp. Cada número aqui
     * tem um `static_assert` do lado C++ — se um deles divergir, a compilação
     * do MOTOR falha, e não o app em runtime.
     */
    private object Off {
        /** LayerId / CompositionId / Scene3DId: sempre em +16. */
        const val LAYER = PodLayout.CMD_OFF_PAYLOAD

        /** O campo que vem logo depois do id, seja u32, u16 ou bool. */
        const val SECOND_U32 = PodLayout.CMD_OFF_PAYLOAD + 8

        /** Payloads que começam direto no offset 16 (sem id). */
        const val ABSOLUTE = PodLayout.CMD_OFF_PAYLOAD

        const val LAYER_CREATE_KIND = PodLayout.CMD_OFF_PAYLOAD + 8
        const val RANGE_START = PodLayout.CMD_OFF_PAYLOAD + 8
        const val RANGE_END = PodLayout.CMD_OFF_PAYLOAD + 16
        const val RANGE_OFFSET = PodLayout.CMD_OFF_PAYLOAD + 24
        const val RANGE_SET_OFFSET = PodLayout.CMD_OFF_PAYLOAD + 32
        const val SPLIT_AT = PodLayout.CMD_OFF_PAYLOAD + 8

        const val POSITION_X = PodLayout.CMD_OFF_PAYLOAD + 8
        const val POSITION_Y = PodLayout.CMD_OFF_PAYLOAD + 12
        const val POSITION_Z = PodLayout.CMD_OFF_PAYLOAD + 16
        const val SCALE_X = PodLayout.CMD_OFF_PAYLOAD + 8
        const val SCALE_Y = PodLayout.CMD_OFF_PAYLOAD + 12
        const val SCALE_Z = PodLayout.CMD_OFF_PAYLOAD + 16
        const val ROTATION_X = PodLayout.CMD_OFF_PAYLOAD + 8
        const val ROTATION_Y = PodLayout.CMD_OFF_PAYLOAD + 12
        const val ROTATION_Z = PodLayout.CMD_OFF_PAYLOAD + 16
        const val ANCHOR_X = PodLayout.CMD_OFF_PAYLOAD + 8
        const val ANCHOR_Y = PodLayout.CMD_OFF_PAYLOAD + 12
        const val ANCHOR_Z = PodLayout.CMD_OFF_PAYLOAD + 16
        const val OPACITY_VALUE = PodLayout.CMD_OFF_PAYLOAD + 8
        const val TRANSFORM_X = PodLayout.CMD_OFF_PAYLOAD + 8
        const val TRANSFORM_SCALE_X = PodLayout.CMD_OFF_PAYLOAD + 20
        const val TRANSFORM_ROT_X = PodLayout.CMD_OFF_PAYLOAD + 32
        const val TRANSFORM_ANCHOR_X = PodLayout.CMD_OFF_PAYLOAD + 44
        const val TRANSFORM_OPACITY = PodLayout.CMD_OFF_PAYLOAD + 56

        const val KEYFRAME_TRACK = PodLayout.CMD_OFF_PAYLOAD
        const val KEYFRAME_TIME = PodLayout.CMD_OFF_PAYLOAD + 24
        const val KEYFRAME_VALUE = PodLayout.CMD_OFF_PAYLOAD + 32

        const val GAIN_VALUE = PodLayout.CMD_OFF_PAYLOAD + 8
        const val TEXT_SIZE_VALUE = PodLayout.CMD_OFF_PAYLOAD + 8
        const val TEXT_COLOR_R = PodLayout.CMD_OFF_PAYLOAD + 8
        const val TEXT_COLOR_G = PodLayout.CMD_OFF_PAYLOAD + 12
        const val TEXT_COLOR_B = PodLayout.CMD_OFF_PAYLOAD + 16
        const val TEXT_COLOR_A = PodLayout.CMD_OFF_PAYLOAD + 20
        const val TEXT_ALIGNMENT = PodLayout.CMD_OFF_PAYLOAD + 8

        const val COMP_WIDTH = PodLayout.CMD_OFF_PAYLOAD + 8
        const val COMP_HEIGHT = PodLayout.CMD_OFF_PAYLOAD + 12
        const val COMP_FPS = PodLayout.CMD_OFF_PAYLOAD + 8
        /** CompDurationPayload { CompositionId; FrameIndex duration }. */
        const val COMP_DURATION = PodLayout.CMD_OFF_PAYLOAD + 8
        /** CompBackgroundPayload { CompositionId; f32 r, g, b, a }. */
        const val COMP_BACKGROUND = PodLayout.CMD_OFF_PAYLOAD + 8

        const val PREVIEW_NUMERATOR = PodLayout.CMD_OFF_PAYLOAD
        const val PREVIEW_DENOMINATOR = PodLayout.CMD_OFF_PAYLOAD + 4
        const val PREVIEW_AUTOMATIC = PodLayout.CMD_OFF_PAYLOAD + 8

        /** EffectAddPayload { LayerId; u32 effectType; u32 index }. */
        const val EFFECT_TYPE = PodLayout.CMD_OFF_PAYLOAD + 8
        const val EFFECT_ADD_INDEX = PodLayout.CMD_OFF_PAYLOAD + 12

        /** Effect*Payload { LayerId; EffectId{index, generation}; u32 param; f32 valor[...] }. */
        const val EFFECT_ID = PodLayout.CMD_OFF_PAYLOAD + 8
        const val EFFECT_PARAM_INDEX = PodLayout.CMD_OFF_PAYLOAD + 16
        const val EFFECT_VALUE = PodLayout.CMD_OFF_PAYLOAD + 20
    }
}

/**
 * Valores de `CommandType` do C++.
 *
 * ORDEM CONTRATO: estes números são os da enumeração em command/Command.hpp. A
 * enumeração só cresce no fim — reordenar mudaria o significado dos comandos
 * que a UI já emite.
 */
object CommandType {
    const val NOP = 0
    const val LAYER_CREATE = 1
    const val LAYER_DELETE = 2
    const val LAYER_DUPLICATE = 3
    const val LAYER_REORDER = 4
    const val LAYER_SET_KIND = 5
    const val LAYER_SET_NAME = 6
    const val LAYER_SET_TIME_RANGE = 7
    const val LAYER_SPLIT = 8
    const val LAYER_SET_VISIBLE = 9
    const val LAYER_SET_LOCKED = 10
    const val LAYER_SET_PARENT = 11
    const val LAYER_SET_BLEND_MODE = 12
    const val LAYER_SET_COMPOSITION = 13
    const val LAYER_SET_TRANSFORM = 14
    const val LAYER_SET_ANCHOR = 15
    const val LAYER_SET_OPACITY = 16
    const val LAYER_SET_SKEW = 17
    const val LAYER_SET_SCALE = 18
    const val LAYER_SET_ROTATION = 19
    const val LAYER_SET_POSITION = 20
    const val KEYFRAME_INSERT = 21
    const val KEYFRAME_DELETE = 22
    const val KEYFRAME_MOVE = 23
    const val KEYFRAME_SET_VALUE = 24
    const val KEYFRAME_SET_INTERPOLATION = 25
    const val KEYFRAME_SET_BEZIER = 26
    const val KEYFRAME_SET_EASING = 27
    const val MASK_CREATE = 28
    const val MASK_DELETE = 29
    const val MASK_SET_OPERATION = 30
    const val MASK_SET_FEATHER = 31
    const val MASK_SET_EXPANSION = 32
    const val MASK_SET_OPACITY = 33
    const val MASK_SET_PATH = 34
    const val MASK_SET_PATH_COMMIT = 35
    const val EFFECT_ADD = 36
    const val EFFECT_REMOVE = 37
    const val EFFECT_REORDER = 38
    const val EFFECT_SET_ENABLED = 39
    const val EFFECT_SET_PARAM = 40
    const val EFFECT_SET_COLOR_PARAM = 41
    const val AUDIO_SET_GAIN = 42
    const val AUDIO_SET_MUTED = 43
    const val AUDIO_SET_SOLO = 44
    const val AUDIO_SET_FADE_IN = 45
    const val AUDIO_SET_FADE_OUT = 46
    const val TEXT_SET_CONTENT = 47
    const val TEXT_SET_FONT = 48
    const val TEXT_SET_SIZE = 49
    const val TEXT_SET_COLOR = 50
    const val TEXT_SET_ALIGNMENT = 51
    const val TEXT_SET_STROKE_WIDTH = 52
    const val TEXT_SET_STROKE_COLOR = 53
    const val COMPOSITION_CREATE = 54
    const val COMPOSITION_DELETE = 55
    const val COMPOSITION_SET_SIZE = 56
    const val COMPOSITION_SET_FPS = 57
    const val COMPOSITION_SET_DURATION = 58
    const val COMPOSITION_SET_BACKGROUND = 59
    const val PROJECT_SET_CURRENT_COMPOSITION = 60
    const val SCENE_LOAD_MODEL = 61
    const val SCENE_SET_CAMERA = 62
    const val SCENE_ADD_LIGHT = 63
    const val SCENE_SET_LIGHT_PARAM = 64
    const val SCENE_SET_MODEL_TRANSFORM = 65
    const val SCENE_SET_ANIMATION_CLIP = 66
    const val SCENE_SET_MATERIAL_PARAM = 67
    const val SCENE_SET_ENVIRONMENT = 68
    const val VIEWPORT_SET_ZOOM = 69
    const val VIEWPORT_SET_PAN = 70
    const val VIEWPORT_SET_ROTATION = 71
    const val VIEWPORT_SET_PREVIEW_SCALE = 72
    const val PLAYBACK_PLAY = 73
    const val PLAYBACK_PAUSE = 74
    const val PLAYBACK_SEEK = 75
    const val PLAYBACK_SET_LOOP = 76
    const val PLAYBACK_SET_SPEED = 77
    const val UNDO = 78
    const val REDO = 79
    const val UNDO_BEGIN_GROUP = 80
    const val UNDO_END_GROUP = 81
    const val EXPORT_REQUEST = 82
    const val EXPORT_CANCEL = 83
    const val PLAYBACK_TOGGLE = 84
    const val PLAYBACK_SCRUB_BEGIN = 85
    const val PLAYBACK_SCRUB = 86
    const val PLAYBACK_SCRUB_END = 87
    const val PLAYBACK_STEP = 88
    const val AUDIO_SET_VOLUME = 89
    const val AUDIO_SET_PAN = 90
}
