# Tools

## `regenerar_icones.py`

Deriva os ícones do Android e do iOS a partir dos três PNGs da marca em
`_identity/branding/icon/`.

O projeto antigo tinha um `tool_icones.py` que fazia isso com `PIL` e dependia
de caminhos do projeto antigo. Ele **não** foi copiado: as densidades, os nomes
e o script de layout do ícone adaptativo eram específicos daquela estrutura.

O que a nova versão precisa produzir:

```
android/app/src/main/res/
├── mipmap-mdpi/ic_launcher.png          48×48
├── mipmap-hdpi/ic_launcher.png          72×72
├── mipmap-xhdpi/ic_launcher.png         96×96
├── mipmap-xxhdpi/ic_launcher.png       144×144
├── mipmap-xxxhdpi/ic_launcher.png      192×192
├── drawable-*/ic_launcher_foreground.png   (ícone adaptativo)
├── drawable-*/ic_launcher_monochrome.png   (tema do sistema)
├── drawable-*/splash_logo.png              (tela de abertura)
└── mipmap-anydpi-v26/ic_launcher.xml       (inset de 16%)

ios/Aurea/Assets.xcassets/
├── AppIcon.appiconset/    (13 tamanhos)
└── LaunchImage.imageset/  (3 tamanhos)
```

Regras que o script precisa respeitar:

- o fundo do ícone adaptativo é `#0F141A` — a MESMA cor de
  `res/values/colors.xml`. Divergir faz a passagem do ícone para o app piscar;
- o inset do ícone adaptativo é de 16% (o sistema recorta em círculo, squircle
  ou gota, e sem o inset a marca é cortada);
- o monochrome é uma silhueta de um canal, não o ícone colorido.

**Estado: não implementado.** Os ícones atuais foram copiados intactos do
projeto antigo, o que é o certo — eles já estão corretos em todas as
densidades, e regerá-los sem necessidade seria trocar o que funciona por uma
chance de erro. O script só será necessário quando a marca mudar.
