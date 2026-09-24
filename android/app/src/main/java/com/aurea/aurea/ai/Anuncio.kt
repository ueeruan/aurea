package com.aurea.aurea.ai

/**
 * O portão de anúncio antes de gerar.
 *
 * A tela tem um botão só, "Assistir e gerar": ele mostra o anúncio e, quando o
 * anúncio termina, a geração começa. Quem decide o que é "mostrar o anúncio" é
 * esta interface — a tela não sabe se por trás tem AdMob, outro provedor ou
 * nada.
 *
 * `disponivel` existe para a tela não prometer o que não tem: sem provedor
 * ligado, o botão diz "Gerar" e a geração sai direto. Assim que um provedor for
 * registrado em `GateDeAnuncio.atual`, o mesmo botão passa a dizer "Assistir e
 * gerar" sem tocar na tela.
 */
interface GateDeAnuncio {

    /** `false` quando não há provedor: a tela não deve falar em anúncio. */
    val disponivel: Boolean

    /**
     * Mostra o anúncio e chama `aoTerminar` quando ele acabar.
     *
     * `aoTerminar(false)` é o caso de o anúncio não ter sido assistido (o
     * usuário fechou antes, ou o provedor falhou). Nesse caso a geração não
     * acontece: o gate é justamente a cobrança.
     *
     * A chamada é sempre assíncrona e sempre chama de volta exatamente uma vez.
     */
    fun exibir(quantidade: Int, aoTerminar: (assistido: Boolean) -> Unit)

    companion object {
        /**
         * O provedor em uso. Trocar isto é o único ponto de integração de um
         * SDK de anúncio.
         */
        @Volatile
        var atual: GateDeAnuncio = SemAnuncio
    }
}

/**
 * Sem provedor ligado: libera na hora.
 *
 * Não é um anúncio falso nem um botão que mente — é a ausência declarada. A
 * tela pergunta `disponivel` e adapta o texto por causa disto.
 */
object SemAnuncio : GateDeAnuncio {
    override val disponivel: Boolean = false
    override fun exibir(quantidade: Int, aoTerminar: (Boolean) -> Unit) = aoTerminar(true)
}
