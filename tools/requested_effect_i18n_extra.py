"""Localized controls for independently authored cell, ray and glitch effects."""
import json
from pathlib import Path

path = Path('tools/effect_i18n_texts.json')
data = json.loads(path.read_text(encoding='utf-8'))
langs = ['en', 'es', 'ru', 'hi', 'id', 'ar']
def add(pt, *values):
    assert len(values) == 6, pt
    data.setdefault(pt, dict(zip(langs, values)))

rows = '''Tamanho das células XY|Cell size XY|Tamaño de celdas XY|Размер ячеек XY|सेल का आकार XY|Ukuran sel XY|حجم الخلايا XY
Vincular tamanho XY|Link XY size|Vincular tamaño XY|Связать размеры XY|XY आकार जोड़ें|Tautkan ukuran XY|ربط حجم XY
Detalhe alto|High detail|Detalle alto|Высокая детализация|उच्च विवरण|Detail tinggi|تفاصيل عالية
Números e letras|Numbers and letters|Números y letras|Цифры и буквы|अंक और अक्षर|Angka dan huruf|أرقام وحروف
Linhas horizontais|Horizontal lines|Líneas horizontales|Горизонтальные линии|क्षैतिज रेखाएँ|Garis horizontal|خطوط أفقية
Linhas verticais|Vertical lines|Líneas verticales|Вертикальные линии|ऊर्ध्वाधर रेखाएँ|Garis vertikal|خطوط عمودية
Anéis|Rings|Anillos|Кольца|छल्ले|Cincin|حلقات
Sólido|Solid|Sólido|Сплошной|ठोस|Padat|مصمت
Diagonal esquerda|Left diagonal|Diagonal izquierda|Диагональ влево|बायाँ विकर्ण|Diagonal kiri|قطري يسار
Diagonal direita|Right diagonal|Diagonal derecha|Диагональ вправо|दायाँ विकर्ण|Diagonal kanan|قطري يمين
Cor dos pixels|Pixel color|Color de píxeles|Цвет пикселей|पिक्सेल का रंग|Warna piksel|لون البكسلات
Cor de fundo|Background color|Color de fondo|Цвет фона|पृष्ठभूमि का रंग|Warna latar|لون الخلفية
Cores originais|Original colors|Colores originales|Исходные цвета|मूल रंग|Warna asli|الألوان الأصلية
Fundo transparente|Transparent background|Fondo transparente|Прозрачный фон|पारदर्शी पृष्ठभूमि|Latar transparan|خلفية شفافة
Intensidade do caos|Chaos strength|Intensidad del caos|Интенсивность хаоса|अव्यवस्था की तीव्रता|Intensitas kekacauan|شدة الفوضى
Padrão aleatório por célula|Random pattern per cell|Patrón aleatorio por celda|Случайный узор ячейки|हर सेल में यादृच्छिक पैटर्न|Pola acak per sel|نمط عشوائي لكل خلية
Níveis de detalhe|Detail levels|Niveles de detalle|Уровни детализации|विवरण के स्तर|Tingkat detail|مستويات التفاصيل
Detecção de tons|Tone detection|Detección de tonos|Определение тонов|टोन पहचान|Deteksi rona|اكتشاف الدرجات
Média RGB|RGB average|Promedio RGB|Среднее RGB|RGB औसत|Rata-rata RGB|متوسط RGB
Comprimento dos raios|Ray length|Longitud de rayos|Длина лучей|किरणों की लंबाई|Panjang sinar|طول الأشعة
Intensidade da luz|Light boost|Intensidad de luz|Усиление света|प्रकाश की तीव्रता|Penguatan cahaya|شدة الضوء
Suavidade do limiar|Threshold softness|Suavidad del umbral|Мягкость порога|सीमा की नरमी|Kelembutan ambang|نعومة العتبة
Canal de entrada|Input channel|Canal de entrada|Входной канал|इनपुट चैनल|Kanal masukan|قناة الإدخال
Máscara circular|Circular mask|Máscara circular|Круглая маска|गोल मास्क|Masker lingkaran|قناع دائري
Centro da máscara|Mask center|Centro de máscara|Центр маски|मास्क का केंद्र|Pusat masker|مركز القناع
Raio da máscara|Mask radius|Radio de máscara|Радиус маски|मास्क की त्रिज्या|Radius masker|نصف قطر القناع
Suavidade da máscara|Mask softness|Suavidad de máscara|Мягкость маски|मास्क की नरमी|Kelembutan masker|نعومة القناع
Detalhe da cintilação|Shimmer detail|Detalle del destello|Детализация мерцания|झिलमिलाहट का विवरण|Detail kilau|تفاصيل الوميض
Fase da cintilação|Shimmer phase|Fase del destello|Фаза мерцания|झिलमिलाहट का चरण|Fase kilau|طور الوميض
Repetir cintilação|Loop shimmer|Repetir destello|Зациклить мерцание|झिलमिलाहट दोहराएँ|Ulangi kilau|تكرار الوميض
Duração do ciclo|Loop duration|Duración del ciclo|Длительность цикла|चक्र की अवधि|Durasi siklus|مدة الدورة
Opacidade da fonte|Source opacity|Opacidad de fuente|Непрозрачность источника|स्रोत की अपारदर्शिता|Opasitas sumber|عتامة المصدر
Opacidade do brilho|Glow opacity|Opacidad del brillo|Непрозрачность свечения|चमक की अपारदर्शिता|Opasitas cahaya|عتامة التوهج
Mistura do brilho|Glow blending|Mezcla del brillo|Смешивание свечения|चमक का मिश्रण|Pencampuran cahaya|مزج التوهج
Máximo|Maximum|Máximo|Максимум|अधिकतम|Maksimum|الحد الأقصى
Método de pontilhado|Dither method|Método de tramado|Метод дизеринга|डिथर विधि|Metode dithering|طريقة التنقيط
Duas cores|Two colors|Dos colores|Два цвета|दो रंग|Dua warna|لونان
Intervalo do glitch|Glitch interval|Intervalo del glitch|Интервал сбоев|ग्लिच का अंतराल|Interval glitch|فاصل التشويش
Duração do glitch|Glitch duration|Duración del glitch|Длительность сбоя|ग्लिच की अवधि|Durasi glitch|مدة التشويش
Semente global|Global seed|Semilla global|Общее зерно|वैश्विक सीड|Benih global|البذرة العامة
Quadro inicial|Start frame|Fotograma inicial|Начальный кадр|प्रारंभिक फ़्रेम|Bingkai awal|الإطار الأول
Disparo|Trigger|Disparo|Запуск|ट्रिगर|Pemicu|المشغل
Cruzamento de intensidade|Intensity crossing|Cruce de intensidad|Пересечение интенсивности|तीव्रता सीमा पार करना|Lintasan intensitas|عبور الشدة
Limiar de intensidade|Intensity threshold|Umbral de intensidad|Порог интенсивности|तीव्रता की सीमा|Ambang intensitas|عتبة الشدة
Variação da intensidade|Intensity randomness|Variación de intensidad|Разброс интенсивности|तीव्रता की विविधता|Variasi intensitas|تفاوت الشدة
Variação do intervalo|Interval randomness|Variación del intervalo|Разброс интервала|अंतराल की विविधता|Variasi interval|تفاوت الفاصل
Variação da duração|Duration randomness|Variación de duración|Разброс длительности|अवधि की विविधता|Variasi durasi|تفاوت المدة
Padrão do dano|Damage pattern|Patrón del daño|Узор повреждений|क्षति का पैटर्न|Pola kerusakan|نمط التلف
Faixas horizontais|Horizontal bands|Franjas horizontales|Горизонтальные полосы|क्षैतिज पट्टियाँ|Pita horizontal|شرائط أفقية
Faixas verticais|Vertical bands|Franjas verticales|Вертикальные полосы|ऊर्ध्वाधर पट्टियाँ|Pita vertikal|شرائط عمودية
Fragmentos|Fragments|Fragmentos|Фрагменты|टुकड़े|Fragmen|شظايا
Deslocamento horizontal|Horizontal shift|Desplazamiento horizontal|Горизонтальный сдвиг|क्षैतिज विस्थापन|Pergeseran horizontal|إزاحة أفقية
Deslocamento vertical|Vertical shift|Desplazamiento vertical|Вертикальный сдвиг|ऊर्ध्वाधर विस्थापन|Pergeseran vertikal|إزاحة عمودية
Faixas de deslocamento|Shift bands|Franjas de desplazamiento|Полосы сдвига|विस्थापन पट्टियाँ|Pita pergeseran|شرائط الإزاحة
Duplicar faixas|Duplicate bands|Duplicar franjas|Дублировать полосы|पट्टियाँ दोहराएँ|Duplikasikan pita|تكرار الشرائط
Inclinação das faixas|Band skew|Inclinación de franjas|Наклон полос|पट्टियों का झुकाव|Kemiringan pita|ميل الشرائط
Tremor XY|Shake XY|Temblor XY|Тряска XY|कंपन XY|Guncangan XY|اهتزاز XY
Rotação do tremor|Shake rotation|Rotación del temblor|Вращение тряски|कंपन का घुमाव|Rotasi guncangan|دوران الاهتزاز
Variação de brilho|Brightness variation|Variación de brillo|Разброс яркости|चमक की विविधता|Variasi kecerahan|تفاوت السطوع
Variação de saturação|Saturation variation|Variación de saturación|Разброс насыщенности|संतृप्ति की विविधता|Variasi saturasi|تفاوت التشبع
Visualização das curvas|Curves view|Vista de curvas|Просмотр кривых|कर्व दृश्य|Tampilan kurva|عرض المنحنيات
Sobre preto|Over black|Sobre negro|На чёрном|काले पर|Di atas hitam|فوق الأسود
Sobre o efeito|Over effect|Sobre el efecto|Поверх эффекта|प्रभाव पर|Di atas efek|فوق التأثير
Janela das curvas|Curve time window|Ventana temporal de curvas|Временное окно кривых|कर्व समय सीमा|Rentang waktu kurva|النافذة الزمنية للمنحنيات
Congelar fonte dos blocos|Freeze block source|Congelar fuente de bloques|Заморозить источник блоков|ब्लॉक स्रोत स्थिर करें|Bekukan sumber blok|تجميد مصدر الكتل
Mapa de deformação|Deformation map|Mapa de deformación|Карта деформации|विकृति मैप|Peta deformasi|خريطة التشويه
Deslocamento XY|Translation XY|Desplazamiento XY|Перемещение XY|विस्थापन XY|Translasi XY|إزاحة XY
Deformação de escala XY|Scale deformation XY|Deformación de escala XY|Деформация масштаба XY|स्केल विकृति XY|Deformasi skala XY|تشويه المقياس XY
Deformação de rotação|Rotation deformation|Deformación de rotación|Деформация вращения|घुमाव विकृति|Deformasi rotasi|تشويه الدوران
Canal de escala e rotação|Scale and rotation channel|Canal de escala y rotación|Канал масштаба и вращения|स्केल और घुमाव चैनल|Kanal skala dan rotasi|قناة المقياس والدوران
Valor neutro|Neutral value|Valor neutro|Нейтральное значение|तटस्थ मान|Nilai netral|القيمة المحايدة
Suavidade do mapa|Map softness|Suavidad del mapa|Мягкость карты|मैप की नरमी|Kelembutan peta|نعومة الخريطة
Deslocamento do mapa|Map offset|Desplazamiento del mapa|Смещение карты|मैप विस्थापन|Offset peta|إزاحة الخريطة'''
for line in rows.splitlines(): add(*line.split('|'))

