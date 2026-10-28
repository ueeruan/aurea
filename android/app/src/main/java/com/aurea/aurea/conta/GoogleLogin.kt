package com.aurea.aurea.conta

import android.app.Activity
import android.content.Context
import androidx.credentials.CredentialManager
import androidx.credentials.CustomCredential
import androidx.credentials.GetCredentialRequest
import androidx.credentials.exceptions.GetCredentialCancellationException
import com.aurea.aurea.BuildConfig
import com.google.android.libraries.identity.googleid.GetSignInWithGoogleOption
import com.google.android.libraries.identity.googleid.GoogleIdTokenCredential
import com.google.android.gms.tasks.Task
import com.google.firebase.FirebaseApp
import com.google.firebase.FirebaseOptions
import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.auth.GoogleAuthProvider
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

// =============================================================================
//  0.4.1: login com Google pelo Firebase Auth.
//
//  Credential Manager (folha "Entrar com o Google") → ID token do Google →
//  Firebase Auth (signInWithCredential) → ID token do FIREBASE, que vai para o
//  servidor do Aurea (POST /api/auth/firebase, discovery/contas.js). O servidor
//  confere o token e abre a conta Aurea do mesmo e-mail — a sessão continua a do
//  Aurea, então Hyper, Comunidade e e-mail/senha seguem iguais.
//
//  O Firebase é iniciado aqui com as opções do BuildConfig (android/firebase.properties),
//  sem o plugin google-services: o mesmo app serve aos pacotes da Play
//  (com.aurea.editor) e do APK direto (com.aurea.aurea). Par do iOS: GoogleLogin.swift.
// =============================================================================

object GoogleLogin {
    /** Sem configuração do Firebase o botão do Google não aparece. */
    val disponivel: Boolean
        get() = BuildConfig.FIREBASE_PROJECT_ID.isNotEmpty() && BuildConfig.FIREBASE_APP_ID.isNotEmpty() &&
            BuildConfig.FIREBASE_API_KEY.isNotEmpty() && BuildConfig.FIREBASE_WEB_CLIENT_ID.isNotEmpty()

    private const val NOME = "aurea"

    private fun app(context: Context): FirebaseApp =
        FirebaseApp.getApps(context).firstOrNull { it.name == NOME } ?: FirebaseApp.initializeApp(
            context.applicationContext,
            FirebaseOptions.Builder()
                .setProjectId(BuildConfig.FIREBASE_PROJECT_ID)
                .setApplicationId(BuildConfig.FIREBASE_APP_ID)
                .setApiKey(BuildConfig.FIREBASE_API_KEY)
                .build(),
            NOME,
        )

    /** O resultado da folha do Google: o token do Firebase, cancelado ou erro. */
    sealed interface Resultado {
        data class Token(val idToken: String) : Resultado
        data object Cancelado : Resultado
        data class Falhou(val motivo: String) : Resultado
    }

    suspend fun entrar(activity: Activity): Resultado = try {
        val opcao = GetSignInWithGoogleOption.Builder(BuildConfig.FIREBASE_WEB_CLIENT_ID).build()
        val pedido = GetCredentialRequest.Builder().addCredentialOption(opcao).build()
        val credencial = CredentialManager.create(activity).getCredential(activity, pedido).credential
        if (credencial !is CustomCredential || credencial.type != GoogleIdTokenCredential.TYPE_GOOGLE_ID_TOKEN_CREDENTIAL) {
            Resultado.Falhou("credencial")
        } else {
            val google = GoogleIdTokenCredential.createFrom(credencial.data)
            val auth = FirebaseAuth.getInstance(app(activity))
            val usuario = auth.signInWithCredential(GoogleAuthProvider.getCredential(google.idToken, null)).aguardar().user
            val token = usuario?.getIdToken(true)?.aguardar()?.token
            // A sessão que vale é a do Aurea: o Firebase só serviu para provar o e-mail.
            auth.signOut()
            if (token.isNullOrEmpty()) Resultado.Falhou("token") else Resultado.Token(token)
        }
    } catch (_: GetCredentialCancellationException) {
        Resultado.Cancelado
    } catch (e: Exception) {
        Resultado.Falhou(e.javaClass.simpleName)
    }

    private suspend fun <T> Task<T>.aguardar(): T = suspendCancellableCoroutine { c ->
        addOnSuccessListener { c.resume(it) }
        addOnFailureListener { c.resumeWithException(it) }
        addOnCanceledListener { c.cancel() }
    }
}
