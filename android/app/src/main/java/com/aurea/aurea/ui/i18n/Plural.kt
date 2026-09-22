package com.aurea.aurea.ui.i18n

import androidx.annotation.PluralsRes
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.pluralStringResource

/**
 * Contagem com a concordância do idioma.
 *
 * Existe porque "%d projeto(s)" não é traduzível: o russo tem três formas
 * (1 проект, 2 проекта, 5 проектов) e o árabe tem seis, incluindo o dual. Quem
 * escreve o plural é o `plurals.xml` de cada idioma; aqui só se pergunta.
 */
@Composable
fun plural(@PluralsRes id: Int, count: Int): String = pluralStringResource(id, count, count)
