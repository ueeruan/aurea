package com.aurea.aurea.home

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel

/** CONTRATO da Home (Beta A.01). Provisória até o port. */
@Composable
fun HomeScreen(store: EditorStore) {
    Column(
        Modifier.fillMaxSize().systemBarsPadding().padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Aurea", style = AureaType.HeadlineLarge)
        Text(
            "Novo projeto",
            style = AureaType.TitleMedium,
            modifier = Modifier.tocavel { store.newProject(1920, 1080, 30f, "Projeto sem título") },
        )
        store.projects.forEach { p ->
            Text(p.title, modifier = Modifier.tocavel { store.openProject(p.path) })
        }
    }
}
