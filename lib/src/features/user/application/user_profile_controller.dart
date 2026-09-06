import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/storage/prefs.dart';

/// Perfil local do usuario (sem conta remota por enquanto).
class UserProfile {
  const UserProfile({this.name = 'Criador(a)', this.email = ''});

  final String name;
  final String email;

  String get initial => name.trim().isEmpty ? 'A' : name.trim()[0].toUpperCase();

  UserProfile copyWith({String? name, String? email}) {
    return UserProfile(name: name ?? this.name, email: email ?? this.email);
  }
}

class UserProfileController extends Notifier<UserProfile> {
  static const _kName = 'user.name';
  static const _kEmail = 'user.email';

  @override
  UserProfile build() {
    final prefs = ref.read(sharedPreferencesProvider);
    return UserProfile(
      name: prefs.getString(_kName) ?? 'Criador(a)',
      email: prefs.getString(_kEmail) ?? '',
    );
  }

  void setName(String name) {
    final value = name.trim();
    if (value.isEmpty) return;
    state = state.copyWith(name: value);
    ref.read(sharedPreferencesProvider).setString(_kName, value);
  }

  void setEmail(String email) {
    state = state.copyWith(email: email.trim());
    ref.read(sharedPreferencesProvider).setString(_kEmail, email.trim());
  }
}

final userProfileProvider =
    NotifierProvider<UserProfileController, UserProfile>(
        UserProfileController.new);
