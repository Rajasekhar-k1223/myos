#include "spirv.h"
#include "kheap.h"
#include "string.h"
#include "mathf.h"

/* ── SPIR-V opcode constants ────────────────────────────────────────────── */
enum {
    Op_ExtInstImport = 11, Op_ExtInst = 12,
    Op_MemoryModel = 14, Op_EntryPoint = 15, Op_ExecutionMode = 16,
    Op_Capability = 17,
    Op_TypeVoid = 19, Op_TypeBool = 20, Op_TypeInt = 21, Op_TypeFloat = 22,
    Op_TypeVector = 23, Op_TypeMatrix = 24,
    Op_TypeImage = 25, Op_TypeSampler = 26, Op_TypeSampledImage = 27,
    Op_TypeArray = 28, Op_TypeStruct = 30,
    Op_TypePointer = 32, Op_TypeFunction = 33,
    Op_ConstantTrue = 41, Op_ConstantFalse = 42,
    Op_Constant = 43, Op_ConstantComposite = 44,
    Op_Variable = 59,
    Op_Load = 61, Op_Store = 62,
    Op_AccessChain = 65,
    Op_Decorate = 71, Op_MemberDecorate = 72,
    Op_Name = 5, Op_MemberName = 6,
    Op_VectorShuffle = 79,
    Op_CompositeConstruct = 80, Op_CompositeExtract = 81, Op_CompositeInsert = 82,
    Op_Transpose = 84,
    Op_SampledImage = 86, Op_ImageSampleImplicitLod = 87,
    Op_ConvertFToU = 109, Op_ConvertFToS = 110,
    Op_ConvertSToF = 111, Op_ConvertUToF = 112,
    Op_FNegate = 127,
    Op_IAdd = 128, Op_FAdd = 129, Op_ISub = 130, Op_FSub = 131,
    Op_IMul = 132, Op_FMul = 133, Op_UDiv = 134, Op_SDiv = 135, Op_FDiv = 136,
    Op_FMod = 141,
    Op_VectorTimesScalar = 142, Op_MatrixTimesScalar = 143,
    Op_VectorTimesMatrix = 144, Op_MatrixTimesVector = 145,
    Op_MatrixTimesMatrix = 146, Op_Dot = 148,
    Op_IEqual = 170, Op_INotEqual = 171,
    Op_FOrdEqual = 180, Op_FOrdNotEqual = 182,
    Op_FOrdLessThan = 184, Op_FOrdGreaterThan = 186,
    Op_FOrdLessThanEqual = 188, Op_FOrdGreaterThanEqual = 190,
    Op_LogicalAnd = 167, Op_LogicalOr = 166, Op_LogicalNot = 168,
    Op_Select = 169,
    Op_Function = 54, Op_FunctionParameter = 55, Op_FunctionEnd = 56,
    Op_Label = 248, Op_Branch = 249, Op_BranchConditional = 250,
    Op_Phi = 245,
    Op_Return = 253, Op_ReturnValue = 254,
    Op_BitwiseOr = 197, Op_BitwiseXor = 198, Op_BitwiseAnd = 199,
    Op_ShiftLeftLogical = 196, Op_ShiftRightLogical = 194,
};

/* GLSL.std.450 extended instruction opcodes */
enum {
    GLSL_FAbs = 4, GLSL_Floor = 8, GLSL_Ceil = 9, GLSL_Fract = 10,
    GLSL_Sin = 13, GLSL_Cos = 14, GLSL_Tan = 15,
    GLSL_Asin = 16, GLSL_Acos = 17, GLSL_Atan2 = 25,
    GLSL_Pow = 26, GLSL_Exp = 27, GLSL_Log = 28, GLSL_Sqrt = 31,
    GLSL_InverseSqrt = 32,
    GLSL_FMin = 37, GLSL_FMax = 40, GLSL_FClamp = 43, GLSL_FMix = 46,
    GLSL_Step = 48, GLSL_SmoothStep = 49,
    GLSL_Length = 66, GLSL_Distance = 67, GLSL_Cross = 68,
    GLSL_Normalize = 69, GLSL_Reflect = 71,
    GLSL_SAbs = 5, GLSL_SMin = 39, GLSL_SMax = 42,
    GLSL_Degrees = 12, GLSL_Radians = 11,
    GLSL_FSign = 6,
};

/* SpvDecoration subset */
enum { Deco_RelaxedPrecision=0, Deco_Block=2, Deco_BuiltIn=11,
       Deco_Location=30, Deco_Binding=33, Deco_DescriptorSet=34,
       Deco_Offset=35, Deco_MatrixStride=52 };

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static float clampf01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
static float vminf(float a, float b) { return a < b ? a : b; }
static float vmaxf(float a, float b) { return a > b ? a : b; }

/* Number of float components for a type. */
static int type_ncomp(const spv_mod_t* m, uint32_t id) {
    if (id >= SPV_MAX_IDS || !m->tvalid[id]) return 1;
    const spv_type_t* t = &m->types[id];
    switch (t->kind) {
    case SPV_T_VOID:  return 0;
    case SPV_T_BOOL: case SPV_T_INT: case SPV_T_UINT: case SPV_T_FLOAT: return 1;
    case SPV_T_VEC:   return t->ncmp;
    case SPV_T_MAT:   return t->ncmp * t->ncmp;
    case SPV_T_STRUCT: {
        int total = 0;
        for (int i = 0; i < (int)t->nmembers; i++)
            total += type_ncomp(m, t->members[i]);
        return total;
    }
    case SPV_T_PTR: return type_ncomp(m, t->elem_id);
    default:        return 1;
    }
}

/* Flat float offset of member[idx] within a struct. */
static int struct_member_off(const spv_mod_t* m, uint32_t struct_type_id, int idx) {
    const spv_type_t* st = &m->types[struct_type_id];
    int off = 0;
    for (int i = 0; i < idx && i < (int)st->nmembers; i++)
        off += type_ncomp(m, st->members[i]);
    return off;
}

