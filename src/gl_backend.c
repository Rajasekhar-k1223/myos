#include "gl_backend.h"
#include "kernel.h"
#include "kheap.h"
#include "string.h"
#include "mathf.h"

/* ── Matrix helpers (column-major, standard GL convention) ─────────────── */

static void mat4_identity(float* m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_mul(float* out, const float* a, const float* b) {
    float r[16];
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) sum += a[k * 4 + row] * b[col * 4 + k];
            r[col * 4 + row] = sum;
        }
    }
    memcpy(out, r, sizeof(r));
}

static void mat4_translate(float* m, float x, float y, float z) {
    mat4_identity(m);
    m[12] = x; m[13] = y; m[14] = z;
}

static void mat4_scale(float* m, float x, float y, float z) {
    mat4_identity(m);
    m[0] = x; m[5] = y; m[10] = z;
}

/* Rodrigues' rotation formula, arbitrary axis, angle in degrees. */
static void mat4_rotate(float* m, float angle_deg, float x, float y, float z) {
    float len = sqrtf(x*x + y*y + z*z);
    if (len < 1e-6f) { mat4_identity(m); return; }
    x /= len; y /= len; z /= len;
    float rad = angle_deg * (3.14159265358979323846f / 180.0f);
    float c = cosf(rad), s = sinf(rad), t = 1.0f - c;

    mat4_identity(m);
    m[0]  = t*x*x + c;     m[4]  = t*x*y - s*z;   m[8]  = t*x*z + s*y;
    m[1]  = t*x*y + s*z;   m[5]  = t*y*y + c;     m[9]  = t*y*z - s*x;
    m[2]  = t*x*z - s*y;   m[6]  = t*y*z + s*x;   m[10] = t*z*z + c;
}

static void mat4_ortho(float* m, float l, float r, float b, float t, float n, float f) {
    mat4_identity(m);
    m[0] = 2.0f / (r - l);
    m[5] = 2.0f / (t - b);
    m[10] = -2.0f / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
}

static void mat4_frustum(float* m, float l, float r, float b, float t, float n, float f) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = 2.0f * n / (r - l);
    m[5] = 2.0f * n / (t - b);
    m[8] = (r + l) / (r - l);
    m[9] = (t + b) / (t - b);
    m[10] = -(f + n) / (f - n);
    m[11] = -1.0f;
    m[14] = -2.0f * f * n / (f - n);
}

/* out = m * (x,y,z,1) -- full homogeneous transform */
static void mat4_transform4(const float* m, float x, float y, float z, float w, float* out) {
    out[0] = m[0]*x + m[4]*y + m[8]*z  + m[12]*w;
    out[1] = m[1]*x + m[5]*y + m[9]*z  + m[13]*w;
    out[2] = m[2]*x + m[6]*y + m[10]*z + m[14]*w;
    out[3] = m[3]*x + m[7]*y + m[11]*z + m[15]*w;
}

/* ── Context state ──────────────────────────────────────────────────────── */

#define GL_MAX_STACK 16
#define GL_MAX_TEXTURES 32
#define GL_MAX_VERTS 4096

typedef struct {
    float x, y, z;
    float r, g, b, a;
    float u, v;
} gl_vertex_t;

typedef struct {
    int in_use;
    int w, h;
    uint8_t* data; /* RGBA8 */
    int filter; /* GL_NEAREST or GL_LINEAR */
} gl_texture_t;

typedef struct {
    uint32_t* canvas;
    int cw, ch;
    float* depth; /* per-pixel depth buffer, same size as canvas */

    int vx, vy, vw, vh;
    float clear_r, clear_g, clear_b, clear_a;

    GLenum matrix_mode;
    float modelview[16];
    float projection[16];
    float mv_stack[GL_MAX_STACK][16];
    float pr_stack[GL_MAX_STACK][16];
    int mv_sp, pr_sp;

    int tex_enabled;
    int blend_enabled;
    int depth_test_enabled;
    GLboolean depth_mask;
    GLenum blend_sfactor, blend_dfactor;
    GLenum depth_func;

    gl_texture_t textures[GL_MAX_TEXTURES];
    GLuint bound_texture; /* 0 = none, else 1-based index */

    GLenum prim_mode;
    gl_vertex_t cur; /* current color/texcoord, applied to next glVertex */
    gl_vertex_t verts[GL_MAX_VERTS];
    int nverts;
} gl_ctx_t;

