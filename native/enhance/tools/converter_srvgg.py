"""Converte um SRVGGNetCompact do Real-ESRGAN (.pth) para ncnn, SEM PyTorch.

Serve para os modelos compactos oficiais (realesr-animevideov3,
realesr-general-x4v3 e realesr-general-wdn-x4v3; release v0.2.5.0 de
https://github.com/xinntao/Real-ESRGAN). Precisa so de numpy.

Seguranca: o .pth e um zip com um pickle. O pickle NUNCA e executado livre:
o leitor so aceita os tres globais que um state_dict de float32 usa
(collections.OrderedDict, torch._utils._rebuild_tensor_v2 e
torch.FloatStorage) e recusa qualquer outro.

Arquitetura (basicsr/archs/srvgg_arch.py):
    conv(3->64) prelu, [conv(64->64) prelu] x num_conv, conv(64->3*r*r),
    pixel_shuffle(r) + nearest(x, r)
Formato ncnn igual ao do pacote oficial realesrgan-ncnn-vulkan: blob de
entrada "data" (RGB 0..1), saida "output"; pesos das convolucoes em fp16
(marca 0x01306B47), bias e inclinacoes do PReLU em float32.

Uso:
    python converter_srvgg.py converter <modelo.pth> <saida.param> <saida.bin> [--fp32]
    python converter_srvgg.py conferir-bin <modelo.pth> <oficial.param> <oficial.bin>
    python converter_srvgg.py conferir-rede <modelo.pth> <x4.param> <x4.bin> <aurea_enhance.dll> [--gpu]
    python converter_srvgg.py conferir-dni <a.pth> <b.pth> <peso_a> <x4.param> <a.bin> <b.bin> <aurea_enhance.dll>
"""
import collections
import ctypes
import io
import math
import pickle
import struct
import sys
import zipfile

import numpy as np

MARCA_FP16 = 0x01306B47


# ------------------------------------------------------------------ leitura
def _reconstruir(storage, offset, tamanho, passo, *_resto):
    n = 1
    for d in tamanho:
        n *= d
    esperado = []
    acumulado = 1
    for d in reversed(tamanho):
        esperado.append(acumulado)
        acumulado *= d
    esperado = tuple(reversed(esperado))
    if n > 1 and tuple(passo) != esperado:
        raise ValueError('tensor nao contiguo: %r %r' % (tamanho, passo))
    return np.array(storage[offset:offset + n], dtype=np.float32).reshape(tamanho)


class _Leitor(pickle.Unpickler):
    def __init__(self, arquivo, zipado, prefixo):
        super().__init__(arquivo)
        self._zip = zipado
        self._prefixo = prefixo

    def find_class(self, modulo, nome):
        if (modulo, nome) == ('collections', 'OrderedDict'):
            return collections.OrderedDict
        if (modulo, nome) == ('torch._utils', '_rebuild_tensor_v2'):
            return _reconstruir
        if (modulo, nome) == ('torch', 'FloatStorage'):
            return 'float32'
        raise pickle.UnpicklingError('global recusado no .pth: %s.%s' % (modulo, nome))

    def persistent_load(self, pid):
        tipo, dtype, chave, _local, numel = pid
        if tipo != 'storage' or dtype != 'float32':
            raise pickle.UnpicklingError('armazenamento inesperado: %r' % (pid,))
        dados = self._zip.read('%s/data/%s' % (self._prefixo, chave))
        arr = np.frombuffer(dados, dtype='<f4')
        if arr.size != numel:
            raise pickle.UnpicklingError('tamanho do armazenamento %s' % chave)
        return arr


def ler_pth(caminho):
    with zipfile.ZipFile(caminho) as z:
        pkl = [n for n in z.namelist() if n.endswith('/data.pkl')]
        if len(pkl) != 1:
            raise ValueError('nao e um .pth zip do PyTorch')
        prefixo = pkl[0][: -len('/data.pkl')]
        obj = _Leitor(io.BytesIO(z.read(pkl[0])), z, prefixo).load()
    for chave in ('params_ema', 'params'):
        if isinstance(obj, dict) and chave in obj:
            obj = obj[chave]
            break
    return collections.OrderedDict((k, v) for k, v in obj.items())


