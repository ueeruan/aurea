import 'dart:ffi' as ffi;

import 'package:thermion_flutter/thermion_flutter.dart' as f;

@ffi.Native<
  ffi.Void Function(
    ffi.Pointer<f.TRenderer>,
    ffi.Pointer<f.TView>,
    ffi.Pointer<f.TSwapChain>,
    ffi.Pointer<ffi.NativeFunction<ffi.Void Function(ffi.Bool)>>,
  )
>(
  symbol: 'Aurea_renderPreviewFrameRenderThread',
  assetId: 'package:thermion_dart/thermion_dart.dart',
)
external void _renderPreview(
  ffi.Pointer<f.TRenderer> renderer,
  ffi.Pointer<f.TView> view,
  ffi.Pointer<f.TSwapChain> swapChain,
  ffi.Pointer<ffi.NativeFunction<ffi.Void Function(ffi.Bool)>> callback,
);

Future<bool> renderFilamentPreview(f.ThermionViewer viewer) async {
  var presented = false;
  final surfaces = viewer.app.renderManager
      .getAttachedSwapChains(viewer.view)
      .toList();
  if (surfaces.isEmpty) throw StateError('No surface attached to preview');
  for (final surface in surfaces) {
    presented |= await f.withBoolCallback(
      (callback) => _renderPreview(
        viewer.app.renderer,
        viewer.view.getNativeHandle(),
        surface.getNativeHandle(),
        callback,
      ),
    );
  }
  return presented;
}

@ffi.Native<
  ffi.Void Function(
    ffi.Pointer<f.TRenderer>,
    ffi.Pointer<ffi.NativeFunction<ffi.Void Function(ffi.Float)>>,
  )
>(
  symbol: 'Aurea_getGpuDurationRenderThread',
  assetId: 'package:thermion_dart/thermion_dart.dart',
)
external void _readGpu(
  ffi.Pointer<f.TRenderer> renderer,
  ffi.Pointer<ffi.NativeFunction<ffi.Void Function(ffi.Float)>> callback,
);

Future<double?> readFilamentGpuMilliseconds(f.ThermionViewer viewer) async {
  final value = await f.withFloatCallback(
    (callback) => _readGpu(viewer.app.renderer, callback),
  );
  return value < 0 ? null : value;
}

@ffi.Native<
  ffi.Void Function(
    ffi.Pointer<f.TView>,
    ffi.Pointer<ffi.NativeFunction<ffi.Void Function(ffi.Float)>>,
  )
>(
  symbol: 'Aurea_getResolutionScaleRenderThread',
  assetId: 'package:thermion_dart/thermion_dart.dart',
)
external void _readScale(
  ffi.Pointer<f.TView> view,
  ffi.Pointer<ffi.NativeFunction<ffi.Void Function(ffi.Float)>> callback,
);

Future<double> readFilamentResolutionScale(f.ThermionViewer viewer) =>
    f.withFloatCallback(
      (callback) => _readScale(viewer.view.getNativeHandle(), callback),
    );

@ffi.Native<
  ffi.Void Function(
    ffi.Pointer<f.TView>,
    ffi.Pointer<f.TRenderer>,
    ffi.Bool,
    ffi.Float,
    ffi.Uint32,
    ffi.Pointer<ffi.NativeFunction<f.VoidCallbackFunction>>,
  )
>(
  symbol: 'Aurea_configureDynamicResolutionRenderThread',
  assetId: 'package:thermion_dart/thermion_dart.dart',
)
external void _configure(
  ffi.Pointer<f.TView> view,
  ffi.Pointer<f.TRenderer> renderer,
  bool enabled,
  double minScale,
  int requestId,
  ffi.Pointer<ffi.NativeFunction<f.VoidCallbackFunction>> callback,
);

Future<void> configureFilamentDynamicResolution(
  f.ThermionViewer viewer, {
  bool exporting = false,
}) => f.withVoidCallback(
  (requestId, callback) => _configure(
    viewer.view.getNativeHandle(),
    viewer.app.renderer,
    !exporting,
    exporting ? 1 : .5,
    requestId,
    callback,
  ),
);
