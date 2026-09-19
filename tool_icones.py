"""Gera TODOS os icones do Aurea a partir da logo nova, e so dela.

Uma fonte, uma conta. O que sai daqui:

  assets/icon/app_icon.png               1024, quadrado cheio (o mestre)
  assets/icon/app_icon_foreground.png    1024, so a marca, fundo vazado
  assets/icon/app_icon_monochrome.png    1024, silhueta branca vazada
  android/.../mipmap-*/ic_launcher.png   48..192, quadrado cheio
  android/.../drawable-*/ic_launcher_foreground.png   108dp de area, 66 safe
  android/.../drawable-*/ic_launcher_monochrome.png
  ios/Runner/Assets.xcassets/AppIcon.appiconset/*.png
  android/.../drawable-*/splash_logo.png  a marca para a tela de abertura
  ios/Runner/Assets.xcassets/LaunchImage.imageset/*.png

A REGRA DE ENQUADRAMENTO E UMA SO para todas as saidas: a marca ocupa a
mesma fracao do lado em todas. Sem isso o icone do Android sai maior que o
do iOS, e o mesmo app parece dois apps dependendo do aparelho.
"""
import io
import json
import os
from PIL import Image, ImageDraw

FONTE = (r'C:\Users\SnyX\AppData\Local\Temp\claude\C--Users-SnyX-Documents-Projetos'
         r'---Claude-Aurea\9e62e731-80d6-456f-9252-ad035dd1644e\images\1.webp')

FUNDO = (0x0F, 0x14, 0x1A)        # AureaColors.bg
BRANCO = (0xFF, 0xFF, 0xFF)

# Quanto do lado a marca ocupa.
CHEIO = 0.68      # icone de app: respira, e o sistema arredonda por cima
ADAPT = 0.50      # foreground do Android: 66/108 do lado garantido
SPLASH = 0.34     # tela de abertura
ICONE_IOS = 0.72


def marca():
    """A marca recortada, com alfa, do arquivo entregue.

    O FUNDO E ACHADO POR INUNDACAO A PARTIR DA BORDA, e nao por "e azul?".

    A primeira versao perguntava pixel a pixel se ele era azul saturado. O
    brilho especular do tubo e quase branco e nao passa nesse teste, entao
    cada reflexo virava um FURO preto no meio da marca — tres deles, visiveis
    a olho nu no icone. Inundar de fora para dentro acerta por construcao:
    o que a tinta alcanca e marca, o resto e fundo, e nao ha como abrir buraco
    no meio de uma peca solida.

    A BORDA SAI LIMPA. O anti-serie do recorte original mistura azul com o
    branco do fundo, e esse branco sobrevive como um halo claro. Depois de
    tirar 1 px da silhueta, o alfa leva um desfoque de 1 px: a borda volta a
    ser suave, agora com a cor da propria marca em vez de branco.
    """
    from PIL import ImageFilter

    im = Image.open(FONTE).convert('RGB')
    w, h = im.size
    px = im.load()

    # 1. o que e FUNDO: claro e neutro. O branco do arquivo e #FEFEFE.
    claro = Image.new('L', (w, h), 0)
    cp = claro.load()
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            if r > 224 and g > 224 and b > 224:
                cp[x, y] = 255

    # 2. inunda de fora para dentro: so o claro LIGADO A BORDA e fundo.
    fora = claro.copy()
    fila = []
    for x in range(w):
        fila.append((x, 0))
        fila.append((x, h - 1))
    for y in range(h):
        fila.append((0, y))
        fila.append((w - 1, y))
    fora_px = fora.load()
    vistos = Image.new('L', (w, h), 0)
    vp = vistos.load()
    while fila:
        x, y = fila.pop()
        if x < 0 or y < 0 or x >= w or y >= h or vp[x, y]:
            continue
        if fora_px[x, y] == 0:
            continue
        vp[x, y] = 255
        fila.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))

    # 3. marca = o que a inundacao NAO alcancou.
    alfa = Image.new('L', (w, h), 255)
    ap = alfa.load()
    for y in range(h):
        for x in range(w):
            if vp[x, y]:
                ap[x, y] = 0

    # 4. come 1 px da borda e devolve a suavidade por desfoque: mata o halo
    #    branco sem serrilhar.
    alfa = alfa.filter(ImageFilter.MinFilter(3)).filter(
        ImageFilter.GaussianBlur(1.0)
    )

    caixa = alfa.getbbox()
    recorte = im.crop(caixa)
    alfa = alfa.crop(caixa)
    saida = Image.new('RGBA', recorte.size)
    saida.paste(recorte, (0, 0))
    saida.putalpha(alfa)
    return saida


