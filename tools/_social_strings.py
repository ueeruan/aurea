"""Native community strings, in the same seven catalogs used by the editor."""
from pathlib import Path
from xml.sax.saxutils import escape
import re

rows = '''community|Comunidade|Community|Comunidad|Сообщество|समुदाय|المجتمع|Komunitas
profile|Perfil|Profile|Perfil|Профиль|प्रोफ़ाइल|الملف الشخصي|Profil
intro|Compartilhe suas criações. Encontre quem inspira você.|Share your creations. Find people who inspire you.|Comparte tus creaciones. Encuentra a quienes te inspiran.|Делитесь творчеством. Находите тех, кто вас вдохновляет.|अपनी रचनाएँ साझा करें। प्रेरणा देने वाले लोगों को खोजें।|شارك إبداعاتك واعثر على من يلهمك.|Bagikan karyamu. Temukan orang yang menginspirasimu.
all|Para você|For you|Para ti|Для вас|आपके लिए|لك|Untukmu
following|Seguindo|Following|Siguiendo|Подписки|फ़ॉलो कर रहे हैं|تتابعهم|Mengikuti
followers|Seguidores|Followers|Seguidores|Подписчики|फ़ॉलोअर|المتابعون|Pengikut
follow|Seguir|Follow|Seguir|Подписаться|फ़ॉलो करें|متابعة|Ikuti
unfollow|Deixar de seguir|Unfollow|Dejar de seguir|Отписаться|अनफ़ॉलो करें|إلغاء المتابعة|Berhenti mengikuti
search|Buscar username|Search username|Buscar usuario|Найти пользователя|यूज़रनेम खोजें|البحث عن اسم مستخدم|Cari nama pengguna
refresh|Atualizar|Refresh|Actualizar|Обновить|रीफ़्रेश|تحديث|Muat ulang
more|Carregar mais|Load more|Cargar más|Загрузить ещё|और लोड करें|تحميل المزيد|Muat lagi
empty|Ainda não há publicações aqui.|No posts here yet.|Aún no hay publicaciones aquí.|Здесь пока нет публикаций.|अभी कोई पोस्ट नहीं है।|لا توجد منشورات هنا بعد.|Belum ada postingan di sini.
create_post|Criar publicação|Create post|Crear publicación|Создать публикацию|पोस्ट बनाएँ|إنشاء منشور|Buat postingan
post_hint|O que você criou?|What did you create?|¿Qué has creado?|Что вы создали?|आपने क्या बनाया?|ماذا أبدعت؟|Apa yang kamu buat?
publish|Publicar|Publish|Publicar|Опубликовать|प्रकाशित करें|نشر|Terbitkan
attach_preset|Preset|Preset|Preset|Пресет|प्रीसेट|إعداد مسبق|Preset
attach_project|Projeto|Project|Proyecto|Проект|प्रोजेक्ट|مشروع|Proyek
remove|Remover|Remove|Quitar|Убрать|हटाएँ|إزالة|Hapus
file_limit|Projetos: até 50 MB. Presets: até 1 MB.|Projects: up to 50 MB. Presets: up to 1 MB.|Proyectos: hasta 50 MB. Presets: hasta 1 MB.|Проекты: до 50 МБ. Пресеты: до 1 МБ.|प्रोजेक्ट: 50 MB तक। प्रीसेट: 1 MB तक।|المشاريع: حتى 50 ميغابايت. الإعدادات: حتى 1 ميغابايت.|Proyek: hingga 50 MB. Preset: hingga 1 MB.
no_files|Salve um preset ou projeto para publicar aqui.|Save a preset or project to publish here.|Guarda un preset o proyecto para publicarlo aquí.|Сохраните пресет или проект для публикации.|यहाँ प्रकाशित करने के लिए प्रीसेट या प्रोजेक्ट सहेजें।|احفظ إعدادًا مسبقًا أو مشروعًا لنشره هنا.|Simpan preset atau proyek untuk diterbitkan di sini.
download|Baixar|Download|Descargar|Скачать|डाउनलोड|تنزيل|Unduh
saved|Preset salvo na sua biblioteca.|Preset saved to your library.|Preset guardado en tu biblioteca.|Пресет сохранён в библиотеке.|प्रीसेट आपकी लाइब्रेरी में सहेजा गया।|تم حفظ الإعداد في مكتبتك.|Preset disimpan ke pustakamu.
likes|Curtidas|Likes|Me gusta|Нравится|पसंद|الإعجابات|Suka
comments|Comentários|Comments|Comentarios|Комментарии|टिप्पणियाँ|التعليقات|Komentar
comment_hint|Escreva um comentário|Write a comment|Escribe un comentario|Напишите комментарий|टिप्पणी लिखें|اكتب تعليقًا|Tulis komentar
no_comments|Seja a primeira pessoa a comentar.|Be the first to comment.|Sé la primera persona en comentar.|Оставьте первый комментарий.|पहली टिप्पणी करें।|كن أول من يعلّق.|Jadilah yang pertama berkomentar.
send|Enviar|Send|Enviar|Отправить|भेजें|إرسال|Kirim
delete|Excluir|Delete|Eliminar|Удалить|मिटाएँ|حذف|Hapus
delete_post|Excluir publicação?|Delete post?|¿Eliminar publicación?|Удалить публикацию?|पोस्ट मिटाएँ?|حذف المنشور؟|Hapus postingan?
edit_profile|Editar perfil|Edit profile|Editar perfil|Изменить профиль|प्रोफ़ाइल बदलें|تعديل الملف الشخصي|Edit profil
username|Username|Username|Nombre de usuario|Имя пользователя|यूज़रनेम|اسم المستخدم|Nama pengguna
name|Nome de exibição|Display name|Nombre visible|Отображаемое имя|दिखने वाला नाम|الاسم الظاهر|Nama tampilan
bio|Bio|Bio|Biografía|О себе|परिचय|نبذة|Bio
photo|Alterar foto|Change photo|Cambiar foto|Изменить фото|फ़ोटो बदलें|تغيير الصورة|Ganti foto
save|Salvar|Save|Guardar|Сохранить|सहेजें|حفظ|Simpan
profile_hint|Username: 3–24 letras de a–z, números ou _.|Username: 3–24 letters a–z, numbers or _.|Usuario: 3–24 letras a–z, números o _.|Имя: 3–24 символа a–z, цифры или _.|यूज़रनेम: 3–24 अक्षर a–z, अंक या _.|الاسم: من 3 إلى 24 حرفًا a–z أو أرقامًا أو _.|Nama: 3–24 huruf a–z, angka atau _.
profile_required|Escolha seu username para participar.|Choose your username to join in.|Elige tu usuario para participar.|Выберите имя пользователя для участия.|शामिल होने के लिए यूज़रनेम चुनें।|اختر اسم مستخدم للمشاركة.|Pilih nama pengguna untuk bergabung.
username_taken|Esse username já está em uso.|That username is already taken.|Ese usuario ya está en uso.|Это имя уже занято.|यह यूज़रनेम पहले से उपयोग में है।|اسم المستخدم مستخدم بالفعل.|Nama pengguna sudah digunakan.
verification|Gerenciar verificação|Manage verification|Gestionar verificación|Управление значком|सत्यापन प्रबंधित करें|إدارة التوثيق|Kelola verifikasi
badge_blue|Criador • azul|Creator • blue|Creador • azul|Автор • синий|क्रिएटर • नीला|منشئ • أزرق|Kreator • biru
badge_green|Testador • verde|Tester • green|Tester • verde|Тестировщик • зелёный|टेस्टर • हरा|مختبر • أخضر|Penguji • hijau
badge_gold|Grande criador • dourado|Leading creator • gold|Gran creador • dorado|Ведущий автор • золотой|प्रमुख क्रिएटर • सुनहरा|منشئ بارز • ذهبي|Kreator unggulan • emas
badge_none|Remover verificação|Remove verification|Quitar verificación|Убрать значок|सत्यापन हटाएँ|إزالة التوثيق|Hapus verifikasi
error|Não foi possível concluir. Confira a conexão e tente novamente.|Could not complete. Check your connection and try again.|No se pudo completar. Revisa la conexión e inténtalo de nuevo.|Не удалось завершить. Проверьте подключение и повторите.|पूरा नहीं हो सका। कनेक्शन जाँचें और फिर कोशिश करें।|تعذر الإكمال. تحقق من الاتصال وحاول مجددًا.|Tidak dapat diselesaikan. Periksa koneksi dan coba lagi.
session_error|Entre novamente na sua conta para usar a Comunidade.|Sign in again to use Community.|Vuelve a iniciar sesión para usar Comunidad.|Войдите снова, чтобы использовать сообщество.|समुदाय का उपयोग करने के लिए फिर साइन इन करें।|سجّل الدخول مجددًا لاستخدام المجتمع.|Masuk kembali untuk menggunakan Komunitas.'''

root = Path(__file__).resolve().parents[1] / 'android/app/src/main/res'
catalogs = ['values', 'values-en', 'values-es', 'values-ru', 'values-hi', 'values-ar', 'values-id']
parsed = [row.split('|') for row in rows.splitlines()]
for language, catalog in enumerate(catalogs):
    path = root / catalog / 'strings.xml'
    source = path.read_text(encoding='utf-8')
    for row in parsed:
        assert len(row) == 8, row[0]
        key = 'social_' + row[0]
        value = escape(row[language + 1]).replace("'", "\\'")
        line = f'    <string name="{key}">{value}</string>'
        if f'name="{key}"' in source:
            source = re.sub(r'    <string name="' + key + r'">.*?</string>', lambda _: line, source)
        else:
            source = source.replace('</resources>', line + '\n</resources>')
    path.write_text(source, encoding='utf-8', newline='\n')
