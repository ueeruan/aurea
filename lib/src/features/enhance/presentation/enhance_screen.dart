import 'package:aurea/src/core/l10n/app_language.dart';
import 'dart:async';
import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:gal/gal.dart';
import 'package:video_player/video_player.dart';

import '../application/enhancement_job.dart';
import '../../editor/domain/aprimoramento_ia.dart';
import '../application/native_enhancer.dart';
import '../domain/color_look.dart';

class EnhanceScreen extends StatefulWidget {
  const EnhanceScreen({super.key});
  @override
  State<EnhanceScreen> createState() => _EnhanceScreenState();
}

class _EnhanceScreenState extends State<EnhanceScreen> {
  final _job = EnhancementJob();
  String? _source, _before, _after, _result;
  /// O MOTOR DE IA EXISTE NESTE APARELHO? Hoje so no Android (a
  /// biblioteca nativa ainda nao e compilada para iOS). Sem ele o
  /// interruptor fica desligado e diz por que, em vez de falhar ao gerar.
  static final bool _iaDisponivel = NativeEnhancer.libraryAvailable;
  bool _video = false,
      _busy = false,
      _ai = _iaDisponivel,
      _showBefore = false,
      _saving = false;
  int _scale = 2;
  double _strength = 1, _aiStrength = 1, _detail = 0;
  double _ruido = reducaoDeRuidoPadrao;
  PerfilDoAprimoramento _perfil = PerfilDoAprimoramento.videoReal;
  ColorLook _look = ColorLook.natural;
  VideoPlayerController? _player;
  EnhanceSettings get _settings => EnhanceSettings(
    ai: _ai,
    scale: _scale,
    look: _look,
    strength: _strength,
    aiStrength: _aiStrength,
    detail: _detail,
    perfil: _perfil,
    reducaoDeRuido: _ruido,
  );
  @override
  void dispose() {
    _player?.dispose();
    unawaited(_job.close());
    super.dispose();
  }

