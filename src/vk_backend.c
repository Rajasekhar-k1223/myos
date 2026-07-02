/*
 * Software Vulkan backend for ElseaOS.
 *
 * Architecture:
 *   • All Vulkan objects are kmalloc'd C structs (no GPU).
 *   • Shaders are SPIR-V modules interpreted by src/spirv.c.
 *   • Command buffers are singly-linked lists of recorded command nodes,
 *     played back synchronously on vkQueueSubmit.
 *   • The swapchain presents to the VESA back-buffer.
 *   • Semaphores and fences are no-ops (single-threaded CPU execution).
 */
#include "vk_backend.h"
#include "spirv.h"
#include "kheap.h"
#include "string.h"
#include "mathf.h"

/* ═══════════════════════════════════════════════════════════════════════════
   Internal struct definitions
   ═══════════════════════════════════════════════════════════════════════════ */

struct vk_instance_t  { int tag; struct vk_phys_dev_t* phys; };
struct vk_phys_dev_t  { int tag; };
struct vk_device_t    { int tag; struct vk_queue_t* queue; };
struct vk_queue_t     { int tag; };
struct vk_semaphore_t { uint8_t signaled; };
struct vk_fence_t     { uint8_t signaled; };

struct vk_devmem_t {
    uint8_t*     ptr;   /* kmalloc'd data */
    VkDeviceSize size;
};

struct vk_buf_t {
    VkDeviceSize       size;
    VkBufferUsageFlags usage;
    VkDeviceMemory     mem;      /* may be NULL before bind */
    VkDeviceSize       mem_off;
};

struct vk_img_t {
    uint32_t       width, height;
    VkFormat       fmt;
    VkDeviceMemory mem;
    VkDeviceSize   mem_off;
    uint32_t*      swp_pixels; /* non-NULL for swapchain images */
};

struct vk_imgview_t {
    VkImage                 image;
    VkFormat                fmt;
    VkImageAspectFlagBits   aspect;
};

struct vk_sampler_t {
    VkFilter             mag, min;
    VkSamplerAddressMode addr_u, addr_v;
};

struct vk_shader_t {
    const uint32_t* code;
    uint32_t        nwords;
    spv_mod_t*      mod;   /* parsed immediately on creation */
};

struct vk_render_pass_t {
    VkAttachmentDescription atts[8];
    int natt;
};

struct vk_framebuffer_t {
    VkRenderPass rp;
    uint32_t     width, height;
    VkImageView  color_views[8];
    int          ncolor;
    VkImageView  depth_view; /* NULL = no depth */
};

struct vk_dsl_t {
    VkDescriptorSetLayoutBinding bindings[16];
    int nbindings;
};

struct vk_desc_pool_t { int tag; };

struct vk_desc_set_t {
    struct {
        uint8_t          valid;
        VkDescriptorType type;
        struct { VkBuffer buf; VkDeviceSize off, range; } ubuf;
        struct { VkImageView iv; VkSampler sampler; } img;
    } b[16]; /* indexed by binding number */
};

struct vk_pipeline_layout_t {
    VkDescriptorSetLayout dsets[8];
    int ndsets;
};

struct vk_pipeline_t {
    spv_mod_t*                   vert_mod;
    spv_mod_t*                   frag_mod;
    VkVertexInputBindingDescription   vib[8];  int nvib;
    VkVertexInputAttributeDescription via[16]; int nvia;
    VkPrimitiveTopology  topology;
    VkCullModeFlags      cull_mode;
    VkFrontFace          front_face;
    VkBool32             depth_test, depth_write;
    VkCompareOp          depth_cmp;
    VkBool32             blend_en;
    VkBlendFactor        src_col, dst_col;
    VkBlendFactor        src_alp, dst_alp;
    VkBlendOp            col_op,  alp_op;
    VkViewport           vp;      /* static viewport (if not dynamic) */
    VkRect2D             sc;      /* static scissor */
    VkPipelineLayout     layout;
};

/* ── Command buffer ─────────────────────────────────────────────────────── */
enum {
    CMD_BEGIN_RP=1, CMD_END_RP, CMD_BIND_PIPE,
    CMD_BIND_VB, CMD_BIND_IB, CMD_BIND_DS,
    CMD_DRAW, CMD_DRAW_IDX,
    CMD_SET_VP, CMD_SET_SC,
    CMD_CLEAR_IMG, CMD_COPY_BUF_IMG, CMD_BARRIER
};

typedef struct vk_cmd_node_t vk_cmd_node_t;
struct vk_cmd_node_t {
    uint8_t type;
    vk_cmd_node_t* next;
    union {
        struct { VkRenderPass rp; VkFramebuffer fb; VkRect2D area;
                 VkClearValue cv[8]; uint32_t ncv; }          begin_rp;
        struct { VkPipeline pipe; }                           bind_pipe;
        struct { VkBuffer b[4]; VkDeviceSize off[4];
                 uint32_t first, count; }                     bind_vb;
        struct { VkBuffer b; VkDeviceSize off; VkIndexType t; } bind_ib;
        struct { VkDescriptorSet ds[8]; uint32_t first, n; } bind_ds;
        struct { uint32_t vc, ic, fv, fi; }                  draw;
        struct { uint32_t ic, iic, fi; int32_t vo; uint32_t fii; } draw_idx;
        struct { VkViewport vp; }                             set_vp;
        struct { VkRect2D sc; }                               set_sc;
        struct { VkImage img; VkClearColorValue col; }        clear_img;
        struct { VkBuffer src; VkImage dst; VkDeviceSize so; } copy_bi;
    };
};

struct vk_cmd_pool_t { int tag; };
struct vk_cmd_buf_t  {
    vk_cmd_node_t* head;
    vk_cmd_node_t* tail;
    uint8_t        recording;
};

struct vk_swapchain_t {
    VkSurfaceKHR surface;
    uint32_t     width, height;
    VkFormat     fmt;
    struct vk_img_t images[3]; /* backed by own pixel arrays */
    uint32_t*       pix   [3];
    int             nimages;
    uint32_t        current;
};

struct vk_surface_t {
    uint32_t* pixels;   /* host ARGB framebuffer (e.g., vesa_get_backbuffer) */
    uint32_t  width, height;
};

/* ── Execution state for one command-buffer playback ────────────────────── */
typedef struct {
    VkPipeline      pipe;
    VkBuffer        vb[4];
    VkDeviceSize    vboff[4];
    VkBuffer        ib;
    VkDeviceSize    iboff;
    VkIndexType     ibtype;
    VkDescriptorSet ds[8];
    int             nds;
    VkViewport      vp;
    VkRect2D        sc;
    VkFramebuffer   fb;
    uint32_t*       col_buf;  /* color framebuffer pixels */
    float*          dep_buf;  /* depth buffer floats, or NULL */
    uint32_t        fb_w, fb_h;
} vk_exec_t;

/* ═══════════════════════════════════════════════════════════════════════════
   Utility helpers
   ═══════════════════════════════════════════════════════════════════════════ */

static inline float vkf_clamp01(float v) { return v<0.f?0.f:v>1.f?1.f:v; }
static inline float vkf_min(float a,float b){return a<b?a:b;}
static inline float vkf_max(float a,float b){return a>b?a:b;}

static int fmt_ncomp(VkFormat f) {
    switch(f) {
    case VK_FORMAT_R32G32_SFLOAT:       return 2;
    case VK_FORMAT_R32G32B32_SFLOAT:    return 3;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return 4;
    default: return 1;
    }
}

static float blend_factor(VkBlendFactor f,
                          float sr,float sg,float sb,float sa,
                          float dr,float dg,float db,float da) {
    (void)sr;(void)sg;(void)sb;(void)dr;(void)dg;(void)db;
    switch(f) {
    case VK_BLEND_FACTOR_ZERO:                return 0.f;
    case VK_BLEND_FACTOR_ONE:                 return 1.f;
    case VK_BLEND_FACTOR_SRC_ALPHA:           return sa;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA: return 1.f - sa;
    case VK_BLEND_FACTOR_DST_ALPHA:           return da;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA: return 1.f - da;
    default: return 1.f;
    }
}

static inline void* buf_ptr(VkBuffer buf) {
    if (!buf || !buf->mem || !buf->mem->ptr) return NULL;
    return (uint8_t*)buf->mem->ptr + buf->mem_off;
}

