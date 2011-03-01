/**************************************************************************
 * 
 * Copyright 2007 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

/* Authors:  Keith Whitwell <keith@tungstengraphics.com>
 */

#include "lp_context.h"
#include "lp_state.h"
#include "lp_surface.h"
#include "lp_tile_cache.h"

#include "draw/draw_context.h"


/**
 * XXX this might get moved someday
 * Set the framebuffer surface info: color buffers, zbuffer, stencil buffer.
 * Here, we flush the old surfaces and update the tile cache to point to the new
 * surfaces.
 */
void
llvmpipe_set_framebuffer_state(struct pipe_context *pipe,
                               const struct pipe_framebuffer_state *fb)
{
   struct llvmpipe_context *lp = llvmpipe_context(pipe);
   uint i;

   for (i = 0; i < PIPE_MAX_COLOR_BUFS; i++) {
      /* check if changing cbuf */
      if (lp->framebuffer.cbufs[i] != fb->cbufs[i]) {
         /* flush old */
         lp_tile_cache_map_transfers(lp->cbuf_cache[i]);
         lp_flush_tile_cache(lp->cbuf_cache[i]);

         /* assign new */
         pipe_surface_reference(&lp->framebuffer.cbufs[i], fb->cbufs[i]);

         /* update cache */
         lp_tile_cache_set_surface(lp->cbuf_cache[i], fb->cbufs[i]);
      }
   }

   lp->framebuffer.nr_cbufs = fb->nr_cbufs;

   /* zbuf changing? */
   if (lp->framebuffer.zsbuf != fb->zsbuf) {

      if(lp->zsbuf_transfer) {
         struct pipe_screen *screen = pipe->screen;

         if(lp->zsbuf_map) {
            screen->transfer_unmap(screen, lp->zsbuf_transfer);
            lp->zsbuf_map = NULL;
         }

         screen->tex_transfer_destroy(lp->zsbuf_transfer);
         lp->zsbuf_transfer = NULL;
      }

      /* assign new */
      pipe_surface_reference(&lp->framebuffer.zsbuf, fb->zsbuf);

      /* Tell draw module how deep the Z/depth buffer is */
      if (lp->framebuffer.zsbuf) {
         int depth_bits;
         double mrd;
         depth_bits = pf_get_component_bits(lp->framebuffer.zsbuf->format,
                                            PIPE_FORMAT_COMP_Z);
         if (depth_bits > 16) {
            mrd = 0.0000001;
         }
         else {
            mrd = 0.00002;
         }
         draw_set_mrd(lp->draw, mrd);
      }
   }

   lp->framebuffer.width = fb->width;
   lp->framebuffer.height = fb->height;

   lp->dirty |= LP_NEW_FRAMEBUFFER;
}