  void _message(String text) {
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: AppText(text)));
    }
  }

  void _changed(VoidCallback change) {
    unawaited(_player?.dispose());
    _player = null;
    setState(() {
      change();
      _after = null;
      _result = null;
    });
  }

  Future<void> _pick() async {
    try {
      final files = await FilePicker.platform.pickFiles(
        type: FileType.media,
        allowMultiple: false,
        withData: false,
      );
      if (!mounted || files == null || files.files.single.path == null) return;
      await _player?.dispose();
      _player = null;
      if (!mounted) return;
      final file = files.files.single;
      final ext = (file.extension ?? file.path!.split('.').last).toLowerCase();
      setState(() {
        _source = file.path;
        _before = null;
        _after = null;
        _result = null;
        _video = ![
          'jpg',
          'jpeg',
          'png',
          'webp',
          'heic',
          'heif',
          'avif',
          'bmp',
          'tiff',
          'tif',
          'gif',
        ].contains(ext);
      });
    } catch (_) {
      _message(
        'Não foi possível abrir este arquivo. Tente selecionar novamente.',
      );
    }
  }

  Future<void> _run({required bool preview}) async {
    if (_busy || _source == null) return;
    setState(() {
      _busy = true;
      _result = null;
    });
    try {
      await _player?.dispose();
      _player = null;
      if (preview) {
        final paths = await _job.preview(_source!, _video, _settings);
        if (mounted) {
          setState(() {
            _before = paths.$1;
            _after = paths.$2;
            _showBefore = false;
          });
        }
      } else {
        final output = await _job.process(_source!, _video, _settings);
        if (!mounted) return;
        setState(() => _result = output.path);
        if (_video) {
          final player = VideoPlayerController.file(output);
          _player = player;
          await player.initialize();
          if (mounted && identical(_player, player)) setState(() {});
        } else {
          setState(() {
            _before = _source;
            _after = output.path;
          });
        }
      }
    } catch (error) {
      final text = error.toString().replaceFirst('Bad state: ', '');
      _message(
        text.contains('Cancelado')
            ? 'Cancelado. Seu original foi mantido.'
            : 'Não foi possível concluir. ${text.contains('null') ? 'Tente 2× ou desligue Ampliar com IA para aplicar apenas as cores.' : text}',
      );
    } finally {
      if (mounted) {
        setState(() => _busy = false);
      } else {
        await _job.close();
      }
    }
  }

  Future<void> _save() async {
    if (_result == null || _saving) return;
    setState(() => _saving = true);
    try {
      if (_video) {
        await Gal.putVideo(_result!);
      } else {
        await Gal.putImage(_result!);
      }
      _message('Salvo na galeria!');
    } catch (_) {
      _message(
        'Não foi possível salvar na galeria. Verifique a permissão de fotos e tente novamente.',
      );
    } finally {
      if (mounted) setState(() => _saving = false);
    }
  }

  void _help() => showDialog<void>(
    context: context,
    builder: (context) => AlertDialog(
      title: const AppText('Qualidade e cor'),
      content: const SingleChildScrollView(
        child: AppText('Escolha uma foto ou vídeo. A IA (Real-ESRGAN, no aparelho, sem enviar sua mídia) reduz ruído e blocos de compressão e amplia. Vídeo real usa o modelo general-x4v3; Animação usa o animevideov3. 1× restaura sem ampliar; 2× e 4× ampliam — os três custam o mesmo processamento.\n\nRedução de ruído (só em vídeo real) mistura os pesos de dois modelos: menos preserva o grão, mais limpa. Intensidade da IA mistura o resultado com o original ampliado. Nitidez realça bordas e não é IA. Os CCs mudam as cores.\n\nComparar processa uma imagem ou o primeiro quadro do vídeo, com o antes ampliado ao mesmo tamanho. Gerar resultado processa todos os quadros na taxa original do vídeo, com o áudio original.\n\nA IA pode suavizar texturas; compare antes de salvar. O original permanece intacto.',
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: const AppText('Entendi'),
        ),
      ],
    ),
  );

  @override
  Widget build(BuildContext context) => PopScope(
    canPop: !_busy,
    onPopInvokedWithResult: (didPop, _) {
      if (!didPop && _busy) _message('Toque em Cancelar antes de sair.');
    },
    child: Scaffold(
      appBar: AppBar(
        title: const AppText('Melhorar qualidade'),
        actions: [
          IconButton(
            tooltip: 'Como funciona',
            icon: const Icon(Icons.help_outline),
            onPressed: _help,
          ),
        ],
      ),
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.all(16),
          children: [
            const AppText(
              'Mais definição. Sua cor.',
              style: TextStyle(fontSize: 24, fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 6),
            const AppText('Amplie com IA e aplique um visual de cor, sem abrir o editor.',
            ),
            const SizedBox(height: 16),
            OutlinedButton.icon(
              onPressed: _busy ? null : _pick,
              icon: const Icon(Icons.add_photo_alternate_outlined),
              label: AppText(
                _source == null ? 'Escolher foto ou vídeo' : 'Trocar arquivo',
              ),
            ),
            if (_source != null) ...[
              const SizedBox(height: 12),
              AspectRatio(
                aspectRatio: 16 / 10,
                child: ClipRRect(
                  borderRadius: BorderRadius.circular(16),
                  child: ColoredBox(color: Colors.black, child: _preview()),
                ),
              ),
              if (_after != null && _player == null)
                SegmentedButton<bool>(
                  segments: const [
                    ButtonSegment(value: true, label: AppText('Antes')),
                    ButtonSegment(value: false, label: AppText('Depois')),
                  ],
                  selected: {_showBefore},
                  onSelectionChanged: (v) =>
                      setState(() => _showBefore = v.first),
                ),
              const SizedBox(height: 16),
              SwitchListTile(
                contentPadding: EdgeInsets.zero,
                title: const AppText('Ampliar com IA'),
                subtitle: AppText(
                  _iaDisponivel
                      ? 'Real-ESRGAN • processamento no aparelho'
                      : 'IA ainda indisponível neste aparelho',
                ),
                value: _ai && _iaDisponivel,
                onChanged: _busy || !_iaDisponivel
                    ? null
                    : (v) => _changed(() => _ai = v),
              ),
              if (_ai) ...[
                const SizedBox(height: 8),
                SegmentedButton<PerfilDoAprimoramento>(
                  key: const ValueKey('melhorar-perfil'),
                  segments: [
                    for (final p in PerfilDoAprimoramento.values)
                      ButtonSegment(value: p, label: AppText(p.emPalavras)),
                  ],
                  selected: {_perfil},
                  onSelectionChanged: _busy
                      ? null
                      : (v) => _changed(() => _perfil = v.first),
                ),
                const SizedBox(height: 8),
              ],
              if (_ai)
                SegmentedButton<int>(
                  segments: const [
                    ButtonSegment(value: 1, label: AppText('1×')),
                    ButtonSegment(value: 2, label: AppText('2×')),
                    ButtonSegment(value: 4, label: AppText('4×')),
                  ],
                  selected: {_scale},
                  onSelectionChanged: _busy
                      ? null
                      : (v) => _changed(() => _scale = v.first),
                ),
              const SizedBox(height: 20),
              const AppText(
                'CCs • Correção de cor',
                style: TextStyle(fontWeight: FontWeight.bold, fontSize: 18),
              ),
              const SizedBox(height: 8),
              Wrap(
                spacing: 8,
                runSpacing: 4,
                children: [
                  for (final look in ColorLook.values)
                    ChoiceChip(
                      label: AppText(look.label),
                      selected: _look == look,
                      onSelected: _busy
                          ? null
                          : (_) => _changed(() => _look = look),
                    ),
                ],
              ),
              if (_look != ColorLook.natural)
                _slider('Intensidade da cor', _strength, (v) => _strength = v),
              if (_ai && _perfil == PerfilDoAprimoramento.videoReal)
                _slider('Redução de ruído', _ruido, (v) => _ruido = v),
              if (_ai)
                _slider('Intensidade da IA', _aiStrength, (v) => _aiStrength = v),
              _slider('Nitidez (não é IA)', _detail, (v) => _detail = v),
              if (_busy) ...[
                ValueListenableBuilder<EnhanceProgress>(
                  valueListenable: _job.progress,
                  builder: (_, p, _) => Column(
                    children: [
                      LinearProgressIndicator(
                        value: p.fraction > 0 ? p.fraction : null,
                      ),
                      const SizedBox(height: 8),
                      AppText(p.label),
                    ],
                  ),
                ),
                TextButton(
                  onPressed: _job.cancel,
                  child: const AppText('Cancelar'),
                ),
              ] else ...[
                OutlinedButton.icon(
                  onPressed: () => _run(preview: true),
                  icon: const Icon(Icons.compare),
                  label: const AppText('Comparar antes e depois'),
                ),
                FilledButton.icon(
                  onPressed: () => _run(preview: false),
                  icon: const Icon(Icons.auto_awesome),
                  label: const AppText('Gerar resultado'),
                ),
              ],
              if (_result != null)
                FilledButton.icon(
                  onPressed: _saving ? null : _save,
                  icon: const Icon(Icons.download),
                  label: AppText(_saving ? 'Salvando…' : 'Salvar na galeria'),
                ),
            ],
          ],
        ),
      ),
    ),
  );
  Widget _preview() {
    final player = _player;
    if (player != null && player.value.isInitialized) {
      return GestureDetector(
        onTap: () async {
          if (player.value.isPlaying) {
            await player.pause();
          } else {
            await player.play();
          }
          if (mounted) setState(() {});
        },
        child: Stack(
          alignment: Alignment.center,
          children: [
            AspectRatio(
              aspectRatio: player.value.aspectRatio,
              child: VideoPlayer(player),
            ),
            if (!player.value.isPlaying)
              const Icon(Icons.play_circle, color: Colors.white, size: 52),
          ],
        ),
      );
    }
    final path = _showBefore
        ? (_before ?? _source)
        : (_after ?? _before ?? (_video ? null : _source));
    return path == null
        ? const Center(
            child: AppText('Toque em Comparar para ver a prévia',
              style: TextStyle(color: Colors.white),
              textAlign: TextAlign.center,
            ),
          )
        : Image.file(
            File(path),
            fit: BoxFit.contain,
            errorBuilder: (_, _, _) => const Center(
              child: AppText('Use Comparar para preparar a imagem',
                style: TextStyle(color: Colors.white),
              ),
            ),
          );
  }

  Widget _slider(String label, double value, void Function(double) update) =>
      Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const SizedBox(height: 14),
          AppText('$label • ${(value * 100).round()}%'),
          Slider(
            value: value,
            onChanged: _busy ? null : (v) => _changed(() => update(v)),
          ),
        ],
      );
}