def camadas(estado):
    """Lista [('conv', w, b) | ('prelu', a)] na ordem do corpo, conferindo
    que e um SRVGGNetCompact com PReLU."""
    corpo = []
    i = 0
    while 'body.%d.weight' % i in estado:
        w = estado['body.%d.weight' % i]
        if w.ndim == 4:
            b = estado['body.%d.bias' % i]
            if w.shape[2:] != (3, 3) or b.shape != (w.shape[0],):
                raise ValueError('convolucao inesperada em body.%d' % i)
            corpo.append(('conv', w, b))
        elif w.ndim == 1:
            corpo.append(('prelu', w))
        else:
            raise ValueError('camada inesperada em body.%d' % i)
        i += 1
    sobra = [k for k in estado if not k.startswith('body.')]
    if sobra:
        raise ValueError('chaves fora do corpo: %r' % sobra[:5])
    if len(corpo) < 3 or corpo[0][0] != 'conv' or corpo[-1][0] != 'conv':
        raise ValueError('corpo nao comeca e termina em convolucao')
    for k in range(1, len(corpo) - 1):
        esperado = 'prelu' if k % 2 == 1 else 'conv'
        if corpo[k][0] != esperado:
            raise ValueError('ordem conv/prelu quebrada em %d' % k)
    saida = corpo[-1][1].shape[0]
    escala = int(round(math.sqrt(saida / 3)))
    if escala * escala * 3 != saida:
        raise ValueError('ultima convolucao nao e 3*r*r')
    return corpo, escala


# ------------------------------------------------------------------ escrita
def escrever_ncnn(corpo, escala, caminho_param, caminho_bin, fp16=True):
    linhas = []
    blobs = ['data', 'data_0', 'data_1']
    linhas.append(('Input', 'entrada', 0, 1, [], ['data'], ''))
    linhas.append(('Split', 'divide', 1, 2, ['data'], ['data_0', 'data_1'], ''))
    atual = 'data_1'
    bin_ = io.BytesIO()
    nconv = 0
    nprelu = 0
    for camada in corpo:
        if camada[0] == 'conv':
            w, b = camada[1], camada[2]
            saida = 'c%d' % nconv
            linhas.append(('Convolution', 'conv%d' % nconv, 1, 1, [atual], [saida],
                           '0=%d 1=3 4=1 5=1 6=%d' % (w.shape[0], w.size)))
            if fp16:
                bin_.write(struct.pack('<I', MARCA_FP16))
                meio = w.astype('<f2').tobytes()
                bin_.write(meio)
                bin_.write(b'\0' * ((4 - len(meio) % 4) % 4))
            else:
                bin_.write(struct.pack('<I', 0))
                bin_.write(w.astype('<f4').tobytes())
            bin_.write(b.astype('<f4').tobytes())
            nconv += 1
        else:
            a = camada[1]
            saida = 'p%d' % nprelu
            linhas.append(('PReLU', 'prelu%d' % nprelu, 1, 1, [atual], [saida], '0=%d' % a.size))
            bin_.write(a.astype('<f4').tobytes())
            nprelu += 1
        blobs.append(saida)
        atual = saida
    linhas.append(('PixelShuffle', 'embaralha', 1, 1, [atual], ['ps'], '0=%d' % escala))
    linhas.append(('Interp', 'amplia', 1, 1, ['data_0'], ['base'],
                   '0=1 1=%.6e 2=%.6e' % (escala, escala)))
    linhas.append(('BinaryOp', 'soma', 2, 1, ['ps', 'base'], ['output'], '0=0'))
    blobs += ['ps', 'base', 'output']
    with io.open(caminho_param, 'w', encoding='ascii', newline='\n') as f:
        f.write('7767517\n%d %d\n' % (len(linhas), len(blobs)))
        for tipo, nome, ni, no, ent, sai, params in linhas:
            f.write(' '.join([tipo, nome, str(ni), str(no)] + ent + sai + ([params] if params else [])) + '\n')
    with open(caminho_bin, 'wb') as f:
        f.write(bin_.getvalue())


