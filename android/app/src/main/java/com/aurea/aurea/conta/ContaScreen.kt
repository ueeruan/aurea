package com.aurea.aurea.conta

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.home.AureaLogo
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel

/**
 * Cadastro/entrada opcionais, abertos pelo menu e fecháveis sem autenticar.
 * O par no iOS é `ContaView.swift`.
 *
 * A senha fica em `remember` (não em `rememberSaveable`): ela não vai para o
 * estado salvo da Activity.
 */
@Composable
fun ContaScreen(conta: ContaViewModel) {
    var email by rememberSaveable { mutableStateOf("") }
    var senha by remember { mutableStateOf("") }
    var mostrar by remember { mutableStateOf(false) }
    val foco = LocalFocusManager.current
    val entrando = conta.entrando
    val enviar = {
        foco.clearFocus()
        conta.enviar(email, senha)
    }

    Box(Modifier.fillMaxSize().background(AureaColors.Background).safeDrawingPadding().imePadding(),
        contentAlignment = Alignment.Center) {
        Column(
            Modifier.widthIn(max = 440.dp).fillMaxWidth().verticalScroll(rememberScrollState())
                .padding(horizontal = 24.dp, vertical = 32.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            AureaLogo(72.dp)
            Spacer(Modifier.height(16.dp))
            Text("aurea", style = AureaType.HeadlineLarge)
            Spacer(Modifier.height(20.dp))
            Text(stringResource(if (entrando) R.string.conta_titulo_entrar else R.string.conta_titulo_cadastro),
                style = AureaType.TitleLarge, textAlign = TextAlign.Center)
            Spacer(Modifier.height(8.dp))
            Text(stringResource(R.string.conta_subtitulo), style = AureaType.Note, textAlign = TextAlign.Center)
            Spacer(Modifier.height(24.dp))

            CampoConta(
                valor = email, aoMudar = { email = it.take(254); conta.erro = null },
                rotulo = stringResource(R.string.conta_email), tag = "conta.email",
                teclado = KeyboardOptions(keyboardType = KeyboardType.Email, imeAction = ImeAction.Next, autoCorrectEnabled = false),
                acoes = KeyboardActions(onNext = { foco.moveFocus(androidx.compose.ui.focus.FocusDirection.Down) }),
            )
            Spacer(Modifier.height(12.dp))
            CampoConta(
                valor = senha, aoMudar = { senha = it.take(ContaLogica.SENHA_MAX * 2); conta.erro = null },
                rotulo = stringResource(R.string.conta_senha), tag = "conta.senha",
                teclado = KeyboardOptions(keyboardType = KeyboardType.Password, imeAction = ImeAction.Done, autoCorrectEnabled = false),
                acoes = KeyboardActions(onDone = { enviar() }),
                transformacao = if (mostrar) VisualTransformation.None else PasswordVisualTransformation(),
                direita = {
                    val rotulo = stringResource(if (mostrar) R.string.conta_ocultar else R.string.conta_mostrar)
                    Text(rotulo, style = AureaType.Pill,
                        modifier = Modifier.clip(RoundedCornerShape(8.dp)).tocavel { mostrar = !mostrar }
                            .padding(horizontal = 8.dp, vertical = 6.dp))
                },
            )
            Text(stringResource(R.string.conta_senha_dica), style = AureaType.Footer,
                modifier = Modifier.fillMaxWidth().padding(top = 6.dp, start = 4.dp))

            conta.erro?.let { e ->
                Text(stringResource(textoDoErro(e)), style = AureaType.Note.copy(color = AureaColors.Danger),
                    textAlign = TextAlign.Center, modifier = Modifier.fillMaxWidth().padding(top = 14.dp).testTag("conta.erro"))
            }
            Spacer(Modifier.height(20.dp))

            val rotuloBotao = stringResource(if (entrando) R.string.conta_entrar else R.string.conta_criar)
            Box(
                Modifier.fillMaxWidth().heightIn(min = 52.dp).clip(RoundedCornerShape(14.dp))
                    .background(if (conta.ocupado) AureaColors.AccentDim else AureaColors.Accent)
                    .testTag("conta.enviar").semantics { contentDescription = rotuloBotao }
                    .tocavel(enabled = !conta.ocupado, role = Role.Button) { enviar() },
                contentAlignment = Alignment.Center,
            ) {
                if (conta.ocupado) CircularProgressIndicator(Modifier.size(22.dp), color = AureaColors.Text, strokeWidth = 2.dp)
                else Text(rotuloBotao, style = AureaType.Button)
            }
            Spacer(Modifier.height(8.dp))
            Text(
                stringResource(if (entrando) R.string.conta_ir_cadastro else R.string.conta_ir_entrar),
                style = AureaType.LinkRow.copy(color = AureaColors.Accent, fontWeight = FontWeight.SemiBold),
                modifier = Modifier.clip(RoundedCornerShape(10.dp)).testTag("conta.alternar")
                    .tocavel(enabled = !conta.ocupado) { conta.entrando = !entrando; conta.erro = null }
                    .padding(horizontal = 12.dp, vertical = 12.dp),
            )
            Spacer(Modifier.height(24.dp))
            Text(stringResource(R.string.conta_privacidade), style = AureaType.Footer, textAlign = TextAlign.Center)
        }
    }
}

internal fun textoDoErro(e: ErroConta): Int = when (e) {
    ErroConta.EMAIL -> R.string.conta_erro_email
    ErroConta.SENHA -> R.string.conta_erro_senha
    ErroConta.CREDENCIAIS -> R.string.conta_erro_credenciais
    ErroConta.EM_USO -> R.string.conta_erro_em_uso
    ErroConta.LIMITE -> R.string.conta_erro_limite
    ErroConta.REDE -> R.string.conta_erro_rede
    ErroConta.SERVICO -> R.string.conta_erro_servico
    ErroConta.EXPIRADA -> R.string.conta_erro_expirada
}

@Composable
private fun CampoConta(
    valor: String,
    aoMudar: (String) -> Unit,
    rotulo: String,
    tag: String,
    teclado: KeyboardOptions,
    acoes: KeyboardActions,
    transformacao: VisualTransformation = VisualTransformation.None,
    direita: (@Composable () -> Unit)? = null,
) {
    Column(Modifier.fillMaxWidth()) {
        Text(rotulo, style = AureaType.BodySmall, modifier = Modifier.padding(start = 4.dp, bottom = 6.dp))
        Row(
            Modifier.fillMaxWidth().heightIn(min = 50.dp).clip(RoundedCornerShape(12.dp))
                .background(AureaColors.SurfaceHigh).border(1.dp, AureaColors.Border, RoundedCornerShape(12.dp))
                .padding(start = 14.dp, end = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            BasicTextField(
                value = valor, onValueChange = aoMudar, singleLine = true,
                textStyle = AureaType.SearchText, cursorBrush = SolidColor(AureaColors.Accent),
                keyboardOptions = teclado, keyboardActions = acoes, visualTransformation = transformacao,
                modifier = Modifier.weight(1f).padding(vertical = 12.dp).testTag(tag).semantics { contentDescription = rotulo },
            )
            direita?.invoke()
        }
    }
}
