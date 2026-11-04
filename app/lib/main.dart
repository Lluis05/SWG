import 'package:flutter/material.dart';

import 'ui/app_colors.dart';
import 'ui/dashboard_screen.dart';

void main() {
  runApp(const SowGuardApp());
}

/// Root of the app: theme + first screen.
class SowGuardApp extends StatelessWidget {
  const SowGuardApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'SowGuard',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(colorSchemeSeed: AppColors.barnGreen, useMaterial3: true),
      home: const DashboardScreen(),
    );
  }
}