static gl_ctx_t G;

void gl_elseaos_init(uint32_t* canvas, int width, int height) {
    memset(&G, 0, sizeof(G));
    G.canvas = canvas;
    G.cw = width;
    G.ch = height;
    G.depth = (float*)kmalloc((size_t)width * height * sizeof(float));
    G.vx = G.vy = 0; G.vw = width; G.vh = height;
    G.clear_a = 1.0f;
    mat4_identity(G.modelview);
    mat4_identity(G.projection);
    G.matrix_mode = GL_MODELVIEW;
    G.depth_func = GL_LESS;
    G.depth_mask = GL_TRUE;
    G.blend_sfactor = GL_SRC_ALPHA;
    G.blend_dfactor = GL_ONE_MINUS_SRC_ALPHA;
    G.cur.r = G.cur.g = G.cur.b = G.cur.a = 1.0f;
}

void gl_elseaos_set_canvas(uint32_t* canvas, int width, int height) {
    G.canvas = canvas;
    if (width != G.cw || height != G.ch) {
        if (G.depth) kfree(G.depth);
        G.depth = (float*)kmalloc((size_t)width * height * sizeof(float));
    }
    G.cw = width;
    G.ch = height;
}

/* ── State ──────────────────────────────────────────────────────────────── */

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    G.vx = x; G.vy = y; G.vw = width; G.vh = height;
}

void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    G.clear_r = r; G.clear_g = g; G.clear_b = b; G.clear_a = a;
}

void glClear(GLbitfield mask) {
    if (mask & GL_COLOR_BUFFER_BIT) {
        uint32_t c = 0xFF000000u
            | ((uint32_t)(G.clear_r * 255.0f) << 16)
            | ((uint32_t)(G.clear_g * 255.0f) << 8)
            | (uint32_t)(G.clear_b * 255.0f);
        for (int i = 0; i < G.cw * G.ch; i++) G.canvas[i] = c;
    }
    if ((mask & GL_DEPTH_BUFFER_BIT) && G.depth) {
        for (int i = 0; i < G.cw * G.ch; i++) G.depth[i] = 1.0f;
    }
}

void glEnable(GLenum cap) {
    if (cap == GL_TEXTURE_2D) G.tex_enabled = 1;
    else if (cap == GL_BLEND) G.blend_enabled = 1;
    else if (cap == GL_DEPTH_TEST) G.depth_test_enabled = 1;
    /* GL_CULL_FACE etc: accepted, not implemented (no backface culling) */
}

void glDisable(GLenum cap) {
    if (cap == GL_TEXTURE_2D) G.tex_enabled = 0;
    else if (cap == GL_BLEND) G.blend_enabled = 0;
    else if (cap == GL_DEPTH_TEST) G.depth_test_enabled = 0;
}

void glBlendFunc(GLenum sfactor, GLenum dfactor) { G.blend_sfactor = sfactor; G.blend_dfactor = dfactor; }
void glDepthFunc(GLenum func) { G.depth_func = func; }
void glDepthMask(GLboolean flag) { G.depth_mask = flag; }
void glFlush(void) {}
void glFinish(void) {}

const GLubyte* glGetString(GLenum name) {
    (void)name;
    return (const GLubyte*)"ElseaOS software GL subset 1.0 (not conformant OpenGL/ES)";
}

/* ── Matrix stack ───────────────────────────────────────────────────────── */

void glMatrixMode(GLenum mode) { G.matrix_mode = mode; }

static float* gl__cur_matrix(void) { return (G.matrix_mode == GL_PROJECTION) ? G.projection : G.modelview; }