/* Resolve pointer's pointee type ID (strip pointer wrapper). */
static uint32_t ptr_pointee(const spv_mod_t* m, uint32_t ptr_type_id) __attribute__((unused));
static uint32_t ptr_pointee(const spv_mod_t* m, uint32_t ptr_type_id) {
    if (ptr_type_id < SPV_MAX_IDS && m->tvalid[ptr_type_id] &&
        m->types[ptr_type_id].kind == SPV_T_PTR)
        return m->types[ptr_type_id].elem_id;
    return ptr_type_id;
}

/* ── Parse phase ─────────────────────────────────────────────────────────── */

spv_mod_t* spv_parse(const uint32_t* words, uint32_t nwords) {
    if (nwords < 5 || words[0] != SPV_MAGIC) return NULL;

    spv_mod_t* m = (spv_mod_t*)kmalloc(sizeof(*m));
    if (!m) return NULL;
    memset(m, 0, sizeof(*m));
    m->words  = words;
    m->nwords = nwords;
    m->bound  = words[3];
    if (m->bound > SPV_MAX_IDS) m->bound = SPV_MAX_IDS;

    /* Initialise decoration defaults to -1 */
    for (int i = 0; i < SPV_MAX_IDS; i++) {
        m->decors[i].location = -1;
        m->decors[i].binding  = -1;
        m->decors[i].dset     = 0;
        m->decors[i].builtin  = -1;
        for (int j = 0; j < SPV_MAX_MEMBERS; j++) {
            m->decors[i].mem_builtin [j] = -1;
            m->decors[i].mem_location[j] = -1;
        }
    }

    uint32_t pc = 5; /* skip header */
    while (pc < nwords) {
        uint32_t w0 = words[pc];
        uint16_t op = (uint16_t)(w0 & 0xFFFF);
        uint16_t wc = (uint16_t)(w0 >> 16);
        if (wc == 0) break;

#define W(i) words[pc + (i)]

        switch (op) {
        case Op_ExtInstImport:
            m->glsl_ext_id = W(1);
            break;

        case Op_EntryPoint:
            m->stage = (uint8_t)W(1);
            m->entry_func_id = W(2);
            break;

        case Op_TypeVoid:
            m->types[W(1)].kind = SPV_T_VOID; m->tvalid[W(1)] = 1; break;
        case Op_TypeBool:
            m->types[W(1)].kind = SPV_T_BOOL; m->tvalid[W(1)] = 1; break;
        case Op_TypeInt:
            m->types[W(1)].kind = (W(3) ? SPV_T_INT : SPV_T_UINT);
            m->tvalid[W(1)] = 1; break;
        case Op_TypeFloat:
            m->types[W(1)].kind = SPV_T_FLOAT; m->tvalid[W(1)] = 1; break;

        case Op_TypeVector: {
            spv_type_t* t = &m->types[W(1)]; m->tvalid[W(1)] = 1;
            t->kind = SPV_T_VEC; t->elem_id = W(2); t->ncmp = (uint8_t)W(3);
            break;
        }
        case Op_TypeMatrix: {
            spv_type_t* t = &m->types[W(1)]; m->tvalid[W(1)] = 1;
            t->kind = SPV_T_MAT; t->elem_id = W(2);
            /* columns = W(3); ncmp = column count = matrix dimension (square assumed) */
            t->ncmp = (uint8_t)W(3);
            break;
        }
        case Op_TypeImage: case Op_TypeSampler:
            m->types[W(1)].kind = SPV_T_IMAGE; m->tvalid[W(1)] = 1; break;
        case Op_TypeSampledImage:
            m->types[W(1)].kind = SPV_T_SAMPLED_IMG;
            m->types[W(1)].elem_id = W(2);
            m->tvalid[W(1)] = 1; break;

        case Op_TypeArray: {
            spv_type_t* t = &m->types[W(1)]; m->tvalid[W(1)] = 1;
            t->kind = SPV_T_ARRAY; t->elem_id = W(2);
            t->ncmp = (wc > 3 && W(3) < SPV_MAX_IDS) ?
                (uint8_t)m->consts[W(3)].f[0] : 4;
            break;
        }
        case Op_TypeStruct: {
            spv_type_t* t = &m->types[W(1)]; m->tvalid[W(1)] = 1;
            t->kind = SPV_T_STRUCT;
            t->nmembers = (uint8_t)(wc - 2);
            if (t->nmembers > SPV_MAX_MEMBERS) t->nmembers = SPV_MAX_MEMBERS;
            for (int i = 0; i < (int)t->nmembers; i++)
                t->members[i] = W(2 + i);
            break;
        }
        case Op_TypePointer: {
            spv_type_t* t = &m->types[W(1)]; m->tvalid[W(1)] = 1;
            t->kind = SPV_T_PTR; t->storage = (uint8_t)W(2); t->elem_id = W(3);
            break;
        }
        case Op_TypeFunction:
            m->types[W(1)].kind = SPV_T_FUNC; m->tvalid[W(1)] = 1; break;

        case Op_Constant: {
            uint32_t id = W(2);
            if (id < SPV_MAX_IDS) {
                m->consts[id].n = 1;
                m->consts[id].f[0] = *(const float*)&words[pc + 3];
                m->cvalid[id] = 1;
            }
            break;
        }
        case Op_ConstantTrue: {
            uint32_t id = W(2);
            if (id < SPV_MAX_IDS) {
                m->consts[id].n = 1; m->consts[id].f[0] = 1.f;
                m->cvalid[id] = 1;
            }
            break;
        }
        case Op_ConstantFalse: {
            uint32_t id = W(2);
            if (id < SPV_MAX_IDS) {
                m->consts[id].n = 1; m->consts[id].f[0] = 0.f;
                m->cvalid[id] = 1;
            }
            break;
        }
        case Op_ConstantComposite: {
            uint32_t type_id = W(1), id = W(2);
            if (id < SPV_MAX_IDS) {
                int n = (int)(wc - 3);
                m->consts[id].n = (uint8_t)n;
                int fi = 0;
                for (int i = 0; i < n && fi < 16; i++) {
                    uint32_t comp = W(3 + i);
                    if (comp < SPV_MAX_IDS && m->cvalid[comp]) {
                        int cn = m->consts[comp].n ? m->consts[comp].n : 1;
                        for (int k = 0; k < cn && fi < 16; k++)
                            m->consts[id].f[fi++] = m->consts[comp].f[k];
                    }
                }
                m->consts[id].n = (uint8_t)fi;
                m->cvalid[id] = 1;
            }
            (void)type_id;
            break;
        }

        case Op_Variable: {
            uint32_t ptr_type = W(1), id = W(2), sc = W(3);
            if (id < SPV_MAX_IDS) {
                m->vvalid[id] = 1;
                m->vsclass[id] = (uint8_t)sc;
                /* pointee type = ptr_type's element */
                m->vtype[id] = (ptr_type < SPV_MAX_IDS && m->tvalid[ptr_type]) ?
                    m->types[ptr_type].elem_id : ptr_type;
            }
            break;
        }

        case Op_Decorate: {
            uint32_t tgt = W(1), deco = W(2);
            if (tgt < SPV_MAX_IDS) {
                if (deco == Deco_Location)     m->decors[tgt].location = (int8_t)W(3);
                else if (deco == Deco_Binding) m->decors[tgt].binding  = (int8_t)W(3);
                else if (deco == Deco_DescriptorSet) m->decors[tgt].dset = (int8_t)W(3);
                else if (deco == Deco_BuiltIn) m->decors[tgt].builtin  = (int8_t)W(3);
            }
            break;
        }
        case Op_MemberDecorate: {
            uint32_t tgt = W(1), member = W(2), deco = W(3);
            if (tgt < SPV_MAX_IDS && member < SPV_MAX_MEMBERS) {
                if (deco == Deco_BuiltIn)
                    m->decors[tgt].mem_builtin [member] = (int8_t)W(4);
                else if (deco == Deco_Location)
                    m->decors[tgt].mem_location[member] = (int8_t)W(4);
            }
            break;
        }

        default: break;
        }
#undef W
        pc += wc;
    }
    return m;
}