/* ── Attribute fetch ─────────────────────────────────────────────────────── */
static void fetch_attribs(const vk_exec_t* ex, uint32_t vi, spv_iface_t* iface) {
    struct vk_pipeline_t* p = (struct vk_pipeline_t*)ex->pipe;
    for (int a = 0; a < p->nvia; a++) {
        const VkVertexInputAttributeDescription* attr = &p->via[a];
        /* find binding stride */
        uint32_t stride = 4;
        for (int b2 = 0; b2 < p->nvib; b2++)
            if (p->vib[b2].binding == attr->binding) { stride = p->vib[b2].stride; break; }
        uint32_t bind = attr->binding;
        if (bind >= 4 || !ex->vb[bind]) continue;
        uint8_t* base = (uint8_t*)buf_ptr(ex->vb[bind]);
        if (!base) continue;
        base += ex->vboff[bind];
        const float* fp = (const float*)(base + vi * stride + attr->offset);
        int nc = fmt_ncomp(attr->format);
        uint32_t loc = attr->location;
        if (loc >= SPV_MAX_LOC) continue;
        iface->inputs[loc].n = (uint8_t)nc;
        for (int i = 0; i < nc; i++) iface->inputs[loc].f[i] = fp[i];
        iface->in_valid[loc] = 1;
    }
}

