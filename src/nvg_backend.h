#pragma once
#include <stdint.h>
#include "nanovg.h"

/*
 * Real (software) NanoVG render backend for this kernel. NanoVG's core
 * (nanovg.c) is renderer-agnostic -- it tessellates paths into vertex
 * lists and calls into an NVGparams vtable, which the official library
 * ships GL/GLES/Metal implementations of. This is a from-scratch CPU
 * implementation of that same vtable: a scanline polygon rasterizer with
 * nonzero-winding multi-contour fill, solid/gradient/image paint
 * evaluation, and scissor clipping, writing directly into a caller-owned
 * 32-bit ARGB (0xAARRGGBB) pixel buffer -- in the kernel, that buffer is
 * vesa_get_backbuffer().
 *
 * canvas/width/height are taken explicitly (rather than reading vesa.c
 * directly) so this backend -- and a test harness exercising it -- can run
 * unmodified on the host against a plain malloc'd buffer.
 */
NVGcontext* nvg_elseaos_create(uint32_t* canvas, int width, int height);
void nvg_elseaos_delete(NVGcontext* ctx);

/* Call after resizing/repointing the canvas (e.g. a new VESA mode). */
void nvg_elseaos_set_canvas(NVGcontext* ctx, uint32_t* canvas, int width, int height);