void spv_free(spv_mod_t* m) { if (m) kfree(m); }

/* ── Execution ───────────────────────────────────────────────────────────── */

/* Per-invocation register file — definition lives in spirv.h as spv_exec_t. */
#define EXEC_MAX SPV_EXEC_MAX

/* ── Value helpers ─────────────────────────────────────────────────────── */

static spv_val_t make_scalar(float v) {
    spv_val_t r; memset(&r, 0, sizeof(r)); r.n = 1; r.f[0] = v; return r;
}

/* Get register (constant or computed). */
static spv_val_t get_reg(const spv_mod_t* m, const spv_exec_t* e, uint32_t id) {
    if (id >= EXEC_MAX) return make_scalar(0.f);
    if (e->rvalid[id]) return e->regs[id];
    if (m->cvalid[id]) return m->consts[id];
    return make_scalar(0.f);
}

/* Load from a pointer value into a result value. */
static spv_val_t exec_load(const spv_mod_t* m, const spv_exec_t* e, spv_val_t ptr,
                            uint32_t result_type_id, const spv_iface_t* iface) {
    spv_val_t result; memset(&result, 0, sizeof(result));
    int n = type_ncomp(m, result_type_id);
    result.n = (uint8_t)(n > 0 ? n : 1);

    if (!ptr.is_ptr) return result;
    uint32_t var_id = ptr.ptr_var;
    int off = (int)ptr.ptr_off;

    if (var_id >= EXEC_MAX) return result;
    uint8_t sc = m->vsclass[var_id];

    if (sc == SPV_SC_INPUT || sc == SPV_SC_UNIFORM_CONSTANT) {
        /* Read from interface input by location */
        int loc = m->decors[var_id].location;
        if (loc >= 0 && loc < SPV_MAX_LOC && iface->in_valid[loc]) {
            const spv_val_t* inp = &iface->inputs[loc];
            for (int i = 0; i < (int)result.n; i++)
                result.f[i] = (i + off < (int)inp->n) ? inp->f[i + off] : 0.f;
        } else {
            /* Could be frag_coord builtin */
            int bi = m->decors[var_id].builtin;
            if (bi == SPV_BI_FRAG_COORD)
                for (int i = 0; i < 4 && i < (int)result.n; i++)
                    result.f[i] = iface->frag_coord[i + off < 4 ? i + off : 0];
        }
    } else if (sc == SPV_SC_UNIFORM) {
        /* Read from UBO by binding */
        int bind = m->decors[var_id].binding;
        if (bind < 0) bind = 0;
        if (bind < SPV_MAX_BIND && iface->ubos[bind]) {
            const float* buf = iface->ubos[bind];
            int total = iface->ubo_n[bind];
            for (int i = 0; i < (int)result.n; i++)
                result.f[i] = (off + i < total) ? buf[off + i] : 0.f;
        }
    } else {
        /* Private / Output / Function-local variable */
        const spv_val_t* vv = &e->vars[var_id];
        for (int i = 0; i < (int)result.n; i++)
            result.f[i] = (off + i < (int)vv->n) ? vv->f[off + i] : 0.f;
    }
    return result;
}