# Algorithm, manufacturer and hardware names are intentionally invariant.
for name in ['Grunge','Floyd Steinberg','False Floyd Steinberg','Filter Lite','Zhigang Fan','Shiau-Fan','Jarvis, Judice & Ninke','Stucki','Burkes','Sierra','Two Row Sierra','Atkinson','Bayer 2x2','Bayer 3x3','Bayer 5x3','Bayer 4x4','Bayer 8x8','Cluster Dot 4x4','Cluster Dot 8x8','Horizontal 2x2','Horizontal 8x1','Horizontal 12x4','Vertical 2x2','Vertical 1x8','Vertical 4x12','Diagonal 5x5','PICO-8','Gameboy','CGA','ZX Spectrum','Web Safe']:
    add(name,*([name]*6))
for code in ['601','709']:
    add(f'Luminância Rec. {code}',f'Rec. {code} luminance',f'Luminancia Rec. {code}',f'Яркость Rec. {code}',f'Rec. {code} ल्यूमिनेंस',f'Luminans Rec. {code}',f'إضاءة Rec. {code}')
for bits in [1,2,4]:
    add(f'Cinza {bits} '+('bit' if bits==1 else 'bits'),f'Gray {bits} bit',f'Gris {bits} bits',f'Серый {bits} бит',f'धूसर {bits} बिट',f'Abu-abu {bits} bit',f'رمادي {bits} بت')
