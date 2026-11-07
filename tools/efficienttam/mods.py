"""Export-friendly rewrites of the EfficientTAM-Ti 512 pieces.

The rewrites keep the math of the upstream modules and reuse their weights. Rules they follow,
so the graphs convert to the ncnn commit AUREA pins (2025-05-03):
  * tensor rank <= 4 once the leading batch 1 is dropped; Linear only on (1, tokens, C)
  * attention spelled as matmul + softmax + matmul (the pinned ncnn has no SDPA layer)
  * RoPE as real arithmetic: q/k projection rows are permuted so complex pairs sit in the
    [re | im] halves. q and k are permuted alike, so the dot product is unchanged
  * attention scale folded into the q projection
  * everything that depends only on shape (pos enc, RoPE tables, image PE, ViT abs pos) is constant
"""
import copy
import math

import torch
import torch.nn as nn
import torch.nn.functional as F

IMG = 512
GRID = 32
TOK = GRID * GRID
WS = 14
PADDED = 42
NWIN = 3
WTOK = WS * WS


def clone_ln(src, dim, eps=None):
    ln = nn.LayerNorm(dim, eps=src.eps if eps is None else eps)
    ln.weight.data.copy_(src.weight.data)
    ln.bias.data.copy_(src.bias.data)
    return ln


def ln2d(ln, x, c, h, w):
    t = x.reshape(1, c, h * w).permute(0, 2, 1)
    t = ln(t)
    return t.permute(0, 2, 1).reshape(1, c, h, w)


class VitAttn(nn.Module):
    def __init__(self, a, windowed):
        super().__init__()
        self.windowed = windowed
        self.heads = a.num_heads
        dim = a.qkv.in_features
        self.hd = dim // self.heads
        self.qkv = copy.deepcopy(a.qkv)
        self.qkv.weight.data[:dim] *= a.scale
        self.qkv.bias.data[:dim] *= a.scale
        self.proj = copy.deepcopy(a.proj)

    def split(self, t):
        if self.windowed:
            t = t.reshape(1, NWIN * NWIN, WTOK, self.heads, self.hd).permute(0, 1, 3, 2, 4)
            return t.reshape(1, NWIN * NWIN * self.heads, WTOK, self.hd)
        return t.reshape(1, TOK, self.heads, self.hd).permute(0, 2, 1, 3)

    def forward(self, x):
        q, k, v = torch.chunk(self.qkv(x), 3, dim=-1)
        q, k, v = self.split(q), self.split(k), self.split(v)
        o = torch.matmul(torch.softmax(torch.matmul(q, k.transpose(-1, -2)), dim=-1), v)
        if self.windowed:
            o = o.reshape(1, NWIN * NWIN, self.heads, WTOK, self.hd).permute(0, 1, 3, 2, 4)
            o = o.reshape(1, NWIN * NWIN * WTOK, self.heads * self.hd)
        else:
            o = o.permute(0, 2, 1, 3).reshape(1, TOK, self.heads * self.hd)
        return self.proj(o)


class VitBlock(nn.Module):
    def __init__(self, b):
        super().__init__()
        self.windowed = b.window_size > 0
        self.dim = b.norm1.normalized_shape[0]
        self.norm1 = clone_ln(b.norm1, self.dim)
        self.norm2 = clone_ln(b.norm2, self.dim)
        self.attn = VitAttn(b.attn, self.windowed)
        self.mlp = copy.deepcopy(b.mlp)

    def partition(self, x):
        c = self.dim
        x = x.reshape(1, GRID, GRID, c)
        x = F.pad(x, (0, 0, 0, PADDED - GRID, 0, PADDED - GRID))
        x = x.reshape(1, NWIN, WS, NWIN, WS * c).permute(0, 1, 3, 2, 4)
        return x.reshape(1, NWIN * NWIN * WTOK, c)

    def unpartition(self, x):
        c = self.dim
        x = x.reshape(1, NWIN, NWIN, WS, WS * c).permute(0, 1, 3, 2, 4)
        x = x.reshape(1, PADDED, PADDED, c)[:, :GRID, :GRID, :]
        return x.reshape(1, TOK, c)

    def forward(self, x):
        n = self.norm1(x)
        if self.windowed:
            n = self.unpartition(self.attn(self.partition(n)))
        else:
            n = self.attn(n)
        x = x + n
        return x + self.mlp(self.norm2(x))


