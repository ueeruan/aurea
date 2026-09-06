import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:photo_manager/photo_manager.dart';

enum GalleryAccess { full, limited, denied, unavailable }

class GalleryAlbum {
  const GalleryAlbum(this.id, this.name);
  final String id;
  final String name;
}

class GalleryAsset {
  const GalleryAsset(
    this.id, {
    required this.video,
    this.duration = Duration.zero,
  });
  final String id;
  final bool video;
  final Duration duration;
}

/// Metadata is paged. Full files are requested only after an explicit tap.
class GalleryService {
  final _albums = <String, AssetPathEntity>{};
  final _assets = <String, AssetEntity>{};
  static const _permission = PermissionRequestOption(
    androidPermission: AndroidPermission(
      type: RequestType.common,
      mediaLocation: false,
    ),
  );

  Future<GalleryAccess> requestAccess() async {
    if (!Platform.isAndroid && !Platform.isIOS && !Platform.isMacOS) {
      return GalleryAccess.unavailable;
    }
    final permission = await PhotoManager.requestPermissionExtend(
      requestOption: _permission,
    );
    if (permission == PermissionState.limited) return GalleryAccess.limited;
    return permission.hasAccess ? GalleryAccess.full : GalleryAccess.denied;
  }

  Future<List<GalleryAlbum>> albums() async {
    final paths = await PhotoManager.getAssetPathList(type: RequestType.common);
    _albums.clear();
    _assets.clear();
    for (final path in paths) {
      _albums[path.id] = path;
    }
    return [
      for (final path in paths)
        GalleryAlbum(path.id, path.isAll ? 'Todos' : path.name),
    ];
  }

  Future<List<GalleryAsset>> page(String album, int page, int size) async {
    final path = _albums[album];
    if (path == null) return [];
    final assets = await path.getAssetListPaged(page: page, size: size);
    for (final asset in assets) {
      _assets[asset.id] = asset;
    }
    return [
      for (final asset in assets)
        GalleryAsset(
          asset.id,
          video: asset.type == AssetType.video,
          duration: Duration(seconds: asset.duration),
        ),
    ];
  }

  Future<Uint8List?> thumbnail(GalleryAsset asset) => _assets[asset.id]!
      .thumbnailDataWithSize(const ThumbnailSize.square(200), quality: 75);

  Future<File?> file(GalleryAsset asset) async {
    // file (not originFile) delivers a compatible image representation on iOS
    // and downloads cloud assets on demand. Never use thumbnail bytes to import.
    return _assets[asset.id]?.loadFile(isOrigin: false);
  }

  Future<void> settings() async {
    await PhotoManager.openSetting();
  }

  Future<void> selectMore() async {
    await PhotoManager.presentLimited(type: RequestType.common);
  }
}

final galleryServiceProvider = Provider.autoDispose<GalleryService>(
  (ref) => GalleryService(),
);
