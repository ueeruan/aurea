# Subir o Aurea no TestFlight

O workflow `.github/workflows/testflight.yml` arquiva, assina **na nuvem**
(certificado gerenciado pela Apple — sem `.p12`, sem Mac) e sobe para o
TestFlight. Ele precisa de quatro coisas da SUA conta Apple, uma vez so.

## 1. Apple Developer Program

A conta precisa estar inscrita no programa pago (developer.apple.com →
Account → Enroll). Sem isso nao existe TestFlight. O **Team ID** (10
caracteres, ex. `A1B2C3D4E5`) aparece em Account → Membership details.

## 2. O app na App Store Connect

appstoreconnect.apple.com → Apps → **+** → New App:

- Platform: iOS
- Name: `Aurea` (se estiver tomado, qualquer outro; o nome da loja nao
  muda o bundle id)
- Primary language: Portuguese (Brazil)
- Bundle ID: `com.aurea.aurea` — se nao aparecer na lista, registre antes
  em developer.apple.com → Certificates, Identifiers & Profiles →
  Identifiers → **+** → App IDs, com exatamente esse id.
- SKU: `aurea`

## 3. A chave de API

appstoreconnect.apple.com → Users and Access → **Integrations** → App
Store Connect API → **Team Keys** → **+**:

- Name: `aurea-ci`
- Access: **App Manager**
- Marque **"Access to Cloud Managed Distribution Certificate"** (e isso
  que dispensa o certificado local).

Anote o **Key ID** e o **Issuer ID** (aparecem na pagina) e **baixe o
`.p8`** — a Apple so deixa baixar uma vez.

## 4. Os segredos no repositorio

No terminal, na pasta do projeto (troque os valores pelos seus):

```bash
"C:/Users/SnyX/.aurea/gh/bin/gh.exe" secret set APPLE_TEAM_ID --repo ruanpablo9928-sys/aurea --body "A1B2C3D4E5"
```

```bash
"C:/Users/SnyX/.aurea/gh/bin/gh.exe" secret set ASC_KEY_ID --repo ruanpablo9928-sys/aurea --body "ABC123DEF4"
```

```bash
"C:/Users/SnyX/.aurea/gh/bin/gh.exe" secret set ASC_ISSUER_ID --repo ruanpablo9928-sys/aurea --body "69a6de70-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
```

```bash
"C:/Users/SnyX/.aurea/gh/bin/gh.exe" secret set ASC_KEY_P8_BASE64 --repo ruanpablo9928-sys/aurea --body "$(base64 -w0 "C:/Users/SnyX/Downloads/AuthKey_ABC123DEF4.p8")"
```

## 5. Enviar

```bash
git tag tf-1 && git push origin tf-1
```

Ou Actions → **TestFlight** → Run workflow. Em ~10 min o build aparece
em App Store Connect → TestFlight (a Apple ainda processa por alguns
minutos antes de liberar para testadores). O numero do build vem do
`pubspec.yaml` (`version: 1.2.0+37` → build 37): **cada envio precisa de
um numero maior** que o anterior na mesma versao.

## Se der errado

- `No suitable application records were found` → o app do passo 2 nao
  existe ou o bundle id nao bate.
- `Cloud signing permission error` → a chave nao e App Manager ou nao
  tem "Access to Cloud Managed Distribution Certificate".
- `ITMS-90XXX` sobre icone/permissao → o log do passo "Subir para o
  TestFlight" diz qual; os textos de permissao e o icone 1024 ja estao
  no projeto.
- Export compliance: `ITSAppUsesNonExemptEncryption=false` ja esta no
  Info.plist (o app nao tem criptografia propria).