void glLoadIdentity(void) { mat4_identity(gl__cur_matrix()); }

void glPushMatrix(void) {
    if (G.matrix_mode == GL_PROJECTION) { if (G.pr_sp < GL_MAX_STACK) memcpy(G.pr_stack[G.pr_sp++], G.projection, sizeof(G.projection)); }
    else { if (G.mv_sp < GL_MAX_STACK) memcpy(G.mv_stack[G.mv_sp++], G.modelview, sizeof(G.modelview)); }
}

void glPopMatrix(void) {
    if (G.matrix_mode == GL_PROJECTION) { if (G.pr_sp > 0) memcpy(G.projection, G.pr_stack[--G.pr_sp], sizeof(G.projection)); }
    else { if (G.mv_sp > 0) memcpy(G.modelview, G.mv_stack[--G.mv_sp], sizeof(G.modelview)); }
}

void glLoadMatrixf(const GLfloat* m) { memcpy(gl__cur_matrix(), m, 16 * sizeof(float)); }

void glMultMatrixf(const GLfloat* m) {
    float* cur = gl__cur_matrix();
    float r[16];
    mat4_mul(r, cur, m);
    memcpy(cur, r, sizeof(r));
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    float t[16]; mat4_translate(t, x, y, z);
    glMultMatrixf(t);
}

void glScalef(GLfloat x, GLfloat y, GLfloat z) {
    float s[16]; mat4_scale(s, x, y, z);
    glMultMatrixf(s);
}

void glRotatef(GLfloat angle_deg, GLfloat x, GLfloat y, GLfloat z) {
    float rot[16]; mat4_rotate(rot, angle_deg, x, y, z);
    glMultMatrixf(rot);
}

void glOrthof(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) {
    float o[16]; mat4_ortho(o, l, r, b, t, n, f);
    glMultMatrixf(o);
}

void glFrustumf(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) {
    float o[16]; mat4_frustum(o, l, r, b, t, n, f);
    glMultMatrixf(o);
}

/* ── Textures ───────────────────────────────────────────────────────────── */

void glGenTextures(GLsizei n, GLuint* textures) {
    for (GLsizei i = 0; i < n; i++) {
        textures[i] = 0;
        for (int t = 0; t < GL_MAX_TEXTURES; t++) {
            if (!G.textures[t].in_use) {
                G.textures[t].in_use = 1;
                G.textures[t].w = G.textures[t].h = 0;
                G.textures[t].data = NULL;
                G.textures[t].filter = GL_NEAREST;
                textures[i] = (GLuint)(t + 1); /* 1-based */
                break;
            }
        }
    }
}

void glDeleteTextures(GLsizei n, const GLuint* textures) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = textures[i];
        if (id == 0 || id > GL_MAX_TEXTURES) continue;
        gl_texture_t* t = &G.textures[id - 1];
        if (t->data) { kfree(t->data); t->data = NULL; }
        t->in_use = 0;
        if (G.bound_texture == id) G.bound_texture = 0;
    }
}

void glBindTexture(GLenum target, GLuint texture) {
    (void)target;
    G.bound_texture = texture;
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const void* data) {
    (void)target; (void)level; (void)internalformat; (void)border; (void)format; (void)type;
    if (G.bound_texture == 0 || G.bound_texture > GL_MAX_TEXTURES) return;
    gl_texture_t* t = &G.textures[G.bound_texture - 1];
    if (t->data) kfree(t->data);
    size_t sz = (size_t)width * height * 4;
    t->data = (uint8_t*)kmalloc(sz);
    t->w = width; t->h = height;
    if (data) memcpy(t->data, data, sz);
    else memset(t->data, 0, sz);
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    (void)target;
    if (G.bound_texture == 0 || G.bound_texture > GL_MAX_TEXTURES) return;
    gl_texture_t* t = &G.textures[G.bound_texture - 1];
    if (pname == GL_TEXTURE_MIN_FILTER || pname == GL_TEXTURE_MAG_FILTER)
        t->filter = param;
}

