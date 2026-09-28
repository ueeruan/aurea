"""Acrescenta aos 7 catalogos as frases do pacote do editor antigo.

Roda uma vez. Idempotente: se a chave ja existe, nao mexe.
"""
import io
import os
import re
import sys

RES = "android/app/src/main/res"
LANGS = ["values", "values-en", "values-es", "values-ru", "values-hi", "values-id", "values-ar"]

# chave -> {idioma: texto}. A chave "values" e o pt-BR (padrao).
S = {
    # --- nomes dos efeitos -------------------------------------------------
    "fx_name_fill": {
        "values": "Preencher", "values-en": "Fill", "values-es": "Rellenar", "values-ru": "Заливка",
        "values-hi": "भरें", "values-id": "Isi", "values-ar": "تعبئة",
    },
    "fx_name_color_balance_hls": {
        "values": "Equilíbrio de cor (HLS)", "values-en": "Color Balance (HLS)",
        "values-es": "Equilibrio de color (HLS)", "values-ru": "Цветовой баланс (HLS)",
        "values-hi": "रंग संतुलन (HLS)", "values-id": "Keseimbangan warna (HLS)",
        "values-ar": "توازن اللون (HLS)",
    },
    "fx_name_zoom_blur": {
        "values": "Desfoque de zoom", "values-en": "Zoom Blur", "values-es": "Desenfoque de zoom",
        "values-ru": "Размытие масштаба", "values-hi": "ज़ूम ब्लर", "values-id": "Blur zoom",
        "values-ar": "ضبابية التقريب",
    },
    "fx_name_bulge": {
        "values": "Bojo", "values-en": "Bulge", "values-es": "Abultamiento", "values-ru": "Выпуклость",
        "values-hi": "उभार", "values-id": "Cembung", "values-ar": "انتفاخ",
    },
    "fx_name_checkerboard": {
        "values": "Xadrez", "values-en": "Checkerboard", "values-es": "Tablero de ajedrez",
        "values-ru": "Шахматная доска", "values-hi": "शतरंज पट्टिका", "values-id": "Papan catur",
        "values-ar": "رقعة الشطرنج",
    },
    "fx_name_hexagonal": {
        "values": "Matriz hexagonal", "values-en": "Hexagonal Array", "values-es": "Matriz hexagonal",
        "values-ru": "Шестиугольная сетка", "values-hi": "षट्कोणीय जाल", "values-id": "Susunan heksagonal",
        "values-ar": "مصفوفة سداسية",
    },
    "fx_name_drop_shadow": {
        "values": "Sombra projetada", "values-en": "Drop Shadow", "values-es": "Sombra paralela",
        "values-ru": "Отбрасываемая тень", "values-hi": "ड्रॉप शैडो", "values-id": "Bayangan jatuh",
        "values-ar": "ظل مُسقط",
    },
    "fx_name_border": {
        "values": "Borda", "values-en": "Border", "values-es": "Borde", "values-ru": "Обрамление",
        "values-hi": "किनारा", "values-id": "Tepi", "values-ar": "إطار",
    },
    # --- descricoes --------------------------------------------------------
    "fx_desc_color_fill": {
        "values": "Cobre a camada com uma cor chapada. A opacidade é o quanto a tinta cobre a imagem, e o que era transparente continua transparente.",
        "values-en": "Covers the layer with a flat colour. Opacity is how much the paint covers the image, and whatever was transparent stays transparent.",
        "values-es": "Cubre la capa con un color plano. La opacidad es cuánto cubre la pintura, y lo que era transparente sigue transparente.",
        "values-ru": "Заливает слой однотонным цветом. Непрозрачность — это насколько краска перекрывает изображение, а прозрачные области остаются прозрачными.",
        "values-hi": "लेयर को एक ठोस रंग से भरता है। अपारदर्शिता बताती है कि रंग कितना ढकता है, और जो पारदर्शी था वह पारदर्शी रहता है।",
        "values-id": "Menutup lapisan dengan warna rata. Opasitas menentukan seberapa banyak cat menutup gambar, dan bagian yang transparan tetap transparan.",
        "values-ar": "يملأ الطبقة بلون واحد. تحدد العتامة مقدار تغطية اللون للصورة، وما كان شفافاً يبقى شفافاً.",
    },
    "fx_desc_color_balance_hls": {
        "values": "Gira o matiz, clareia até o branco ou escurece até o preto e ajusta a saturação, tudo num controle só.",
        "values-en": "Rotates hue, lightens toward white or darkens toward black and adjusts saturation, all in one control.",
        "values-es": "Gira el matiz, aclara hacia el blanco o oscurece hacia el negro y ajusta la saturación, todo en un solo control.",
        "values-ru": "Поворачивает оттенок, осветляет к белому или затемняет к чёрному и настраивает насыщенность — всё в одном элементе.",
        "values-hi": "रंग घुमाता है, सफ़ेद की ओर हल्का या काले की ओर गहरा करता है और संतृप्ति समायोजित करता है, सब एक ही नियंत्रण में।",
        "values-id": "Memutar hue, mencerahkan ke putih atau menggelapkan ke hitam, dan mengatur saturasi, semuanya dalam satu kontrol.",
        "values-ar": "يدوّر درجة اللون، ويفتّح نحو الأبيض أو يغمّق نحو الأسود، ويضبط التشبع، كل ذلك في عنصر تحكم واحد.",
    },
    "fx_desc_blur_zoom": {
        "values": "Rastro radial: as bordas correm mais que o centro, como ao empurrar a lente. A intensidade é a fração da distância até o centro.",
        "values-en": "Radial streak: the edges travel further than the centre, as when you push the lens. The amount is the fraction of the distance to the centre.",
        "values-es": "Estela radial: los bordes recorren más que el centro, como al empujar la lente. La intensidad es la fracción de la distancia al centro.",
        "values-ru": "Радиальный шлейф: края проходят больше, чем центр, как при наезде объектива. Интенсивность — доля расстояния до центра.",
        "values-hi": "रेडियल लकीर: किनारे केंद्र से ज़्यादा चलते हैं, जैसे लेंस धकेलने पर। तीव्रता केंद्र तक की दूरी का अनुपात है।",
        "values-id": "Jejak radial: tepi bergerak lebih jauh daripada pusat, seperti saat mendorong lensa. Intensitas adalah sebagian dari jarak ke pusat.",
        "values-ar": "خط شعاعي: تتحرك الحواف أكثر من المركز، كما عند دفع العدسة. الشدة هي نسبة المسافة إلى المركز.",
    },
    "fx_desc_distort_bulge": {
        "values": "Estufa o meio da imagem para fora ou puxa para dentro dentro de um raio. Fora do raio nada muda.",
        "values-en": "Pushes the middle of the image out, or pulls it in, inside a radius. Outside the radius nothing changes.",
        "values-es": "Empuja el centro de la imagen hacia fuera o hacia dentro dentro de un radio. Fuera del radio nada cambia.",
        "values-ru": "Выпучивает середину изображения наружу или втягивает внутрь в пределах радиуса. За радиусом ничего не меняется.",
        "values-hi": "छवि के बीच को बाहर धकेलता है या अंदर खींचता है, एक त्रिज्या के भीतर। त्रिज्या के बाहर कुछ नहीं बदलता।",
        "values-id": "Mendorong bagian tengah gambar keluar atau menariknya masuk di dalam radius. Di luar radius tidak ada yang berubah.",
        "values-ar": "يدفع وسط الصورة للخارج أو يسحبه للداخل داخل نصف قطر، وخارجه لا يتغير شيء.",
    },
    "fx_desc_pattern_checkerboard": {
        "values": "Células alternadas pintadas sobre a camada, com tamanho, âncora, rotação e suavidade da borda ajustáveis.",
        "values-en": "Alternating cells painted over the layer, with adjustable size, anchor, rotation and edge softness.",
        "values-es": "Celdas alternas pintadas sobre la capa, con tamaño, ancla, rotación y suavidad de borde ajustables.",
        "values-ru": "Чередующиеся клетки поверх слоя — с настраиваемым размером, привязкой, поворотом и мягкостью края.",
        "values-hi": "लेयर पर बारी-बारी रंगी कोशिकाएँ, समायोज्य आकार, एंकर, घुमाव और किनारे की कोमलता के साथ।",
        "values-id": "Sel bergantian di atas lapisan, dengan ukuran, jangkar, rotasi, dan kelembutan tepi yang dapat diatur.",
        "values-ar": "خلايا متبادلة فوق الطبقة، بحجم ومرساة ودوران ونعومة حواف قابلة للضبط.",
    },
    "fx_desc_pattern_hexagonal": {
        "values": "A malha de favos como contorno das células: painel de LED hexagonal, telhado de vidro.",
        "values-en": "The honeycomb mesh as cell outlines: a hexagonal LED panel, a glass roof.",
        "values-es": "La malla de panal como contorno de las celdas: panel de LED hexagonal, techo de vidrio.",
        "values-ru": "Сотовая сетка как контуры ячеек: шестиугольная LED-панель, стеклянная крыша.",
        "values-hi": "कोशिकाओं की रूपरेखा के रूप में मधुकोश जाल: षट्कोणीय LED पैनल, काँच की छत।",
        "values-id": "Jaring sarang lebah sebagai garis tepi sel: panel LED heksagonal, atap kaca.",
        "values-ar": "شبكة قرص العسل كحدود للخلايا: لوح LED سداسي، سقف زجاجي.",
    },
    "fx_desc_stylize_drop_shadow": {
        "values": "A silhueta da camada deslocada, borrada e pintada atrás dela. Separa texto e forma do fundo.",
        "values-en": "The silhouette of the layer, offset, blurred and painted behind it. Separates text and shapes from the background.",
        "values-es": "La silueta de la capa, desplazada, desenfocada y pintada detrás de ella. Separa texto y formas del fondo.",
        "values-ru": "Силуэт слоя, смещённый, размытый и нарисованный позади него. Отделяет текст и фигуры от фона.",
        "values-hi": "लेयर की आकृति, खिसकी हुई, धुँधली और उसके पीछे रंगी हुई। टेक्स्ट और आकार को पृष्ठभूमि से अलग करती है।",
        "values-id": "Siluet lapisan, digeser, diburamkan, dan dilukis di belakangnya. Memisahkan teks dan bentuk dari latar.",
        "values-ar": "ظل الطبقة، مُزاحاً ومضبباً ومرسوماً خلفها. يفصل النص والأشكال عن الخلفية.",
    },
    "fx_desc_stylize_border": {
        "values": "Um contorno sólido em volta da silhueta da camada, com cor e largura em pixels.",
        "values-en": "A solid outline around the silhouette of the layer, with colour and width in pixels.",
        "values-es": "Un contorno sólido alrededor de la silueta de la capa, con color y ancho en píxeles.",
        "values-ru": "Сплошной контур вокруг силуэта слоя, с цветом и шириной в пикселях.",
        "values-hi": "लेयर की आकृति के चारों ओर ठोस रूपरेखा, रंग और पिक्सेल में चौड़ाई के साथ।",
        "values-id": "Garis tepi penuh di sekeliling siluet lapisan, dengan warna dan lebar dalam piksel.",
        "values-ar": "حدود صلبة حول ظل الطبقة، بلون وعرض بالبكسل.",
    },
    # --- rotulos de parametro reusados ------------------------------------
    "fx_ancora": {
        "values": "Âncora", "values-en": "Anchor", "values-es": "Ancla", "values-ru": "Привязка",
        "values-hi": "एंकर", "values-id": "Jangkar", "values-ar": "مرساة",
    },
    "fx_distancia": {
        "values": "Distância", "values-en": "Distance", "values-es": "Distancia",
        "values-ru": "Расстояние", "values-hi": "दूरी", "values-id": "Jarak", "values-ar": "مسافة",
    },
    "fx_largura_celula": {
        "values": "Largura da célula", "values-en": "Cell width", "values-es": "Ancho de celda",
        "values-ru": "Ширина ячейки", "values-hi": "कोशिका की चौड़ाई", "values-id": "Lebar sel",
        "values-ar": "عرض الخلية",
    },
    "fx_altura_celula": {
        "values": "Altura da célula", "values-en": "Cell height", "values-es": "Alto de celda",
        "values-ru": "Высота ячейки", "values-hi": "कोशिका की ऊँचाई", "values-id": "Tinggi sel",
        "values-ar": "ارتفاع الخلية",
    },
    "fx_cor_sombra": {
        "values": "Cor da sombra", "values-en": "Shadow colour", "values-es": "Color de sombra",
        "values-ru": "Цвет тени", "values-hi": "शैडो का रंग", "values-id": "Warna bayangan",
        "values-ar": "لون الظل",
    },
    # --- modos de mesclagem novos -----------------------------------------
    "pn_blend_divide": {
        "values": "Divisão", "values-en": "Divide", "values-es": "Dividir", "values-ru": "Деление",
        "values-hi": "विभाजन", "values-id": "Bagi", "values-ar": "قسمة",
    },
    "pn_blend_vivid_light": {
        "values": "Luz intensa", "values-en": "Vivid Light", "values-es": "Luz intensa",
        "values-ru": "Яркий свет", "values-hi": "विविड लाइट", "values-id": "Cahaya vivid",
        "values-ar": "ضوء زاهي",
    },
    "pn_blend_linear_dodge": {
        "values": "Subexposição linear", "values-en": "Linear Dodge", "values-es": "Sobreexposición lineal",
        "values-ru": "Линейное осветление", "values-hi": "लीनियर डॉज", "values-id": "Dodge linear",
        "values-ar": "تفتيح خطي",
    },
    "pn_blend_linear_burn": {
        "values": "Superexposição linear", "values-en": "Linear Burn", "values-es": "Subexposición lineal",
        "values-ru": "Линейное затемнение", "values-hi": "लीनियर बर्न", "values-id": "Burn linear",
        "values-ar": "تعتيم خطي",
    },
}

HEADER = (
    "    <!-- Pacote do editor antigo (2026-09-27): os efeitos e os modos de\n"
    "         mesclagem que so ele tinha. -->\n"
)


def main():
    added_total = 0
    for lang in LANGS:
        path = os.path.join(RES, lang, "strings.xml")
        with io.open(path, encoding="utf-8") as f:
            text = f.read()
        have = set(re.findall(r'<string name="([^"]+)"', text))
        lines = []
        added = 0
        for key, by_lang in S.items():
            if key in have:
                continue
            value = by_lang[lang]
            if "'" in value or "&" in value or "<" in value or ">" in value or "%" in value:
                print("AVISO: caractere a escapar em", key, lang, file=sys.stderr)
                return 1
            lines.append('    <string name="%s">%s</string>' % (key, value))
            added += 1
        if not lines:
            print("%-12s nada a fazer" % lang)
            continue
        block = HEADER + "\n".join(lines) + "\n"
        text = text.replace("</resources>", block + "</resources>", 1)
        with io.open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        added_total += added
        print("%-12s +%d chaves" % (lang, added))
    print("total", added_total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
