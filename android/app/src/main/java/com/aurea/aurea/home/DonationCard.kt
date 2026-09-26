package com.aurea.aurea.home

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.widget.Toast
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType

internal object AureaDonations {
    var launchPromptPending by mutableStateOf(true)
    const val PIX = "00020126360014br.gov.bcb.pix0114+55889961267175204000053039865802BR5911Ruan  Pablo6009Sao Paulo62240520daqr16872346348818576304A1F0"
    const val PAYPAL = "https://www.paypal.com/donate/?business=C7C2A2UH88NGW&no_recurring=0&item_name=Manter+o+Aurea+APP+funcionando+de+gra%C3%A7a.&currency_code=BRL"
}

/** Optional, user-opened support sheet. Never leaves an active export for a browser. */
@Composable
internal fun DonationCard(exporting: Boolean = false) {
    var open by remember { mutableStateOf(false) }
    var copied by remember { mutableStateOf(false) }
    val context = LocalContext.current
    fun copy(value: String) {
        (context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager)
            .setPrimaryClip(ClipData.newPlainText("Aurea", value))
        copied = true
    }
    Row(Modifier.fillMaxWidth().padding(vertical = 12.dp)
        .background(AureaColors.Surface, RoundedCornerShape(16.dp))
        .clickable { copied = false; open = true }.padding(16.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(stringResource(R.string.donation_title), style = AureaType.TitleMedium)
            Text(stringResource(R.string.donation_note), style = AureaType.BodySmall, color = AureaColors.Muted)
        }
        Text("♡", style = AureaType.HeadlineLarge, color = AureaColors.Accent)
    }
    if (open) AlertDialog(
        onDismissRequest = { open = false },
        containerColor = AureaColors.Surface,
        title = { Text(stringResource(R.string.donation_title)) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text(stringResource(R.string.donation_detail))
                Image(painterResource(R.drawable.donation_pix_qr), stringResource(R.string.donation_pix_qr),
                    Modifier.sizeIn(maxWidth = 240.dp, maxHeight = 240.dp).aspectRatio(1f))
                Text("Pix · Ruan Pablo", color = AureaColors.Muted)
                OutlinedButton(onClick = { copy(AureaDonations.PIX) }, modifier = Modifier.fillMaxWidth()) {
                    Text(stringResource(R.string.donation_copy_pix))
                }
                OutlinedButton(onClick = {
                    if (exporting) copy(AureaDonations.PAYPAL)
                    else try {
                        context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(AureaDonations.PAYPAL)))
                    } catch (_: android.content.ActivityNotFoundException) {
                        copy(AureaDonations.PAYPAL)
                        Toast.makeText(context, R.string.donation_copied, Toast.LENGTH_SHORT).show()
                    }
                }, modifier = Modifier.fillMaxWidth()) {
                    Text(stringResource(if (exporting) R.string.donation_copy_paypal else R.string.donation_paypal))
                }
                if (exporting) Text(stringResource(R.string.donation_export_note), color = AureaColors.Muted)
                if (copied) Text(stringResource(R.string.donation_copied), color = AureaColors.Accent)
            }
        },
        confirmButton = { TextButton(onClick = { open = false }) { Text(stringResource(R.string.editor_fechar)) } },
    )
}
