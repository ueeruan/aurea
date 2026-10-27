package com.aurea.aurea.editor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Bugs 0.3.4 (seleção de mídia):
 * - bug 4: a prévia mostrava a mídia escolhida ANTES. O valor carregado leva a
 *   chave da mídia; o resultado de uma mídia que perdeu o foco nunca aparece,
 *   nem quando chega fora de ordem.
 * - bug 5: áudio não aparecia na seleção de mídia. O seletor de documentos pede
 *   fotos, vídeos e áudio, e o arquivo de áudio vira camada de áudio.
 */
class MediaSelectionTest {
    @Test fun previewShowsOnlyTheFocusedMedia() {
        val a = "content://media/a"
        val b = "content://media/b"
        val c = "content://media/c"
        // Tocou A, depois B e C; a resposta de B chega depois do foco ir para C.
        val lateB = LoadedFor(b, "imagem B")
        assertNull("resposta velha não pode aparecer", lateB.valueFor(c))
        assertNull(LoadedFor(a, "imagem A").valueFor(c))
        assertEquals("imagem C", LoadedFor(c, "imagem C").valueFor(c))
        // Antes de qualquer resposta: nada (nunca a mídia anterior).
        assertNull(LoadedFor<String>(Unit, null).valueFor(a))
    }

    @Test fun pickerAsksForAudioToo() {
        val types = PickedMedia.PICKER_TYPES.toList()
        assertTrue(types.contains("image/*"))
        assertTrue(types.contains("video/*"))
        assertTrue(types.contains("audio/*"))
    }

    @Test fun audioFilesBecomeAudio() {
        assertEquals(PickedMedia.Kind.Audio, PickedMedia.kind("audio/mpeg", "musica.mp3"))
        assertEquals(PickedMedia.Kind.Audio, PickedMedia.kind("audio/mp4", "voz.m4a"))
        assertEquals(PickedMedia.Kind.Audio, PickedMedia.kind("audio/x-wav", "efeito.wav"))
        assertEquals(PickedMedia.Kind.Audio, PickedMedia.kind("audio/wav", "SEM_EXTENSAO"))
        // Provedores que erram o tipo: a extensão de áudio vence.
        assertEquals(PickedMedia.Kind.Audio, PickedMedia.kind("video/mp4", "gravacao.m4a"))
        assertEquals(PickedMedia.Kind.Audio, PickedMedia.kind("application/octet-stream", "Trilha.MP3"))
        assertEquals(PickedMedia.Kind.Audio, PickedMedia.kind(null, "som.wav"))
        assertEquals(PickedMedia.Kind.Audio, PickedMedia.kind("application/ogg", "faixa"))
    }

    @Test fun photosAndVideosKeepTheirKind() {
        assertEquals(PickedMedia.Kind.Video, PickedMedia.kind("video/mp4", "clipe.mp4"))
        assertEquals(PickedMedia.Kind.Video, PickedMedia.kind("video/quicktime", "clipe.mov"))
        assertEquals(PickedMedia.Kind.Image, PickedMedia.kind("image/png", "foto.png"))
        assertEquals(PickedMedia.Kind.Image, PickedMedia.kind("image/gif", "anim.gif"))
        assertEquals(PickedMedia.Kind.Image, PickedMedia.kind(null, "arte.psd"))
    }
}
