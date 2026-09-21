package com.aurea.aurea

import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.ui.Modifier
import com.aurea.aurea.editor.EditorViewModel
import com.aurea.aurea.editor.HomeScreen
import com.aurea.aurea.ui.AureaColors
import com.aurea.aurea.ui.AureaTheme

/**
 * A única Activity do app.
 *
 * A UI é toda Compose. Ela NÃO processa frame de vídeo: o motor desenha direto
 * num `SurfaceView` e o Compose desenha por cima (painéis, timeline, controles).
 * São duas superfícies independentes — é isso que permite o preview rodar a
 * 60 fps enquanto a UI respira com o resto do sistema.
 *
 * Sem `setRequestedOrientation`: o editor precisa virar junto com o aparelho, e
 * travar em retrato mutilaria o trabalho de quem edita 16:9 na horizontal.
 */
class MainActivity : ComponentActivity() {

    private lateinit var viewModel: EditorViewModel

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        // A tela não apaga enquanto o usuário edita. Um projeto longo com o
        // dedo parado no meio de uma timeline é interrompido pelo descanso de
        // tela, e o usuário perde o contexto.
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        viewModel = EditorViewModel(application)

        setContent {
            AureaTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = AureaColors.Background,
                ) {
                    // Uma rota só por enquanto. A navegação entre Home e editor
                    // é estado, não uma pilha: o usuário nunca tem dois
                    // projetos abertos, e voltar para a Home fecha o projeto.
                    HomeScreen(viewModel)
                }
            }
        }
    }

    override fun onStart() {
        super.onStart()
        viewModel.onEnterForeground()
    }

    override fun onStop() {
        // O sistema pode matar o app em background a qualquer momento. O motor
        // é suspenso explicitamente para liberar GPU e cache antes disso — e o
        // projeto permanece em memória, então voltar não perde trabalho.
        viewModel.onEnterBackground()
        super.onStop()
    }

    override fun onDestroy() {
        viewModel.shutdown()
        super.onDestroy()
    }
}
