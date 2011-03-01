/*
Copyright (C) The Weather Channel, Inc.  2002.  All Rights Reserved.

The Weather Channel (TM) funded Tungsten Graphics to develop the
initial release of the Radeon 8500 driver under the XFree86 license.
This notice must be preserved.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice (including the
next paragraph) shall be included in all copies or substantial
portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/

/**
 * \file
 *
 * \author Keith Whitwell <keith@tungstengraphics.com>
 *
 * \todo Enable R300 texture tiling code?
 */

#include "main/glheader.h"
#include "main/imports.h"
#include "main/context.h"
#include "main/macros.h"
#include "main/teximage.h"
#include "main/texobj.h"
#include "main/enums.h"
#include "main/simple_list.h"

#include "r300_context.h"
#include "r300_state.h"
#include "r300_ioctl.h"
#include "radeon_mipmap_tree.h"
#include "r300_tex.h"
#include "r300_reg.h"

#define VALID_FORMAT(f) ( ((f) <= MESA_FORMAT_RGBA_DXT5			\
			   || ((f) >= MESA_FORMAT_RGBA_FLOAT32 &&	\
			       (f) <= MESA_FORMAT_INTENSITY_FLOAT16))	\
			  && tx_table[f].flag )

#define _ASSIGN(entry, format)				\
	[ MESA_FORMAT_ ## entry ] = { format, 0, 1}

/*
 * Note that the _REV formats are the same as the non-REV formats.  This is
 * because the REV and non-REV formats are identical as a byte string, but
 * differ when accessed as 16-bit or 32-bit words depending on the endianness of
 * the host.  Since the textures are transferred to the R300 as a byte string
 * (i.e. without any byte-swapping), the R300 sees the REV and non-REV formats
 * identically.  -- paulus
 */

static const struct tx_table {
	GLuint format, filter, flag;
} tx_table[] = {
	/* *INDENT-OFF* */
#ifdef MESA_LITTLE_ENDIAN
	_ASSIGN(RGBA8888, R300_EASY_TX_FORMAT(Y, Z, W, X, W8Z8Y8X8)),
	_ASSIGN(RGBA8888_REV, R300_EASY_TX_FORMAT(Z, Y, X, W, W8Z8Y8X8)),
	_ASSIGN(ARGB8888, R300_EASY_TX_FORMAT(X, Y, Z, W, W8Z8Y8X8)),
	_ASSIGN(ARGB8888_REV, R300_EASY_TX_FORMAT(W, Z, Y, X, W8Z8Y8X8)),
#else
	_ASSIGN(RGBA8888, R300_EASY_TX_FORMAT(Z, Y, X, W, W8Z8Y8X8)),
	_ASSIGN(RGBA8888_REV, R300_EASY_TX_FORMAT(Y, Z, W, X, W8Z8Y8X8)),
	_ASSIGN(ARGB8888, R300_EASY_TX_FORMAT(W, Z, Y, X, W8Z8Y8X8)),
	_ASSIGN(ARGB8888_REV, R300_EASY_TX_FORMAT(X, Y, Z, W, W8Z8Y8X8)),
#endif
	_ASSIGN(XRGB8888, R300_EASY_TX_FORMAT(X, Y, Z, ONE, W8Z8Y8X8)),
	_ASSIGN(RGB888, R300_EASY_TX_FORMAT(X, Y, Z, ONE, W8Z8Y8X8)),
	_ASSIGN(RGB565, R300_EASY_TX_FORMAT(X, Y, Z, ONE, Z5Y6X5)),
	_ASSIGN(RGB565_REV, R300_EASY_TX_FORMAT(X, Y, Z, ONE, Z5Y6X5)),
	_ASSIGN(ARGB4444, R300_EASY_TX_FORMAT(X, Y, Z, W, W4Z4Y4X4)),
	_ASSIGN(ARGB4444_REV, R300_EASY_TX_FORMAT(X, Y, Z, W, W4Z4Y4X4)),
	_ASSIGN(ARGB1555, R300_EASY_TX_FORMAT(X, Y, Z, W, W1Z5Y5X5)),
	_ASSIGN(ARGB1555_REV, R300_EASY_TX_FORMAT(X, Y, Z, W, W1Z5Y5X5)),
	_ASSIGN(AL88, R300_EASY_TX_FORMAT(X, X, X, Y, Y8X8)),
	_ASSIGN(AL88_REV, R300_EASY_TX_FORMAT(X, X, X, Y, Y8X8)),
	_ASSIGN(RGB332, R300_EASY_TX_FORMAT(X, Y, Z, ONE, Z3Y3X2)),
	_ASSIGN(A8, R300_EASY_TX_FORMAT(ZERO, ZERO, ZERO, X, X8)),
	_ASSIGN(L8, R300_EASY_TX_FORMAT(X, X, X, ONE, X8)),
	_ASSIGN(I8, R300_EASY_TX_FORMAT(X, X, X, X, X8)),
	_ASSIGN(CI8, R300_EASY_TX_FORMAT(X, X, X, X, X8)),
	_ASSIGN(YCBCR, R300_EASY_TX_FORMAT(X, Y, Z, ONE, G8R8_G8B8) | R300_TX_FORMAT_YUV_MODE),
	_ASSIGN(YCBCR_REV, R300_EASY_TX_FORMAT(X, Y, Z, ONE, G8R8_G8B8) | R300_TX_FORMAT_YUV_MODE),
	_ASSIGN(RGB_DXT1, R300_EASY_TX_FORMAT(X, Y, Z, ONE, DXT1)),
	_ASSIGN(RGBA_DXT1, R300_EASY_TX_FORMAT(X, Y, Z, W, DXT1)),
	_ASSIGN(RGBA_DXT3, R300_EASY_TX_FORMAT(X, Y, Z, W, DXT3)),
	_ASSIGN(RGBA_DXT5, R300_EASY_TX_FORMAT(Y, Z, W, X, DXT5)),
	_ASSIGN(RGBA_FLOAT32, R300_EASY_TX_FORMAT(Z, Y, X, W, FL_R32G32B32A32)),
	_ASSIGN(RGBA_FLOAT16, R300_EASY_TX_FORMAT(Z, Y, X, W, FL_R16G16B16A16)),
	_ASSIGN(RGB_FLOAT32, 0xffffffff),
	_ASSIGN(RGB_FLOAT16, 0xffffffff),
	_ASSIGN(ALPHA_FLOAT32, R300_EASY_TX_FORMAT(ZERO, ZERO, ZERO, X, FL_I32)),
	_ASSIGN(ALPHA_FLOAT16, R300_EASY_TX_FORMAT(ZERO, ZERO, ZERO, X, FL_I16)),
	_ASSIGN(LUMINANCE_FLOAT32, R300_EASY_TX_FORMAT(X, X, X, ONE, FL_I32)),
	_ASSIGN(LUMINANCE_FLOAT16, R300_EASY_TX_FORMAT(X, X, X, ONE, FL_I16)),
	_ASSIGN(LUMINANCE_ALPHA_FLOAT32, R300_EASY_TX_FORMAT(X, X, X, Y, FL_I32A32)),
	_ASSIGN(LUMINANCE_ALPHA_FLOAT16, R300_EASY_TX_FORMAT(X, X, X, Y, FL_I16A16)),
	_ASSIGN(INTENSITY_FLOAT32, R300_EASY_TX_FORMAT(X, X, X, X, FL_I32)),
	_ASSIGN(INTENSITY_FLOAT16, R300_EASY_TX_FORMAT(X, X, X, X, FL_I16)),
	_ASSIGN(Z16, R300_EASY_TX_FORMAT(X, X, X, X, X16)),
	_ASSIGN(Z24_S8, R300_EASY_TX_FORMAT(X, X, X, X, X24_Y8)),
	_ASSIGN(S8_Z24, R300_EASY_TX_FORMAT(Y, Y, Y, Y, X24_Y8)),
	_ASSIGN(Z32, R300_EASY_TX_FORMAT(X, X, X, X, X32)),
	/* EXT_texture_sRGB */
	_ASSIGN(SRGBA8, R300_EASY_TX_FORMAT(Y, Z, W, X, W8Z8Y8X8) | R300_TX_FORMAT_GAMMA),
	_ASSIGN(SLA8, R300_EASY_TX_FORMAT(X, X, X, Y, Y8X8) | R300_TX_FORMAT_GAMMA),
	_ASSIGN(SL8, R300_EASY_TX_FORMAT(X, X, X, ONE, X8) | R300_TX_FORMAT_GAMMA),
	/* *INDENT-ON* */
};

#undef _ASSIGN

void r300SetDepthTexMode(struct gl_texture_object *tObj)
{
	static const GLuint formats[3][3] = {
		{
			R300_EASY_TX_FORMAT(X, X, X, ONE, X16),
			R300_EASY_TX_FORMAT(X, X, X, X, X16),
			R300_EASY_TX_FORMAT(ZERO, ZERO, ZERO, X, X16),
		},
		{
			R300_EASY_TX_FORMAT(Y, Y, Y, ONE, X24_Y8),
			R300_EASY_TX_FORMAT(Y, Y, Y, Y, X24_Y8),
			R300_EASY_TX_FORMAT(ZERO, ZERO, ZERO, Y, X24_Y8),
		},
		{
			R300_EASY_TX_FORMAT(X, X, X, ONE, X32),
			R300_EASY_TX_FORMAT(X, X, X, X, X32),
			R300_EASY_TX_FORMAT(ZERO, ZERO, ZERO, X, X32),
		},
	};
	const GLuint *format;
	radeonTexObjPtr t;

	if (!tObj)
		return;

	t = radeon_tex_obj(tObj);

	switch (tObj->Image[0][tObj->BaseLevel]->TexFormat) {
	case MESA_FORMAT_Z16:
		format = formats[0];
		break;
	case MESA_FORMAT_S8_Z24:
		format = formats[1];
		break;
	case MESA_FORMAT_Z32:
		format = formats[2];
		break;
	default:
		/* Error...which should have already been caught by higher
		 * levels of Mesa.
		 */
		ASSERT(0);
		return;
	}

	switch (tObj->DepthMode) {
	case GL_LUMINANCE:
		t->pp_txformat = format[0];
		break;
	case GL_INTENSITY:
		t->pp_txformat = format[1];
		break;
	case GL_ALPHA:
		t->pp_txformat = format[2];
		break;
	default:
		/* Error...which should have already been caught by higher
		 * levels of Mesa.
		 */
		ASSERT(0);
		return;
	}
}


/**
 * Compute the cached hardware register values for the given texture object.
 *
 * \param rmesa Context pointer
 * \param t the r300 texture object
 */
static void setup_hardware_state(r300ContextPtr rmesa, radeonTexObj *t)
{
	const struct gl_texture_image *firstImage;
	firstImage = t->base.Image[0][t->minLod];

	if (!t->image_override
	    && VALID_FORMAT(firstImage->TexFormat)) {
		if (firstImage->_BaseFormat == GL_DEPTH_COMPONENT) {
			r300SetDepthTexMode(&t->base);
		} else {
			t->pp_txformat = tx_table[firstImage->TexFormat].format;
		}

		t->pp_txfilter |= tx_table[firstImage->TexFormat].filter;
	} else if (!t->image_override) {
		_mesa_problem(NULL, "unexpected texture format in %s",
			      __FUNCTION__);
		return;
	}

	if (t->image_override && t->bo)
		return;

	t->pp_txsize = (((R300_TX_WIDTHMASK_MASK & ((firstImage->Width - 1) << R300_TX_WIDTHMASK_SHIFT)))
			| ((R300_TX_HEIGHTMASK_MASK & ((firstImage->Height - 1) << R300_TX_HEIGHTMASK_SHIFT)))
			| ((R300_TX_DEPTHMASK_MASK & ((firstImage->DepthLog2) << R300_TX_DEPTHMASK_SHIFT)))
			| ((R300_TX_MAX_MIP_LEVEL_MASK & ((t->maxLod - t->minLod) << R300_TX_MAX_MIP_LEVEL_SHIFT))));

	t->tile_bits = 0;

	if (t->base.Target == GL_TEXTURE_CUBE_MAP)
		t->pp_txformat |= R300_TX_FORMAT_CUBIC_MAP;
	if (t->base.Target == GL_TEXTURE_3D)
		t->pp_txformat |= R300_TX_FORMAT_3D;


	if (t->base.Target == GL_TEXTURE_RECTANGLE_NV) {
		unsigned int align = (64 / _mesa_get_format_bytes(firstImage->TexFormat)) - 1;
		t->pp_txsize |= R300_TX_SIZE_TXPITCH_EN;
		if (!t->image_override)
			t->pp_txpitch = ((firstImage->Width + align) & ~align) - 1;
	}

	if (rmesa->radeon.radeonScreen->chip_family >= CHIP_FAMILY_RV515) {
	    if (firstImage->Width > 2048)
		t->pp_txpitch |= R500_TXWIDTH_BIT11;
            else
		t->pp_txpitch &= ~R500_TXWIDTH_BIT11;
	    if (firstImage->Height > 2048)
		t->pp_txpitch |= R500_TXHEIGHT_BIT11;
            else
		t->pp_txpitch &= ~R500_TXHEIGHT_BIT11;
	}
}

/**
 * Ensure the given texture is ready for rendering.
 *
 * Mostly this means populating the texture object's mipmap tree.
 */
static GLboolean r300_validate_texture(GLcontext * ctx, struct gl_texture_object *texObj)
{
	r300ContextPtr rmesa = R300_CONTEXT(ctx);
	radeonTexObj *t = radeon_tex_obj(texObj);

	if (!radeon_validate_texture_miptree(ctx, texObj))
		return GL_FALSE;

	/* Configure the hardware registers (more precisely, the cached version
	 * of the hardware registers). */
	setup_hardware_state(rmesa, t);

	t->validated = GL_TRUE;
	return GL_TRUE;
}

/**
 * Ensure all enabled and complete textures are uploaded along with any buffers being used.
 */
GLboolean r300ValidateBuffers(GLcontext * ctx)
{
	r300ContextPtr rmesa = R300_CONTEXT(ctx);
	struct radeon_renderbuffer *rrb;
	int i;
	int ret;

	radeon_cs_space_reset_bos(rmesa->radeon.cmdbuf.cs);

	rrb = radeon_get_colorbuffer(&rmesa->radeon);
	/* color buffer */
	if (rrb && rrb->bo) {
		radeon_cs_space_add_persistent_bo(rmesa->radeon.cmdbuf.cs,
						  rrb->bo, 0,
						  RADEON_GEM_DOMAIN_VRAM);
	}

	/* depth buffer */
	rrb = radeon_get_depthbuffer(&rmesa->radeon);
	if (rrb && rrb->bo) {
		radeon_cs_space_add_persistent_bo(rmesa->radeon.cmdbuf.cs,
						  rrb->bo, 0,
						  RADEON_GEM_DOMAIN_VRAM);
	}
	
	for (i = 0; i < ctx->Const.MaxTextureImageUnits; ++i) {
		radeonTexObj *t;

		if (!ctx->Texture.Unit[i]._ReallyEnabled)
			continue;

		if (!r300_validate_texture(ctx, ctx->Texture.Unit[i]._Current)) {
			_mesa_warning(ctx,
				      "failed to validate texture for unit %d.\n",
				      i);
		}
		t = radeon_tex_obj(ctx->Texture.Unit[i]._Current);
		if (t->image_override && t->bo)
			radeon_cs_space_add_persistent_bo(rmesa->radeon.cmdbuf.cs,
							  t->bo,
							  RADEON_GEM_DOMAIN_GTT | RADEON_GEM_DOMAIN_VRAM, 0);
		else if (t->mt->bo)
			radeon_cs_space_add_persistent_bo(rmesa->radeon.cmdbuf.cs,
							  t->mt->bo,
							  RADEON_GEM_DOMAIN_GTT | RADEON_GEM_DOMAIN_VRAM, 0);
	}

	ret = radeon_cs_space_check_with_bo(rmesa->radeon.cmdbuf.cs, first_elem(&rmesa->radeon.dma.reserved)->bo, RADEON_GEM_DOMAIN_GTT, 0);
	if (ret)
		return GL_FALSE;
	return GL_TRUE;
}

void r300SetTexOffset(__DRIcontext * pDRICtx, GLint texname,
		      unsigned long long offset, GLint depth, GLuint pitch)
{
	r300ContextPtr rmesa = pDRICtx->driverPrivate;
	struct gl_texture_object *tObj =
	    _mesa_lookup_texture(rmesa->radeon.glCtx, texname);
	radeonTexObjPtr t = radeon_tex_obj(tObj);
	uint32_t pitch_val;

	if (!tObj)
		return;

	t->image_override = GL_TRUE;

	if (!offset)
		return;

	t->bo = NULL;
	t->override_offset = offset;
	t->pp_txpitch &= (1 << 13) -1;
	pitch_val = pitch;

	switch (depth) {
	case 32:
		t->pp_txformat = R300_EASY_TX_FORMAT(X, Y, Z, W, W8Z8Y8X8);
		t->pp_txfilter |= tx_table[2].filter;
		pitch_val /= 4;
		break;
	case 24:
	default:
		t->pp_txformat = R300_EASY_TX_FORMAT(X, Y, Z, ONE, W8Z8Y8X8);
		t->pp_txfilter |= tx_table[4].filter;
		pitch_val /= 4;
		break;
	case 16:
		t->pp_txformat = R300_EASY_TX_FORMAT(X, Y, Z, ONE, Z5Y6X5);
		t->pp_txfilter |= tx_table[5].filter;
		pitch_val /= 2;
		break;
	}
	pitch_val--;

	t->pp_txpitch |= pitch_val;
}

void r300SetTexBuffer2(__DRIcontext *pDRICtx, GLint target, GLint glx_texture_format, __DRIdrawable *dPriv)
{
	struct gl_texture_unit *texUnit;
	struct gl_texture_object *texObj;
	struct gl_texture_image *texImage;
	struct radeon_renderbuffer *rb;
	radeon_texture_image *rImage;
	radeonContextPtr radeon;
	r300ContextPtr rmesa;
	struct radeon_framebuffer *rfb;
	radeonTexObjPtr t;
	uint32_t pitch_val;
	uint32_t internalFormat, type, format;

	type = GL_BGRA;
	format = GL_UNSIGNED_BYTE;
	internalFormat = (glx_texture_format == GLX_TEXTURE_FORMAT_RGB_EXT ? 3 : 4);

	radeon = pDRICtx->driverPrivate;
	rmesa = pDRICtx->driverPrivate;

	rfb = dPriv->driverPrivate;
        texUnit = &radeon->glCtx->Texture.Unit[radeon->glCtx->Texture.CurrentUnit];
	texObj = _mesa_select_tex_object(radeon->glCtx, texUnit, target);
        texImage = _mesa_get_tex_image(radeon->glCtx, texObj, target, 0);

	rImage = get_radeon_texture_image(texImage);
	t = radeon_tex_obj(texObj);
        if (t == NULL) {
    	    return;
    	}

	radeon_update_renderbuffers(pDRICtx, dPriv);
	/* back & depth buffer are useless free them right away */
	rb = (void*)rfb->base.Attachment[BUFFER_DEPTH].Renderbuffer;
	if (rb && rb->bo) {
		radeon_bo_unref(rb->bo);
        rb->bo = NULL;
	}
	rb = (void*)rfb->base.Attachment[BUFFER_BACK_LEFT].Renderbuffer;
	if (rb && rb->bo) {
		radeon_bo_unref(rb->bo);
		rb->bo = NULL;
	}
	rb = rfb->color_rb[0];
	if (rb->bo == NULL) {
		/* Failed to BO for the buffer */
		return;
	}
	
	_mesa_lock_texture(radeon->glCtx, texObj);
	if (t->bo) {
		radeon_bo_unref(t->bo);
		t->bo = NULL;
	}
	if (rImage->bo) {
		radeon_bo_unref(rImage->bo);
		rImage->bo = NULL;
	}

	radeon_miptree_unreference(&t->mt);
	radeon_miptree_unreference(&rImage->mt);

	_mesa_init_teximage_fields(radeon->glCtx, target, texImage,
				   rb->base.Width, rb->base.Height, 1, 0, rb->cpp);
	texImage->RowStride = rb->pitch / rb->cpp;
	rImage->bo = rb->bo;
	radeon_bo_ref(rImage->bo);
	t->bo = rb->bo;
	radeon_bo_ref(t->bo);
	t->tile_bits = 0;
	t->image_override = GL_TRUE;
	t->override_offset = 0;
	t->pp_txpitch &= (1 << 13) -1;
	pitch_val = rb->pitch;
	switch (rb->cpp) {
	case 4:
		if (glx_texture_format == GLX_TEXTURE_FORMAT_RGB_EXT)
			t->pp_txformat = R300_EASY_TX_FORMAT(X, Y, Z, ONE, W8Z8Y8X8);
		else
			t->pp_txformat = R300_EASY_TX_FORMAT(X, Y, Z, W, W8Z8Y8X8);
		t->pp_txfilter |= tx_table[2].filter;
		pitch_val /= 4;
		break;
	case 3:
	default:
		t->pp_txformat = R300_EASY_TX_FORMAT(X, Y, Z, ONE, W8Z8Y8X8);
		t->pp_txfilter |= tx_table[4].filter;
		pitch_val /= 4;
		break;
	case 2:
		t->pp_txformat = R300_EASY_TX_FORMAT(X, Y, Z, ONE, Z5Y6X5);
		t->pp_txfilter |= tx_table[5].filter;
		pitch_val /= 2;
		break;
	}
	pitch_val--;
	t->pp_txsize = (((R300_TX_WIDTHMASK_MASK & ((rb->base.Width - 1) << R300_TX_WIDTHMASK_SHIFT)))
			| ((R300_TX_HEIGHTMASK_MASK & ((rb->base.Height - 1) << R300_TX_HEIGHTMASK_SHIFT))));
	t->pp_txsize |= R300_TX_SIZE_TXPITCH_EN;
	t->pp_txpitch |= pitch_val;

	if (rmesa->radeon.radeonScreen->chip_family >= CHIP_FAMILY_RV515) {
	    if (rb->base.Width > 2048)
		t->pp_txpitch |= R500_TXWIDTH_BIT11;
            else
		t->pp_txpitch &= ~R500_TXWIDTH_BIT11;
	    if (rb->base.Height > 2048)
		t->pp_txpitch |= R500_TXHEIGHT_BIT11;
            else
		t->pp_txpitch &= ~R500_TXHEIGHT_BIT11;
	}
	t->validated = GL_TRUE;
	_mesa_unlock_texture(radeon->glCtx, texObj);
	return;
}

void r300SetTexBuffer(__DRIcontext *pDRICtx, GLint target, __DRIdrawable *dPriv)
{
        r300SetTexBuffer2(pDRICtx, target, GLX_TEXTURE_FORMAT_RGBA_EXT, dPriv);
}