/* Store src into the location a pointer refers to. */
static void exec_store(const spv_mod_t* m, spv_exec_t* e, spv_val_t ptr,
                       spv_val_t src, spv_iface_t* iface) {
    if (!ptr.is_ptr) return;
    uint32_t var_id = ptr.ptr_var;
    int off = (int)ptr.ptr_off;
    if (var_id >= EXEC_MAX) return;
    uint8_t sc = m->vsclass[var_id];

    if (sc == SPV_SC_OUTPUT) {
        /* Check struct member builtins first */
        uint32_t vt = m->vtype[var_id];
        if (vt < SPV_MAX_IDS && m->types[vt].kind == SPV_T_STRUCT) {
            /* Find which member this offset lands in */
            const spv_type_t* st = &m->types[vt];
            int cur = 0;
            for (int mi = 0; mi < (int)st->nmembers; mi++) {
                int msz = type_ncomp(m, st->members[mi]);
                if (off >= cur && off < cur + msz) {
                    int bi = m->decors[vt].mem_builtin[mi];
                    if (bi == SPV_BI_POSITION) {
                        for (int i = 0; i < (int)src.n && i + off - cur < 4; i++)
                            iface->gl_position[i + off - cur] = src.f[i];
                        return;
                    }
                    int ml = m->decors[vt].mem_location[mi];
                    if (ml >= 0 && ml < SPV_MAX_LOC) {
                        spv_val_t* out = &iface->outputs[ml];
                        for (int i = 0; i < (int)src.n && off - cur + i < 16; i++)
                            out->f[off - cur + i] = src.f[i];
                        if ((int)out->n < (int)src.n) out->n = src.n;
                        return;
                    }
                    break;
                }
                cur += msz;
            }
        }
        /* Plain output variable: check its builtin and location */
        int bi = m->decors[var_id].builtin;
        if (bi == SPV_BI_POSITION) {
            for (int i = 0; i < (int)src.n && off + i < 4; i++)
                iface->gl_position[off + i] = src.f[i];
            return;
        }
        int loc = m->decors[var_id].location;
        if (loc >= 0 && loc < SPV_MAX_LOC) {
            spv_val_t* out = &iface->outputs[loc];
            for (int i = 0; i < (int)src.n && off + i < 16; i++)
                out->f[off + i] = src.f[i];
            if ((int)out->n < (int)src.n + off) out->n = (uint8_t)(src.n + off);
            return;
        }
    }
    /* Default: write to local variable backing store */
    spv_val_t* vv = &e->vars[var_id];
    for (int i = 0; i < (int)src.n && off + i < 16; i++)
        vv->f[off + i] = src.f[i];
    int newn = off + (int)src.n;
    if (newn > (int)vv->n) vv->n = (uint8_t)newn;
}

/* Component-wise binary op. */
static spv_val_t binop(spv_val_t a, spv_val_t b, int op, int n) {
    spv_val_t r; memset(&r, 0, sizeof(r)); r.n = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        float av = i < (int)a.n ? a.f[i] : 0.f;
        float bv = i < (int)b.n ? b.f[i] : 0.f;
        float rv;
        switch (op) {
        case Op_FAdd: rv = av + bv; break;
        case Op_FSub: rv = av - bv; break;
        case Op_FMul: rv = av * bv; break;
        case Op_FDiv: rv = bv != 0.f ? av / bv : 0.f; break;
        case Op_FMod: rv = fmodf(av, bv); break;
        case Op_IAdd: rv = (float)((int)av + (int)bv); break;
        case Op_ISub: rv = (float)((int)av - (int)bv); break;
        case Op_IMul: rv = (float)((int)av * (int)bv); break;
        case Op_SDiv: case Op_UDiv: rv = bv != 0.f ? (float)((int)av / (int)bv) : 0.f; break;
        case Op_FOrdLessThan: rv = (av <  bv) ? 1.f : 0.f; break;
        case Op_FOrdGreaterThan: rv = (av >  bv) ? 1.f : 0.f; break;
        case Op_FOrdLessThanEqual: rv = (av <= bv) ? 1.f : 0.f; break;
        case Op_FOrdGreaterThanEqual: rv = (av >= bv) ? 1.f : 0.f; break;
        case Op_FOrdEqual: rv = (av == bv) ? 1.f : 0.f; break;
        case Op_FOrdNotEqual: rv = (av != bv) ? 1.f : 0.f; break;
        case Op_IEqual: rv = ((int)av == (int)bv) ? 1.f : 0.f; break;
        case Op_INotEqual: rv = ((int)av != (int)bv) ? 1.f : 0.f; break;
        case Op_LogicalAnd: rv = (av != 0.f && bv != 0.f) ? 1.f : 0.f; break;
        case Op_LogicalOr:  rv = (av != 0.f || bv != 0.f) ? 1.f : 0.f; break;
        case Op_BitwiseOr:  rv = (float)((int)av | (int)bv); break;
        case Op_BitwiseAnd: rv = (float)((int)av & (int)bv); break;
        case Op_BitwiseXor: rv = (float)((int)av ^ (int)bv); break;
        case Op_ShiftLeftLogical:  rv = (float)((unsigned)av << (unsigned)bv); break;
        case Op_ShiftRightLogical: rv = (float)((unsigned)av >> (unsigned)bv); break;
        default: rv = 0.f; break;
        }
        r.f[i] = rv;
    }
    return r;
}

/* Texture sampling (nearest or bilinear determined by wrap around). */
static spv_val_t sample_tex(const spv_iface_t* iface, int binding, spv_val_t uv) {
    spv_val_t r; memset(&r, 0, sizeof(r)); r.n = 4;
    if (binding < 0 || binding >= SPV_MAX_BIND || !iface->tex_data[binding]) {
        r.f[0] = r.f[1] = r.f[2] = r.f[3] = 1.f;
        return r;
    }
    float u = uv.f[0], v = uv.f[1];
    int w = iface->tex_w[binding], h = iface->tex_h[binding];
    const uint8_t* data = iface->tex_data[binding];
    /* Repeat wrap, bilinear */
    u = u - floorf(u); v = v - floorf(v);
    float px = u * w - 0.5f, py = v * h - 0.5f;
    int x0 = (int)floorf(px), y0 = (int)floorf(py);
    int x1 = x0 + 1, y1 = y0 + 1;
    float fx = px - x0, fy = py - y0;
    x0 = ((x0 % w) + w) % w; x1 = ((x1 % w) + w) % w;
    y0 = ((y0 % h) + h) % h; y1 = ((y1 % h) + h) % h;
    const uint8_t* p00 = data + (y0 * w + x0) * 4;
    const uint8_t* p10 = data + (y0 * w + x1) * 4;
    const uint8_t* p01 = data + (y1 * w + x0) * 4;
    const uint8_t* p11 = data + (y1 * w + x1) * 4;
    for (int i = 0; i < 4; i++)
        r.f[i] = (p00[i]*(1-fx)*(1-fy) + p10[i]*fx*(1-fy)
                + p01[i]*(1-fx)*fy    + p11[i]*fx*fy) / 255.f;
    return r;
}

