import 'package:flutter/material.dart';

import 'core/theme/app_theme.dart';
import 'features/projects/presentation/home_shell.dart';

class AureaApp extends StatelessWidget {
  const AureaApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Aurea',
      debugShowCheckedModeBanner: false,
      theme: AppTheme.dark,
      home: const HomeShell(),
    );
  }
}
