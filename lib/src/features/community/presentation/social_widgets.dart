import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../application/social_service.dart';
import '../application/conta_da_comunidade.dart';
import '../domain/post_da_comunidade.dart';

class ProfileAvatar extends StatelessWidget {
  const ProfileAvatar({
    super.key,
    required this.name,
    this.url,
    this.radius = 22,
  });
  final String name;
  final String? url;
  final double radius;
  @override
  Widget build(BuildContext context) {
    final fallback = Center(
      child: Text(
        name.trim().isEmpty ? 'A' : name.trim().characters.first.toUpperCase(),
        style: TextStyle(fontSize: radius * .8, fontWeight: FontWeight.w700),
      ),
    );
    final path = url;
    return ClipOval(
      child: SizedBox(
        width: radius * 2,
        height: radius * 2,
        child: ColoredBox(
          color: Theme.of(context).colorScheme.primaryContainer,
          child: path == null || path.isEmpty
              ? fallback
              : path.startsWith('https://')
              ? Image.network(
                  path,
                  fit: BoxFit.cover,
                  cacheWidth: (radius * 6).round(),
                  errorBuilder: (_, e, s) => fallback,
                )
              : Image.file(
                  File(path),
                  fit: BoxFit.cover,
                  errorBuilder: (_, e, s) => fallback,
                ),
        ),
      ),
    );
  }
}

class VerifiedBadge extends StatefulWidget {
  const VerifiedBadge({super.key, this.official = false, this.size = 18});
  final bool official;
  final double size;
  @override
  State<VerifiedBadge> createState() => _VerifiedBadgeState();
}

class _VerifiedBadgeState extends State<VerifiedBadge>
    with SingleTickerProviderStateMixin {
  late final AnimationController _animation = AnimationController(
    vsync: this,
    duration: const Duration(seconds: 3),
  );
  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    if (widget.official && !MediaQuery.disableAnimationsOf(context)) {
      _animation.repeat();
    } else {
      _animation.stop();
    }
  }

  @override
  void dispose() {
    _animation.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Tooltip(
    message: widget.official ? 'Aurea oficial' : 'Conta verificada',
    child: Semantics(
      label: widget.official ? 'Aurea oficial verificado' : 'Conta verificada',
      child: AnimatedBuilder(
        animation: _animation,
        builder: (_, _) => ShaderMask(
          blendMode: BlendMode.srcIn,
          shaderCallback: (rect) => LinearGradient(
            begin: Alignment(-3 + _animation.value * 6, -1),
            end: Alignment(-2 + _animation.value * 6, 1),
            colors: widget.official
                ? const [Color(0xff258bff), Colors.white, Color(0xff258bff)]
                : const [Color(0xff258bff), Color(0xff258bff)],
          ).createShader(rect),
          child: Icon(
            Icons.verified_rounded,
            color: Colors.white,
            size: widget.size,
          ),
        ),
      ),
    ),
  );
}

class PostLikeButton extends ConsumerStatefulWidget {
  const PostLikeButton({super.key, required this.post});
  final PostDaComunidade post;
  @override
  ConsumerState<PostLikeButton> createState() => _PostLikeButtonState();
}

class _PostLikeButtonState extends ConsumerState<PostLikeButton> {
  late bool _liked = widget.post.curtiu;
  late int _count = widget.post.curtidas;
  bool _busy = false;
  @override
  void didUpdateWidget(PostLikeButton old) {
    super.didUpdateWidget(old);
    if (!_busy) {
      _liked = widget.post.curtiu;
      _count = widget.post.curtidas;
    }
  }

  Future<void> _toggle() async {
    final account = ref.read(contaDaComunidadeProvider);
    if (account == null || _busy) return;
    setState(() => _busy = true);
    try {
      final r = await ref
          .read(socialServiceProvider)
          .request(
            '/social/posts/${widget.post.id}/like',
            account.codigo,
            method: _liked ? 'DELETE' : 'POST',
          );
      if (mounted) {
        setState(() {
          _liked = r['curtiu'] == true;
          _count = (r['curtidas'] as num).toInt();
        });
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text('$e')));
      }
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) => Row(
    mainAxisSize: MainAxisSize.min,
    children: [
      IconButton(
        tooltip: _liked ? 'Descurtir' : 'Curtir',
        onPressed: _busy ? null : _toggle,
        icon: Icon(
          _liked ? Icons.favorite : Icons.favorite_border,
          color: _liked ? Colors.pinkAccent : null,
        ),
      ),
      if (_count > 0) Text('$_count'),
    ],
  );
}