# ------------------------------------------------------------------ leitura ncnn
def ler_param(caminho):
    linhas = io.open(caminho, encoding='ascii').read().split('\n')
    assert linhas[0].strip() == '7767517'
    out = []
    for l in linhas[2:]:
        partes = l.split()
        if not partes:
            continue
        tipo = partes[0]
        ni, no = int(partes[2]), int(partes[3])
        params = {}
        for kv in partes[4 + ni + no:]:
            k, v = kv.split('=')
            params[int(k)] = v
        out.append((tipo, params))
    return out


def pesos_do_bin(param, caminho_bin):
    """Lista de arrays float32 na ordem do arquivo (pesos, bias, inclinacoes)."""
    d = open(caminho_bin, 'rb').read()
    pos = 0
    out = []

    def cru(n):
        nonlocal pos
        a = np.frombuffer(d, dtype='<f4', count=n, offset=pos).astype(np.float32)
        pos += 4 * n
        return a

    for tipo, p in param:
        if tipo == 'Convolution':
            n = int(p[6])
            (marca,) = struct.unpack_from('<I', d, pos)
            pos += 4
            if marca == MARCA_FP16:
                a = np.frombuffer(d, dtype='<f2', count=n, offset=pos).astype(np.float32)
                pos += (2 * n + 3) // 4 * 4
            elif marca == 0:
                a = cru(n)
            else:
                raise ValueError('marca de peso nao suportada: %x' % marca)
            out.append(a)
            if p.get(5, '0') == '1':
                out.append(cru(int(p[0])))
        elif tipo == 'PReLU':
            out.append(cru(int(p.get(0, '1'))))
        elif tipo in ('Input', 'Split', 'PixelShuffle', 'Interp', 'BinaryOp'):
            continue
        else:
            raise ValueError('camada com pesos desconhecida: %s' % tipo)
    if pos != len(d):
        raise ValueError('sobraram %d bytes no bin' % (len(d) - pos))
    return out


# ------------------------------------------------------------------ referencia
def _conv3(x, w, b):
    c, h, wd = x.shape
    pad = np.pad(x, ((0, 0), (1, 1), (1, 1)))
    col = np.lib.stride_tricks.sliding_window_view(pad, (3, 3), axis=(1, 2))  # c,h,w,3,3
    col = col.transpose(1, 2, 0, 3, 4).reshape(h, wd, c * 9)
    y = col @ w.reshape(w.shape[0], -1).T + b
    return y.transpose(2, 0, 1)


def rede_numpy(corpo, escala, rgb01):
    """SRVGGNetCompact em numpy (float64). rgb01: (h, w, 3) em 0..1."""
    x = rgb01.transpose(2, 0, 1).astype(np.float64)
    y = x
    for camada in corpo:
        if camada[0] == 'conv':
            y = _conv3(y, camada[1].astype(np.float64), camada[2].astype(np.float64))
        else:
            a = camada[1].astype(np.float64)[:, None, None]
            y = np.maximum(y, 0) + a * np.minimum(y, 0)
    c, h, w = y.shape
    r = escala
    y = y.reshape(c // (r * r), r, r, h, w).transpose(0, 3, 1, 4, 2).reshape(c // (r * r), h * r, w * r)
    base = x.repeat(r, axis=1).repeat(r, axis=2)
    return (y + base).transpose(1, 2, 0)


def _dll(caminho):
    dll = ctypes.CDLL(caminho)
    dll.ae_create.restype = ctypes.c_void_p
    dll.ae_create.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int32, ctypes.c_int32, ctypes.c_char_p, ctypes.c_int32]
    dll.ae_destroy.argtypes = [ctypes.c_void_p]
    dll.ae_process.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int32, ctypes.c_int32, ctypes.c_int32, ctypes.c_float, ctypes.c_void_p, ctypes.c_void_p]
    if hasattr(dll, 'ae_create_dni'):
        dll.ae_create_dni.restype = ctypes.c_void_p
        dll.ae_create_dni.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_float, ctypes.c_int32, ctypes.c_int32, ctypes.c_char_p, ctypes.c_int32]
    return dll


