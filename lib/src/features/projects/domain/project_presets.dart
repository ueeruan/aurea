import 'package:flutter/cupertino.dart';

/// Opcoes de configuracao oferecidas ao criar um projeto.
class AspectOption {
  const AspectOption({
    required this.key,
    required this.label,
    required this.hint,
    required this.ratio,
    required this.icon,
  });

  final String key;
  final String label;
  final String hint;
  final double ratio;
  final IconData icon;
}

abstract final class ProjectPresets {
  static const aspects = <AspectOption>[
    AspectOption(
      key: '16:9',
      label: '16:9',
      hint: 'YouTube / TV',
      ratio: 16 / 9,
      icon: CupertinoIcons.tv,
    ),
    AspectOption(
      key: '9:16',
      label: '9:16',
      hint: 'Reels / TikTok',
      ratio: 9 / 16,
      icon: CupertinoIcons.device_phone_portrait,
    ),
    AspectOption(
      key: '1:1',
      label: '1:1',
      hint: 'Feed',
      ratio: 1,
      icon: CupertinoIcons.square,
    ),
    AspectOption(
      key: '4:5',
      label: '4:5',
      hint: 'Instagram',
      ratio: 4 / 5,
      icon: CupertinoIcons.photo,
    ),
  ];

  static const resolutions = <int>[720, 1080, 2160];
  static const fpsOptions = <int>[24, 30, 60];

  static AspectOption aspectByKey(String key) => aspects.firstWhere(
        (a) => a.key == key,
        orElse: () => aspects.first,
      );

  static String resolutionLabel(int height) => switch (height) {
        720 => 'HD 720p',
        1080 => 'Full HD 1080p',
        2160 => '4K 2160p',
        _ => '${height}p',
      };
}