def enquadrar(m, lado, fracao):
    """A marca centralizada num quadrado de `lado`, ocupando `fracao` dele."""
    alvo = max(1, int(round(lado * fracao)))
    e = alvo / m.width
    r = m.resize(
        (alvo, max(1, int(round(m.height * e)))), Image.LANCZOS
    )
    q = Image.new('RGBA', (lado, lado), (0, 0, 0, 0))
    q.paste(r, ((lado - r.width) // 2, (lado - r.height) // 2), r)
    return q


def sobre(marca_rgba, fundo, lado):
    q = Image.new('RGB', (lado, lado), fundo)
    q.paste(marca_rgba, (0, 0), marca_rgba)
    return q


def silhueta(marca_rgba, cor, lado):
    q = Image.new('RGBA', (lado, lado), (0, 0, 0, 0))
    tinta = Image.new('RGBA', (lado, lado), cor + (255,))
    q.paste(tinta, (0, 0), marca_rgba)
    return q


def salvar(im, caminho):
    os.makedirs(os.path.dirname(caminho), exist_ok=True)
    im.save(caminho, 'PNG', optimize=True)
    return caminho


def main():
    m = marca()
    print('marca recortada: %dx%d' % m.size)

    dest = 'assets/icon'
    salvar(sobre(enquadrar(m, 1024, CHEIO), FUNDO, 1024), f'{dest}/app_icon.png')
    salvar(enquadrar(m, 1024, ADAPT), f'{dest}/app_icon_foreground.png')
    salvar(silhueta(enquadrar(m, 1024, ADAPT), BRANCO, 1024),
           f'{dest}/app_icon_monochrome.png')

    # ------------------------------------------------------------- Android
    dens = {'mdpi': 1, 'hdpi': 1.5, 'xhdpi': 2, 'xxhdpi': 3, 'xxxhdpi': 4}
    for d, k in dens.items():
        launcher = int(round(48 * k))
        salvar(sobre(enquadrar(m, launcher, CHEIO), FUNDO, launcher),
               f'android/app/src/main/res/mipmap-{d}/ic_launcher.png')
        # o foreground do adaptativo vive numa area de 108dp; o sistema
        # mostra so os 72 do meio e garante 66. A marca entra em 50/108.
        fg = int(round(108 * k))
        salvar(enquadrar(m, fg, ADAPT),
               f'android/app/src/main/res/drawable-{d}/ic_launcher_foreground.png')
        salvar(silhueta(enquadrar(m, fg, ADAPT), BRANCO, fg),
               f'android/app/src/main/res/drawable-{d}/ic_launcher_monochrome.png')
        splash = int(round(160 * k))
        salvar(enquadrar(m, splash, SPLASH),
               f'android/app/src/main/res/drawable-{d}/splash_logo.png')

    # ---------------------------------------------------------------- iOS
    pasta = 'ios/Runner/Assets.xcassets/AppIcon.appiconset'
    ct = json.load(io.open(f'{pasta}/Contents.json', encoding='utf-8'))
    for item in ct['images']:
        arq = item.get('filename')
        if not arq:
            continue
        lado = int(round(float(item['size'].split('x')[0]) *
                         int(item['scale'].replace('x', ''))))
        salvar(sobre(enquadrar(m, lado, ICONE_IOS), FUNDO, lado),
               f'{pasta}/{arq}')

    pasta = 'ios/Runner/Assets.xcassets/LaunchImage.imageset'
    for arq, lado in (('LaunchImage.png', 320),
                      ('LaunchImage@2x.png', 640),
                      ('LaunchImage@3x.png', 960)):
        salvar(sobre(enquadrar(m, lado, SPLASH), FUNDO, lado),
               f'{pasta}/{arq}')

    print('pronto')


if __name__ == '__main__':
    main()