for bits in [3,6,9]: add(f'RGB {bits} bits',*([f'RGB {bits} bit']*6))
directions = [
    ['à direita','right','derecha','вправо','दाएँ','kanan','يمين'],
    ['abaixo à direita','down right','abajo derecha','вниз вправо','नीचे दाएँ','kanan bawah','أسفل اليمين'],
    ['abaixo','down','abajo','вниз','नीचे','bawah','أسفل'],
    ['abaixo à esquerda','down left','abajo izquierda','вниз влево','नीचे बाएँ','kiri bawah','أسفل اليسار'],
    ['à esquerda','left','izquierda','влево','बाएँ','kiri','يسار'],
    ['acima à esquerda','up left','arriba izquierda','вверх влево','ऊपर बाएँ','kiri atas','أعلى اليسار'],
    ['acima','up','arriba','вверх','ऊपर','atas','أعلى'],
    ['acima à direita','up right','arriba derecha','вверх вправо','ऊपर दाएँ','kanan atas','أعلى اليمين']]
for pt,en,es,ru,hi,id,ar in directions:
    add('Raio '+pt,'Ray '+en,'Rayo '+es,'Луч '+ru,hi+' किरण','Sinar '+id,'شعاع '+ar)
    add('Cores '+pt,'Colors '+en,'Colores '+es,'Цвета '+ru,hi+' रंग','Warna '+id,'ألوان '+ar)