/* ── Immediate mode ─────────────────────────────────────────────────────── */

void glBegin(GLenum mode) {
    G.prim_mode = mode;
    G.nverts = 0;
}

void glColor3f(GLfloat r, GLfloat g, GLfloat b) {
    G.cur.r = r; G.cur.g = g; G.cur.b = b; G.cur.a = 1.0f;
}

void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    G.cur.r = r; G.cur.g = g; G.cur.b = b; G.cur.a = a;
}

void glColor3ub(GLubyte r, GLubyte g, GLubyte b) {
    G.cur.r = r / 255.0f; G.cur.g = g / 255.0f; G.cur.b = b / 255.0f; G.cur.a = 1.0f;
}

void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) {
    G.cur.r = r / 255.0f; G.cur.g = g / 255.0f; G.cur.b = b / 255.0f; G.cur.a = a / 255.0f;
}

void glTexCoord2f(GLfloat u, GLfloat v) { G.cur.u = u; G.cur.v = v; }
void glNormal3f(GLfloat x, GLfloat y, GLfloat z) { (void)x; (void)y; (void)z; }

static void gl__push_vertex(float x, float y, float z) {
    if (G.nverts >= GL_MAX_VERTS) return;
    gl_vertex_t* vv = &G.verts[G.nverts++];
    *vv = G.cur;
    vv->x = x; vv->y = y; vv->z = z;
}

void glVertex2f(GLfloat x, GLfloat y) { gl__push_vertex(x, y, 0.0f); }
void glVertex3f(GLfloat x, GLfloat y, GLfloat z) { gl__push_vertex(x, y, z); }

/* ── Core rasterizer ────────────────────────────────────────────────────── */

