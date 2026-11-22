package com.aurea.aurea.editor

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.scale
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

// Ícones das ferramentas da Edição, em traços numa grade 24×24. A mesma tabela está
// em EditModeView.swift: o desenho é igual nas duas plataformas.

internal enum class EditIcon(val filled: Boolean, strokes: Array<FloatArray>) {
    Select(true, arrayOf(floatArrayOf(6f, 3f, 19f, 13f, 13f, 14f, 16f, 20f, 13f, 22f, 10f, 16f, 6f, 20f, 6f, 3f))),
    Ripple(false, arrayOf(floatArrayOf(9f, 4f, 5f, 4f, 3f, 6f, 3f, 18f, 5f, 20f, 9f, 20f), floatArrayOf(8f, 12f, 21f, 12f), floatArrayOf(16f, 7f, 21f, 12f, 16f, 17f))),
    Roll(false, arrayOf(floatArrayOf(3f, 5f, 6f, 5f, 6f, 19f, 3f, 19f), floatArrayOf(21f, 5f, 18f, 5f, 18f, 19f, 21f, 19f), floatArrayOf(12f, 5f, 12f, 19f), floatArrayOf(9f, 9f, 6f, 12f, 9f, 15f), floatArrayOf(15f, 9f, 18f, 12f, 15f, 15f))),
    Razor(false, arrayOf(floatArrayOf(4f, 4f, 8f, 3f, 10f, 6f, 8f, 9f, 4f, 8f, 4f, 4f), floatArrayOf(4f, 16f, 8f, 15f, 10f, 18f, 8f, 21f, 4f, 20f, 4f, 16f), floatArrayOf(9f, 8f, 21f, 19f), floatArrayOf(9f, 16f, 21f, 5f))),
    Slip(false, arrayOf(floatArrayOf(6f, 5f, 3f, 5f, 3f, 19f, 6f, 19f), floatArrayOf(18f, 5f, 21f, 5f, 21f, 19f, 18f, 19f), floatArrayOf(7f, 12f, 17f, 12f), floatArrayOf(10f, 9f, 7f, 12f, 10f, 15f), floatArrayOf(14f, 9f, 17f, 12f, 14f, 15f))),
    Slide(false, arrayOf(floatArrayOf(8f, 4f, 16f, 4f, 18f, 6f, 18f, 11f, 16f, 13f, 8f, 13f, 6f, 11f, 6f, 6f, 8f, 4f), floatArrayOf(3f, 19f, 21f, 19f), floatArrayOf(6f, 16f, 3f, 19f, 6f, 22f), floatArrayOf(18f, 16f, 21f, 19f, 18f, 22f))),
    Snap(false, arrayOf(floatArrayOf(4f, 4f, 9f, 4f, 9f, 13f, 12f, 16f, 15f, 13f, 15f, 4f, 20f, 4f, 20f, 14f, 17f, 19f, 12f, 21f, 7f, 19f, 4f, 14f, 4f, 4f), floatArrayOf(4f, 8f, 9f, 8f), floatArrayOf(15f, 8f, 20f, 8f))),
    MarkIn(false, arrayOf(floatArrayOf(15f, 4f, 9f, 4f, 7f, 6f, 7f, 18f, 9f, 20f, 15f, 20f), floatArrayOf(10f, 12f, 20f, 12f), floatArrayOf(16f, 8f, 20f, 12f, 16f, 16f))),
    MarkOut(false, arrayOf(floatArrayOf(9f, 4f, 15f, 4f, 17f, 6f, 17f, 18f, 15f, 20f, 9f, 20f), floatArrayOf(4f, 12f, 14f, 12f), floatArrayOf(8f, 8f, 4f, 12f, 8f, 16f))),
    Lift(false, arrayOf(floatArrayOf(12f, 15f, 12f, 4f), floatArrayOf(8f, 8f, 12f, 4f, 16f, 8f), floatArrayOf(3f, 17f, 3f, 20f, 8f, 20f), floatArrayOf(21f, 17f, 21f, 20f, 16f, 20f))),
    Extract(false, arrayOf(floatArrayOf(12f, 14f, 12f, 3f), floatArrayOf(8f, 7f, 12f, 3f, 16f, 7f), floatArrayOf(3f, 19f, 9f, 19f), floatArrayOf(6f, 16f, 9f, 19f, 6f, 22f), floatArrayOf(21f, 19f, 15f, 19f), floatArrayOf(18f, 16f, 15f, 19f, 18f, 22f))),
    RazorAll(false, arrayOf(floatArrayOf(3f, 5f, 9f, 5f), floatArrayOf(15f, 5f, 21f, 5f), floatArrayOf(3f, 12f, 9f, 12f), floatArrayOf(15f, 12f, 21f, 12f), floatArrayOf(3f, 19f, 9f, 19f), floatArrayOf(15f, 19f, 21f, 19f), floatArrayOf(12f, 3f, 12f, 21f))),
    AddVideo(false, arrayOf(floatArrayOf(4f, 7f, 12f, 7f, 14f, 9f, 14f, 15f, 12f, 17f, 3f, 17f, 3f, 8f, 4f, 7f), floatArrayOf(19f, 9f, 19f, 15f), floatArrayOf(16f, 12f, 22f, 12f))),
    AddAudio(false, arrayOf(floatArrayOf(3f, 10f, 3f, 14f), floatArrayOf(6f, 7f, 6f, 17f), floatArrayOf(9f, 9f, 9f, 15f), floatArrayOf(12f, 11f, 12f, 13f), floatArrayOf(19f, 9f, 19f, 15f), floatArrayOf(16f, 12f, 22f, 12f))),
    ClearTracks(false, arrayOf(floatArrayOf(4f, 6f, 20f, 6f), floatArrayOf(9f, 6f, 9f, 3f, 15f, 3f, 15f, 6f), floatArrayOf(6f, 6f, 7f, 19f, 9f, 21f, 15f, 21f, 17f, 19f, 18f, 6f))),
    PlaceEnd(false, arrayOf(floatArrayOf(3f, 12f, 15f, 12f), floatArrayOf(11f, 8f, 15f, 12f, 11f, 16f), floatArrayOf(19f, 5f, 21f, 5f, 21f, 19f, 19f, 19f))),
    PlaceInsert(false, arrayOf(floatArrayOf(3f, 20f, 8f, 20f), floatArrayOf(16f, 20f, 21f, 20f), floatArrayOf(12f, 3f, 12f, 17f), floatArrayOf(8f, 13f, 12f, 17f, 16f, 13f))),
    PlaceOverwrite(false, arrayOf(floatArrayOf(12f, 3f, 12f, 12f), floatArrayOf(8f, 8f, 12f, 12f, 16f, 8f), floatArrayOf(4f, 16f, 20f, 16f, 21f, 17f, 21f, 20f, 20f, 21f, 4f, 21f, 3f, 20f, 3f, 17f, 4f, 16f))),
    Back(false, arrayOf(floatArrayOf(12f, 5f, 5f, 12f, 12f, 19f), floatArrayOf(5f, 12f, 20f, 12f))),
    ChevronDown(false, arrayOf(floatArrayOf(5f, 9f, 12f, 16f, 19f, 9f))),
    More(false, arrayOf(floatArrayOf(3f, 12f, 5f, 10f, 7f, 12f, 5f, 14f, 3f, 12f), floatArrayOf(10f, 12f, 12f, 10f, 14f, 12f, 12f, 14f, 10f, 12f), floatArrayOf(17f, 12f, 19f, 10f, 21f, 12f, 19f, 14f, 17f, 12f))),
    Export(false, arrayOf(floatArrayOf(5f, 12f, 5f, 19f, 7f, 21f, 17f, 21f, 19f, 19f, 19f, 12f), floatArrayOf(12f, 15f, 12f, 3f), floatArrayOf(7f, 8f, 12f, 3f, 17f, 8f))),
    Play(true, arrayOf(floatArrayOf(8f, 4f, 20f, 12f, 8f, 20f, 6f, 19f, 6f, 5f, 8f, 4f))),
    Pause(true, arrayOf(floatArrayOf(5f, 4f, 8f, 4f, 9f, 5f, 9f, 19f, 8f, 20f, 5f, 20f, 4f, 19f, 4f, 5f, 5f, 4f), floatArrayOf(16f, 4f, 19f, 4f, 20f, 5f, 20f, 19f, 19f, 20f, 16f, 20f, 15f, 19f, 15f, 5f, 16f, 4f))),
    Undo(false, arrayOf(floatArrayOf(8f, 5f, 3f, 10f, 8f, 15f), floatArrayOf(3f, 10f, 16f, 10f, 20f, 14f, 20f, 19f))),
    Redo(false, arrayOf(floatArrayOf(16f, 5f, 21f, 10f, 16f, 15f), floatArrayOf(21f, 10f, 8f, 10f, 4f, 14f, 4f, 19f))),
    Fullscreen(false, arrayOf(floatArrayOf(4f, 9f, 4f, 4f, 9f, 4f), floatArrayOf(15f, 4f, 20f, 4f, 20f, 9f), floatArrayOf(20f, 15f, 20f, 20f, 15f, 20f), floatArrayOf(9f, 20f, 4f, 20f, 4f, 15f))),
    FullscreenExit(false, arrayOf(floatArrayOf(4f, 9f, 9f, 9f, 9f, 4f), floatArrayOf(15f, 4f, 15f, 9f, 20f, 9f), floatArrayOf(20f, 15f, 15f, 15f, 15f, 20f), floatArrayOf(9f, 20f, 9f, 15f, 4f, 15f))),
    Plus(false, arrayOf(floatArrayOf(12f, 4f, 12f, 20f), floatArrayOf(4f, 12f, 20f, 12f))),
    Close(false, arrayOf(floatArrayOf(6f, 6f, 18f, 18f), floatArrayOf(18f, 6f, 6f, 18f))),
    Start(false, arrayOf(floatArrayOf(5f, 5f, 5f, 19f), floatArrayOf(19f, 5f, 9f, 12f, 19f, 19f, 19f, 5f))),
    Lock(false, arrayOf(floatArrayOf(6f, 11f, 18f, 11f, 20f, 13f, 20f, 19f, 18f, 21f, 6f, 21f, 4f, 19f, 4f, 13f, 6f, 11f), floatArrayOf(8f, 11f, 8f, 7f, 10f, 4f, 14f, 4f, 16f, 7f, 16f, 11f), floatArrayOf(12f, 15f, 12f, 17f))),
    Unlock(false, arrayOf(floatArrayOf(6f, 11f, 18f, 11f, 20f, 13f, 20f, 19f, 18f, 21f, 6f, 21f, 4f, 19f, 4f, 13f, 6f, 11f), floatArrayOf(8f, 11f, 8f, 7f, 10f, 4f, 14f, 4f, 16f, 7f), floatArrayOf(12f, 15f, 12f, 17f))),
    Eye(false, arrayOf(floatArrayOf(2f, 12f, 7f, 7f, 12f, 5f, 17f, 7f, 22f, 12f, 17f, 17f, 12f, 19f, 7f, 17f, 2f, 12f), floatArrayOf(9f, 12f, 12f, 9f, 15f, 12f, 12f, 15f, 9f, 12f))),
    EyeOff(false, arrayOf(floatArrayOf(2f, 12f, 7f, 7f, 12f, 5f, 17f, 7f, 22f, 12f, 17f, 17f, 12f, 19f, 7f, 17f, 2f, 12f), floatArrayOf(4f, 3f, 20f, 21f))),
    Speaker(false, arrayOf(floatArrayOf(3f, 9f, 6f, 9f, 11f, 5f, 11f, 19f, 6f, 15f, 3f, 15f, 3f, 9f), floatArrayOf(15f, 9f, 17f, 12f, 15f, 15f), floatArrayOf(18f, 6f, 21f, 9f, 21f, 15f, 18f, 18f))),
    Muted(false, arrayOf(floatArrayOf(3f, 9f, 6f, 9f, 11f, 5f, 11f, 19f, 6f, 15f, 3f, 15f, 3f, 9f), floatArrayOf(15f, 9f, 21f, 15f), floatArrayOf(21f, 9f, 15f, 15f))),
    Text(false, arrayOf(floatArrayOf(4f, 7f, 4f, 5f, 20f, 5f, 20f, 7f), floatArrayOf(12f, 5f, 12f, 19f), floatArrayOf(9f, 19f, 15f, 19f))),
    Font(false, arrayOf(floatArrayOf(3f, 19f, 9f, 5f, 15f, 19f), floatArrayOf(5f, 14f, 13f, 14f), floatArrayOf(21f, 12f, 17f, 12f, 16f, 15f, 16f, 17f, 18f, 19f, 21f, 19f, 21f, 12f))),
    Captions(false, arrayOf(floatArrayOf(4f, 5f, 20f, 5f, 22f, 7f, 22f, 16f, 20f, 18f, 10f, 18f, 6f, 21f, 6f, 18f, 4f, 18f, 2f, 16f, 2f, 7f, 4f, 5f), floatArrayOf(6f, 10f, 11f, 10f), floatArrayOf(14f, 10f, 18f, 10f), floatArrayOf(6f, 14f, 18f, 14f))),
    Effects(false, arrayOf(floatArrayOf(12f, 2f, 14.5f, 9.5f, 22f, 12f, 14.5f, 14.5f, 12f, 22f, 9.5f, 14.5f, 2f, 12f, 9.5f, 9.5f, 12f, 2f), floatArrayOf(19f, 3f, 19f, 7f), floatArrayOf(17f, 5f, 21f, 5f))),
    Overlay(false, arrayOf(floatArrayOf(5f, 9f, 15f, 9f, 17f, 11f, 17f, 19f, 15f, 21f, 5f, 21f, 3f, 19f, 3f, 11f, 5f, 9f), floatArrayOf(8f, 6f, 8f, 5f, 10f, 3f, 19f, 3f, 21f, 5f, 21f, 14f, 20f, 15f))),
    Filters(false, arrayOf(floatArrayOf(12f, 3f, 16f, 7f, 12f, 11f, 8f, 7f, 12f, 3f), floatArrayOf(7f, 11f, 11f, 15f, 7f, 19f, 3f, 15f, 7f, 11f), floatArrayOf(17f, 11f, 21f, 15f, 17f, 19f, 13f, 15f, 17f, 11f))),
    Adjust(false, arrayOf(floatArrayOf(3f, 6f, 10f, 6f), floatArrayOf(14f, 6f, 21f, 6f), floatArrayOf(10f, 6f, 12f, 4f, 14f, 6f, 12f, 8f, 10f, 6f), floatArrayOf(3f, 12f, 6f, 12f), floatArrayOf(10f, 12f, 21f, 12f), floatArrayOf(6f, 12f, 8f, 10f, 10f, 12f, 8f, 14f, 6f, 12f), floatArrayOf(3f, 18f, 14f, 18f), floatArrayOf(18f, 18f, 21f, 18f), floatArrayOf(14f, 18f, 16f, 16f, 18f, 18f, 16f, 20f, 14f, 18f))),
    Crop(false, arrayOf(floatArrayOf(7f, 3f, 7f, 16f, 9f, 18f, 21f, 18f), floatArrayOf(3f, 7f, 15f, 7f, 17f, 9f, 17f, 21f))),
    Background(false, arrayOf(floatArrayOf(12f, 3f, 21f, 12f, 14f, 19f, 10f, 19f, 3f, 12f, 12f, 3f), floatArrayOf(3f, 12f, 21f, 12f), floatArrayOf(21f, 15f, 19f, 19f, 20f, 21f, 22f, 21f, 23f, 19f, 21f, 15f))),
    Shapes(false, arrayOf(floatArrayOf(5f, 4f, 15f, 4f, 17f, 6f, 17f, 10f), floatArrayOf(9f, 18f, 5f, 18f, 3f, 16f, 3f, 6f, 5f, 4f), floatArrayOf(16f, 10f, 22f, 14f, 20f, 21f, 12f, 21f, 10f, 14f, 16f, 10f))),
    Sequence(false, arrayOf(floatArrayOf(4f, 5f, 16f, 5f, 18f, 7f, 18f, 15f, 16f, 17f, 4f, 17f, 2f, 15f, 2f, 7f, 4f, 5f), floatArrayOf(7f, 20f, 20f, 20f, 22f, 18f, 22f, 9f), floatArrayOf(8f, 8f, 13f, 11f, 8f, 14f, 8f, 8f))),
    Presets(false, arrayOf(floatArrayOf(5f, 3f, 19f, 3f, 21f, 5f, 21f, 19f, 19f, 21f, 5f, 21f, 3f, 19f, 3f, 5f, 5f, 3f), floatArrayOf(12f, 6f, 14f, 10f, 18f, 12f, 14f, 14f, 12f, 18f, 10f, 14f, 6f, 12f, 10f, 10f, 12f, 6f))),
    Music(false, arrayOf(floatArrayOf(9f, 17f, 9f, 5f, 20f, 3f, 20f, 14f), floatArrayOf(9f, 15f, 6f, 14f, 3f, 16f, 3f, 19f, 6f, 20f, 9f, 18f, 9f, 15f), floatArrayOf(20f, 12f, 17f, 11f, 14f, 13f, 14f, 16f, 17f, 17f, 20f, 15f, 20f, 12f), floatArrayOf(9f, 9f, 20f, 7f))),
    Voice(false, arrayOf(floatArrayOf(10f, 3f, 14f, 3f, 16f, 5f, 16f, 12f, 14f, 14f, 10f, 14f, 8f, 12f, 8f, 5f, 10f, 3f), floatArrayOf(5f, 11f, 5f, 14f, 9f, 18f, 15f, 18f, 19f, 14f, 19f, 11f), floatArrayOf(12f, 18f, 12f, 21f), floatArrayOf(8f, 21f, 16f, 21f))),
    Waveform(false, arrayOf(floatArrayOf(2f, 12f, 5f, 12f, 7f, 7f, 10f, 18f, 13f, 4f, 16f, 20f, 18f, 12f, 22f, 12f))),
    Speed(false, arrayOf(floatArrayOf(4f, 18f, 3f, 14f, 5f, 8f, 9f, 4f, 15f, 4f, 19f, 8f, 21f, 14f, 20f, 18f, 4f, 18f), floatArrayOf(10f, 14f, 16f, 8f))),
    Delete(false, arrayOf(floatArrayOf(4f, 6f, 20f, 6f), floatArrayOf(8f, 6f, 9f, 3f, 15f, 3f, 16f, 6f), floatArrayOf(6f, 6f, 7f, 19f, 9f, 21f, 15f, 21f, 17f, 19f, 18f, 6f), floatArrayOf(10f, 10f, 10f, 16f), floatArrayOf(14f, 10f, 14f, 16f))),
    Mask(false, arrayOf(floatArrayOf(5f, 3f, 19f, 3f, 21f, 5f, 21f, 19f, 19f, 21f, 5f, 21f, 3f, 19f, 3f, 5f, 5f, 3f), floatArrayOf(9f, 9f, 15f, 9f, 15f, 15f, 9f, 15f, 9f, 9f), floatArrayOf(3f, 13f, 8f, 8f), floatArrayOf(4f, 20f, 9f, 15f), floatArrayOf(13f, 21f, 21f, 13f))),
    Replace(false, arrayOf(floatArrayOf(4f, 10f, 4f, 7f, 7f, 4f, 19f, 4f), floatArrayOf(15f, 1f, 19f, 4f, 15f, 8f), floatArrayOf(20f, 14f, 20f, 17f, 17f, 20f, 5f, 20f), floatArrayOf(9f, 16f, 5f, 20f, 9f, 23f))),
    Cutout(false, arrayOf(floatArrayOf(3f, 8f, 3f, 4f, 7f, 4f), floatArrayOf(17f, 4f, 21f, 4f, 21f, 8f), floatArrayOf(21f, 16f, 21f, 20f, 17f, 20f), floatArrayOf(7f, 20f, 3f, 20f, 3f, 16f), floatArrayOf(9f, 15f, 9f, 12f, 12f, 8f, 15f, 12f, 15f, 15f, 12f, 17f, 9f, 15f))),
    Freeze(false, arrayOf(floatArrayOf(12f, 2f, 12f, 22f), floatArrayOf(3f, 7f, 21f, 17f), floatArrayOf(3f, 17f, 21f, 7f), floatArrayOf(9f, 4f, 12f, 7f, 15f, 4f), floatArrayOf(9f, 20f, 12f, 17f, 15f, 20f), floatArrayOf(4f, 10f, 8f, 10f, 7f, 6f), floatArrayOf(20f, 14f, 16f, 14f, 17f, 18f), floatArrayOf(4f, 14f, 8f, 14f, 7f, 18f), floatArrayOf(20f, 10f, 16f, 10f, 17f, 6f))),
    Track(false, arrayOf(floatArrayOf(3f, 8f, 3f, 3f, 8f, 3f), floatArrayOf(16f, 3f, 21f, 3f, 21f, 8f), floatArrayOf(21f, 16f, 21f, 21f, 16f, 21f), floatArrayOf(8f, 21f, 3f, 21f, 3f, 16f), floatArrayOf(7f, 12f, 12f, 7f, 17f, 12f, 12f, 17f, 7f, 12f))),
    MainTrack(false, arrayOf(floatArrayOf(3f, 5f, 21f, 5f), floatArrayOf(3f, 10f, 12f, 10f, 14f, 12f, 14f, 17f, 12f, 19f, 3f, 19f, 3f, 10f), floatArrayOf(18f, 14f, 22f, 14f), floatArrayOf(20f, 12f, 22f, 14f, 20f, 16f))),
    Pencil(false, arrayOf(floatArrayOf(4f, 15f, 15f, 4f, 17f, 3f, 21f, 7f, 20f, 9f, 9f, 20f, 3f, 21f, 4f, 15f), floatArrayOf(14f, 5f, 19f, 10f))),
    Transform(false, arrayOf(floatArrayOf(12f, 2f, 12f, 22f), floatArrayOf(2f, 12f, 22f, 12f), floatArrayOf(9f, 5f, 12f, 2f, 15f, 5f), floatArrayOf(9f, 19f, 12f, 22f, 15f, 19f), floatArrayOf(5f, 9f, 2f, 12f, 5f, 15f), floatArrayOf(19f, 9f, 22f, 12f, 19f, 15f))),
    Duplicate(false, arrayOf(floatArrayOf(8f, 9f, 19f, 9f, 21f, 11f, 21f, 19f, 19f, 21f, 8f, 21f, 6f, 19f, 6f, 11f, 8f, 9f), floatArrayOf(3f, 15f, 3f, 5f, 5f, 3f, 16f, 3f, 18f, 5f), floatArrayOf(13.5f, 12f, 13.5f, 18f), floatArrayOf(10.5f, 15f, 16.5f, 15f))),
    Opacity(false, arrayOf(floatArrayOf(12f, 3f, 17f, 5f, 21f, 10f, 21f, 14f, 17f, 19f, 12f, 21f, 7f, 19f, 3f, 14f, 3f, 10f, 7f, 5f, 12f, 3f, 12f, 21f), floatArrayOf(6f, 15f, 12f, 9f), floatArrayOf(5f, 10f, 10f, 5f), floatArrayOf(9f, 19f, 12f, 16f))),
    Draw(false, arrayOf(floatArrayOf(3f, 16f, 7f, 5f, 10f, 4f, 10f, 9f, 6f, 17f, 8f, 20f, 14f, 14f, 16f, 13f, 17f, 16f, 16f, 19f, 18f, 20f, 22f, 17f))),
    Vector(false, arrayOf(floatArrayOf(5f, 19f, 11f, 6f, 19f, 13f), floatArrayOf(3f, 19f, 5f, 17f, 7f, 19f, 5f, 21f, 3f, 19f), floatArrayOf(9f, 6f, 11f, 4f, 13f, 6f, 11f, 8f, 9f, 6f), floatArrayOf(17f, 13f, 19f, 11f, 21f, 13f, 19f, 15f, 17f, 13f))),
    ShapeEdit(false, arrayOf(floatArrayOf(5f, 4f, 17f, 4f, 19f, 6f, 19f, 11f, 17f, 13f, 5f, 13f, 3f, 11f, 3f, 6f, 5f, 4f), floatArrayOf(3f, 19f, 10f, 19f), floatArrayOf(14f, 19f, 21f, 19f), floatArrayOf(10f, 19f, 12f, 17f, 14f, 19f, 12f, 21f, 10f, 19f))),
    Cube(false, arrayOf(floatArrayOf(12f, 2f, 21f, 7f, 21f, 17f, 12f, 22f, 3f, 17f, 3f, 7f, 12f, 2f), floatArrayOf(3f, 7f, 12f, 12f, 21f, 7f), floatArrayOf(12f, 12f, 12f, 22f)));

    val paths by lazy {
        strokes.map { points ->
            Path().apply {
                moveTo(points[0], points[1])
                for (i in 2 until points.size step 2) lineTo(points[i], points[i + 1])
                if (filled) close()
            }
        }
    }
}

private val editIconStroke = Stroke(width = 1.7f, cap = StrokeCap.Round, join = StrokeJoin.Round)

@Composable
internal fun EditToolIcon(icon: EditIcon, color: Color, size: Dp = 22.dp) {
    Canvas(Modifier.size(size)) {
        val k = this.size.width / 24f
        scale(k, k, pivot = Offset.Zero) {
            icon.paths.forEach { path ->
                if (icon.filled) drawPath(path, color) else drawPath(path, color, style = editIconStroke)
            }
        }
    }
}