class ImageEncoder(nn.Module):
    """(1,3,512,512) normalized image -> (1,256,32,32)"""

    def __init__(self, m):
        super().__init__()
        from efficient_track_anything.modeling.backbones.utils import get_abs_pos
        t = m.image_encoder.trunk
        self.patch = copy.deepcopy(t.patch_embed.proj)
        pos = get_abs_pos(t.pos_embed, t.pretrain_use_cls_token, (GRID, GRID))
        self.pos = nn.Parameter(pos.reshape(1, TOK, -1).clone())
        self.blocks = nn.ModuleList([VitBlock(b) for b in t.blocks])
        c = m.image_encoder.neck.convs[0]
        self.c1 = copy.deepcopy(c.conv_1x1)
        self.c3 = copy.deepcopy(c.conv_3x3)
        self.n0 = clone_ln(c.norm_0, 256)
        self.n1 = clone_ln(c.norm_1, 256)

    def forward(self, x):
        x = self.patch(x).reshape(1, 192, TOK).permute(0, 2, 1) + self.pos
        for b in self.blocks:
            x = b(x)
        x = x.permute(0, 2, 1).reshape(1, 192, GRID, GRID)
        x = ln2d(self.n0, self.c1(x), 256, GRID, GRID)
        return ln2d(self.n1, self.c3(x), 256, GRID, GRID)


class PromptEmbed(nn.Module):
    """coords (1,N,2) in 0..1, onehot (1,N,5) over labels [-1,0,1,2,3] -> sparse tokens (1,N,256)"""

    def __init__(self, m):
        super().__init__()
        p = m.sam_prompt_encoder
        self.gauss = nn.Parameter(p.pe_layer.positional_encoding_gaussian_matrix.clone() * (2 * math.pi))
        self.table = nn.Parameter(torch.cat([p.not_a_point_embed.weight] +
                                            [e.weight for e in p.point_embeddings], 0).clone())

    def forward(self, coords, onehot):
        c = torch.matmul(coords * 2 - 1, self.gauss)
        pe = torch.cat((torch.sin(c), torch.cos(c)), dim=-1)
        real = 1 - onehot[:, :, 0:1]
        return pe * real + torch.matmul(onehot, self.table)


class MaskPrompt(nn.Module):
    """previous low-res logits (1,1,128,128) -> dense prompt (1,256,32,32)"""

    def __init__(self, m):
        super().__init__()
        d = m.sam_prompt_encoder.mask_downscaling
        self.c0, self.c1, self.c2 = copy.deepcopy(d[0]), copy.deepcopy(d[3]), copy.deepcopy(d[6])
        self.n0 = clone_ln(d[1], 4)
        self.n1 = clone_ln(d[4], 16)
        self.act = nn.GELU()

    def forward(self, x):
        x = self.act(ln2d(self.n0, self.c0(x), 4, 64, 64))
        x = self.act(ln2d(self.n1, self.c1(x), 16, 32, 32))
        return self.c2(x)