def _cena(h, w, semente=7):
    rng = np.random.default_rng(semente)
    yy, xx = np.mgrid[0:h, 0:w]
    img = np.stack([
        0.5 + 0.35 * np.sin(xx / 3.1) * np.cos(yy / 4.3),
        (xx / w) * 0.8 + 0.1,
        0.5 + 0.3 * np.cos((xx + yy) / 2.7),
    ], axis=-1) + rng.normal(0, 0.04, (h, w, 3))
    return np.clip(np.round(img * 255), 0, 255).astype(np.uint8)


def _comparar_com_motor(dll, motor, corpo, escala, rotulo):
    h, w = 24, 32
    rgb = _cena(h, w)
    out = np.zeros((h * escala, w * escala, 3), np.uint8)
    rc = dll.ae_process(motor, rgb.ctypes.data, w, h, escala, 1.0, out.ctypes.data, None)
    assert rc == 0, rc
    ref = rede_numpy(corpo, escala, rgb / 255.0)
    ref8 = np.clip(ref * 255, 0, 255)
    # O motor arredonda depois do clamp em float; a referencia faz igual.
    ref8 = np.floor(ref8 + 0.5).astype(np.int32)
    diff = np.abs(out.astype(np.int32) - ref8)
    print('%s: diferenca maxima %d, media %.4f niveis (de 255), %d pixels acima de 2' % (
        rotulo, diff.max(), diff.mean(), int((diff > 2).sum())))
    return diff


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == 'converter':
        corpo, escala = camadas(ler_pth(argv[2]))
        escrever_ncnn(corpo, escala, argv[3], argv[4], fp16='--fp32' not in argv)
        print('ok: %d camadas no corpo, escala x%d' % (len(corpo), escala))
        return 0
    if cmd == 'conferir-bin':
        corpo, escala = camadas(ler_pth(argv[2]))
        oficiais = pesos_do_bin(ler_param(argv[3]), argv[4])
        nossos = []
        for c in corpo:
            nossos += [c[1].reshape(-1), c[2]] if c[0] == 'conv' else [c[1]]
        assert len(oficiais) == len(nossos), (len(oficiais), len(nossos))
        pior = 0.0
        for a, b in zip(oficiais, nossos):
            assert a.size == b.size
            pior = max(pior, float(np.abs(a - b.astype(np.float16).astype(np.float32)).max()))
        print('bin oficial x conversao: %d blocos, diferenca maxima %.3g (depois de fp16)' % (len(nossos), pior))
        return 0
    if cmd == 'conferir-rede':
        corpo, escala = camadas(ler_pth(argv[2]))
        dll = _dll(argv[5])
        err = ctypes.create_string_buffer(256)
        motor = dll.ae_create(argv[3].encode(), argv[4].encode(), escala, 1 if '--gpu' in argv else 0, err, 256)
        assert motor, err.value
        try:
            diff = _comparar_com_motor(dll, motor, corpo, escala, 'ncnn x referencia numpy')
        finally:
            dll.ae_destroy(motor)
        return 0 if diff.max() <= 3 else 1
    if cmd == 'conferir-dni':
        ca, escala = camadas(ler_pth(argv[2]))
        cb, _ = camadas(ler_pth(argv[3]))
        peso = float(argv[4])
        mistura = []
        for a, b in zip(ca, cb):
            if a[0] == 'conv':
                mistura.append(('conv', peso * a[1] + (1 - peso) * b[1], peso * a[2] + (1 - peso) * b[2]))
            else:
                mistura.append(('prelu', peso * a[1] + (1 - peso) * b[1]))
        dll = _dll(argv[8])
        err = ctypes.create_string_buffer(256)
        motor = dll.ae_create_dni(argv[5].encode(), argv[6].encode(), argv[7].encode(), peso, escala, 0, err, 256)
        assert motor, err.value
        try:
            diff = _comparar_com_motor(dll, motor, mistura, escala, 'DNI peso %.2f: ncnn x referencia numpy' % peso)
        finally:
            dll.ae_destroy(motor)
        return 0 if diff.max() <= 3 else 1
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
