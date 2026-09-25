// =============================================================================
//  Aurea / platform / ios / bridge / IOSSurfaceView.mm
//
//  A view do preview. UIView cujo layer É um CAMetalLayer (via `+layerClass`).
//
//  A REGRA QUE ESTE ARQUIVO EXISTE PARA GARANTIR: nenhum pixel do preview passa
//  pela CPU. O motor desenha no drawable do layer e o compositor do sistema o
//  mostra. Não há UIImage, não há `drawRect:`, não há captura de tela — é a
//  mesma decisão do Android, onde o preview vai para o ANativeWindow do
//  SurfaceView e nunca por bitmap.
//
//  Três coisas moram aqui, e só elas:
//   1. o CAMetalLayer com o tamanho em PIXELS (o drawableSize segue a escala da
//      tela, não os pontos);
//   2. o CADisplayLink — um tique por vsync chamando `requestRender` no motor;
//   3. o ciclo de vida da superfície: attach quando a view ganha janela,
//      resize quando muda de tamanho, detach quando perde a janela (e o detach
//      ESPERA a GPU largar o layer, como o `nativeDetachSurface` do Android).
// =============================================================================
#import "AureaEngine.h"

#import <QuartzCore/QuartzCore.h>

@implementation AureaMetalView {
    CADisplayLink* _link;
    BOOL _attached;
    int _resizedWidth;     // último tamanho entregue ao motor (attach/resize)
    int _resizedHeight;
}