class Att(nn.Module):
    def __init__(self, a):
        super().__init__()
        self.h = a.num_heads
        self.d = a.internal_dim // a.num_heads
        self.q = copy.deepcopy(a.q_proj)
        s = 1.0 / math.sqrt(self.d)
        self.q.weight.data *= s
        self.q.bias.data *= s
        self.k = copy.deepcopy(a.k_proj)
        self.v = copy.deepcopy(a.v_proj)
        self.o = copy.deepcopy(a.out_proj)

    def split(self, t):
        return t.reshape(1, -1, self.h, self.d).permute(0, 2, 1, 3)

    def forward(self, q, k, v):
        q, k, v = self.split(self.q(q)), self.split(self.k(k)), self.split(self.v(v))
        o = torch.matmul(torch.softmax(torch.matmul(q, k.transpose(-1, -2)), dim=-1), v)
        return self.o(o.permute(0, 2, 1, 3).reshape(1, -1, self.h * self.d))


class TwoWayBlock(nn.Module):
    def __init__(self, b):
        super().__init__()
        self.skip_pe = b.skip_first_layer_pe
        self.self_attn = Att(b.self_attn)
        self.t2i = Att(b.cross_attn_token_to_image)
        self.i2t = Att(b.cross_attn_image_to_token)
        self.mlp = copy.deepcopy(b.mlp)
        self.n1, self.n2 = clone_ln(b.norm1, 256), clone_ln(b.norm2, 256)
        self.n3, self.n4 = clone_ln(b.norm3, 256), clone_ln(b.norm4, 256)

    def forward(self, queries, keys, query_pe, key_pe):
        if self.skip_pe:
            queries = self.self_attn(queries, queries, queries)
        else:
            q = queries + query_pe
            queries = queries + self.self_attn(q, q, queries)
        queries = self.n1(queries)
        q, k = queries + query_pe, keys + key_pe
        queries = self.n2(queries + self.t2i(q, k, keys))
        queries = self.n3(queries + self.mlp(queries))
        q, k = queries + query_pe, keys + key_pe
        keys = self.n4(keys + self.i2t(k, q, queries))
        return queries, keys


class MaskDecoder(nn.Module):
    """pix (1,256,32,32), dense (1,256,32,32), sparse (1,N,256)
    -> masks (1,4,128,128), iou (1,4), obj_ptr for each token (1,4,256), object score (1,1)"""

    def __init__(self, m):
        super().__init__()
        d = m.sam_mask_decoder
        t = d.transformer
        self.tokens = nn.Parameter(torch.cat([d.obj_score_token.weight, d.iou_token.weight,
                                              d.mask_tokens.weight], 0)[None].clone())
        pe = m.sam_prompt_encoder.get_dense_pe().reshape(1, 256, TOK).permute(0, 2, 1)
        self.image_pe = nn.Parameter(pe.clone().contiguous())
        self.layers = nn.ModuleList([TwoWayBlock(b) for b in t.layers])
        self.final = Att(t.final_attn_token_to_image)
        self.nf = clone_ln(t.norm_final_attn, 256)
        u = d.output_upscaling
        self.dc1, self.dc2 = copy.deepcopy(u[0]), copy.deepcopy(u[3])
        self.ln = clone_ln(u[1], 64)
        self.act = nn.GELU()
        self.hyper = nn.ModuleList([copy.deepcopy(x) for x in d.output_hypernetworks_mlps])
        self.iou = copy.deepcopy(d.iou_prediction_head)
        self.score = copy.deepcopy(d.pred_obj_score_head)
        self.ptr = copy.deepcopy(m.obj_ptr_proj)

    def forward(self, pix, dense, sparse):
        tokens = torch.cat((self.tokens, sparse), dim=1)
        keys = (pix + dense).reshape(1, 256, TOK).permute(0, 2, 1)
        queries = tokens
        for l in self.layers:
            queries, keys = l(queries, keys, tokens, self.image_pe)
        q, k = queries + tokens, keys + self.image_pe
        queries = self.nf(queries + self.final(q, k, keys))

        src = keys.permute(0, 2, 1).reshape(1, 256, GRID, GRID)
        up = self.act(ln2d(self.ln, self.dc1(src), 64, 64, 64))
        up = self.act(self.dc2(up))

        mask_tokens = queries[:, 2:6, :]
        hyper = torch.cat([self.hyper[i](queries[:, 2 + i:3 + i, :]) for i in range(4)], dim=1)
        masks = torch.matmul(hyper, up.reshape(1, 32, 128 * 128)).reshape(1, 4, 128, 128)
        return masks, self.iou(queries[:, 1:2, :]), self.ptr(mask_tokens), self.score(queries[:, 0:1, :])


