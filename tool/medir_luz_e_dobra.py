# -*- coding: utf-8 -*-
"""Le os renders do AE e tira os numeros que o efeito precisa.

Duas medidas, uma por efeito:

  * CC LIGHT SWEEP — onde a faixa esta, qual a largura dela em funcao do
    parametro Width, e quanto ela SOMA em cima da cor de origem. A
    pergunta que decide a implementacao: a luz e uma silhueta branca
    somada, ou uma luz colorida que tinge o conteudo?
  * CC PAGE TURN — o raio da dobra, o quanto a pagina rola, e onde a
    aba termina. A dobra e um cilindro, e o que se mede aqui e a relacao
    entre "Fold Radius" e o raio em pixels da imagem.
"""
import math
import os

from PIL import Image

PASTA = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                     'build', 'qa', 'ae-novos')


def abrir(nome):
    return Image.open(os.path.join(PASTA, nome)).convert('RGB')


def perfil_horizontal(im, y):
    return [im.getpixel((x, y)) for x in range(im.width)]


def diferenca(a, b, y):
    la, lb = perfil_horizontal(a, y), perfil_horizontal(b, y)
    return [max(abs(p[i] - q[i]) for i in range(3)) for p, q in zip(la, lb)]


def onde_comeca(lista, corte=4):
    for i, v in enumerate(lista):
        if v > corte:
            return i
    return None


def linhas_de_luz(im, base, y):
    """As colunas em que a luz somou mais que 4 niveis, naquela linha."""
    d = diferenca(im, base, y)
    dentro = [i for i, v in enumerate(d) if v > 4]
    if not dentro:
        return None
    return dentro[0], dentro[-1], max(d)


def relatar_luz():
    base = abrir('R3_src.png')
    print('== CC LIGHT SWEEP ==')
    for nome, rotulo in [('R3_ls_a.png', 'padrao (Center 100,50 / Dir -30 / Width 50)'),
                         ('R3_ls_b.png', 'Center 60,100 / Dir 0'),
                         ('R3_ls_c.png', 'Width 100 / Sweep 60'),
                         ('R3_ls_d.png', 'Shape 1 / Edge 100')]:
        try:
            im = abrir(nome)
        except Exception as e:
            print('  %s: %s' % (nome, e))
            continue
        for y in (50, 100, 150):
            r = linhas_de_luz(im, base, y)
            if r:
                a, b, pico = r
                print('  %-12s y=%3d  faixa %3d..%3d  largura %3d  pico +%d'
                      % (rotulo, y, a, b, b - a + 1, pico))
                break
        # a diferenca por canal no pico: diz se a luz e branca (soma igual
        # nos tres) ou colorida (soma diferente).
        d = diferenca(im, base, 100)
        i = d.index(max(d))
        p0, p1 = base.getpixel((i, 100)), im.getpixel((i, 100))
        print('      no pixel mais claro (x=%d): origem %s -> luz %s  (delta R%d G%d B%d)'
              % (i, p0, p1, p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]))


def ajuste_de_forma(nome, cx, cy, direcao, largura, folga=80):
    """Qual curva cai como o render do AE, e em que extensao.

    O pico e a semi-extensao sao ajustados por minimos quadrados para
    cada familia de curva, e o que decide e o erro medio. Cuidado com as
    22 primeiras linhas: os quatro renders do Light Sweep trazem um
    borrao claro na borda de CIMA (delta de ate 224) que nao acompanha
    nenhum parametro do efeito. Ele nao e da faixa, e entra no ajuste
    como se fosse.
    """
    base = abrir('R3_src.png')
    im = abrir(nome)
    a = math.radians(direcao)
    nx, ny = math.cos(a), math.sin(a)
    dados = []
    for yy in range(22, im.height):
        for xx in range(im.width):
            p0, p1 = base.getpixel((xx, yy)), im.getpixel((xx, yy))
            # ONDE A SOMA ESTOURA EM 255 A MEDIDA MENTE: so entra pixel
            # com folga para toda a luz que o render chegou a somar.
            if max(p0) > 255 - folga:
                continue
            delta = p1[0] - p0[0]
            if delta < 0:
                continue
            d = abs((xx + 0.5 - cx) * nx + (yy + 0.5 - cy) * ny)
            dados.append((d, float(delta)))
    formas = {
        'reta': lambda t: max(0.0, 1.0 - t),
        'quadratica': lambda t: max(0.0, 1.0 - t) ** 2,
        'pot 1,5': lambda t: max(0.0, 1.0 - t) ** 1.5,
        'cosseno': lambda t: 0.5 * (1 + math.cos(math.pi * t)) if t < 1 else 0.0,
        'smoothstep': lambda t: (1 - 3 * t * t + 2 * t ** 3) if t < 1 else 0.0,
    }
    print('  -- %s  (%d pixels)' % (nome, len(dados)))
    for rotulo, f in formas.items():
        melhor = None
        for i in range(1, 600):
            semi = i * 0.5
            num = den = 0.0
            for d, v in dados:
                g = f(d / semi)
                num += g * v
                den += g * g
            if den == 0:
                continue
            pico = num / den
            erro = 0.0
            for d, v in dados:
                e = pico * f(d / semi) - v
                erro += e * e
            erro = math.sqrt(erro / len(dados))
            if melhor is None or erro < melhor[0]:
                melhor = (erro, pico, semi)
        print('     %-11s erro %5.2f niveis  pico %5.1f  semi %5.1f px  '
              '(semi/Largura %.2f)'
              % (rotulo, melhor[0], melhor[1], melhor[2],
                 melhor[2] / float(largura)))


