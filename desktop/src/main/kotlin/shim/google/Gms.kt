// Google Play Services e ML Kit no PC: não existem no Windows. Os poucos usos
// diretos da UI recebem o resultado "nada encontrado"/"indisponível".
@file:Suppress("unused", "UNUSED_PARAMETER")

package com.google.android.gms.tasks

open class Task<T> internal constructor(internal val value: T?, internal val error: Exception?)

object Tasks {
    @JvmStatic fun <T> await(task: Task<T>): T = task.value ?: throw (task.error ?: java.util.concurrent.ExecutionException(IllegalStateException("pc")))
    @JvmStatic fun <T> await(task: Task<T>, timeout: Long, unit: java.util.concurrent.TimeUnit): T = await(task)
    @JvmStatic fun <T> forResult(v: T): Task<T> = Task(v, null)
    @JvmStatic fun <T> forException(e: Exception): Task<T> = Task(null, e)
}