class CXBlock(nn.Module):
    def __init__(self, b):
        super().__init__()
        self.dw = copy.deepcopy(b.dwconv)
        self.norm = clone_ln(b.norm, 256)
        self.p1 = copy.deepcopy(b.pwconv1)
        self.p2 = copy.deepcopy(b.pwconv2)
        self.p2.weight.data *= b.gamma.data[:, None]   # layer scale folded
        self.p2.bias.data *= b.gamma.data
        self.act = nn.GELU()

    def forward(self, x):
        t = self.dw(x).reshape(1, 256, TOK).permute(0, 2, 1)
        t = self.p2(self.act(self.p1(self.norm(t))))
        return x + t.permute(0, 2, 1).reshape(1, 256, GRID, GRID)


class MemoryEncoder(nn.Module):
    """pix (1,256,32,32) raw image features, mask (1,1,512,512) scaled for memory -> (1,64,32,32)"""

    def __init__(self, m):
        super().__init__()
        e = m.memory_encoder
        d = e.mask_downsampler.encoder
        self.convs = nn.ModuleList([copy.deepcopy(d[i]) for i in (0, 3, 6, 9)])
        self.norms = nn.ModuleList([clone_ln(d[i], c) for i, c in ((1, 4), (4, 16), (7, 64), (10, 256))])
        self.dims = ((4, 256), (16, 128), (64, 64), (256, 32))
        self.last = copy.deepcopy(d[12])
        self.act = nn.GELU()
        self.proj = copy.deepcopy(e.pix_feat_proj)
        self.fuser = nn.ModuleList([CXBlock(b) for b in e.fuser.layers])
        self.out = copy.deepcopy(e.out_proj)

    def forward(self, pix, mask):
        x = mask
        for c, n, (ch, s) in zip(self.convs, self.norms, self.dims):
            x = self.act(ln2d(n, c(x), ch, s, s))
        x = self.proj(pix) + self.last(x)
        for b in self.fuser:
            x = b(x)
        return self.out(x)


def permute_pairs(lin):
    idx = torch.cat([torch.arange(0, lin.out_features, 2), torch.arange(1, lin.out_features, 2)])
    lin.weight.data = lin.weight.data[idx].clone()
    lin.bias.data = lin.bias.data[idx].clone()


def rope(x, c, s):
    re, im = torch.chunk(x, 2, dim=-1)
    return torch.cat((re * c - im * s, re * s + im * c), dim=-1)


class MemLayer(nn.Module):
    def __init__(self, l):
        super().__init__()
        self.n1, self.n2, self.n3 = clone_ln(l.norm1, 256), clone_ln(l.norm2, 256), clone_ln(l.norm3, 256)
        sa, ca = l.self_attn, l.cross_attn_image
        self.sq, self.sk, self.sv, self.so = [copy.deepcopy(x) for x in (sa.q_proj, sa.k_proj, sa.v_proj, sa.out_proj)]
        self.cq, self.ck, self.cv, self.co = [copy.deepcopy(x) for x in (ca.q_proj, ca.k_proj, ca.v_proj, ca.out_proj)]
        for lin in (self.sq, self.sk, self.cq, self.ck):
            permute_pairs(lin)
        s = 1.0 / math.sqrt(256)
        for lin in (self.sq, self.cq):
            lin.weight.data *= s
            lin.bias.data *= s
        self.l1, self.l2 = copy.deepcopy(l.linear1), copy.deepcopy(l.linear2)

    def forward(self, x, mem, mem_k, cq, sq, ck, sk):
        n = self.n1(x)
        q, k, v = rope(self.sq(n), cq, sq), rope(self.sk(n), cq, sq), self.sv(n)
        x = x + self.so(torch.matmul(torch.softmax(torch.matmul(q, k.transpose(-1, -2)), dim=-1), v))
        n = self.n2(x)
        q, k, v = rope(self.cq(n), cq, sq), rope(self.ck(mem_k), ck, sk), self.cv(mem)
        x = x + self.co(torch.matmul(torch.softmax(torch.matmul(q, k.transpose(-1, -2)), dim=-1), v))
        return x + self.l2(torch.relu(self.l1(self.n3(x))))


