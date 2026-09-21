package com.aurea.aurea.editor

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.outlined.Movie
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.ui.AureaColors
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * A Home.
 *
 * O QUE ELA É: a porta de entrada. Abrir o app tem que deixar óbvio o próximo
 * passo — criar um projeto ou abrir um que já existe. Nada mais.
 *
 * O QUE ELA NÃO É: um painel de descoberta. Sem feed, sem sugestões, sem
 * carrossel de modelos. Quem abre o Aurea quer editar, não navegar.
 *
 * O visual segue a identidade: fundo `#0F141A` (o mesmo do ícone e da splash),
 * a logo oficial no topo, e o resto em superfícies discretas.
 */
@Composable
fun HomeScreen(viewModel: EditorViewModel) {
    val ui = viewModel.ui

    LaunchedEffect(Unit) { viewModel.refreshRecentProjects() }

    if (ui.screen == Screen.Editor) {
        EditorScreen(viewModel)
        return
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(AureaColors.Background)
            .systemBarsPadding()
            .padding(horizontal = 20.dp),
    ) {
        Spacer(Modifier.height(48.dp))

        // A logo oficial. É o mesmo arquivo que gera o ícone do app.
        Image(
            painter = painterResource(R.drawable.splash_logo),
            contentDescription = "Aurea Editor",
            modifier = Modifier
                .height(64.dp)
                .align(Alignment.CenterHorizontally),
        )

        Spacer(Modifier.height(12.dp))

        Text(
            text = "Aurea Editor",
            style = MaterialTheme.typography.titleLarge,
            color = AureaColors.OnSurface,
            modifier = Modifier.align(Alignment.CenterHorizontally),
        )

        Spacer(Modifier.height(40.dp))

        // As duas ações primárias. "Novo projeto" é preenchido porque é o que a
        // maioria quer; "Abrir" é contornado porque é o caminho de quem já tem
        // trabalho — e um botão de mesmo peso para os dois faria a escolha
        // parecer mais difícil do que é.
        Button(
            onClick = { viewModel.newProject(1920, 1080, 60f, "Projeto sem título") },
            modifier = Modifier
                .fillMaxWidth()
                .height(52.dp),
            shape = RoundedCornerShape(12.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = AureaColors.Accent,
                contentColor = AureaColors.Background,
            ),
        ) {
            Icon(Icons.Filled.Add, contentDescription = null, modifier = Modifier.size(20.dp))
            Spacer(Modifier.width(10.dp))
            Text("Novo projeto", fontWeight = FontWeight.SemiBold)
        }

        Spacer(Modifier.height(10.dp))

        OutlinedButton(
            onClick = { viewModel.ui.recentProjects.firstOrNull()?.let { viewModel.openProject(it.path) } },
            enabled = viewModel.ui.recentProjects.isNotEmpty(),
            modifier = Modifier
                .fillMaxWidth()
                .height(52.dp),
            shape = RoundedCornerShape(12.dp),
        ) {
            Icon(Icons.Filled.FolderOpen, contentDescription = null, modifier = Modifier.size(20.dp))
            Spacer(Modifier.width(10.dp))
            Text("Continuar de onde parei")
        }

        Spacer(Modifier.height(36.dp))

        if (ui.recoveryAvailable) {
            RecoveryBanner(
                onRecover = { /* a recuperação é oferecida ao abrir o projeto */ },
                onDiscard = { viewModel.discardRecovery() },
            )
            Spacer(Modifier.height(20.dp))
        }

        Text(
            text = "Projetos recentes",
            style = MaterialTheme.typography.labelLarge,
            color = AureaColors.OnSurfaceMuted,
        )

        Spacer(Modifier.height(10.dp))

        if (ui.recentProjects.isEmpty()) {
            EmptyProjectsHint()
        } else {
            LazyColumn(
                modifier = Modifier.fillMaxSize(),
                contentPadding = PaddingValues(bottom = 32.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                items(ui.recentProjects, key = { it.path }) { project ->
                    ProjectCard(project) { viewModel.openProject(project.path) }
                }
            }
        }
    }

    ui.errorMessage?.let { message ->
        ErrorDialog(message = message, onDismiss = { viewModel.dismissError() })
    }
}

@Composable
private fun ProjectCard(project: RecentProject, onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(AureaColors.Surface)
            .clickable(onClick = onClick)
            .padding(14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier = Modifier
                .size(44.dp)
                .clip(RoundedCornerShape(8.dp))
                .background(AureaColors.SurfaceHighest),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                Icons.Outlined.Movie,
                contentDescription = null,
                tint = AureaColors.Accent,
                modifier = Modifier.size(22.dp),
            )
        }

        Spacer(Modifier.width(14.dp))

        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = project.title,
                style = MaterialTheme.typography.bodyLarge,
                color = AureaColors.OnSurface,
            )
            Spacer(Modifier.height(2.dp))
            Text(
                text = "${formatDate(project.modifiedMs)} · ${formatSize(project.sizeBytes)}",
                style = MaterialTheme.typography.bodySmall,
                color = AureaColors.OnSurfaceFaint,
            )
        }
    }
}

@Composable
private fun EmptyProjectsHint() {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(top = 24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            text = "Nenhum projeto ainda",
            style = MaterialTheme.typography.bodyMedium,
            color = AureaColors.OnSurfaceFaint,
        )
        Spacer(Modifier.height(6.dp))
        Text(
            text = "Toque em Novo projeto para começar.",
            style = MaterialTheme.typography.bodySmall,
            color = AureaColors.OnSurfaceFaint,
            fontSize = 12.sp,
        )
    }
}

@Composable
private fun RecoveryBanner(onRecover: () -> Unit, onDiscard: () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(AureaColors.SurfaceHighest)
            .padding(14.dp),
    ) {
        Text(
            text = "Sessão anterior não foi salva",
            style = MaterialTheme.typography.labelLarge,
            color = AureaColors.Warning,
        )
        Spacer(Modifier.height(4.dp))
        Text(
            text = "O Aurea guardou o que você fez. Abra o projeto para recuperar.",
            style = MaterialTheme.typography.bodySmall,
            color = AureaColors.OnSurfaceMuted,
        )
        Spacer(Modifier.height(10.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = onRecover, shape = RoundedCornerShape(8.dp)) { Text("Recuperar") }
            OutlinedButton(onClick = onDiscard, shape = RoundedCornerShape(8.dp)) { Text("Descartar") }
        }
    }
}

private fun formatDate(ms: Long): String {
    if (ms <= 0) return "—"
    val fmt = SimpleDateFormat("dd/MM/yyyy HH:mm", Locale.forLanguageTag("pt-BR"))
    return fmt.format(Date(ms))
}

private fun formatSize(bytes: Long): String = when {
    bytes >= 1024L * 1024L * 1024L -> "%.1f GB".format(bytes / (1024.0 * 1024.0 * 1024.0))
    bytes >= 1024L * 1024L -> "%.1f MB".format(bytes / (1024.0 * 1024.0))
    bytes >= 1024L -> "%.0f KB".format(bytes / 1024.0)
    else -> "$bytes B"
}
