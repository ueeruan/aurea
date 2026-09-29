package com.aurea.aurea.state

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ModelSniffTest {
    @Test
    fun recognisesModelsWithoutExtension() {
        assertEquals("glb", sniffModelFormat(byteArrayOf('g'.code.toByte(), 'l'.code.toByte(), 'T'.code.toByte(), 'F'.code.toByte(), 2, 0, 0, 0)))
        assertEquals("fbx", sniffModelFormat("Kaydara FBX Binary  \u0000\u001a\u0000".toByteArray(Charsets.ISO_8859_1)))
        assertEquals("fbx", sniffModelFormat("; FBX 7.4.0 project file\nFBXHeaderExtension:  {".toByteArray()))
        assertEquals("gltf", sniffModelFormat("{\n  \"asset\": {\"version\": \"2.0\"}, \"meshes\": []".toByteArray()))
        assertEquals("obj", sniffModelFormat("# Blender\nmtllib a.mtl\no Cube\nv 1 1 1\nv 0 0 0\nvn 0 1 0\nf 1 2 3\n".toByteArray()))
    }

    @Test
    fun rejectsOtherFiles() {
        assertNull(sniffModelFormat(byteArrayOf(0x89.toByte(), 'P'.code.toByte(), 'N'.code.toByte(), 'G'.code.toByte())))
        assertNull(sniffModelFormat("hello world\nthis is a note\n".toByteArray()))
        assertNull(sniffModelFormat("{\"name\": \"x\"}".toByteArray()))
        assertNull(sniffModelFormat(ByteArray(0)))
    }
}