/* Find the word-index of OpLabel {label_id} within a function body search range. */
static uint32_t find_label(const spv_mod_t* m, uint32_t label_id,
                           uint32_t from, uint32_t end) {
    for (uint32_t i = from; i < end; ) {
        uint16_t op = (uint16_t)(m->words[i] & 0xFFFF);
        uint16_t wc = (uint16_t)(m->words[i] >> 16);
        if (wc == 0) break;
        if (op == Op_Label && i + 1 < end && m->words[i + 1] == label_id)
            return i;
        i += wc;
    }
    return end; /* not found → causes loop exit */
}

/* ── Exec context lifecycle ──────────────────────────────────────────────── */
spv_exec_t* spv_alloc_exec(void) {
    spv_exec_t* e = (spv_exec_t*)kmalloc(sizeof(*e));
    if (e) memset(e, 0, sizeof(*e));
    return e;
}
void spv_free_exec(spv_exec_t* e) { if (e) kfree(e); }

/* ── Main execution ──────────────────────────────────────────────────────── */

static int spv_run(spv_mod_t* m, spv_iface_t* iface, spv_exec_t* e);

int spv_execute_ex(spv_mod_t* m, spv_iface_t* iface, spv_exec_t* e) {
    memset(e, 0, sizeof(*e));
    return spv_run(m, iface, e);
}

int spv_execute(spv_mod_t* m, spv_iface_t* iface) {
    spv_exec_t* e = spv_alloc_exec();
    if (!e) return -1;
    int r = spv_run(m, iface, e);
    spv_free_exec(e);
    return r;
}

