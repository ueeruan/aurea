package com.aurea.aurea.state

/**
 * Formato de modelo 3D pelos primeiros bytes (o nome que o seletor entrega
 * pode vir sem extensão). Devolve "glb", "fbx", "gltf", "obj" ou null.
 */
internal fun sniffModelFormat(head: ByteArray): String? {
    if (head.size >= 4 && head[0] == 'g'.code.toByte() && head[1] == 'l'.code.toByte() &&
        head[2] == 'T'.code.toByte() && head[3] == 'F'.code.toByte()) return "glb"
    val text = String(head, Charsets.ISO_8859_1)
    if (text.startsWith("Kaydara FBX Binary")) return "fbx"
    if (text.contains("FBXHeaderExtension") || text.trimStart().startsWith("; FBX")) return "fbx"
    val trimmed = text.trimStart('\uFEFF', ' ', '\t', '\r', '\n')
    if (trimmed.startsWith("{") && text.contains("\"asset\"")) return "gltf"
    // OBJ: texto com linhas "v x y z" (ou mtllib/o/g antes delas).
    if (head.none { it == 0.toByte() }) {
        val lines = trimmed.lineSequence().map { it.trim() }.filter { it.isNotEmpty() && !it.startsWith("#") }.take(40).toList()
        if (lines.any { it.startsWith("v ") } && lines.all { l -> OBJ_KEYS.any { l.startsWith(it) } }) return "obj"
    }
    return null
}

private val OBJ_KEYS = listOf("v ", "vt ", "vn ", "vp ", "f ", "o ", "g ", "s ", "l ", "mtllib ", "usemtl ")