/* Clamp float to [0,1] */
static float gl__clampf(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

/* Clamp int to [lo, hi] */
static int gl__clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Sample texture at (u,v) in [0,1]^2, returns RGBA as four floats */
static void gl__tex_sample(const gl_texture_t* t, float u, float v,
                            float* or_, float* og, float* ob, float* oa) {
    if (!t || !t->data || t->w == 0 || t->h == 0) {
        *or_ = *og = *ob = *oa = 1.0f;
        return;
    }
    /* wrap: repeat */
    u = u - floorf(u);
    v = v - floorf(v);

    if (t->filter == GL_LINEAR) {
        float px = u * t->w - 0.5f, py = v * t->h - 0.5f;
        int x0 = (int)floorf(px), y0 = (int)floorf(py);
        int x1 = x0 + 1, y1 = y0 + 1;
        float fx = px - x0, fy = py - y0;
        /* wrap sample coords */
        x0 = ((x0 % t->w) + t->w) % t->w;
        x1 = ((x1 % t->w) + t->w) % t->w;
        y0 = ((y0 % t->h) + t->h) % t->h;
        y1 = ((y1 % t->h) + t->h) % t->h;
        const uint8_t* p00 = t->data + (y0 * t->w + x0) * 4;
        const uint8_t* p10 = t->data + (y0 * t->w + x1) * 4;
        const uint8_t* p01 = t->data + (y1 * t->w + x0) * 4;
        const uint8_t* p11 = t->data + (y1 * t->w + x1) * 4;
#define BLERP4(c) ((p00[c]*(1-fx)*(1-fy) + p10[c]*fx*(1-fy) + p01[c]*(1-fx)*fy + p11[c]*fx*fy) / 255.0f)
        *or_ = BLERP4(0); *og = BLERP4(1); *ob = BLERP4(2); *oa = BLERP4(3);
#undef BLERP4
    } else {
        int ix = (int)(u * t->w) % t->w;
        int iy = (int)(v * t->h) % t->h;
        const uint8_t* p = t->data + (iy * t->w + ix) * 4;
        *or_ = p[0] / 255.0f; *og = p[1] / 255.0f; *ob = p[2] / 255.0f; *oa = p[3] / 255.0f;
    }
}

/* Depth comparison using G.depth_func */
static int gl__depth_pass(float stored, float incoming) {
    switch (G.depth_func) {
        case GL_ALWAYS: return 1;
        case GL_LESS:   return incoming < stored;
        default:        return incoming < stored;
    }
}

/* Write one pixel with current blend state into canvas */
static void gl__write_pixel(int px, int py, float fr, float fg, float fb, float fa, float depth) {
    if (px < G.vx || px >= G.vx + G.vw) return;
    if (py < G.vy || py >= G.vy + G.vh) return;
    int idx = py * G.cw + px;
    if (G.depth_test_enabled && G.depth) {
        if (!gl__depth_pass(G.depth[idx], depth)) return;
        if (G.depth_mask) G.depth[idx] = depth;
    }
    fa = gl__clampf(fa);
    if (G.blend_enabled && G.blend_sfactor == GL_SRC_ALPHA && G.blend_dfactor == GL_ONE_MINUS_SRC_ALPHA) {
        /* Standard SRC_OVER alpha blend */
        uint32_t dst = G.canvas[idx];
        float dr = ((dst >> 16) & 0xFF) / 255.0f;
        float dg = ((dst >> 8)  & 0xFF) / 255.0f;
        float db = ( dst        & 0xFF) / 255.0f;
        fr = fr * fa + dr * (1.0f - fa);
        fg = fg * fa + dg * (1.0f - fa);
        fb = fb * fa + db * (1.0f - fa);
        fa = 1.0f;
    }
    uint8_t r8 = (uint8_t)(gl__clampf(fr) * 255.0f);
    uint8_t g8 = (uint8_t)(gl__clampf(fg) * 255.0f);
    uint8_t b8 = (uint8_t)(gl__clampf(fb) * 255.0f);
    uint8_t a8 = (uint8_t)(gl__clampf(fa) * 255.0f);
    G.canvas[idx] = ((uint32_t)a8 << 24) | ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | b8;
}

/* Transform a raw vertex through modelview * projection, perspective divide, viewport.
   Returns screen-space (sx, sy, depth in [-1,1]). Returns 0 if behind near plane. */
static int gl__project(const gl_vertex_t* v, float* sx, float* sy, float* sz) {
    float clip[4];
    float eye[4];
    /* eye = modelview * (x,y,z,1) */
    mat4_transform4(G.modelview, v->x, v->y, v->z, 1.0f, eye);
    /* clip = projection * eye */
    mat4_transform4(G.projection, eye[0], eye[1], eye[2], eye[3], clip);
    if (clip[3] == 0.0f) return 0;
    /* perspective divide */
    float ndc_x = clip[0] / clip[3];
    float ndc_y = clip[1] / clip[3];
    float ndc_z = clip[2] / clip[3];
    /* viewport transform: ndc [-1,1] -> screen pixels */
    *sx = (ndc_x + 1.0f) * 0.5f * G.vw + G.vx;
    *sy = (1.0f - ndc_y) * 0.5f * G.vh + G.vy; /* flip Y: NDC +Y up, screen +Y down */
    *sz = ndc_z; /* depth in [-1,1], store as-is for depth test */
    return 1;
}

/* Rasterize one triangle defined by three projected screen-space vertices.
   Interpolates (r,g,b,a,u,v,depth) barycentrically. */
static void gl__raster_triangle(
    float ax, float ay, float az, float ar, float ag, float ab, float aa, float au, float av,
    float bx, float by, float bz, float br, float bg, float bb, float ba, float bu, float bv,
    float cx, float cy, float cz, float cr, float cg, float cb, float ca, float cu, float cv)
{
    /* Bounding box */
    int minx = gl__clampi((int)floorf(fminf(ax, fminf(bx, cx))), G.vx, G.vx + G.vw - 1);
    int maxx = gl__clampi((int)ceilf( fmaxf(ax, fmaxf(bx, cx))), G.vx, G.vx + G.vw - 1);
    int miny = gl__clampi((int)floorf(fminf(ay, fminf(by, cy))), G.vy, G.vy + G.vh - 1);
    int maxy = gl__clampi((int)ceilf( fmaxf(ay, fmaxf(by, cy))), G.vy, G.vy + G.vh - 1);

    float denom = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    if (fabsf(denom) < 0.5f) return; /* degenerate */

    const gl_texture_t* tex = NULL;
    if (G.tex_enabled && G.bound_texture > 0 && G.bound_texture <= GL_MAX_TEXTURES)
        tex = &G.textures[G.bound_texture - 1];

    for (int py = miny; py <= maxy; py++) {
        for (int px = minx; px <= maxx; px++) {
            float fx = px + 0.5f, fy = py + 0.5f;
            /* Barycentric weights */
            float wa = ((by - cy) * (fx - cx) + (cx - bx) * (fy - cy)) / denom;
            float wb = ((cy - ay) * (fx - cx) + (ax - cx) * (fy - cy)) / denom;
            float wc = 1.0f - wa - wb;
            if (wa < 0.0f || wb < 0.0f || wc < 0.0f) continue;

            float depth = wa * az + wb * bz + wc * cz;
            float fr = wa * ar + wb * br + wc * cr;
            float fg = wa * ag + wb * bg + wc * cg;
            float fb = wa * ab + wb * bb + wc * cb;
            float fa = wa * aa + wb * ba + wc * ca;
            float u  = wa * au + wb * bu + wc * cu;
            float v  = wa * av + wb * bv + wc * cv;

            if (tex && tex->data) {
                float tr, tg, tb, ta;
                gl__tex_sample(tex, u, v, &tr, &tg, &tb, &ta);
                /* GL_MODULATE: vertex colour × texel */
                fr *= tr; fg *= tg; fb *= tb; fa *= ta;
            }

            gl__write_pixel(px, py, fr, fg, fb, fa, depth);
        }
    }
}

/* Project one gl_vertex_t → screen, fill out sx/sy/sz, return 0 if clipped */
static int gl__proj_v(const gl_vertex_t* v, float* sx, float* sy, float* sz) {
    return gl__project(v, sx, sy, sz);
}

/* Emit a screen-space triangle (attribute pass-through) */
static void gl__emit(const gl_vertex_t* a, float ax, float ay, float az,
                     const gl_vertex_t* b, float bx, float by, float bz,
                     const gl_vertex_t* c, float cx, float cy, float cz) {
    gl__raster_triangle(
        ax, ay, az, a->r, a->g, a->b, a->a, a->u, a->v,
        bx, by, bz, b->r, b->g, b->b, b->a, b->u, b->v,
        cx, cy, cz, c->r, c->g, c->b, c->a, c->u, c->v);
}

/* Rasterize a thick line segment (as two triangles) */
static void gl__raster_line(
    float ax, float ay, float az, float ar, float ag, float ab, float aa,
    float bx, float by, float bz, float br, float bg, float bb, float ba)
{
    float dx = bx - ax, dy = by - ay;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 0.5f) return;
    float nx = -dy / len * 0.5f, ny = dx / len * 0.5f; /* half-pixel-wide perpendicular */

    /* 4 corners of the line quad */
    float qax = ax + nx, qay = ay + ny;
    float qbx = ax - nx, qby = ay - ny;
    float qcx = bx + nx, qcy = by + ny;
    float qdx = bx - nx, qdy = by - ny;

    gl__raster_triangle(
        qax, qay, az, ar, ag, ab, aa, 0, 0,
        qbx, qby, az, ar, ag, ab, aa, 0, 0,
        qcx, qcy, bz, br, bg, bb, ba, 0, 0);
    gl__raster_triangle(
        qbx, qby, az, ar, ag, ab, aa, 0, 0,
        qdx, qdy, bz, br, bg, bb, ba, 0, 0,
        qcx, qcy, bz, br, bg, bb, ba, 0, 0);
}