static int spv_run(spv_mod_t* m, spv_iface_t* iface, spv_exec_t* e) {

    /* Pre-load constants into registers (they're immutable). */
    for (uint32_t i = 0; i < SPV_MAX_IDS; i++) {
        if (m->cvalid[i]) { e->regs[i] = m->consts[i]; e->rvalid[i] = 1; }
    }

    /* Find the entry function body.  Scan for OpFunction with entry_func_id. */
    uint32_t func_pc = 0;
    for (uint32_t i = 5; i < m->nwords; ) {
        uint16_t op = (uint16_t)(m->words[i] & 0xFFFF);
        uint16_t wc = (uint16_t)(m->words[i] >> 16);
        if (wc == 0) break;
        if (op == Op_Function && i + 2 < m->nwords && m->words[i + 2] == m->entry_func_id)
        { func_pc = i; break; }
        i += wc;
    }
    if (!func_pc) { return -1; }

    /* Find end of function (OpFunctionEnd). */
    uint32_t func_end = func_pc;
    for (uint32_t i = func_pc; i < m->nwords; ) {
        uint16_t op = (uint16_t)(m->words[i] & 0xFFFF);
        uint16_t wc = (uint16_t)(m->words[i] >> 16);
        if (wc == 0) break;
        if (op == Op_FunctionEnd) { func_end = i; break; }
        i += wc;
    }

    /* Sampled-image binding cache: sampledimage_bind[id] = binding index */
    int sampled_bind[EXEC_MAX];
    memset(sampled_bind, -1, sizeof(sampled_bind));

    uint32_t pc = func_pc;
    while (pc < func_end) {
        uint32_t w0 = m->words[pc];
        uint16_t op = (uint16_t)(w0 & 0xFFFF);
        uint16_t wc = (uint16_t)(w0 >> 16);
        if (wc == 0) break;

#define W(i) (m->words[pc + (i)])

        switch (op) {
        case Op_Function: case Op_FunctionParameter:
            break;

        case Op_Label: {
            e->prev_label = e->cur_label;
            e->cur_label  = W(1);
            break;
        }

        case Op_Variable: {
            /* Function-scope variable: init to zero */
            uint32_t id = W(2), sc = W(3);
            if (id < EXEC_MAX) {
                m->vvalid[id] = 1; m->vsclass[id] = (uint8_t)sc;
                m->vtype[id] = (W(1) < SPV_MAX_IDS && m->tvalid[W(1)]) ?
                    m->types[W(1)].elem_id : W(1);
            }
            break;
        }

        case Op_Load: {
            uint32_t rt = W(1), id = W(2), ptr_id = W(3);
            spv_val_t pv = get_reg(m, e, ptr_id);
            if (!pv.is_ptr) {
                /* Direct read from uniform/sampler variable */
                spv_val_t tmp; memset(&tmp, 0, sizeof(tmp));
                tmp.is_ptr = 1; tmp.ptr_var = ptr_id; tmp.ptr_off = 0;
                pv = tmp;
            }
            if (id < EXEC_MAX) { e->regs[id] = exec_load(m, e, pv, rt, iface); e->rvalid[id] = 1; }
            break;
        }

        case Op_Store: {
            uint32_t ptr_id = W(1), val_id = W(2);
            spv_val_t pv = get_reg(m, e, ptr_id);
            spv_val_t sv = get_reg(m, e, val_id);
            if (!pv.is_ptr) {
                spv_val_t tmp; memset(&tmp, 0, sizeof(tmp));
                tmp.is_ptr = 1; tmp.ptr_var = ptr_id; tmp.ptr_off = 0;
                pv = tmp;
            }
            exec_store(m, e, pv, sv, iface);
            break;
        }

        case Op_AccessChain: {
            /* result_type(1), result_id(2), base_ptr(3), index_0(4), ... */
            uint32_t id = W(2), base_id = W(3);
            spv_val_t pv = get_reg(m, e, base_id);
            if (!pv.is_ptr) {
                /* base is a variable, not yet a pointer value */
                memset(&pv, 0, sizeof(pv));
                pv.is_ptr = 1; pv.ptr_var = base_id; pv.ptr_off = 0;
            }
            /* Walk the index chain */
            uint32_t cur_type = m->vtype[pv.ptr_var]; /* type of current aggregate */
            /* Adjust cur_type through already-traversed depth */
            for (int ci = 0; ci < (int)(wc - 4); ci++) {
                uint32_t idx_id = W(4 + ci);
                int idx = (int)get_reg(m, e, idx_id).f[0];
                if (cur_type < SPV_MAX_IDS && m->tvalid[cur_type]) {
                    const spv_type_t* ct = &m->types[cur_type];
                    if (ct->kind == SPV_T_STRUCT) {
                        pv.ptr_off += (uint16_t)struct_member_off(m, cur_type, idx);
                        cur_type = ct->members[idx < (int)ct->nmembers ? idx : 0];
                    } else if (ct->kind == SPV_T_VEC || ct->kind == SPV_T_MAT ||
                               ct->kind == SPV_T_ARRAY) {
                        int esz = type_ncomp(m, ct->elem_id);
                        pv.ptr_off += (uint16_t)(idx * esz);
                        cur_type = ct->elem_id;
                    }
                }
            }
            if (id < EXEC_MAX) { e->regs[id] = pv; e->rvalid[id] = 1; }
            break;
        }

        case Op_SampledImage: {
            /* result_type(1) result_id(2) image_id(3) sampler_id(4)
               We just propagate the binding from the image variable. */
            uint32_t id = W(2), img_id = W(3);
            if (id < EXEC_MAX) {
                sampled_bind[id] = (img_id < EXEC_MAX) ? m->decors[img_id].binding : -1;
                e->rvalid[id] = 1;
            }
            break;
        }

        case Op_ImageSampleImplicitLod: {
            uint32_t id = W(2), sampled_id = W(3), coord_id = W(4);
            spv_val_t uv = get_reg(m, e, coord_id);
            int bind = (sampled_id < EXEC_MAX) ? sampled_bind[sampled_id] : -1;
            if (bind < 0 && sampled_id < EXEC_MAX) bind = m->decors[sampled_id].binding;
            if (id < EXEC_MAX) { e->regs[id] = sample_tex(iface, bind, uv); e->rvalid[id] = 1; }
            break;
        }

        /* ── Arithmetic ─────────────────────────────────────────────────── */
        case Op_FNegate: {
            uint32_t id = W(2); spv_val_t a = get_reg(m, e, W(3));
            spv_val_t r = a;
            for (int i = 0; i < (int)r.n; i++) r.f[i] = -r.f[i];
            if (id < EXEC_MAX) { e->regs[id] = r; e->rvalid[id] = 1; }
            break;
        }
        case Op_LogicalNot: case Op_ConvertFToU: case Op_ConvertFToS:
        case Op_ConvertSToF: case Op_ConvertUToF: {
            uint32_t id = W(2); spv_val_t a = get_reg(m, e, W(3));
            if (op == Op_LogicalNot) { spv_val_t r = a; for (int i=0;i<r.n;i++) r.f[i]=(a.f[i]==0.f)?1.f:0.f; if(id<EXEC_MAX){e->regs[id]=r;e->rvalid[id]=1;} }
            else { if(id<EXEC_MAX){e->regs[id]=a;e->rvalid[id]=1;} }
            break;
        }

        case Op_FAdd: case Op_FSub: case Op_FMul: case Op_FDiv: case Op_FMod:
        case Op_IAdd: case Op_ISub: case Op_IMul: case Op_SDiv: case Op_UDiv:
        case Op_FOrdLessThan: case Op_FOrdGreaterThan:
        case Op_FOrdLessThanEqual: case Op_FOrdGreaterThanEqual:
        case Op_FOrdEqual: case Op_FOrdNotEqual:
        case Op_IEqual: case Op_INotEqual:
        case Op_LogicalAnd: case Op_LogicalOr:
        case Op_BitwiseOr: case Op_BitwiseAnd: case Op_BitwiseXor:
        case Op_ShiftLeftLogical: case Op_ShiftRightLogical: {
            uint32_t id = W(2);
            spv_val_t a = get_reg(m, e, W(3));
            spv_val_t b = get_reg(m, e, W(4));
            int n = (int)(a.n > b.n ? a.n : b.n); if (n < 1) n = 1;
            if (id < EXEC_MAX) { e->regs[id] = binop(a, b, op, n); e->rvalid[id] = 1; }
            break;
        }

        case Op_Dot: {
            uint32_t id = W(2);
            spv_val_t a = get_reg(m, e, W(3)), b = get_reg(m, e, W(4));
            float dot = 0.f;
            for (int i = 0; i < (int)a.n; i++) dot += a.f[i] * b.f[i];
            if (id < EXEC_MAX) { e->regs[id] = make_scalar(dot); e->rvalid[id] = 1; }
            break;
        }

        case Op_VectorTimesScalar: {
            uint32_t id = W(2); spv_val_t v = get_reg(m, e, W(3)), s = get_reg(m, e, W(4));
            spv_val_t r = v;
            for (int i = 0; i < (int)r.n; i++) r.f[i] *= s.f[0];
            if (id < EXEC_MAX) { e->regs[id] = r; e->rvalid[id] = 1; }
            break;
        }
        case Op_MatrixTimesScalar: {
            uint32_t id = W(2); spv_val_t v = get_reg(m, e, W(3)), s = get_reg(m, e, W(4));
            spv_val_t r = v;
            for (int i = 0; i < (int)r.n; i++) r.f[i] *= s.f[0];
            if (id < EXEC_MAX) { e->regs[id] = r; e->rvalid[id] = 1; }
            break;
        }

        case Op_MatrixTimesVector: {
            /* mat (column-major) * vec → vec  (result_type=1, id=2, mat=3, vec=4) */
            uint32_t id = W(2);
            spv_val_t mat = get_reg(m, e, W(3)), vec = get_reg(m, e, W(4));
            int dim = (int)vec.n; /* dim × dim matrix × dim vector */
            spv_val_t r; memset(&r, 0, sizeof(r)); r.n = (uint8_t)dim;
            for (int row = 0; row < dim; row++) {
                float sum = 0.f;
                for (int col = 0; col < dim; col++)
                    sum += mat.f[col * dim + row] * vec.f[col];
                r.f[row] = sum;
            }
            if (id < EXEC_MAX) { e->regs[id] = r; e->rvalid[id] = 1; }
            break;
        }
        case Op_VectorTimesMatrix: {
            /* vec * mat → vec  (result_type=1, id=2, vec=3, mat=4) */
            uint32_t id = W(2);
            spv_val_t vec = get_reg(m, e, W(3)), mat = get_reg(m, e, W(4));
            int dim = (int)vec.n;
            spv_val_t r; memset(&r, 0, sizeof(r)); r.n = (uint8_t)dim;
            for (int col = 0; col < dim; col++) {
                float sum = 0.f;
                for (int row = 0; row < dim; row++)
                    sum += vec.f[row] * mat.f[col * dim + row];
                r.f[col] = sum;
            }
            if (id < EXEC_MAX) { e->regs[id] = r; e->rvalid[id] = 1; }
            break;
        }
        case Op_MatrixTimesMatrix: {
            uint32_t id = W(2);
            spv_val_t A = get_reg(m, e, W(3)), B = get_reg(m, e, W(4));
            int dim = 4; /* assume mat4 */
            spv_val_t r; memset(&r, 0, sizeof(r)); r.n = (uint8_t)(dim * dim);
            for (int col = 0; col < dim; col++)
                for (int row = 0; row < dim; row++) {
                    float sum = 0.f;
                    for (int k = 0; k < dim; k++)
                        sum += A.f[k * dim + row] * B.f[col * dim + k];
                    r.f[col * dim + row] = sum;
                }
            if (id < EXEC_MAX) { e->regs[id] = r; e->rvalid[id] = 1; }
            break;
        }
        case Op_Transpose: {
            uint32_t id = W(2); spv_val_t m_ = get_reg(m, e, W(3));
            int dim = 4; spv_val_t r = m_;
            for (int c = 0; c < dim; c++) for (int r2 = 0; r2 < dim; r2++)
                r.f[c * dim + r2] = m_.f[r2 * dim + c];
            if (id < EXEC_MAX) { e->regs[id] = r; e->rvalid[id] = 1; }
            break;
        }

        case Op_VectorShuffle: {
            /* result_type(1), id(2), v1(3), v2(4), component_literals(5..) */
            uint32_t id = W(2);
            spv_val_t v1 = get_reg(m, e, W(3)), v2 = get_reg(m, e, W(4));
            spv_val_t r; memset(&r, 0, sizeof(r));
            int nc = (int)(wc - 5); r.n = (uint8_t)nc;
            for (int i = 0; i < nc; i++) {
                uint32_t comp = W(5 + i);
                int n1 = (int)v1.n;
                r.f[i] = (comp < (uint32_t)n1) ? v1.f[comp]
                       : ((comp - n1) < (int)v2.n) ? v2.f[comp - n1] : 0.f;
            }
            if (id < EXEC_MAX) { e->regs[id] = r; e->rvalid[id] = 1; }
            break;
        }

        case Op_CompositeConstruct: {
            uint32_t rt = W(1), id = W(2);
            spv_val_t r; memset(&r, 0, sizeof(r));
            int nc = type_ncomp(m, rt); r.n = (uint8_t)(nc > 0 ? nc : wc - 3);
            int fi = 0;
            for (int i = 0; i < (int)(wc - 3) && fi < 16; i++) {
                spv_val_t src = get_reg(m, e, W(3 + i));
                int sn = src.n ? (int)src.n : 1;
                for (int k = 0; k < sn && fi < 16; k++) r.f[fi++] = src.f[k];
            }
            if (nc > 0) r.n = (uint8_t)nc;
            if (id < EXEC_MAX) { e->regs[id] = r; e->rvalid[id] = 1; }
            break;
        }
        case Op_CompositeExtract: {
            uint32_t id = W(2);
            spv_val_t src = get_reg(m, e, W(3));
            spv_val_t r; memset(&r, 0, sizeof(r)); r.n = 1;
            int off = (wc > 4) ? (int)W(4) : 0;
            if (wc > 5) {
                off = (int)W(4);
                off = off * 4 + (int)W(5); /* rough 2-level heuristic */
            }
            r.f[0] = (off < 16) ? src.f[off] : 0.f;
            if (id < EXEC_MAX) { e->regs[id] = r; e->rvalid[id] = 1; }
            break;
        }
        case Op_CompositeInsert: {
            uint32_t id = W(2);
            spv_val_t obj = get_reg(m, e, W(3));
            spv_val_t composite = get_reg(m, e, W(4));
            int idx = (wc > 5) ? (int)W(5) : 0;
            spv_val_t r = composite;
            if (idx < 16) r.f[idx] = obj.f[0];
            if (id < EXEC_MAX) { e->regs[id] = r; e->rvalid[id] = 1; }
            break;
        }

        case Op_Select: {
            uint32_t id = W(2);
            spv_val_t cond = get_reg(m, e, W(3));
            spv_val_t tv   = get_reg(m, e, W(4));
            spv_val_t fv   = get_reg(m, e, W(5));
            if (id < EXEC_MAX) {
                e->regs[id] = (cond.f[0] != 0.f) ? tv : fv;
                e->rvalid[id] = 1;
            }
            break;
        }

        /* ── GLSL.std.450 extended instructions ─────────────────────────── */
        case Op_ExtInst: {
            /* result_type(1) result_id(2) set(3) inst(4) args(5..) */
            uint32_t id = W(2), inst = W(4);
            spv_val_t a = (wc > 5) ? get_reg(m, e, W(5)) : make_scalar(0.f);
            spv_val_t b = (wc > 6) ? get_reg(m, e, W(6)) : make_scalar(0.f);
            spv_val_t c = (wc > 7) ? get_reg(m, e, W(7)) : make_scalar(0.f);
            spv_val_t r; memset(&r, 0, sizeof(r));
            r.n = a.n ? a.n : 1;
            switch (inst) {
            case GLSL_FAbs:  for(int i=0;i<r.n;i++) r.f[i]=fabsf(a.f[i]); break;
            case GLSL_FSign: for(int i=0;i<r.n;i++) r.f[i]=(a.f[i]>0.f)?1.f:(a.f[i]<0.f)?-1.f:0.f; break;
            case GLSL_Floor: for(int i=0;i<r.n;i++) r.f[i]=floorf(a.f[i]); break;
            case GLSL_Ceil:  for(int i=0;i<r.n;i++) r.f[i]=ceilf(a.f[i]);  break;
            case GLSL_Fract: for(int i=0;i<r.n;i++) r.f[i]=a.f[i]-floorf(a.f[i]); break;
            case GLSL_Radians: for(int i=0;i<r.n;i++) r.f[i]=a.f[i]*(3.14159265f/180.f); break;
            case GLSL_Degrees: for(int i=0;i<r.n;i++) r.f[i]=a.f[i]*(180.f/3.14159265f); break;
            case GLSL_Sin:  for(int i=0;i<r.n;i++) r.f[i]=sinf(a.f[i]);  break;
            case GLSL_Cos:  for(int i=0;i<r.n;i++) r.f[i]=cosf(a.f[i]);  break;
            case GLSL_Tan:  for(int i=0;i<r.n;i++) r.f[i]=tanf(a.f[i]);  break;
            case GLSL_Asin: for(int i=0;i<r.n;i++) r.f[i]=acosf(sqrtf(1.f-a.f[i]*a.f[i])); break;
            case GLSL_Acos: for(int i=0;i<r.n;i++) r.f[i]=acosf(a.f[i]); break;
            case GLSL_Atan2: for(int i=0;i<r.n;i++) r.f[i]=atan2f(a.f[i],b.f[i]); break;
            case GLSL_Pow:  for(int i=0;i<r.n;i++) { float base=a.f[i]; float ex=b.f[i]; float res=1.f; if(ex>=0){int ni=(int)ex;for(int j=0;j<ni;j++)res*=base;}else{int ni=(int)(-ex);for(int j=0;j<ni;j++)res*=base;res=1.f/res;} r.f[i]=res; } break;
            case GLSL_Exp:  for(int i=0;i<r.n;i++) r.f[i]=expf(a.f[i]);  break;
            case GLSL_Sqrt: for(int i=0;i<r.n;i++) r.f[i]=sqrtf(a.f[i] > 0.f ? a.f[i] : 0.f); break;
            case GLSL_InverseSqrt: for(int i=0;i<r.n;i++) { float v=a.f[i]>0.f?a.f[i]:1e-6f; r.f[i]=1.f/sqrtf(v); } break;
            case GLSL_FMin: for(int i=0;i<r.n;i++) r.f[i]=vminf(a.f[i],b.f[i]); break;
            case GLSL_FMax: for(int i=0;i<r.n;i++) r.f[i]=vmaxf(a.f[i],b.f[i]); break;
            case GLSL_SMin: for(int i=0;i<r.n;i++) r.f[i]=(float)(((int)a.f[i]<(int)b.f[i])?(int)a.f[i]:(int)b.f[i]); break;
            case GLSL_SMax: for(int i=0;i<r.n;i++) r.f[i]=(float)(((int)a.f[i]>(int)b.f[i])?(int)a.f[i]:(int)b.f[i]); break;
            case GLSL_FClamp: for(int i=0;i<r.n;i++) r.f[i]=vminf(vmaxf(a.f[i],b.f[i]),c.f[i]); break;
            case GLSL_FMix: for(int i=0;i<r.n;i++) r.f[i]=a.f[i]*(1.f-c.f[i>=(int)c.n?0:i])+b.f[i]*c.f[i>=(int)c.n?0:i]; break;
            case GLSL_Step: for(int i=0;i<r.n;i++) r.f[i]=(b.f[i]<a.f[i])?0.f:1.f; break;
            case GLSL_SmoothStep: for(int i=0;i<r.n;i++){float t=clampf01((b.f[i]-a.f[i])/(c.f[i]-a.f[i]+1e-6f));r.f[i]=t*t*(3.f-2.f*t);}break;
            case GLSL_Length: { float len=0.f; for(int i=0;i<(int)a.n;i++) len+=a.f[i]*a.f[i]; r.n=1; r.f[0]=sqrtf(len); break; }
            case GLSL_Distance: { float len=0.f; for(int i=0;i<(int)a.n;i++){float d=a.f[i]-b.f[i];len+=d*d;} r.n=1; r.f[0]=sqrtf(len); break; }
            case GLSL_Normalize: {
                float len=0.f; for(int i=0;i<(int)a.n;i++) len+=a.f[i]*a.f[i]; len=sqrtf(len);
                r=a; if(len>1e-8f) for(int i=0;i<(int)r.n;i++) r.f[i]/=len; break;
            }
            case GLSL_Cross: /* vec3 only */
                r.n=3;
                r.f[0]=a.f[1]*b.f[2]-a.f[2]*b.f[1];
                r.f[1]=a.f[2]*b.f[0]-a.f[0]*b.f[2];
                r.f[2]=a.f[0]*b.f[1]-a.f[1]*b.f[0]; break;
            case GLSL_Reflect: /* I - 2*dot(N,I)*N */
                { float d=0.f; for(int i=0;i<(int)a.n;i++) d+=a.f[i]*b.f[i]; /* I=a, N=b */
                  r=a; for(int i=0;i<(int)r.n;i++) r.f[i]=a.f[i]-2.f*d*b.f[i]; break; }
            default: r = a; break;
            }
            if (id < EXEC_MAX) { e->regs[id] = r; e->rvalid[id] = 1; }
            break;
        }

        /* ── Control flow ───────────────────────────────────────────────── */
        case Op_Branch: {
            uint32_t target = W(1);
            pc = find_label(m, target, func_pc, func_end);
            continue;
        }
        case Op_BranchConditional: {
            spv_val_t cond = get_reg(m, e, W(1));
            uint32_t target = (cond.f[0] != 0.f) ? W(2) : W(3);
            pc = find_label(m, target, func_pc, func_end);
            continue;
        }
        case Op_Phi: {
            /* result_type(1) result_id(2) (value, block)... */
            uint32_t id = W(2);
            spv_val_t result; memset(&result, 0, sizeof(result));
            for (int i = 3; i + 1 < (int)wc; i += 2) {
                if (W(i + 1) == e->prev_label) {
                    result = get_reg(m, e, W(i));
                    break;
                }
            }
            if (id < EXEC_MAX) { e->regs[id] = result; e->rvalid[id] = 1; }
            break;
        }

        case Op_Return:
        case Op_ReturnValue:
        case Op_FunctionEnd:
            goto done;

        default: break;
        }
#undef W
        pc += wc;
    }
done:
    return 0;
}
