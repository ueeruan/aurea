"""Add localized native effect controls while preserving existing translations."""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
path = root / 'tools/effect_i18n_texts.json'
data = json.loads(path.read_text(encoding='utf-8'))
rows = [
    ['Remover camada','Remove layer','Eliminar capa','Удалить слой','लेयर हटाएँ','Hapus lapisan','إزالة الطبقة'],
    ['Revelar camada','Reveal layer','Revelar capa','Показать слой','लेयर दिखाएँ','Tampilkan lapisan','إظهار الطبقة'],
    ['Transição automática','Automatic transition','Transición automática','Автоматический переход','स्वचालित ट्रांज़िशन','Transisi otomatis','انتقال تلقائي'],
    ['Frequência relativa X','Relative X frequency','Frecuencia relativa X','Относительная частота X','सापेक्ष X आवृत्ति','Frekuensi X relatif','التردد النسبي X'],
    ['Morph inicial','Initial morph','Morph inicial','Начальный морфинг','प्रारंभिक मॉर्फ','Morph awal','التحوّل الأولي'],
    ['Velocidade do morph','Morph speed','Velocidad del morph','Скорость морфинга','मॉर्फ की गति','Kecepatan morph','سرعة التحوّل'],
    ['Intensidade das bolhas','Bubble amount','Intensidad de burbujas','Интенсивность пузырьков','बुलबुलों की तीव्रता','Intensitas gelembung','شدة الفقاعات'],
    ['Suavidade das bolhas','Bubble softness','Suavidad de burbujas','Мягкость пузырьков','बुलबुलों की नरमी','Kelembutan gelembung','نعومة الفقاعات'],
    ['Rotação da deformação','Warp rotation','Rotación de deformación','Поворот деформации','विकृति का घुमाव','Rotasi deformasi','دوران التشويه'],
    ['Intensidade do gradiente','Gradient strength','Intensidad del degradado','Интенсивность градиента','ग्रेडिएंट की तीव्रता','Intensitas gradien','شدة التدرج'],
    ['Ângulo do gradiente','Gradient angle','Ángulo del degradado','Угол градиента','ग्रेडिएंट का कोण','Sudut gradien','زاوية التدرج'],
    ['Opacidade da borda','Border opacity','Opacidad del borde','Непрозрачность границы','किनारे की अपारदर्शिता','Opasitas tepi','عتامة الحافة'],
    ['Suavidade da faixa','Band softness','Suavidad de la franja','Мягкость полосы','पट्टी की नरमी','Kelembutan pita','نعومة الشريط'],
    ['Deslocamento da borda','Border shift','Desplazamiento del borde','Смещение границы','किनारे का विस्थापन','Pergeseran tepi','إزاحة الحافة'],
    ['Largura do brilho','Glow width','Ancho del brillo','Ширина свечения','चमक की चौड़ाई','Lebar cahaya','عرض التوهج'],
    ['Largura por canal RGB','RGB channel widths','Anchos de canales RGB','Ширина каналов RGB','RGB चैनलों की चौड़ाई','Lebar kanal RGB','عرض قنوات RGB'],
    ['Ruído do brilho','Glow noise','Ruido del brillo','Шум свечения','चमक का नॉइज़','Derau cahaya','ضوضاء التوهج'],
    ['Frequência do ruído','Noise frequency','Frecuencia del ruido','Частота шума','नॉइज़ की आवृत्ति','Frekuensi derau','تردد الضوضاء'],
    ['Velocidade do ruído','Noise speed','Velocidad del ruido','Скорость шума','नॉइज़ की गति','Kecepatan derau','سرعة الضوضاء'],
    ['Processamento de alfa','Alpha processing','Procesamiento de alfa','Обработка альфа-канала','अल्फ़ा प्रोसेसिंग','Pemrosesan alfa','معالجة ألفا'],
    ['Tudo opaco','All opaque','Todo opaco','Всё непрозрачно','सब अपारदर्शी','Semua buram','الكل معتم'],
    ['Pré-multiplicado','Premultiplied','Premultiplicado','Предумноженный','पूर्व-गुणित','Prakali','مضروب مسبقًا'],
    ['Camada de fundo','Background layer','Capa de fondo','Фоновый слой','पृष्ठभूमि लेयर','Lapisan latar','طبقة الخلفية'],
    ['Fase inicial','Initial phase','Fase inicial','Начальная фаза','प्रारंभिक चरण','Fase awal','الطور الأولي'],
    ['Velocidade da fase','Phase speed','Velocidad de fase','Скорость фазы','चरण की गति','Kecepatan fase','سرعة الطور'],
    ['Gradiente do plasma','Plasma gradient','Gradiente del plasma','Градиент плазмы','प्लाज़्मा ग्रेडिएंट','Gradien plasma','تدرج البلازما'],
    ['Ângulo do plasma','Plasma angle','Ángulo del plasma','Угол плазмы','प्लाज़्मा का कोण','Sudut plasma','زاوية البلازما'],
    ['Camadas de plasma','Plasma layers','Capas de plasma','Слои плазмы','प्लाज़्मा लेयर','Lapisan plasma','طبقات البلازما'],
    ['Camada da lente','Lens layer','Capa de lente','Слой линзы','लेंस लेयर','Lapisan lensa','طبقة العدسة'],
    ['Camada do matte','Matte layer','Capa matte','Слой маски','मैट लेयर','Lapisan matte','طبقة القناع'],
    ['Ajuste fino','Fine adjustment','Ajuste fino','Точная настройка','सूक्ष्म समायोजन','Penyesuaian halus','ضبط دقيق'],
    ['Suavidade da lente','Lens softness','Suavidad de la lente','Мягкость линзы','लेंस की नरमी','Kelembutan lensa','نعومة العدسة'],
    ['Intensidade relativa XY','Relative XY strength','Intensidad relativa XY','Относительная интенсивность XY','सापेक्ष XY तीव्रता','Intensitas XY relatif','الشدة النسبية XY'],
    ['Bordas horizontais','Horizontal edges','Bordes horizontales','Горизонтальные края','क्षैतिज किनारे','Tepi horizontal','الحواف الأفقية'],
    ['Bordas verticais','Vertical edges','Bordes verticales','Вертикальные края','ऊर्ध्वाधर किनारे','Tepi vertikal','الحواف العمودية'],
    ['Sem repetição','No wrapping','Sin repetición','Без повторения','बिना दोहराव','Tanpa pengulangan','بدون تكرار'],
    ['Filtragem adaptativa','Adaptive filtering','Filtrado adaptativo','Адаптивная фильтрация','अनुकूली फ़िल्टरिंग','Pemfilteran adaptif','ترشيح تكيفي'],
    ['Suavidade do matte','Matte softness','Suavidad del matte','Мягкость маски','मैट की नरमी','Kelembutan matte','نعومة القناع'],
    ['Inverter matte','Invert matte','Invertir matte','Инвертировать маску','मैट उलटें','Balik matte','عكس القناع'],
    ['Canal do matte','Matte channel','Canal del matte','Канал маски','मैट चैनल','Kanal matte','قناة القناع'],
    ['Recortar esquerda','Crop left','Recortar izquierda','Обрезать слева','बाईं ओर काटें','Pangkas kiri','قص اليسار'],
    ['Recortar topo','Crop top','Recortar arriba','Обрезать сверху','ऊपर से काटें','Pangkas atas','قص الأعلى'],
    ['Recortar direita','Crop right','Recortar derecha','Обрезать справа','दाईं ओर काटें','Pangkas kanan','قص اليمين'],
    ['Recortar base','Crop bottom','Recortar abajo','Обрезать снизу','नीचे से काटें','Pangkas bawah','قص الأسفل'],
]
languages = ['en','es','ru','hi','id','ar']
for pt, *translations in rows:
    data.setdefault(pt, dict(zip(languages, translations)))
path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(f'Registered {len(rows)} localized control texts, preserving existing entries.')