void glEnd(void) {
    int n = G.nverts;
    if (n == 0) return;

    /* Pre-project all vertices */
    float* sx = (float*)kmalloc(n * sizeof(float));
    float* sy = (float*)kmalloc(n * sizeof(float));
    float* sz = (float*)kmalloc(n * sizeof(float));
    int* ok   = (int*)  kmalloc(n * sizeof(int));
    for (int i = 0; i < n; i++)
        ok[i] = gl__proj_v(&G.verts[i], &sx[i], &sy[i], &sz[i]);

    switch (G.prim_mode) {
        case GL_TRIANGLES:
            for (int i = 0; i + 2 < n; i += 3) {
                if (!ok[i] || !ok[i+1] || !ok[i+2]) continue;
                gl__emit(&G.verts[i],   sx[i],   sy[i],   sz[i],
                         &G.verts[i+1], sx[i+1], sy[i+1], sz[i+1],
                         &G.verts[i+2], sx[i+2], sy[i+2], sz[i+2]);
            }
            break;

        case GL_TRIANGLE_STRIP:
            for (int i = 0; i + 2 < n; i++) {
                if (!ok[i] || !ok[i+1] || !ok[i+2]) continue;
                /* Flip winding on odd triangles to keep consistent front face */
                if (i & 1)
                    gl__emit(&G.verts[i+1], sx[i+1], sy[i+1], sz[i+1],
                             &G.verts[i],   sx[i],   sy[i],   sz[i],
                             &G.verts[i+2], sx[i+2], sy[i+2], sz[i+2]);
                else
                    gl__emit(&G.verts[i],   sx[i],   sy[i],   sz[i],
                             &G.verts[i+1], sx[i+1], sy[i+1], sz[i+1],
                             &G.verts[i+2], sx[i+2], sy[i+2], sz[i+2]);
            }
            break;

        case GL_TRIANGLE_FAN:
            for (int i = 1; i + 1 < n; i++) {
                if (!ok[0] || !ok[i] || !ok[i+1]) continue;
                gl__emit(&G.verts[0], sx[0], sy[0], sz[0],
                         &G.verts[i], sx[i], sy[i], sz[i],
                         &G.verts[i+1], sx[i+1], sy[i+1], sz[i+1]);
            }
            break;

        case GL_QUADS:
            /* Each group of 4 vertices → 2 triangles (0-1-2, 0-2-3) */
            for (int i = 0; i + 3 < n; i += 4) {
                if (!ok[i] || !ok[i+1] || !ok[i+2] || !ok[i+3]) continue;
                gl__emit(&G.verts[i],   sx[i],   sy[i],   sz[i],
                         &G.verts[i+1], sx[i+1], sy[i+1], sz[i+1],
                         &G.verts[i+2], sx[i+2], sy[i+2], sz[i+2]);
                gl__emit(&G.verts[i],   sx[i],   sy[i],   sz[i],
                         &G.verts[i+2], sx[i+2], sy[i+2], sz[i+2],
                         &G.verts[i+3], sx[i+3], sy[i+3], sz[i+3]);
            }
            break;

        case GL_LINES:
            for (int i = 0; i + 1 < n; i += 2) {
                if (!ok[i] || !ok[i+1]) continue;
                const gl_vertex_t* a = &G.verts[i];
                const gl_vertex_t* b = &G.verts[i+1];
                gl__raster_line(sx[i], sy[i], sz[i], a->r, a->g, a->b, a->a,
                                sx[i+1], sy[i+1], sz[i+1], b->r, b->g, b->b, b->a);
            }
            break;

        case GL_LINE_STRIP:
            for (int i = 0; i + 1 < n; i++) {
                if (!ok[i] || !ok[i+1]) continue;
                const gl_vertex_t* a = &G.verts[i];
                const gl_vertex_t* b = &G.verts[i+1];
                gl__raster_line(sx[i], sy[i], sz[i], a->r, a->g, a->b, a->a,
                                sx[i+1], sy[i+1], sz[i+1], b->r, b->g, b->b, b->a);
            }
            break;

        default:
            break;
    }

    kfree(sx); kfree(sy); kfree(sz); kfree(ok);
    G.nverts = 0;
}
