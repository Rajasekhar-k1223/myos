#pragma once
/*
 * Minimal SPIR-V interpreter for the ElseaOS software Vulkan backend.
 * Supports the instruction subset emitted by glslangValidator/shaderc for
 * typical 2D/3D rendering shaders:
 *   - All scalar/vector/matrix arithmetic
 *   - Struct access chains (for gl_PerVertex / UBO member access)
 *   - Uniform buffer objects (read-only, std140 float layout)
 *   - Combined image samplers (nearest + bilinear)
 *   - If/else control flow (OpBranchConditional + OpPhi)
 *   - GLSL.std.450 builtins: sqrt, sin, cos, abs, length, normalize,
 *       min, max, clamp, mix, pow, exp, floor, ceil, fract, step, smoothstep
 * Not supported: loops, function calls (shaders must be flat), geometry/compute.
 */
#include <stdint.h>
#include <stddef.h>

#define SPV_MAGIC 0x07230203u

/* Storage classes */
#define SPV_SC_UNIFORM_CONSTANT 0
#define SPV_SC_INPUT            1
#define SPV_SC_UNIFORM          2
#define SPV_SC_OUTPUT           3
#define SPV_SC_PRIVATE          6
#define SPV_SC_FUNCTION         7

/* Builtin IDs */
#define SPV_BI_POSITION        0
#define SPV_BI_POINT_SIZE      1
#define SPV_BI_FRAG_COORD      15
#define SPV_BI_FRAG_DEPTH      22
#define SPV_BI_VERTEX_INDEX    42
#define SPV_BI_INSTANCE_INDEX  43

/* Type kinds */
#define SPV_T_VOID          0
#define SPV_T_BOOL          1
#define SPV_T_INT           2
#define SPV_T_UINT          3
#define SPV_T_FLOAT         4
#define SPV_T_VEC           5  /* ncmp components of elem_id type */
#define SPV_T_MAT           6  /* ncmp×ncmp column-major float matrix */
#define SPV_T_STRUCT        7
#define SPV_T_PTR           8  /* pointer to elem_id, in storage class 'storage' */
#define SPV_T_FUNC          9
#define SPV_T_IMAGE        10
#define SPV_T_SAMPLED_IMG  11
#define SPV_T_ARRAY        12

#define SPV_MAX_MEMBERS  8
#define SPV_MAX_IDS      256
#define SPV_MAX_LOC      16  /* max in/out locations */
#define SPV_MAX_BIND     16  /* max descriptor bindings */

typedef struct {
    uint8_t  kind;
    uint8_t  ncmp;     /* VEC: num components; MAT: dimension (2/3/4) */
    uint8_t  nmembers; /* STRUCT */
    uint8_t  storage;  /* PTR: storage class */
    uint32_t elem_id;  /* VEC/MAT/PTR/ARRAY: element type ID */
    uint32_t members[SPV_MAX_MEMBERS]; /* STRUCT: member type IDs */
} spv_type_t;

/* A runtime value — all numeric data stored as floats (column-major for matrices). */
typedef struct {
    uint8_t  n;              /* number of float components (0 = none) */
    uint8_t  is_ptr;         /* 1 if this is a pointer value */
    float    f[16];          /* data: scalar in f[0], vec4 in f[0..3], mat4 in f[0..15] */
    uint32_t ptr_var;        /* pointer: base variable ID */
    uint16_t ptr_off;        /* pointer: flat float offset within variable's value */
} spv_val_t;

typedef struct {
    int8_t location;                         /* -1 = not set */
    int8_t binding;                          /* -1 = not set */
    int8_t dset;
    int8_t builtin;                          /* -1 = not set; SPV_BI_* */
    int8_t mem_builtin [SPV_MAX_MEMBERS];    /* per-member builtin  */
    int8_t mem_location[SPV_MAX_MEMBERS];    /* per-member location */
} spv_decor_t;

/* Parsed, ready-to-execute module.  Allocated with kmalloc by spv_parse(). */
typedef struct {
    const uint32_t* words;
    uint32_t        nwords;
    uint32_t        bound;     /* max ID + 1 */

    spv_type_t  types [SPV_MAX_IDS];
    uint8_t     tvalid[SPV_MAX_IDS]; /* 1 if types[i] is populated */

    spv_decor_t decors[SPV_MAX_IDS];

    uint8_t     vvalid [SPV_MAX_IDS]; /* 1 if this ID is an OpVariable */
    uint8_t     vsclass[SPV_MAX_IDS]; /* storage class */
    uint32_t    vtype  [SPV_MAX_IDS]; /* pointee type ID */

    spv_val_t   consts [SPV_MAX_IDS]; /* filled by OpConstant / OpConstantComposite */
    uint8_t     cvalid [SPV_MAX_IDS];

    uint32_t    entry_func_id;
    uint32_t    glsl_ext_id;  /* GLSL.std.450 import ID, 0 if none */
    uint8_t     stage;        /* 0=vertex, 4=fragment */
} spv_mod_t;

/* ── Shader I/O interface ───────────────────────────────────────────────── */

typedef struct {
    /* Inputs by location (vertex attributes or interpolated fragment inputs) */
    spv_val_t   inputs [SPV_MAX_LOC];
    int         in_valid[SPV_MAX_LOC];

    /* Uniform buffers by binding — raw float arrays (std430/scalar layout) */
    const float* ubos     [SPV_MAX_BIND];
    int          ubo_n    [SPV_MAX_BIND]; /* count in floats */

    /* Combined image samplers by binding */
    const uint8_t* tex_data[SPV_MAX_BIND]; /* RGBA8, row-major */
    int            tex_w   [SPV_MAX_BIND];
    int            tex_h   [SPV_MAX_BIND];

    /* Outputs by location (vertex varyings, fragment color outputs) */
    spv_val_t   outputs[SPV_MAX_LOC];

    /* Builtins */
    float gl_position [4]; /* vertex: written by shader; fragment: not used */
    float frag_coord  [4]; /* fragment: filled by backend (x, y, z=depth, w=1) */
} spv_iface_t;

/* ── Reusable execution context ─────────────────────────────────────────── */
/* Per-invocation register file.  Pre-allocate once and reuse across many
   shader invocations (e.g., per-vertex / per-fragment in a draw call). */
#define SPV_EXEC_MAX SPV_MAX_IDS
typedef struct {
    spv_val_t regs[SPV_EXEC_MAX];
    uint8_t   rvalid[SPV_EXEC_MAX];
    spv_val_t vars[SPV_EXEC_MAX];
    uint32_t  prev_label;
    uint32_t  cur_label;
} spv_exec_t;

/* ── API ────────────────────────────────────────────────────────────────── */

/* Parse a SPIR-V binary (data/nwords points to 32-bit words; must remain valid
   for the module's lifetime).  Returns NULL on error.
   The returned module must be freed with spv_free(). */
spv_mod_t* spv_parse(const uint32_t* data, uint32_t nwords);
void       spv_free (spv_mod_t* mod);

/* Allocate / free a reusable execution context. */
spv_exec_t* spv_alloc_exec(void);
void        spv_free_exec(spv_exec_t* e);

/* Execute with a pre-allocated context (context is reset internally before use).
   Use this in hot paths (e.g., per-fragment) to avoid repeated kmalloc/kfree. */
int spv_execute_ex(spv_mod_t* mod, spv_iface_t* iface, spv_exec_t* e);

/* Convenience wrapper — allocates and frees a temporary execution context. */
int spv_execute(spv_mod_t* mod, spv_iface_t* iface);
