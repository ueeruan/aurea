@file:Suppress("unused", "UNUSED_PARAMETER")

package com.google.mlkit.vision.pose

import com.google.android.gms.tasks.Task
import com.google.android.gms.tasks.Tasks
import com.google.mlkit.vision.common.InputImage

/** Pose do corpo (ML Kit): no PC não há detector; a pose volta vazia. */
class PoseLandmark internal constructor(val landmarkType: Int, val position3D: PointF3D, val inFrameLikelihood: Float)
class PointF3D(val x: Float, val y: Float, val z: Float)
class Pose internal constructor(val allPoseLandmarks: List<PoseLandmark>) {
    fun getPoseLandmark(type: Int): PoseLandmark? = null
}

open class PoseDetectorOptionsBase

class PoseDetector internal constructor() : AutoCloseable {
    fun process(image: InputImage): Task<Pose> = Tasks.forResult(Pose(emptyList()))
    override fun close() {}
}

object PoseDetection {
    @JvmStatic fun getClient(options: PoseDetectorOptionsBase): PoseDetector = PoseDetector()
}