for count in [1,3,5]: add(f'{count} '+('cor' if count==1 else 'cores'),f'{count} '+('color' if count==1 else 'colors'),f'{count} colores',f'{count} цветов',f'{count} रंग',f'{count} warna',f'{count} ألوان')
for m in [1,2,3]:
    add(f'Mapa {m}',f'Map {m}',f'Mapa {m}',f'Карта {m}',f'मैप {m}',f'Peta {m}',f'خريطة {m}')
    add(f'Cores do mapa {m}',f'Map {m} colors',f'Colores del mapa {m}',f'Цвета карты {m}',f'मैप {m} के रंग',f'Warna peta {m}',f'ألوان الخريطة {m}')
    for c in range(1,6): add(f'Mapa {m}: cor {c}',f'Map {m}: color {c}',f'Mapa {m}: color {c}',f'Карта {m}: цвет {c}',f'मैप {m}: रंग {c}',f'Peta {m}: warna {c}',f'خريطة {m}: لون {c}')
groups = [
    ('blocos','dos blocos',['blocks','bloques','блоков','ब्लॉक','blok','الكتل']),
    ('deslocamento','do deslocamento',['shift','desplazamiento','сдвига','विस्थापन','pergeseran','الإزاحة']),
    ('tremor','do tremor',['shake','temblor','тряски','कंपन','guncangan','الاهتزاز']),
    ('flicker','do flicker',['flicker','parpadeo','мерцания','झिलमिलाहट','kedipan','الوميض'])]
prefixes = [
    ('Intensidade',['Strength','Intensidad','Интенсивность','तीव्रता','Intensitas','شدة']),
    ('Intervalo',['Interval','Intervalo','Интервал','अंतराल','Interval','فاصل']),
    ('Mínimo',['Minimum','Mínimo','Минимум','न्यूनतम','Minimum','حد أدنى']),
    ('Pico',['Peak','Pico','Пик','चरम','Puncak','ذروة']),
    ('Variação',['Randomness','Variación','Разброс','विविधता','Variasi','تفاوت']),
    ('Atraso',['Offset','Retraso','Задержка','विलंब','Jeda','تأخير']),
    ('Semente',['Seed','Semilla','Зерно','सीड','Benih','بذرة'])]
for group,article,names in groups:
    add('Ativar '+('dano em blocos' if group=='blocos' else group),*[a+' '+b for a,b in zip(['Enable','Activar','Включить','सक्रिय करें','Aktifkan','تفعيل'],names)])
    for pt,translated in prefixes: add(pt+' '+article,*[a+' '+b for a,b in zip(translated,names)])
path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('Localized extra controls in six languages.')