def confere_modelo(nome, cx, cy, direcao, largura, intensidade):
    """O modelo `255*Int*(1-d/(2*Largura))^2` contra o render, em faixas.

    So entram pixels com folga para toda a luz somada: onde a soma bate no
    teto de 255 o valor medido nao e o valor da luz.
    """
    base = abrir('R3_src.png')
    im = abrir(nome)
    teto = 255.0 * intensidade
    a = math.radians(direcao)
    nx, ny = math.cos(a), math.sin(a)
    meias = {}
    for yy in range(22, im.height):
        for xx in range(im.width):
            p0, p1 = base.getpixel((xx, yy)), im.getpixel((xx, yy))
            if max(p0) > 255 - teto:
                continue
            delta = p1[0] - p0[0]
            if delta < 0:
                continue
            d = abs((xx + 0.5 - cx) * nx + (yy + 0.5 - cy) * ny)
            meias.setdefault(int(d // 20) * 20, []).append(float(delta))
    print('  -- %s  Intensidade %.0f%%  Largura %d  (modelo: pico %.1f, '
          'semi %d px)' % (nome, intensidade * 100, largura, teto, largura * 2))
    erro = 0.0
    n = 0
    for k in sorted(meias):
        v = meias[k]
        if len(v) < 200:
            continue
        d = k + 10.0
        modelo = teto * max(0.0, 1.0 - d / (largura * 2.0)) ** 2
        medido = sum(v) / len(v)
        erro += abs(medido - modelo)
        n += 1
        print('     d=%3d..%3d  medido %6.1f  modelo %6.1f  diferenca %5.1f'
              % (k, k + 19, medido, modelo, medido - modelo))
    if n:
        print('     erro medio do modelo: %.1f niveis em %d faixas'
              % (erro / n, n))


def relatar_dobra():
    base = abrir('R3_src.png')
    print()
    print('== CC PAGE TURN ==')
    for nome, rotulo in [('R3_pt_a.png', 'padrao: Fold(150,100) Dir -60 Raio 50'),
                         ('R3_pt_b.png', 'Fold(110,100) Dir -60 Raio 50'),
                         ('R3_pt_c.png', 'Fold(150,100) Dir -60 Raio 10'),
                         ('R3_pt_d.png', 'Fold(100,150) Dir 0 Raio 50')]:
        try:
            im = abrir(nome)
        except Exception as e:
            print('  %s: %s' % (nome, e))
            continue
        print('  -- %s' % rotulo)
        for y in (25, 50, 75, 100, 125, 150, 175):
            linhas = diferenca(im, base, y)
            mudou = [i for i, v in enumerate(linhas) if v > 6]
            if not mudou:
                print('     y=%3d  intacta' % y)
                continue
            print('     y=%3d  muda de x=%3d ate x=%3d (%d px)'
                  % (y, mudou[0], mudou[-1], len(mudou)))
        # o brilho especular da dobra: o pixel mais claro do quadro
        claro, onde = 0, None
        for y in range(0, im.height, 2):
            for x in range(0, im.width, 2):
                p = im.getpixel((x, y))
                l = sum(p)
                if l > claro:
                    claro, onde = l, (x, y, p)
        print('     mais claro do quadro: %s soma %d' % (onde, claro))


if __name__ == '__main__':
    relatar_luz()
    print()
    print('== CC LIGHT SWEEP, a forma da queda ==')
    ajuste_de_forma('R3_ls_a.png', 100, 50, -30, 50)
    ajuste_de_forma('R3_ls_b.png', 60, 100, 0, 50)
    # O RENDER DE 60% NAO SERVE PARA AJUSTAR A FORMA: somando ate 153
    # niveis sobram poucos pixels sem corte, e o pouco que sobra esta so
    # na cauda, onde a luz ja e fraca. Ele serve para OUTRA coisa —
    # conferir o modelo ja ajustado com outro Width e outra Intensidade.
    confere_modelo('R3_ls_c.png', 100, 100, -30, largura=100, intensidade=0.60)
    relatar_dobra()