class MemoryAttention(nn.Module):
    """pix (1,256,32,32), memory/pos (1,M,64), RoPE tables for the keys (1,M,128) -> (1,256,32,32).
    M is free: spatial frames take the tiled table rows, object pointers take cos=1 sin=0."""

    def __init__(self, m, curr_pos):
        super().__init__()
        a = m.memory_attention
        self.layers = nn.ModuleList([MemLayer(l) for l in a.layers])
        self.norm = clone_ln(a.norm, 256)
        cis = a.layers[0].self_attn.compute_cis(end_x=GRID, end_y=GRID)
        self.cos = nn.Parameter(cis.real[None].clone().contiguous())
        self.sin = nn.Parameter(cis.imag[None].clone().contiguous())
        self.pos = nn.Parameter(0.1 * curr_pos.clone())

    def forward(self, pix, mem, mem_pos, ck, sk):
        x = pix.reshape(1, 256, TOK).permute(0, 2, 1) + self.pos
        mem_k = mem + mem_pos
        for l in self.layers:
            x = l(x, mem, mem_k, self.cos, self.sin, ck, sk)
        return self.norm(x).permute(0, 2, 1).reshape(1, 256, GRID, GRID)


def constants(m):
    """Everything the loop needs that is not a network, as numpy."""
    x = torch.zeros(1, 256, GRID, GRID)
    vis_pos = m.image_encoder.neck.position_encoding(x)                 # (1,256,32,32)
    mem_pos = m.memory_encoder.position_encoding(torch.zeros(1, 64, GRID, GRID))
    cis = m.memory_attention.layers[0].self_attn.compute_cis(end_x=GRID, end_y=GRID)
    return dict(
        vis_pos=vis_pos.reshape(1, 256, TOK).permute(0, 2, 1).contiguous().numpy(),
        mem_pos=mem_pos.reshape(64, TOK).t().contiguous().numpy(),      # (1024,64)
        tpos=m.maskmem_tpos_enc.data.reshape(7, 64).numpy(),
        no_mem=m.no_mem_embed.data.reshape(256).numpy(),
        no_obj_ptr=m.no_obj_ptr.data.reshape(256).numpy(),
        no_mask=m.sam_prompt_encoder.no_mask_embed.weight.data.reshape(256).numpy(),
        cos=cis.real.contiguous().numpy(), sin=cis.imag.contiguous().numpy(),
        gauss=(m.sam_prompt_encoder.pe_layer.positional_encoding_gaussian_matrix.data * (2 * math.pi)).numpy(),
        table=torch.cat([m.sam_prompt_encoder.not_a_point_embed.weight.data] +
                        [e.weight.data for e in m.sam_prompt_encoder.point_embeddings], 0).numpy(),
    )


def build_all(m):
    c = constants(m)
    return dict(
        image_encoder=ImageEncoder(m).eval(),
        prompt_embed=PromptEmbed(m).eval(),
        mask_prompt=MaskPrompt(m).eval(),
        mask_decoder=MaskDecoder(m).eval(),
        memory_encoder=MemoryEncoder(m).eval(),
        memory_attention=MemoryAttention(m, torch.from_numpy(c["vis_pos"])).eval(),
    ), c