// O layer da view É o CAMetalLayer. Sem esta linha o UIKit criaria um CALayer
// comum e o motor não teria onde desenhar.
+ (Class)layerClass {
    return [CAMetalLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        [self commonInit];
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder*)coder {
    self = [super initWithCoder:coder];
    if (self) {
        [self commonInit];
    }
    return self;
}

- (void)commonInit {
    self.opaque = YES;
    // Mesma cor da área de trabalho que o motor limpa (RenderSettings::pasteboard),
    // para não piscar preto antes do primeiro quadro.
    self.backgroundColor = [UIColor colorWithRed:0.149 green:0.173 blue:0.208 alpha:1.0];
    self.userInteractionEnabled = YES;
    self.multipleTouchEnabled = YES;
    [self configureLayer];
}

- (CAMetalLayer*)metalLayer {
    return (CAMetalLayer*)self.layer;
}

- (void)configureLayer {
    CAMetalLayer* layer = self.metalLayer;
    if (!layer) return;
    // Um dispositivo só: o mesmo com que o backend do motor foi criado. O layer
    // recusa drawables de outro dispositivo.
    if (!self.device) self.device = MTLCreateSystemDefaultDevice();
    layer.device = self.device;
    // BGRA8 é o formato de apresentação universal do iOS (o backend decide o
    // formato interno do render; o drawable é este).
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    layer.opaque = YES;
    layer.allowsNextDrawableTimeout = NO;
    // Sem `presentsWithTransaction`: o motor apresenta pelo commit normal do
    // drawable, que é o caminho de menor latência (o modo de transação é para
    // sincronizar com o CATransaction, e travaria a thread de render).
    layer.presentsWithTransaction = NO;
    layer.maximumDrawableCount = 3;   // casa com BackendConfig::framesInFlight
    [self updateDrawableSize];
}

- (void)setDevice:(id<MTLDevice>)device {
    // O SwiftUI reatribui a cada atualização da view. Reconfigurar o layer
    // de novo desfazia o que o motor ajustou no attach (framebufferOnly,
    // timeout do drawable) enquanto a thread de render estava em nextDrawable.
    if (_device == device && self.metalLayer.device == device) return;
    _device = device;
    [self configureLayer];
}

- (void)setEngine:(AureaEngine*)engine {
    if (_engine == engine) {
        // Mesmo motor reatribuído: é o caminho de quando ele subiu DEPOIS de a
        // view existir — só tenta o attach, sem mexer em mais nada.
        if (self.window && !_attached) [self attachIfNeeded];
        return;
    }
    _engine = engine;
    // A view pode ganhar o motor depois de já estar na tela (o SwiftUI monta a
    // view antes de a sessão subir o motor).
    if (self.window) [self attachIfNeeded];
}

- (void)updateDrawableSize {
    CAMetalLayer* layer = self.metalLayer;
    if (!layer) return;
    const CGFloat scale = self.window.screen.scale > 0 ? self.window.screen.scale : UIScreen.mainScreen.scale;
    layer.contentsScale = scale;
    // PIXELS, não pontos: é o tamanho que o swapchain vai ter. Arredondado para
    // baixo e no mínimo 1 para nunca pedir um drawable de zero.
    const CGSize pixels = CGSizeMake(MAX(1.0, floor(self.bounds.size.width * scale)),
                                     MAX(1.0, floor(self.bounds.size.height * scale)));
    if (layer.drawableSize.width != pixels.width || layer.drawableSize.height != pixels.height) {
        layer.drawableSize = pixels;
    }
}

- (void)layoutSubviews {
    [super layoutSubviews];
    [self updateDrawableSize];
    if (_attached) {
        // Só quando o tamanho muda: o resize pega o lock do render, e o
        // layout roda em qualquer mudança do SwiftUI em volta.
        const CGSize size = self.metalLayer.drawableSize;
        if ((int)size.width != _resizedWidth || (int)size.height != _resizedHeight) {
            _resizedWidth = (int)size.width; _resizedHeight = (int)size.height;
            [self.engine resizeSurfaceWidth:_resizedWidth height:_resizedHeight];
        }
    }
}

// =============================================================================
// Ciclo de vida da superfície
// =============================================================================
- (void)didMoveToWindow {
    [super didMoveToWindow];
    if (self.window) {
        [self attachIfNeeded];
        [self startDisplayLink];
    } else {
        [self stopDisplayLink];
        [self detach];
    }
}

- (void)attachIfNeeded {
    if (_attached || !self.engine || !self.window) return;
    if (!self.engine.running) return;   // o motor ainda não subiu; `setEngine:` ou o próximo layout volta aqui
    [self updateDrawableSize];
    const CGSize size = self.metalLayer.drawableSize;
    if (size.width < 1 || size.height < 1) return;
    _attached = [self.engine attachMetalLayer:self.metalLayer width:(int)size.width height:(int)size.height];
    _resizedWidth = (int)size.width; _resizedHeight = (int)size.height;
    if (_attached) {
        [self willChangeValueForKey:@"surfaceAttached"];
        _surfaceAttached = YES;
        [self didChangeValueForKey:@"surfaceAttached"];
    }
}

- (void)detach {
    if (!_attached) return;
    // Espera a GPU largar o layer: depois desta chamada a view pode morrer.
    [self.engine detachSurface];
    _attached = NO;
    [self willChangeValueForKey:@"surfaceAttached"];
    _surfaceAttached = NO;
    [self didChangeValueForKey:@"surfaceAttached"];
}

- (void)dealloc {
    // `dealloc` não pode chamar métodos virtuais nem bloquear na GPU por muito
    // tempo, mas o detach precisa acontecer ANTES do layer sumir — a alternativa
    // é o motor desenhar num layer morto.
    [self stopDisplayLink];
    [self detach];
}

// =============================================================================
// CADisplayLink
// =============================================================================
- (void)startDisplayLink {
    if (_link) return;
    if (self.isPaused) return;
    // O display link é criado pela classe CADisplayLink e invalidado
    // explicitamente em stopDisplayLink quando a view sai da tela.
    _link = [CADisplayLink displayLinkWithTarget:self selector:@selector(onTick:)];
    // `common` e não `default`: durante um arrasto o runloop entra em
    // tracking e um link no modo default ficaria parado — o preview congelaria
    // justamente enquanto a pessoa mexe.
    [_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

- (void)stopDisplayLink {
    [_link invalidate];
    _link = nil;
}

- (void)setPaused:(BOOL)paused {
    _paused = paused;
    if (paused) {
        [self stopDisplayLink];
    } else if (self.window) {
        [self startDisplayLink];
    }
}

/// Um tique por vsync. Não desenha nem força: só acorda a thread de render,
/// que coalesce (nada a mostrar = nada é desenhado — ver `render_frame(true)`).
/// Com `requestRender` aqui o motor redesenhava o quadro inteiro a cada vsync,
/// parado ou tocando vídeo de 30 fps a 60 Hz: calor, bateria e o lock do
/// modelo disputado com a UI.
- (void)onTick:(CADisplayLink*)link {
    (void)link;
    [self.engine wakeRender];
}

// =============================================================================
// Toque: repassado ao palco do SwiftUI pela camada de UI, não por aqui. A view
// só declara que aceita toque (o gesto de pinça/arrasto do palco funciona por
// cima dela).
// =============================================================================
- (BOOL)canBecomeFirstResponder {
    return NO;
}

@end
