package com.aurea.aurea.hyper

import android.graphics.Bitmap
import android.graphics.PointF
import android.media.FaceDetector
import androidx.annotation.Keep
import java.nio.ShortBuffer

/**
 * O detector de rostos da plataforma para a etapa Face do AureaHyper
 * (aurea/hyper/HyperNeural.hpp `set_face_detector`). O motor chama da thread
 * do grafo, pelo JNI (aurea_jni.cpp `android_face_detector`), com o quadro já
 * reduzido para até 640 px em RGB565.
 *
 * Usa o `android.media.FaceDetector` do próprio sistema (API 1): nenhum SDK
 * novo, nenhum download, nada sai do aparelho e o APK não cresce. O ML Kit
 * (com os 5 pontos do rosto) pode entrar depois atrás desta mesma função; ele
 * pede o Google Play Services ou ~ alguns MB de modelo no APK e uma revisão da
 * seção de Segurança dos dados da Play.
 *
 * Saída: [x, y, largura, altura, confiança, yaw] por rosto, em pixels da
 * imagem recebida. O detector do sistema dá o meio dos olhos e a distância
 * entre eles; a caixa sai dessas medidas (proporção de um rosto de frente).
 */
@Keep
object HyperFaceDetector {
    @JvmStatic
    fun detect(rgb565: ShortArray, width: Int, height: Int, maxFaces: Int): FloatArray? {
        if (width < 16 || height < 16 || width % 2 != 0 || rgb565.size < width * height) return null
        return try {
            val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.RGB_565)
            bitmap.copyPixelsFromBuffer(ShortBuffer.wrap(rgb565, 0, width * height))
            val faces = arrayOfNulls<FaceDetector.Face>(maxFaces.coerceIn(1, 16))
            val n = FaceDetector(width, height, faces.size).findFaces(bitmap, faces)
            bitmap.recycle()
            val out = ArrayList<Float>(n * 6)
            val mid = PointF()
            for (i in 0 until n) {
                val f = faces[i] ?: continue
                if (f.confidence() < MIN_CONFIDENCE) continue
                f.getMidPoint(mid)
                val eyes = f.eyesDistance()
                if (eyes <= 1f) continue
                val w = eyes * 2.2f
                val h = eyes * 2.6f
                out += (mid.x - w / 2f).coerceAtLeast(0f)
                out += (mid.y - eyes * 1.0f).coerceAtLeast(0f)
                out += w.coerceAtMost(width.toFloat())
                out += h.coerceAtMost(height.toFloat())
                out += f.confidence()
                out += f.pose(FaceDetector.Face.EULER_Y)
            }
            out.toFloatArray()
        } catch (_: Throwable) {
            null
        }
    }

    /** Abaixo disto o próprio sistema considera o rosto incerto (documentação do FaceDetector). */
    private const val MIN_CONFIDENCE = 0.3f
}
