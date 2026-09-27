package com.aurea.aurea.conta

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ContaLogicaTest {
    @Test fun semSessaoGuardadaOAppMostraSoAContaComSessaoAbreDireto() {
        assertEquals(ContaEstado.Fora, ContaLogica.inicial(null))
        assertEquals(ContaEstado.Fora, ContaLogica.inicial(SessaoGuardada("", "a@b.co")))
        assertEquals(ContaEstado.Fora, ContaLogica.inicial(SessaoGuardada("t".repeat(43), " ")))
        assertEquals(ContaEstado.Dentro("a@aurea.app"), ContaLogica.inicial(SessaoGuardada("t".repeat(43), "a@aurea.app")))
    }

    @Test fun emailNormalizadoIgualAoWorker() {
        assertEquals("ana.silva+aurea@exemplo.com.br", ContaLogica.normalizarEmail("  Ana.Silva+aurea@Exemplo.COM.br "))
        for (ruim in listOf("", "ana", "ana@", "@x.com", "a@b", "a@@b.com", "a b@x.com", "a@x.c", ".a@x.com", "a..b@x.com", "a@-x.com")) {
            assertNull(ruim, ContaLogica.normalizarEmail(ruim))
        }
    }

    @Test fun senhaDe8a128PontosDeCodigo() {
        assertFalse(ContaLogica.senhaValida("1234567"))
        assertTrue(ContaLogica.senhaValida("12345678"))
        assertTrue(ContaLogica.senhaValida("x".repeat(128)))
        assertFalse(ContaLogica.senhaValida("x".repeat(129)))
        assertFalse("7 emojis = 7 caracteres, nao 14", ContaLogica.senhaValida("🔒".repeat(7)))
        assertTrue(ContaLogica.senhaValida("🔒".repeat(8)))
        assertEquals(ErroConta.EMAIL, ContaLogica.validar("nao-e-email", "1"))
        assertEquals(ErroConta.SENHA, ContaLogica.validar("a@aurea.app", "1"))
        assertNull(ContaLogica.validar(" A@Aurea.app ", "12345678"))
    }

    @Test fun errosDoServidorViramMensagensGenericas() {
        assertEquals(ErroConta.LIMITE, ContaLogica.erroDoServidor(429, "muitas_tentativas"))
        assertEquals(ErroConta.EM_USO, ContaLogica.erroDoServidor(409, "email_em_uso"))
        assertEquals(ErroConta.CREDENCIAIS, ContaLogica.erroDoServidor(401, "credenciais_invalidas"))
        assertEquals(ErroConta.EMAIL, ContaLogica.erroDoServidor(400, "email_invalido"))
        assertEquals(ErroConta.SENHA, ContaLogica.erroDoServidor(400, "senha_curta"))
        assertEquals(ErroConta.SERVICO, ContaLogica.erroDoServidor(503, "contas_indisponiveis"))
        assertEquals(ErroConta.SERVICO, ContaLogica.erroDoServidor(500, null))
    }

    @Test fun soUm401DerrubaQuemJaEntrouOfflineSegueDentro() {
        val dentro = ContaEstado.Dentro("a@aurea.app")
        assertEquals(ContaEstado.Fora, ContaLogica.aposRevalidar(dentro, Revalidacao.INVALIDA))
        assertEquals(dentro, ContaLogica.aposRevalidar(dentro, Revalidacao.SEM_RESPOSTA))
        assertEquals(dentro, ContaLogica.aposRevalidar(dentro, Revalidacao.VALIDA))
        assertEquals(ContaEstado.Fora, ContaLogica.aposRevalidar(ContaEstado.Fora, Revalidacao.VALIDA))

        fun r(status: Int) = ContaApi.Resposta(status, JSONObject())
        assertEquals(Revalidacao.VALIDA, ContaApi.revalidacao(r(200)))
        assertEquals(Revalidacao.INVALIDA, ContaApi.revalidacao(r(401)))
        for (s in listOf(0, 429, 500, 503)) assertEquals(Revalidacao.SEM_RESPOSTA, ContaApi.revalidacao(r(s)))
        assertEquals("muitas_tentativas", ContaApi.Resposta(429, JSONObject().put("error", "muitas_tentativas")).codigo)
    }

    @Test fun revalidaNaAberturaEDepoisACada6Horas() {
        assertTrue(ContaLogica.precisaRevalidar(0L, 1_000L))
        assertFalse(ContaLogica.precisaRevalidar(1_000L, 1_000L + 5 * 3600_000L))
        assertTrue(ContaLogica.precisaRevalidar(1_000L, 1_000L + 6 * 3600_000L))
    }
}
