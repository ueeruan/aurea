package com.aurea.aurea.ui.ds

/**
 * 1.0.6: chaves de LazyColumn/LazyGrid sempre únicas.
 *
 * Duas linhas com a mesma chave fecham o app na medição ("Key "1#|0|0" was
 * already used", navegador de fontes do 2225). A lista vem do motor ou do
 * aparelho e pode repetir (fonte com nome vazio, arquivo .ttc com várias faces
 * no mesmo caminho): a primeira ocorrência guarda a chave natural — rolagem e
 * animação não mudam — e as repetidas ganham "#2", "#3"…
 */
fun <T> uniqueListKeys(items: List<T>, keyOf: (T) -> String): List<String> {
    val used = HashSet<String>(items.size * 2)
    return items.map { item ->
        val base = keyOf(item)
        var key = base
        var n = 1
        while (!used.add(key)) key = "$base#${++n}"
        key
    }
}