/* ── Descriptor binding → spv_iface ─────────────────────────────────────── */
static void bind_desc(const vk_exec_t* ex, spv_iface_t* iface) {
    for (int d = 0; d < ex->nds; d++) {
        struct vk_desc_set_t* ds = (struct vk_desc_set_t*)ex->ds[d];
        if (!ds) continue;
        for (int b = 0; b < 16; b++) {
            if (!ds->b[b].valid) continue;
            if (ds->b[b].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                ds->b[b].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
                VkBuffer buf = ds->b[b].ubuf.buf;
                if (buf && buf->mem && buf->mem->ptr) {
                    iface->ubos[b] = (const float*)((uint8_t*)buf->mem->ptr
                                        + buf->mem_off + ds->b[b].ubuf.off);
                    VkDeviceSize rng = ds->b[b].ubuf.range;
                    iface->ubo_n[b] = (rng == (VkDeviceSize)-1) ?
                        (int)(buf->size / 4) : (int)(rng / 4);
                }
            } else if (ds->b[b].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
                       ds->b[b].type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
                VkImageView iv = ds->b[b].img.iv;
                if (!iv || !iv->image) continue;
                struct vk_img_t* img = (struct vk_img_t*)iv->image;
                uint8_t* tex = NULL;
                if (img->swp_pixels) tex = (uint8_t*)img->swp_pixels;
                else if (img->mem && img->mem->ptr)
                    tex = (uint8_t*)img->mem->ptr + img->mem_off;
                if (!tex) continue;
                iface->tex_data[b] = tex;
                iface->tex_w[b]    = (int)img->width;
                iface->tex_h[b]    = (int)img->height;
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   Rasterizer
   ═══════════════════════════════════════════════════════════════════════════ */

/* Processed vertex after vertex shader */
typedef struct {
    float clip[4];   /* gl_Position in clip space */
    float sx, sy;    /* screen pixels */
    float sz;        /* depth in [0,1] */
    float vary[SPV_MAX_LOC][4]; /* interpolated varyings */
    int   nvarying;
} vkv_t;

static void project_vertex(const vkv_t* v, const VkViewport* vp, vkv_t* out) {
    *out = *v;
    float w = v->clip[3];
    if (w == 0.f) w = 1e-6f;
    float xn = v->clip[0] / w;
    float yn = v->clip[1] / w;
    float zn = v->clip[2] / w;
    /* Vulkan NDC: x in [-1,1], y in [-1,1] (y-up in NDC, y-down in screen) */
    out->sx = (xn + 1.f) * 0.5f * vp->width  + vp->x;
    out->sy = (1.f - yn) * 0.5f * vp->height + vp->y; /* flip Y */
    out->sz = zn * (vp->maxDepth - vp->minDepth) + vp->minDepth;
    /* clamp depth */
    out->sz = out->sz < 0.f ? 0.f : out->sz > 1.f ? 1.f : out->sz;
}

static float edge_fn(float ax,float ay,float bx,float by,float px,float py) {
    return (px - ax)*(by - ay) - (py - ay)*(bx - ax);
}

static int depth_pass(VkCompareOp op, float frag, float existing) {
    switch (op) {
    case VK_COMPARE_OP_LESS:             return frag < existing;
    case VK_COMPARE_OP_LESS_OR_EQUAL:    return frag <= existing;
    case VK_COMPARE_OP_GREATER:          return frag > existing;
    case VK_COMPARE_OP_GREATER_OR_EQUAL: return frag >= existing;
    case VK_COMPARE_OP_EQUAL:            return frag == existing;
    case VK_COMPARE_OP_NOT_EQUAL:        return frag != existing;
    case VK_COMPARE_OP_ALWAYS:           return 1;
    default:                             return 0;
    }
}

static void raster_triangle(const vk_exec_t* ex, const vkv_t* a,
                            const vkv_t* b, const vkv_t* c,
                            spv_exec_t* fexec) {
    struct vk_pipeline_t* p = (struct vk_pipeline_t*)ex->pipe;
    if (!ex->col_buf) return;

    /* Bounding box (with scissor clip) */
    int x0 = (int)vkf_max(vkf_min(vkf_min(a->sx,b->sx),c->sx), (float)ex->sc.offset.x);
    int y0 = (int)vkf_max(vkf_min(vkf_min(a->sy,b->sy),c->sy), (float)ex->sc.offset.y);
    int x1 = (int)vkf_min(vkf_max(vkf_max(a->sx,b->sx),c->sx)+1.f,
                           (float)(ex->sc.offset.x + (int)ex->sc.extent.width));
    int y1 = (int)vkf_min(vkf_max(vkf_max(a->sy,b->sy),c->sy)+1.f,
                           (float)(ex->sc.offset.y + (int)ex->sc.extent.height));
    x0 = x0 < 0 ? 0 : x0; y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > (int)ex->fb_w ? (int)ex->fb_w : x1;
    y1 = y1 > (int)ex->fb_h ? (int)ex->fb_h : y1;

    float area = edge_fn(a->sx,a->sy, b->sx,b->sy, c->sx,c->sy);
    if (area == 0.f) return;
    float inv_area = 1.f / area;

    /* Perspective-correct: 1/w per vertex */
    float inv_wa = 1.f / (a->clip[3] != 0.f ? a->clip[3] : 1e-6f);
    float inv_wb = 1.f / (b->clip[3] != 0.f ? b->clip[3] : 1e-6f);
    float inv_wc = 1.f / (c->clip[3] != 0.f ? c->clip[3] : 1e-6f);

    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float fp = (float)px + 0.5f;
            float fq = (float)py + 0.5f;
            float la = edge_fn(b->sx,b->sy, c->sx,c->sy, fp,fq) * inv_area;
            float lb = edge_fn(c->sx,c->sy, a->sx,a->sy, fp,fq) * inv_area;
            float lc = edge_fn(a->sx,a->sy, b->sx,b->sy, fp,fq) * inv_area;
            if (la < 0.f || lb < 0.f || lc < 0.f) continue;

            /* Perspective-correct weights */
            float denom = la*inv_wa + lb*inv_wb + lc*inv_wc;
            if (denom == 0.f) continue;
            float pa = la*inv_wa / denom;
            float pb = lb*inv_wb / denom;
            float pc = lc*inv_wc / denom;

            /* Depth */
            float depth = pa*a->sz + pb*b->sz + pc*c->sz;
            int pix_idx = py * (int)ex->fb_w + px;

            if (p->depth_test && ex->dep_buf) {
                if (!depth_pass(p->depth_cmp, depth, ex->dep_buf[pix_idx])) continue;
            }

            /* Build fragment shader interface */
            spv_iface_t iface;
            memset(&iface, 0, sizeof(iface));
            bind_desc(ex, &iface);

            /* Interpolate varyings from vertex outputs */
            int nloc = a->nvarying > b->nvarying ? a->nvarying : b->nvarying;
            if (c->nvarying > nloc) nloc = c->nvarying;
            for (int loc = 0; loc < nloc && loc < SPV_MAX_LOC; loc++) {
                iface.inputs[loc].n = 4;
                for (int k = 0; k < 4; k++)
                    iface.inputs[loc].f[k] = pa*a->vary[loc][k]
                                           + pb*b->vary[loc][k]
                                           + pc*c->vary[loc][k];
                iface.in_valid[loc] = 1;
            }

            iface.frag_coord[0] = fp;
            iface.frag_coord[1] = fq;
            iface.frag_coord[2] = depth;
            iface.frag_coord[3] = 1.f;

            if (!p->frag_mod) continue;
            spv_execute_ex(p->frag_mod, &iface, fexec);

            /* Output color at location 0 */
            float fr = iface.outputs[0].f[0];
            float fg = iface.outputs[0].f[1];
            float fb_c = iface.outputs[0].f[2];
            float fa = iface.outputs[0].n >= 4 ? iface.outputs[0].f[3] : 1.f;

            /* Depth write */
            if (p->depth_test && p->depth_write && ex->dep_buf)
                ex->dep_buf[pix_idx] = depth;

            /* Blending */
            if (p->blend_en) {
                uint32_t dst = ex->col_buf[pix_idx];
                float dr = ((dst>>16)&0xFF)/255.f;
                float dg = ((dst>>8 )&0xFF)/255.f;
                float db_c= ( dst     &0xFF)/255.f;
                float da = ((dst>>24)&0xFF)/255.f;
                float sf = blend_factor(p->src_col,fr,fg,fb_c,fa,dr,dg,db_c,da);
                float df = blend_factor(p->dst_col,fr,fg,fb_c,fa,dr,dg,db_c,da);
                fr   = vkf_clamp01(fr*sf   + dr*df);
                fg   = vkf_clamp01(fg*sf   + dg*df);
                fb_c = vkf_clamp01(fb_c*sf + db_c*df);
                float saf = blend_factor(p->src_alp,fr,fg,fb_c,fa,dr,dg,db_c,da);
                float daf = blend_factor(p->dst_alp,fr,fg,fb_c,fa,dr,dg,db_c,da);
                fa = vkf_clamp01(fa*saf + da*daf);
            }

            uint8_t R = (uint8_t)(fr   * 255.f + 0.5f);
            uint8_t G = (uint8_t)(fg   * 255.f + 0.5f);
            uint8_t B = (uint8_t)(fb_c * 255.f + 0.5f);
            uint8_t A = (uint8_t)(fa   * 255.f + 0.5f);
            ex->col_buf[pix_idx] = ((uint32_t)A<<24)|((uint32_t)R<<16)|
                                   ((uint32_t)G<<8)|B;
        }
    }
}

/* ─── Execute a single vertex through the vertex shader ─────────────────── */
static void exec_vertex(const vk_exec_t* ex, uint32_t vi,
                        spv_exec_t* vexec, vkv_t* out) {
    struct vk_pipeline_t* p = (struct vk_pipeline_t*)ex->pipe;
    spv_iface_t iface;
    memset(&iface, 0, sizeof(iface));

    fetch_attribs(ex, vi, &iface);
    bind_desc(ex, &iface);

    if (p->vert_mod) spv_execute_ex(p->vert_mod, &iface, vexec);

    for (int i = 0; i < 4; i++) out->clip[i] = iface.gl_position[i];
    if (out->clip[3] == 0.f) out->clip[3] = 1.f;

    /* Copy per-vertex outputs (varyings) */
    out->nvarying = 0;
    for (int loc = 0; loc < SPV_MAX_LOC; loc++) {
        for (int k = 0; k < 4; k++)
            out->vary[loc][k] = iface.outputs[loc].f[k];
        if (iface.outputs[loc].n > 0) out->nvarying = loc + 1;
    }

    project_vertex(out, &ex->vp, out);
}

/* ─── Cull triangle by winding order ────────────────────────────────────── */
static int should_cull(const vkv_t* a, const vkv_t* b, const vkv_t* c,
                       VkCullModeFlags cull, VkFrontFace ff) {
    if (cull == VK_CULL_MODE_NONE) return 0;
    float area = edge_fn(a->sx,a->sy, b->sx,b->sy, c->sx,c->sy);
    /* area > 0: CCW in screen (Y-down) = CW from viewer = back face for default Vulkan */
    int is_front = (ff == VK_FRONT_FACE_COUNTER_CLOCKWISE) ? (area < 0.f) : (area > 0.f);
    if (!is_front && (cull & VK_CULL_MODE_BACK_BIT))  return 1;
    if ( is_front && (cull & VK_CULL_MODE_FRONT_BIT)) return 1;
    return 0;
}

/* ─── Draw N vertices with the current pipeline ─────────────────────────── */
static void exec_draw(vk_exec_t* ex, uint32_t vtx_count, uint32_t first_vtx) {
    struct vk_pipeline_t* p = (struct vk_pipeline_t*)ex->pipe;
    if (!p) return;

    spv_exec_t* ve = spv_alloc_exec();
    spv_exec_t* fe = spv_alloc_exec();
    if (!ve || !fe) { spv_free_exec(ve); spv_free_exec(fe); return; }

    vkv_t verts[3];
    uint32_t q = 0; /* ring index for strip/fan */

    for (uint32_t i = 0; i < vtx_count; i++) {
        uint32_t vi = first_vtx + i;
        switch (p->topology) {
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
            exec_vertex(ex, vi, ve, &verts[i % 3]);
            if (i % 3 == 2) {
                if (!should_cull(&verts[0],&verts[1],&verts[2],
                                 p->cull_mode, p->front_face))
                    raster_triangle(ex,&verts[0],&verts[1],&verts[2], fe);
            }
            break;
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
            exec_vertex(ex, vi, ve, &verts[q % 3]);
            q++;
            if (q >= 3) {
                /* Alternate winding for odd triangles */
                int a=( q-3)%3, b=(q-2)%3, c=(q-1)%3;
                if ((q-3)&1) { int t=a;a=b;b=t; } /* swap for correct winding */
                if (!should_cull(&verts[a],&verts[b],&verts[c],
                                 p->cull_mode, p->front_face))
                    raster_triangle(ex,&verts[a],&verts[b],&verts[c], fe);
            }
            break;
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN: {
            /* first vertex is fan center */
            if (i == 0) { exec_vertex(ex, vi, ve, &verts[0]); break; }
            exec_vertex(ex, vi, ve, &verts[1 + (i-1)%2]);
            if (i >= 2) {
                int b2 = 1 + (i-1)%2, c2 = 1 + (i-2)%2;
                if (!should_cull(&verts[0],&verts[b2],&verts[c2],
                                 p->cull_mode, p->front_face))
                    raster_triangle(ex,&verts[0],&verts[b2],&verts[c2], fe);
            }
            break;
        }
        default:
            break;
        }
    }

    spv_free_exec(ve);
    spv_free_exec(fe);
}

/* ─── Indexed draw ───────────────────────────────────────────────────────── */
static void exec_draw_indexed(vk_exec_t* ex, uint32_t idx_count,
                              uint32_t first_idx, int32_t vtx_off) {
    if (!ex->ib) return;
    uint8_t* ib_data = (uint8_t*)buf_ptr(ex->ib) + ex->iboff;
    if (!ib_data) return;

    struct vk_pipeline_t* p = (struct vk_pipeline_t*)ex->pipe;
    if (!p) return;

    spv_exec_t* ve = spv_alloc_exec();
    spv_exec_t* fe = spv_alloc_exec();
    if (!ve || !fe) { spv_free_exec(ve); spv_free_exec(fe); return; }

    vkv_t verts[3];
    for (uint32_t i = 0; i < idx_count; i++) {
        uint32_t raw_idx;
        if (ex->ibtype == VK_INDEX_TYPE_UINT16)
            raw_idx = ((const uint16_t*)ib_data)[first_idx + i];
        else
            raw_idx = ((const uint32_t*)ib_data)[first_idx + i];
        uint32_t vi = (uint32_t)((int32_t)raw_idx + vtx_off);

        exec_vertex(ex, vi, ve, &verts[i % 3]);
        if (i % 3 == 2) {
            if (!should_cull(&verts[0],&verts[1],&verts[2],
                             p->cull_mode, p->front_face))
                raster_triangle(ex,&verts[0],&verts[1],&verts[2], fe);
        }
    }
    spv_free_exec(ve);
    spv_free_exec(fe);
}

/* ─── Set up framebuffer pointers for a render pass ─────────────────────── */
static void setup_fb(vk_exec_t* ex) {
    struct vk_framebuffer_t* fb = (struct vk_framebuffer_t*)ex->fb;
    ex->col_buf = NULL; ex->dep_buf = NULL;
    ex->fb_w = 0; ex->fb_h = 0;
    if (!fb) return;
    ex->fb_w = fb->width; ex->fb_h = fb->height;

    if (fb->ncolor > 0 && fb->color_views[0]) {
        struct vk_imgview_t* iv = (struct vk_imgview_t*)fb->color_views[0];
        struct vk_img_t* img = (struct vk_img_t*)iv->image;
        if (img->swp_pixels)
            ex->col_buf = img->swp_pixels;
        else if (img->mem && img->mem->ptr)
            ex->col_buf = (uint32_t*)((uint8_t*)img->mem->ptr + img->mem_off);
    }
    if (fb->depth_view) {
        struct vk_imgview_t* iv = (struct vk_imgview_t*)fb->depth_view;
        struct vk_img_t* img = (struct vk_img_t*)iv->image;
        if (img->mem && img->mem->ptr)
            ex->dep_buf = (float*)((uint8_t*)img->mem->ptr + img->mem_off);
    }
}

/* ─── Command-buffer playback ───────────────────────────────────────────── */
static void play_cmd_buf(struct vk_cmd_buf_t* cb) {
    vk_exec_t ex;
    memset(&ex, 0, sizeof(ex));

    /* Default scissor / viewport */
    ex.vp.x=0; ex.vp.y=0; ex.vp.width=0; ex.vp.height=0;
    ex.vp.minDepth=0.f; ex.vp.maxDepth=1.f;
    ex.sc.offset.x=0; ex.sc.offset.y=0;
    ex.sc.extent.width=0x7FFFFFFF; ex.sc.extent.height=0x7FFFFFFF;

    for (vk_cmd_node_t* n = cb->head; n; n = n->next) {
        switch (n->type) {
        case CMD_BEGIN_RP: {
            ex.fb = n->begin_rp.fb;
            setup_fb(&ex);
            struct vk_framebuffer_t* fb = (struct vk_framebuffer_t*)ex.fb;
            /* Apply load-op clears */
            for (uint32_t ci = 0; ci < n->begin_rp.ncv && ci < 8; ci++) {
                if (ci == 0 && ex.col_buf && ex.fb_w && ex.fb_h) {
                    uint32_t R = (uint32_t)(vkf_clamp01(n->begin_rp.cv[ci].color.float32[0]) * 255.f);
                    uint32_t G = (uint32_t)(vkf_clamp01(n->begin_rp.cv[ci].color.float32[1]) * 255.f);
                    uint32_t B = (uint32_t)(vkf_clamp01(n->begin_rp.cv[ci].color.float32[2]) * 255.f);
                    uint32_t A = (uint32_t)(vkf_clamp01(n->begin_rp.cv[ci].color.float32[3]) * 255.f);
                    uint32_t col = (A<<24)|(R<<16)|(G<<8)|B;
                    for (uint32_t j = 0; j < ex.fb_w * ex.fb_h; j++) ex.col_buf[j] = col;
                } else if (ci > 0 && ex.dep_buf && fb && (int)ci == fb->ncolor) {
                    for (uint32_t j = 0; j < ex.fb_w * ex.fb_h; j++)
                        ex.dep_buf[j] = n->begin_rp.cv[ci].depthStencil.depth;
                }
            }
            /* Default viewport/scissor to framebuffer size if not set */
            if (ex.vp.width == 0 && fb) {
                ex.vp.width  = (float)fb->width;
                ex.vp.height = (float)fb->height;
                ex.sc.extent.width  = fb->width;
                ex.sc.extent.height = fb->height;
            }
            break;
        }
        case CMD_END_RP:
            break;
        case CMD_BIND_PIPE:
            ex.pipe = n->bind_pipe.pipe;
            break;
        case CMD_BIND_VB:
            for (uint32_t i = 0; i < n->bind_vb.count; i++) {
                uint32_t slot = n->bind_vb.first + i;
                if (slot < 4) { ex.vb[slot] = n->bind_vb.b[i]; ex.vboff[slot] = n->bind_vb.off[i]; }
            }
            break;
        case CMD_BIND_IB:
            ex.ib = n->bind_ib.b; ex.iboff = n->bind_ib.off; ex.ibtype = n->bind_ib.t;
            break;
        case CMD_BIND_DS:
            for (uint32_t i = 0; i < n->bind_ds.n; i++) {
                uint32_t slot = n->bind_ds.first + i;
                if (slot < 8) ex.ds[slot] = n->bind_ds.ds[i];
            }
            if ((int)n->bind_ds.first + (int)n->bind_ds.n > ex.nds)
                ex.nds = (int)(n->bind_ds.first + n->bind_ds.n);
            break;
        case CMD_DRAW:
            exec_draw(&ex, n->draw.vc, n->draw.fv);
            break;
        case CMD_DRAW_IDX:
            exec_draw_indexed(&ex, n->draw_idx.ic, n->draw_idx.fi, n->draw_idx.vo);
            break;
        case CMD_SET_VP:
            ex.vp = n->set_vp.vp;
            break;
        case CMD_SET_SC:
            ex.sc = n->set_sc.sc;
            break;
        case CMD_CLEAR_IMG: {
            struct vk_img_t* img = (struct vk_img_t*)n->clear_img.img;
            if (!img) break;
            uint32_t* pixels = img->swp_pixels;
            if (!pixels && img->mem && img->mem->ptr)
                pixels = (uint32_t*)((uint8_t*)img->mem->ptr + img->mem_off);
            if (!pixels) break;
            uint32_t R = (uint32_t)(vkf_clamp01(n->clear_img.col.float32[0]) * 255.f);
            uint32_t G = (uint32_t)(vkf_clamp01(n->clear_img.col.float32[1]) * 255.f);
            uint32_t B = (uint32_t)(vkf_clamp01(n->clear_img.col.float32[2]) * 255.f);
            uint32_t A = (uint32_t)(vkf_clamp01(n->clear_img.col.float32[3]) * 255.f);
            uint32_t col = (A<<24)|(R<<16)|(G<<8)|B;
            for (uint32_t j = 0; j < img->width * img->height; j++) pixels[j] = col;
            break;
        }
        case CMD_COPY_BUF_IMG: {
            VkBuffer src = n->copy_bi.src;
            struct vk_img_t* dst = (struct vk_img_t*)n->copy_bi.dst;
            if (!src || !dst) break;
            uint8_t* sdata = (uint8_t*)buf_ptr(src) + n->copy_bi.so;
            uint8_t* ddata = NULL;
            if (dst->swp_pixels) ddata = (uint8_t*)dst->swp_pixels;
            else if (dst->mem && dst->mem->ptr)
                ddata = (uint8_t*)dst->mem->ptr + dst->mem_off;
            if (sdata && ddata)
                memcpy(ddata, sdata, dst->width * dst->height * 4);
            break;
        }
        case CMD_BARRIER:
            break; /* No-op: CPU is coherent */
        default:
            break;
        }
    }
}

/* ── Append a node to a command buffer ───────────────────────────────────── */
static vk_cmd_node_t* cb_push(struct vk_cmd_buf_t* cb, uint8_t type) {
    vk_cmd_node_t* n = (vk_cmd_node_t*)kmalloc(sizeof(*n));
    if (!n) return NULL;
    memset(n, 0, sizeof(*n));
    n->type = type;
    if (!cb->head) cb->head = n;
    else           cb->tail->next = n;
    cb->tail = n;
    return n;
}

/* ═══════════════════════════════════════════════════════════════════════════
   Instance / device / queue
   ═══════════════════════════════════════════════════════════════════════════ */

VkResult vkCreateInstance(const VkInstanceCreateInfo* ci, const void* a, VkInstance* out) {
    (void)ci;(void)a;
    struct vk_instance_t* inst = (struct vk_instance_t*)kmalloc(sizeof(*inst));
    if (!inst) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(inst, 0, sizeof(*inst));
    inst->tag = 0x494E5354; /* 'INST' */
    struct vk_phys_dev_t* pd = (struct vk_phys_dev_t*)kmalloc(sizeof(*pd));
    if (!pd) { kfree(inst); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    pd->tag = 0x50485953;
    inst->phys = pd;
    *out = inst;
    return VK_SUCCESS;
}
void vkDestroyInstance(VkInstance inst, const void* a) {
    (void)a;
    if (!inst) return;
    if (inst->phys) kfree(inst->phys);
    kfree(inst);
}
VkResult vkEnumeratePhysicalDevices(VkInstance inst, uint32_t* cnt, VkPhysicalDevice* devs) {
    if (*cnt == 0 || !devs) { *cnt = 1; return VK_SUCCESS; }
    devs[0] = inst->phys; *cnt = 1;
    return VK_SUCCESS;
}
void vkGetPhysicalDeviceProperties(VkPhysicalDevice pd, VkPhysicalDeviceProperties* p) {
    (void)pd;
    memset(p, 0, sizeof(*p));
    p->apiVersion    = (1<<22)|(0<<12)|0;
    p->vendorID      = 0xE15E; /* ElseaOS */
    p->deviceID      = 1;
    p->deviceType    = 4; /* VK_PHYSICAL_DEVICE_TYPE_CPU */
    memcpy(p->deviceName, "ElseaOS Software Renderer", 26);
}
void vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties* mp) {
    (void)pd; memset(mp, 0, sizeof(*mp));
    mp->memoryTypeCount = 1;
}
void vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice pd, uint32_t* cnt,
                                              VkQueueFamilyProperties* props) {
    (void)pd;
    if (*cnt == 0 || !props) { *cnt = 1; return; }
    memset(&props[0], 0, sizeof(props[0]));
    props[0].queueFlags = 0xF; /* graphics | compute | transfer | sparse */
    props[0].queueCount = 1;
    *cnt = 1;
}
VkResult vkCreateDevice(VkPhysicalDevice pd, const VkDeviceCreateInfo* ci,
                        const void* a, VkDevice* out) {
    (void)pd;(void)ci;(void)a;
    struct vk_device_t* dev = (struct vk_device_t*)kmalloc(sizeof(*dev));
    if (!dev) return VK_ERROR_OUT_OF_HOST_MEMORY;
    dev->tag = 0x44455649;
    struct vk_queue_t* q = (struct vk_queue_t*)kmalloc(sizeof(*q));
    if (!q) { kfree(dev); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    q->tag = 0x51554555;
    dev->queue = q;
    *out = dev;
    return VK_SUCCESS;
}
void vkDestroyDevice(VkDevice dev, const void* a) {
    (void)a;
    if (!dev) return;
    if (dev->queue) kfree(dev->queue);
    kfree(dev);
}
void vkGetDeviceQueue(VkDevice dev, uint32_t qi, uint32_t qi2, VkQueue* q) {
    (void)qi;(void)qi2; *q = dev->queue;
}
VkResult vkDeviceWaitIdle(VkDevice dev) { (void)dev; return VK_SUCCESS; }

/* ═══════════════════════════════════════════════════════════════════════════
   Memory
   ═══════════════════════════════════════════════════════════════════════════ */

VkResult vkAllocateMemory(VkDevice dev, const VkMemoryAllocateInfoFull* ai,
                          const void* a, VkDeviceMemory* out) {
    (void)dev;(void)a;
    struct vk_devmem_t* m = (struct vk_devmem_t*)kmalloc(sizeof(*m));
    if (!m) return VK_ERROR_OUT_OF_HOST_MEMORY;
    m->size = ai->allocationSize;
    m->ptr  = (uint8_t*)kmalloc((uint32_t)ai->allocationSize);
    if (!m->ptr) { kfree(m); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    memset(m->ptr, 0, (uint32_t)ai->allocationSize);
    *out = m;
    return VK_SUCCESS;
}
void vkFreeMemory(VkDevice dev, VkDeviceMemory mem, const void* a) {
    (void)dev;(void)a;
    if (!mem) return;
    if (mem->ptr) kfree(mem->ptr);
    kfree(mem);
}
VkResult vkMapMemory(VkDevice dev, VkDeviceMemory mem, VkDeviceSize off,
                     VkDeviceSize size, VkFlags flags, void** pp) {
    (void)dev;(void)size;(void)flags;
    if (!mem || !mem->ptr) return VK_ERROR_OUT_OF_HOST_MEMORY;
    *pp = (uint8_t*)mem->ptr + off;
    return VK_SUCCESS;
}
void vkUnmapMemory(VkDevice dev, VkDeviceMemory mem) { (void)dev;(void)mem; }

/* ═══════════════════════════════════════════════════════════════════════════
   Buffers
   ═══════════════════════════════════════════════════════════════════════════ */

VkResult vkCreateBuffer(VkDevice dev, const VkBufferCreateInfo* ci,
                        const void* a, VkBuffer* out) {
    (void)dev;(void)a;
    struct vk_buf_t* b = (struct vk_buf_t*)kmalloc(sizeof(*b));
    if (!b) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(b, 0, sizeof(*b));
    b->size = ci->size; b->usage = ci->usage;
    *out = b; return VK_SUCCESS;
}
void vkDestroyBuffer(VkDevice dev, VkBuffer b, const void* a) {
    (void)dev;(void)a; if (b) kfree(b);
}
void vkGetBufferMemoryRequirements(VkDevice dev, VkBuffer b, VkMemoryRequirements* r) {
    (void)dev; memset(r,0,sizeof(*r));
    r->size = b->size; r->alignment = 4; r->memoryTypeBits = 1;
}
VkResult vkBindBufferMemory(VkDevice dev, VkBuffer b, VkDeviceMemory mem, VkDeviceSize off) {
    (void)dev; b->mem = mem; b->mem_off = off; return VK_SUCCESS;
}

/* ═══════════════════════════════════════════════════════════════════════════
   Images
   ═══════════════════════════════════════════════════════════════════════════ */

VkResult vkCreateImage(VkDevice dev, const VkImageCreateInfo* ci,
                       const void* a, VkImage* out) {
    (void)dev;(void)a;
    struct vk_img_t* img = (struct vk_img_t*)kmalloc(sizeof(*img));
    if (!img) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(img, 0, sizeof(*img));
    img->width  = ci->extent.width;
    img->height = ci->extent.height;
    img->fmt    = ci->format;
    *out = img; return VK_SUCCESS;
}
void vkDestroyImage(VkDevice dev, VkImage img, const void* a) {
    (void)dev;(void)a; if (img) kfree(img);
}
void vkGetImageMemoryRequirements(VkDevice dev, VkImage img, VkMemoryRequirements* r) {
    (void)dev; memset(r,0,sizeof(*r));
    /* 4 bytes per pixel for RGBA8, 4 bytes for D32 */
    r->size = (VkDeviceSize)img->width * img->height * 4;
    r->alignment = 4; r->memoryTypeBits = 1;
}
VkResult vkBindImageMemory(VkDevice dev, VkImage img, VkDeviceMemory mem, VkDeviceSize off) {
    (void)dev; img->mem = mem; img->mem_off = off; return VK_SUCCESS;
}
VkResult vkCreateImageView(VkDevice dev, const VkImageViewCreateInfo* ci,
                           const void* a, VkImageView* out) {
    (void)dev;(void)a;
    struct vk_imgview_t* iv = (struct vk_imgview_t*)kmalloc(sizeof(*iv));
    if (!iv) return VK_ERROR_OUT_OF_HOST_MEMORY;
    iv->image  = ci->image;
    iv->fmt    = ci->format;
    iv->aspect = (VkImageAspectFlagBits)ci->subresourceRange.aspectMask;
    *out = iv; return VK_SUCCESS;
}
void vkDestroyImageView(VkDevice dev, VkImageView iv, const void* a) {
    (void)dev;(void)a; if (iv) kfree(iv);
}
VkResult vkCreateSampler(VkDevice dev, const VkSamplerCreateInfo* ci,
                         const void* a, VkSampler* out) {
    (void)dev;(void)a;
    struct vk_sampler_t* s = (struct vk_sampler_t*)kmalloc(sizeof(*s));
    if (!s) return VK_ERROR_OUT_OF_HOST_MEMORY;
    s->mag    = ci->magFilter; s->min    = ci->minFilter;
    s->addr_u = ci->addressModeU; s->addr_v = ci->addressModeV;
    *out = s; return VK_SUCCESS;
}
void vkDestroySampler(VkDevice dev, VkSampler s, const void* a) {
    (void)dev;(void)a; if (s) kfree(s);
}

/* ═══════════════════════════════════════════════════════════════════════════
   Shader modules
   ═══════════════════════════════════════════════════════════════════════════ */

VkResult vkCreateShaderModule(VkDevice dev, const VkShaderModuleCreateInfo* ci,
                              const void* a, VkShaderModule* out) {
    (void)dev;(void)a;
    struct vk_shader_t* s = (struct vk_shader_t*)kmalloc(sizeof(*s));
    if (!s) return VK_ERROR_OUT_OF_HOST_MEMORY;
    s->code   = ci->pCode;
    s->nwords = (uint32_t)(ci->codeSize / 4);
    s->mod    = spv_parse(ci->pCode, s->nwords); /* parse eagerly */
    *out = s; return VK_SUCCESS;
}
void vkDestroyShaderModule(VkDevice dev, VkShaderModule s, const void* a) {
    (void)dev;(void)a;
    if (!s) return;
    spv_free(s->mod);
    kfree(s);
}

/* ═══════════════════════════════════════════════════════════════════════════
   Render pass / framebuffer
   ═══════════════════════════════════════════════════════════════════════════ */

VkResult vkCreateRenderPass(VkDevice dev, const VkRenderPassCreateInfo* ci,
                            const void* a, VkRenderPass* out) {
    (void)dev;(void)a;
    struct vk_render_pass_t* rp = (struct vk_render_pass_t*)kmalloc(sizeof(*rp));
    if (!rp) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(rp,0,sizeof(*rp));
    rp->natt = (int)ci->attachmentCount;
    if (rp->natt > 8) rp->natt = 8;
    for (int i = 0; i < rp->natt; i++) rp->atts[i] = ci->pAttachments[i];
    *out = rp; return VK_SUCCESS;
}
void vkDestroyRenderPass(VkDevice dev, VkRenderPass rp, const void* a) {
    (void)dev;(void)a; if (rp) kfree(rp);
}
VkResult vkCreateFramebuffer(VkDevice dev, const VkFramebufferCreateInfo* ci,
                             const void* a, VkFramebuffer* out) {
    (void)dev;(void)a;
    struct vk_framebuffer_t* fb = (struct vk_framebuffer_t*)kmalloc(sizeof(*fb));
    if (!fb) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(fb,0,sizeof(*fb));
    fb->rp = ci->renderPass;
    fb->width = ci->width; fb->height = ci->height;
    /* Determine which views are color vs depth using their image format */
    for (uint32_t i = 0; i < ci->attachmentCount && i < 8; i++) {
        VkImageView iv = ci->pAttachments[i];
        if (!iv) continue;
        struct vk_imgview_t* v = (struct vk_imgview_t*)iv;
        if (v->fmt == VK_FORMAT_D32_SFLOAT ||
            (v->aspect & VK_IMAGE_ASPECT_DEPTH_BIT))
            fb->depth_view = iv;
        else
            fb->color_views[fb->ncolor++] = iv;
    }
    *out = fb; return VK_SUCCESS;
}
void vkDestroyFramebuffer(VkDevice dev, VkFramebuffer fb, const void* a) {
    (void)dev;(void)a; if (fb) kfree(fb);
}

/* ═══════════════════════════════════════════════════════════════════════════
   Descriptor sets
   ═══════════════════════════════════════════════════════════════════════════ */

VkResult vkCreateDescriptorSetLayout(VkDevice dev, const VkDescriptorSetLayoutCreateInfo* ci,
                                     const void* a, VkDescriptorSetLayout* out) {
    (void)dev;(void)a;
    struct vk_dsl_t* dsl = (struct vk_dsl_t*)kmalloc(sizeof(*dsl));
    if (!dsl) return VK_ERROR_OUT_OF_HOST_MEMORY;
    dsl->nbindings = (int)ci->bindingCount;
    if (dsl->nbindings > 16) dsl->nbindings = 16;
    for (int i = 0; i < dsl->nbindings; i++) dsl->bindings[i] = ci->pBindings[i];
    *out = dsl; return VK_SUCCESS;
}
void vkDestroyDescriptorSetLayout(VkDevice dev, VkDescriptorSetLayout dsl, const void* a) {
    (void)dev;(void)a; if (dsl) kfree(dsl);
}
VkResult vkCreateDescriptorPool(VkDevice dev, const VkDescriptorPoolCreateInfo* ci,
                                const void* a, VkDescriptorPool* out) {
    (void)dev;(void)ci;(void)a;
    struct vk_desc_pool_t* p = (struct vk_desc_pool_t*)kmalloc(sizeof(*p));
    if (!p) return VK_ERROR_OUT_OF_HOST_MEMORY;
    p->tag = 0x504F4F4C;
    *out = p; return VK_SUCCESS;
}
void vkDestroyDescriptorPool(VkDevice dev, VkDescriptorPool dp, const void* a) {
    (void)dev;(void)a; if (dp) kfree(dp);
}
VkResult vkAllocateDescriptorSets(VkDevice dev, const VkDescriptorSetAllocateInfo* ai,
                                  VkDescriptorSet* out) {
    (void)dev;
    for (uint32_t i = 0; i < ai->descriptorSetCount; i++) {
        struct vk_desc_set_t* ds = (struct vk_desc_set_t*)kmalloc(sizeof(*ds));
        if (!ds) return VK_ERROR_OUT_OF_HOST_MEMORY;
        memset(ds, 0, sizeof(*ds));
        out[i] = ds;
    }
    return VK_SUCCESS;
}
void vkUpdateDescriptorSets(VkDevice dev, uint32_t wcnt, const VkWriteDescriptorSet* writes,
                            uint32_t ccnt, const void* copies) {
    (void)dev;(void)ccnt;(void)copies;
    for (uint32_t i = 0; i < wcnt; i++) {
        struct vk_desc_set_t* ds = (struct vk_desc_set_t*)writes[i].dstSet;
        if (!ds) continue;
        uint32_t bind = writes[i].dstBinding;
        if (bind >= 16) continue;
        ds->b[bind].valid = 1;
        ds->b[bind].type  = writes[i].descriptorType;
        if (writes[i].pBufferInfo) {
            ds->b[bind].ubuf.buf = writes[i].pBufferInfo->buffer;
            ds->b[bind].ubuf.off = writes[i].pBufferInfo->offset;
            ds->b[bind].ubuf.range = writes[i].pBufferInfo->range;
        }
        if (writes[i].pImageInfo) {
            ds->b[bind].img.iv      = writes[i].pImageInfo->imageView;
            ds->b[bind].img.sampler = writes[i].pImageInfo->sampler;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   Pipeline layout / graphics pipeline
   ═══════════════════════════════════════════════════════════════════════════ */

VkResult vkCreatePipelineLayout(VkDevice dev, const VkPipelineLayoutCreateInfo* ci,
                                const void* a, VkPipelineLayout* out) {
    (void)dev;(void)a;
    struct vk_pipeline_layout_t* pl = (struct vk_pipeline_layout_t*)kmalloc(sizeof(*pl));
    if (!pl) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pl->ndsets = (int)ci->setLayoutCount;
    if (pl->ndsets > 8) pl->ndsets = 8;
    for (int i = 0; i < pl->ndsets; i++) pl->dsets[i] = ci->pSetLayouts[i];
    *out = pl; return VK_SUCCESS;
}
void vkDestroyPipelineLayout(VkDevice dev, VkPipelineLayout pl, const void* a) {
    (void)dev;(void)a; if (pl) kfree(pl);
}

VkResult vkCreateGraphicsPipelines(VkDevice dev, void* cache, uint32_t cnt,
                                   const VkGraphicsPipelineCreateInfo* cis,
                                   const void* a, VkPipeline* out) {
    (void)dev;(void)cache;(void)a;
    for (uint32_t pi = 0; pi < cnt; pi++) {
        const VkGraphicsPipelineCreateInfo* ci = &cis[pi];
        struct vk_pipeline_t* p = (struct vk_pipeline_t*)kmalloc(sizeof(*p));
        if (!p) return VK_ERROR_OUT_OF_HOST_MEMORY;
        memset(p, 0, sizeof(*p));

        /* Find vertex and fragment shader stages */
        for (uint32_t si = 0; si < ci->stageCount; si++) {
            struct vk_shader_t* sm = (struct vk_shader_t*)ci->pStages[si].module;
            if (!sm) continue;
            if (ci->pStages[si].stage == VK_SHADER_STAGE_VERTEX_BIT)
                p->vert_mod = sm->mod;
            else if (ci->pStages[si].stage == VK_SHADER_STAGE_FRAGMENT_BIT)
                p->frag_mod = sm->mod;
        }

        /* Vertex input */
        if (ci->pVertexInputState) {
            const VkPipelineVertexInputStateCreateInfo* vi = ci->pVertexInputState;
            p->nvib = (int)vi->vertexBindingDescriptionCount;
            if (p->nvib > 8) p->nvib = 8;
            for (int i = 0; i < p->nvib; i++) p->vib[i] = vi->pVertexBindingDescriptions[i];
            p->nvia = (int)vi->vertexAttributeDescriptionCount;
            if (p->nvia > 16) p->nvia = 16;
            for (int i = 0; i < p->nvia; i++) p->via[i] = vi->pVertexAttributeDescriptions[i];
        }

        /* Input assembly */
        p->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        if (ci->pInputAssemblyState)
            p->topology = ci->pInputAssemblyState->topology;

        /* Rasterization */
        p->cull_mode  = VK_CULL_MODE_NONE;
        p->front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        if (ci->pRasterizationState) {
            p->cull_mode  = ci->pRasterizationState->cullMode;
            p->front_face = ci->pRasterizationState->frontFace;
        }

        /* Depth stencil */
        p->depth_test  = VK_FALSE;
        p->depth_write = VK_TRUE;
        p->depth_cmp   = VK_COMPARE_OP_LESS;
        if (ci->pDepthStencilState) {
            p->depth_test  = ci->pDepthStencilState->depthTestEnable;
            p->depth_write = ci->pDepthStencilState->depthWriteEnable;
            p->depth_cmp   = ci->pDepthStencilState->depthCompareOp;
        }

        /* Blending */
        p->blend_en  = VK_FALSE;
        p->src_col   = VK_BLEND_FACTOR_ONE;
        p->dst_col   = VK_BLEND_FACTOR_ZERO;
        p->src_alp   = VK_BLEND_FACTOR_ONE;
        p->dst_alp   = VK_BLEND_FACTOR_ZERO;
        p->col_op    = VK_BLEND_OP_ADD;
        p->alp_op    = VK_BLEND_OP_ADD;
        if (ci->pColorBlendState && ci->pColorBlendState->attachmentCount > 0) {
            const VkPipelineColorBlendAttachmentState* at = &ci->pColorBlendState->pAttachments[0];
            p->blend_en = at->blendEnable;
            p->src_col  = at->srcColorBlendFactor; p->dst_col = at->dstColorBlendFactor;
            p->src_alp  = at->srcAlphaBlendFactor; p->dst_alp = at->dstAlphaBlendFactor;
            p->col_op   = at->colorBlendOp;        p->alp_op  = at->alphaBlendOp;
        }

        /* Static viewport/scissor */
        if (ci->pViewportState) {
            if (ci->pViewportState->pViewports && ci->pViewportState->viewportCount > 0)
                p->vp = ci->pViewportState->pViewports[0];
            if (ci->pViewportState->pScissors && ci->pViewportState->scissorCount > 0)
                p->sc = ci->pViewportState->pScissors[0];
        }

        p->layout = ci->layout;
        out[pi] = p;
    }
    return VK_SUCCESS;
}
void vkDestroyPipeline(VkDevice dev, VkPipeline p, const void* a) {
    (void)dev;(void)a; if (p) kfree(p);
}

/* ═══════════════════════════════════════════════════════════════════════════
   Command pool / buffer
   ═══════════════════════════════════════════════════════════════════════════ */

VkResult vkCreateCommandPool(VkDevice dev, const VkCommandPoolCreateInfo* ci,
                             const void* a, VkCommandPool* out) {
    (void)dev;(void)ci;(void)a;
    struct vk_cmd_pool_t* cp = (struct vk_cmd_pool_t*)kmalloc(sizeof(*cp));
    if (!cp) return VK_ERROR_OUT_OF_HOST_MEMORY;
    cp->tag = 0x504F4F4C;
    *out = cp; return VK_SUCCESS;
}
void vkDestroyCommandPool(VkDevice dev, VkCommandPool cp, const void* a) {
    (void)dev;(void)a; if (cp) kfree(cp);
}
VkResult vkAllocateCommandBuffers(VkDevice dev, const VkCommandBufferAllocateInfo* ai,
                                  VkCommandBuffer* out) {
    (void)dev;
    for (uint32_t i = 0; i < ai->commandBufferCount; i++) {
        struct vk_cmd_buf_t* cb = (struct vk_cmd_buf_t*)kmalloc(sizeof(*cb));
        if (!cb) return VK_ERROR_OUT_OF_HOST_MEMORY;
        memset(cb,0,sizeof(*cb));
        out[i] = cb;
    }
    return VK_SUCCESS;
}
void vkFreeCommandBuffers(VkDevice dev, VkCommandPool cp, uint32_t cnt,
                          const VkCommandBuffer* cbs) {
    (void)dev;(void)cp;
    for (uint32_t i = 0; i < cnt; i++) {
        struct vk_cmd_buf_t* cb = (struct vk_cmd_buf_t*)cbs[i];
        if (!cb) continue;
        for (vk_cmd_node_t* n = cb->head; n; ) {
            vk_cmd_node_t* nx = n->next; kfree(n); n = nx;
        }
        kfree(cb);
    }
}

/* ── Recording API ───────────────────────────────────────────────────────── */

VkResult vkBeginCommandBuffer(VkCommandBuffer cb, const VkCommandBufferBeginInfo* bi) {
    (void)bi;
    /* Reset existing nodes */
    for (vk_cmd_node_t* n = cb->head; n; ) {
        vk_cmd_node_t* nx = n->next; kfree(n); n = nx;
    }
    cb->head = cb->tail = NULL;
    cb->recording = 1;
    return VK_SUCCESS;
}
VkResult vkEndCommandBuffer(VkCommandBuffer cb) {
    cb->recording = 0;
    return VK_SUCCESS;
}
void vkCmdBeginRenderPass(VkCommandBuffer cb, const VkRenderPassBeginInfo* bi,
                          VkSubpassContents sc) {
    (void)sc;
    vk_cmd_node_t* n = cb_push(cb, CMD_BEGIN_RP);
    if (!n) return;
    n->begin_rp.rp  = bi->renderPass;
    n->begin_rp.fb  = bi->framebuffer;
    n->begin_rp.area = bi->renderArea;
    n->begin_rp.ncv = bi->clearValueCount < 8 ? bi->clearValueCount : 8;
    for (uint32_t i = 0; i < n->begin_rp.ncv; i++) n->begin_rp.cv[i] = bi->pClearValues[i];
}
void vkCmdEndRenderPass(VkCommandBuffer cb) { cb_push(cb, CMD_END_RP); }

void vkCmdBindPipeline(VkCommandBuffer cb, VkPipelineBindPoint bp, VkPipeline pipe) {
    (void)bp;
    vk_cmd_node_t* n = cb_push(cb, CMD_BIND_PIPE);
    if (n) n->bind_pipe.pipe = pipe;
}
void vkCmdBindVertexBuffers(VkCommandBuffer cb, uint32_t first, uint32_t cnt,
                            const VkBuffer* bufs, const VkDeviceSize* offsets) {
    vk_cmd_node_t* n = cb_push(cb, CMD_BIND_VB);
    if (!n) return;
    n->bind_vb.first = first; n->bind_vb.count = cnt < 4 ? cnt : 4;
    for (uint32_t i = 0; i < n->bind_vb.count; i++) {
        n->bind_vb.b[i] = bufs[i]; n->bind_vb.off[i] = offsets[i];
    }
}
void vkCmdBindIndexBuffer(VkCommandBuffer cb, VkBuffer buf, VkDeviceSize off, VkIndexType t) {
    vk_cmd_node_t* n = cb_push(cb, CMD_BIND_IB);
    if (n) { n->bind_ib.b = buf; n->bind_ib.off = off; n->bind_ib.t = t; }
}
void vkCmdBindDescriptorSets(VkCommandBuffer cb, VkPipelineBindPoint bp,
                             VkPipelineLayout layout, uint32_t first, uint32_t cnt,
                             const VkDescriptorSet* sets, uint32_t dc, const uint32_t* dyn) {
    (void)bp;(void)layout;(void)dc;(void)dyn;
    vk_cmd_node_t* n = cb_push(cb, CMD_BIND_DS);
    if (!n) return;
    n->bind_ds.first = first; n->bind_ds.n = cnt < 8 ? cnt : 8;
    for (uint32_t i = 0; i < n->bind_ds.n; i++) n->bind_ds.ds[i] = sets[i];
}
void vkCmdDraw(VkCommandBuffer cb, uint32_t vc, uint32_t ic, uint32_t fv, uint32_t fi) {
    (void)ic;(void)fi;
    vk_cmd_node_t* n = cb_push(cb, CMD_DRAW);
    if (n) { n->draw.vc = vc; n->draw.fv = fv; }
}
void vkCmdDrawIndexed(VkCommandBuffer cb, uint32_t ic, uint32_t iic, uint32_t fi,
                      int32_t vo, uint32_t fii) {
    (void)iic;(void)fii;
    vk_cmd_node_t* n = cb_push(cb, CMD_DRAW_IDX);
    if (n) { n->draw_idx.ic = ic; n->draw_idx.fi = fi; n->draw_idx.vo = vo; }
}
void vkCmdSetViewport(VkCommandBuffer cb, uint32_t first, uint32_t cnt, const VkViewport* vps) {
    (void)first;(void)cnt;
    vk_cmd_node_t* n = cb_push(cb, CMD_SET_VP);
    if (n) n->set_vp.vp = vps[0];
}
void vkCmdSetScissor(VkCommandBuffer cb, uint32_t first, uint32_t cnt, const VkRect2D* scs) {
    (void)first;(void)cnt;
    vk_cmd_node_t* n = cb_push(cb, CMD_SET_SC);
    if (n) n->set_sc.sc = scs[0];
}
void vkCmdPipelineBarrier(VkCommandBuffer cb, VkFlags s, VkFlags d, VkFlags dep,
                          uint32_t mb, const void* mbs, uint32_t bb, const void* bbs,
                          uint32_t ib2, const VkImageMemoryBarrier* ibs) {
    (void)s;(void)d;(void)dep;(void)mb;(void)mbs;(void)bb;(void)bbs;(void)ib2;(void)ibs;
    cb_push(cb, CMD_BARRIER);
}
void vkCmdCopyBufferToImage(VkCommandBuffer cb, VkBuffer src, VkImage dst,
                            VkImageLayout layout, uint32_t rc, const void* regions) {
    (void)layout;(void)rc;(void)regions;
    vk_cmd_node_t* n = cb_push(cb, CMD_COPY_BUF_IMG);
    if (n) { n->copy_bi.src = src; n->copy_bi.dst = dst; n->copy_bi.so = 0; }
}
void vkCmdClearColorImage(VkCommandBuffer cb, VkImage img, VkImageLayout layout,
                          const VkClearColorValue* col, uint32_t rc, const VkImageSubresourceRange* rs) {
    (void)layout;(void)rc;(void)rs;
    vk_cmd_node_t* n = cb_push(cb, CMD_CLEAR_IMG);
    if (n) { n->clear_img.img = img; n->clear_img.col = *col; }
}

/* ═══════════════════════════════════════════════════════════════════════════
   Queue submit
   ═══════════════════════════════════════════════════════════════════════════ */

VkResult vkQueueSubmit(VkQueue q, uint32_t cnt, const VkSubmitInfo* si, VkFence fence) {
    (void)q;(void)fence;
    for (uint32_t i = 0; i < cnt; i++)
        for (uint32_t j = 0; j < si[i].commandBufferCount; j++)
            play_cmd_buf((struct vk_cmd_buf_t*)si[i].pCommandBuffers[j]);
    return VK_SUCCESS;
}
VkResult vkQueueWaitIdle(VkQueue q) { (void)q; return VK_SUCCESS; }

/* ═══════════════════════════════════════════════════════════════════════════
   Swapchain + surface
   ═══════════════════════════════════════════════════════════════════════════ */

VkResult vkCreateElseaSurface(VkInstance inst, const VkElseaSurfaceCreateInfoKHR* info,
                              const void* a, VkSurfaceKHR* out) {
    (void)inst;(void)a;
    struct vk_surface_t* s = (struct vk_surface_t*)kmalloc(sizeof(*s));
    if (!s) return VK_ERROR_OUT_OF_HOST_MEMORY;
    s->pixels = info->pixels;
    s->width  = (uint32_t)info->width;
    s->height = (uint32_t)info->height;
    *out = s; return VK_SUCCESS;
}

VkResult vkCreateSwapchainKHR(VkDevice dev, const VkSwapchainCreateInfoKHR* ci,
                              const void* a, VkSwapchainKHR* out) {
    (void)dev;(void)a;
    struct vk_swapchain_t* sw = (struct vk_swapchain_t*)kmalloc(sizeof(*sw));
    if (!sw) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(sw, 0, sizeof(*sw));
    sw->surface  = ci->surface;
    sw->fmt      = ci->imageFormat;
    sw->width    = ci->imageExtent.width;
    sw->height   = ci->imageExtent.height;
    sw->nimages  = (int)(ci->minImageCount < 3 ? ci->minImageCount + 1 : 3);
    if (sw->nimages < 2) sw->nimages = 2;
    for (int i = 0; i < sw->nimages; i++) {
        uint32_t* px = (uint32_t*)kmalloc(sw->width * sw->height * 4);
        if (!px) { /* cleanup partial */
            for (int j = 0; j < i; j++) kfree(sw->pix[j]);
            kfree(sw); return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        memset(px, 0, sw->width * sw->height * 4);
        sw->pix[i] = px;
        sw->images[i].width      = sw->width;
        sw->images[i].height     = sw->height;
        sw->images[i].fmt        = sw->fmt;
        sw->images[i].swp_pixels = px;
    }
    *out = sw; return VK_SUCCESS;
}
void vkDestroySwapchainKHR(VkDevice dev, VkSwapchainKHR sw, const void* a) {
    (void)dev;(void)a;
    if (!sw) return;
    for (int i = 0; i < sw->nimages; i++) if (sw->pix[i]) kfree(sw->pix[i]);
    kfree(sw);
}
VkResult vkGetSwapchainImagesKHR(VkDevice dev, VkSwapchainKHR sw,
                                  uint32_t* cnt, VkImage* imgs) {
    (void)dev;
    if (!imgs) { *cnt = (uint32_t)sw->nimages; return VK_SUCCESS; }
    uint32_t n = *cnt < (uint32_t)sw->nimages ? *cnt : (uint32_t)sw->nimages;
    for (uint32_t i = 0; i < n; i++) imgs[i] = &sw->images[i];
    *cnt = n;
    return VK_SUCCESS;
}
VkResult vkAcquireNextImageKHR(VkDevice dev, VkSwapchainKHR sw, uint64_t timeout,
                               VkSemaphore sem, VkFence fence, uint32_t* idx) {
    (void)dev;(void)timeout;
    if (sem) ((struct vk_semaphore_t*)sem)->signaled = 1;
    if (fence) ((struct vk_fence_t*)fence)->signaled = 1;
    *idx = sw->current;
    return VK_SUCCESS;
}
VkResult vkQueuePresentKHR(VkQueue q, const VkPresentInfoKHR* pi) {
    (void)q;
    for (uint32_t i = 0; i < pi->swapchainCount; i++) {
        struct vk_swapchain_t* sw = (struct vk_swapchain_t*)pi->pSwapchains[i];
        if (!sw) continue;
        uint32_t idx = pi->pImageIndices[i];
        if ((int)idx >= sw->nimages) continue;
        struct vk_surface_t* surf = (struct vk_surface_t*)sw->surface;
        if (surf && surf->pixels && sw->pix[idx]) {
            uint32_t npix = sw->width * sw->height;
            memcpy(surf->pixels, sw->pix[idx], npix * 4);
        }
        /* Advance to next image */
        sw->current = (sw->current + 1) % (uint32_t)sw->nimages;
    }
    return VK_SUCCESS;
}

/* ═══════════════════════════════════════════════════════════════════════════
   Semaphores / fences
   ═══════════════════════════════════════════════════════════════════════════ */

VkResult vkCreateSemaphore(VkDevice dev, const VkSemaphoreCreateInfo* ci,
                           const void* a, VkSemaphore* out) {
    (void)dev;(void)ci;(void)a;
    struct vk_semaphore_t* s = (struct vk_semaphore_t*)kmalloc(sizeof(*s));
    if (!s) return VK_ERROR_OUT_OF_HOST_MEMORY;
    s->signaled = 0; *out = s; return VK_SUCCESS;
}
void vkDestroySemaphore(VkDevice dev, VkSemaphore s, const void* a) {
    (void)dev;(void)a; if (s) kfree(s);
}
VkResult vkCreateFence(VkDevice dev, const VkFenceCreateInfo* ci,
                       const void* a, VkFence* out) {
    (void)dev;(void)a;
    struct vk_fence_t* f = (struct vk_fence_t*)kmalloc(sizeof(*f));
    if (!f) return VK_ERROR_OUT_OF_HOST_MEMORY;
    f->signaled = (ci->flags & 1) ? 1 : 0; /* VK_FENCE_CREATE_SIGNALED_BIT=1 */
    *out = f; return VK_SUCCESS;
}
void vkDestroyFence(VkDevice dev, VkFence f, const void* a) {
    (void)dev;(void)a; if (f) kfree(f);
}
VkResult vkWaitForFences(VkDevice dev, uint32_t cnt, const VkFence* fences,
                         VkBool32 all, uint64_t timeout) {
    (void)dev;(void)cnt;(void)fences;(void)all;(void)timeout;
    return VK_SUCCESS; /* CPU is always done */
}
VkResult vkResetFences(VkDevice dev, uint32_t cnt, const VkFence* fences) {
    (void)dev;
    for (uint32_t i = 0; i < cnt; i++)
        if (fences[i]) ((struct vk_fence_t*)fences[i])->signaled = 0;
    return VK_SUCCESS;
}
