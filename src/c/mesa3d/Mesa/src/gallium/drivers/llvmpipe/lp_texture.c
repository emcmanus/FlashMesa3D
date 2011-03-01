/**************************************************************************
 * 
 * Copyright 2006 Tungsten Graphics, Inc., Cedar Park, Texas.
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
 /*
  * Authors:
  *   Keith Whitwell <keith@tungstengraphics.com>
  *   Michel Dänzer <michel@tungstengraphics.com>
  */

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_inlines.h"
#include "pipe/internal/p_winsys_screen.h"
#include "util/u_math.h"
#include "util/u_memory.h"

#include "lp_context.h"
#include "lp_state.h"
#include "lp_texture.h"
#include "lp_tex_cache.h"
#include "lp_screen.h"
#include "lp_winsys.h"


/* Simple, maximally packed layout.
 */


/* Conventional allocation path for non-display textures:
 */
static boolean
llvmpipe_texture_layout(struct llvmpipe_screen *screen,
                        struct llvmpipe_texture * lpt)
{
   struct pipe_texture *pt = &lpt->base;
   unsigned level;
   unsigned width = pt->width[0];
   unsigned height = pt->height[0];
   unsigned depth = pt->depth[0];

   unsigned buffer_size = 0;

   pf_get_block(lpt->base.format, &lpt->base.block);

   for (level = 0; level <= pt->last_level; level++) {
      unsigned nblocksx, nblocksy;

      pt->width[level] = width;
      pt->height[level] = height;
      pt->depth[level] = depth;
      pt->nblocksx[level] = pf_get_nblocksx(&pt->block, width);  
      pt->nblocksy[level] = pf_get_nblocksy(&pt->block, height);

      /* Allocate storage for whole quads. This is particularly important
       * for depth surfaces, which are currently stored in a swizzled format. */
      nblocksx = pf_get_nblocksx(&pt->block, align(width, 2));
      nblocksy = pf_get_nblocksy(&pt->block, align(height, 2));

      lpt->stride[level] = align(nblocksx*pt->block.size, 16);

      lpt->level_offset[level] = buffer_size;

      buffer_size += (nblocksy *
                      ((pt->target == PIPE_TEXTURE_CUBE) ? 6 : depth) *
                      lpt->stride[level]);

      width  = minify(width);
      height = minify(height);
      depth = minify(depth);
   }

   lpt->data = align_malloc(buffer_size, 16);

   return lpt->data != NULL;
}

static boolean
llvmpipe_displaytarget_layout(struct llvmpipe_screen *screen,
                              struct llvmpipe_texture * lpt)
{
   struct llvmpipe_winsys *winsys = screen->winsys;

   pf_get_block(lpt->base.format, &lpt->base.block);
   lpt->base.nblocksx[0] = pf_get_nblocksx(&lpt->base.block, lpt->base.width[0]);  
   lpt->base.nblocksy[0] = pf_get_nblocksy(&lpt->base.block, lpt->base.height[0]);  

   lpt->dt = winsys->displaytarget_create(winsys,
                                          lpt->base.format,
                                          lpt->base.width[0],
                                          lpt->base.height[0],
                                          16,
                                          &lpt->stride[0] );

   return lpt->dt != NULL;
}





static struct pipe_texture *
llvmpipe_texture_create(struct pipe_screen *_screen,
                        const struct pipe_texture *templat)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(_screen);
   struct llvmpipe_texture *lpt = CALLOC_STRUCT(llvmpipe_texture);
   if (!lpt)
      return NULL;

   lpt->base = *templat;
   pipe_reference_init(&lpt->base.reference, 1);
   lpt->base.screen = &screen->base;

   /* XXX: The xlib state tracker is brain-dead and will request
    * PIPE_FORMAT_Z16_UNORM no matter how much we tell it we don't support it.
    */
   if(lpt->base.format == PIPE_FORMAT_Z16_UNORM)
      lpt->base.format = PIPE_FORMAT_Z32_UNORM;

   if (lpt->base.tex_usage & (PIPE_TEXTURE_USAGE_DISPLAY_TARGET |
                              PIPE_TEXTURE_USAGE_PRIMARY)) {
      if (!llvmpipe_displaytarget_layout(screen, lpt))
         goto fail;
   }
   else {
      if (!llvmpipe_texture_layout(screen, lpt))
         goto fail;
   }
    
   return &lpt->base;

 fail:
   FREE(lpt);
   return NULL;
}


static struct pipe_texture *
llvmpipe_texture_blanket(struct pipe_screen * screen,
                         const struct pipe_texture *base,
                         const unsigned *stride,
                         struct pipe_buffer *buffer)
{
   /* FIXME */
#if 0
   struct llvmpipe_texture *lpt;
   assert(screen);

   /* Only supports one type */
   if (base->target != PIPE_TEXTURE_2D ||
       base->last_level != 0 ||
       base->depth[0] != 1) {
      return NULL;
   }

   lpt = CALLOC_STRUCT(llvmpipe_texture);
   if (!lpt)
      return NULL;

   lpt->base = *base;
   pipe_reference_init(&lpt->base.reference, 1);
   lpt->base.screen = screen;
   lpt->base.nblocksx[0] = pf_get_nblocksx(&lpt->base.block, lpt->base.width[0]);  
   lpt->base.nblocksy[0] = pf_get_nblocksy(&lpt->base.block, lpt->base.height[0]);  
   lpt->stride[0] = stride[0];

   pipe_buffer_reference(&lpt->buffer, buffer);

   return &lpt->base;
#else
   return NULL;
#endif
}


static void
llvmpipe_texture_destroy(struct pipe_texture *pt)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(pt->screen);
   struct llvmpipe_texture *lpt = llvmpipe_texture(pt);

   if(lpt->dt) {
      struct llvmpipe_winsys *winsys = screen->winsys;
      winsys->displaytarget_destroy(winsys, lpt->dt);
   }
   else
      align_free(lpt->data);

   FREE(lpt);
}


static struct pipe_surface *
llvmpipe_get_tex_surface(struct pipe_screen *screen,
                         struct pipe_texture *pt,
                         unsigned face, unsigned level, unsigned zslice,
                         unsigned usage)
{
   struct llvmpipe_texture *lpt = llvmpipe_texture(pt);
   struct pipe_surface *ps;

   assert(level <= pt->last_level);

   ps = CALLOC_STRUCT(pipe_surface);
   if (ps) {
      pipe_reference_init(&ps->reference, 1);
      pipe_texture_reference(&ps->texture, pt);
      ps->format = pt->format;
      ps->width = pt->width[level];
      ps->height = pt->height[level];
      ps->offset = lpt->level_offset[level];
      ps->usage = usage;

      /* Because we are llvmpipe, anything that the state tracker
       * thought was going to be done with the GPU will actually get
       * done with the CPU.  Let's adjust the flags to take that into
       * account.
       */
      if (ps->usage & PIPE_BUFFER_USAGE_GPU_WRITE) {
         /* GPU_WRITE means "render" and that can involve reads (blending) */
         ps->usage |= PIPE_BUFFER_USAGE_CPU_WRITE | PIPE_BUFFER_USAGE_CPU_READ;
      }

      if (ps->usage & PIPE_BUFFER_USAGE_GPU_READ)
         ps->usage |= PIPE_BUFFER_USAGE_CPU_READ;

      if (ps->usage & (PIPE_BUFFER_USAGE_CPU_WRITE |
                       PIPE_BUFFER_USAGE_GPU_WRITE)) {
         /* Mark the surface as dirty.  The tile cache will look for this. */
         lpt->timestamp++;
         llvmpipe_screen(screen)->timestamp++;
      }

      ps->face = face;
      ps->level = level;
      ps->zslice = zslice;

      if (pt->target == PIPE_TEXTURE_CUBE) {
         ps->offset += face * pt->nblocksy[level] * lpt->stride[level];
      }
      else if (pt->target == PIPE_TEXTURE_3D) {
         ps->offset += zslice * pt->nblocksy[level] * lpt->stride[level];
      }
      else {
         assert(face == 0);
         assert(zslice == 0);
      }
   }
   return ps;
}


static void 
llvmpipe_tex_surface_destroy(struct pipe_surface *surf)
{
   /* Effectively do the texture_update work here - if texture images
    * needed post-processing to put them into hardware layout, this is
    * where it would happen.  For llvmpipe, nothing to do.
    */
   assert(surf->texture);
   pipe_texture_reference(&surf->texture, NULL);
   FREE(surf);
}


static struct pipe_transfer *
llvmpipe_get_tex_transfer(struct pipe_screen *screen,
                          struct pipe_texture *texture,
                          unsigned face, unsigned level, unsigned zslice,
                          enum pipe_transfer_usage usage,
                          unsigned x, unsigned y, unsigned w, unsigned h)
{
   struct llvmpipe_texture *lptex = llvmpipe_texture(texture);
   struct llvmpipe_transfer *lpt;

   assert(texture);
   assert(level <= texture->last_level);

   lpt = CALLOC_STRUCT(llvmpipe_transfer);
   if (lpt) {
      struct pipe_transfer *pt = &lpt->base;
      pipe_texture_reference(&pt->texture, texture);
      pt->format = texture->format;
      pt->block = texture->block;
      pt->x = x;
      pt->y = y;
      pt->width = w;
      pt->height = h;
      pt->nblocksx = texture->nblocksx[level];
      pt->nblocksy = texture->nblocksy[level];
      pt->stride = lptex->stride[level];
      pt->usage = usage;
      pt->face = face;
      pt->level = level;
      pt->zslice = zslice;

      lpt->offset = lptex->level_offset[level];

      if (texture->target == PIPE_TEXTURE_CUBE) {
         lpt->offset += face * pt->nblocksy * pt->stride;
      }
      else if (texture->target == PIPE_TEXTURE_3D) {
         lpt->offset += zslice * pt->nblocksy * pt->stride;
      }
      else {
         assert(face == 0);
         assert(zslice == 0);
      }
      return pt;
   }
   return NULL;
}


static void 
llvmpipe_tex_transfer_destroy(struct pipe_transfer *transfer)
{
   /* Effectively do the texture_update work here - if texture images
    * needed post-processing to put them into hardware layout, this is
    * where it would happen.  For llvmpipe, nothing to do.
    */
   assert (transfer->texture);
   pipe_texture_reference(&transfer->texture, NULL);
   FREE(transfer);
}


static void *
llvmpipe_transfer_map( struct pipe_screen *_screen,
                       struct pipe_transfer *transfer )
{
   struct llvmpipe_screen *screen = llvmpipe_screen(_screen);
   ubyte *map, *xfer_map;
   struct llvmpipe_texture *lpt;

   assert(transfer->texture);
   lpt = llvmpipe_texture(transfer->texture);

   if(lpt->dt) {
      struct llvmpipe_winsys *winsys = screen->winsys;

      map = winsys->displaytarget_map(winsys, lpt->dt,
                                      pipe_transfer_buffer_flags(transfer));
      if (map == NULL)
         return NULL;
   }
   else
      map = lpt->data;

   /* May want to different things here depending on read/write nature
    * of the map:
    */
   if (transfer->texture && (transfer->usage & PIPE_TRANSFER_WRITE))
   {
      /* Do something to notify sharing contexts of a texture change.
       * In llvmpipe, that would mean flushing the texture cache.
       */
      screen->timestamp++;
   }
   
   xfer_map = map + llvmpipe_transfer(transfer)->offset +
      transfer->y / transfer->block.height * transfer->stride +
      transfer->x / transfer->block.width * transfer->block.size;
   /*printf("map = %p  xfer map = %p\n", map, xfer_map);*/
   return xfer_map;
}


static void
llvmpipe_transfer_unmap(struct pipe_screen *_screen,
                       struct pipe_transfer *transfer)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(_screen);
   struct llvmpipe_texture *lpt;

   assert(transfer->texture);
   lpt = llvmpipe_texture(transfer->texture);

   if(lpt->dt) {
      struct llvmpipe_winsys *winsys = screen->winsys;
      winsys->displaytarget_unmap(winsys, lpt->dt);
   }
}


void
llvmpipe_init_texture_funcs(struct llvmpipe_context *lp)
{
}


void
llvmpipe_init_screen_texture_funcs(struct pipe_screen *screen)
{
   screen->texture_create = llvmpipe_texture_create;
   screen->texture_blanket = llvmpipe_texture_blanket;
   screen->texture_destroy = llvmpipe_texture_destroy;

   screen->get_tex_surface = llvmpipe_get_tex_surface;
   screen->tex_surface_destroy = llvmpipe_tex_surface_destroy;

   screen->get_tex_transfer = llvmpipe_get_tex_transfer;
   screen->tex_transfer_destroy = llvmpipe_tex_transfer_destroy;
   screen->transfer_map = llvmpipe_transfer_map;
   screen->transfer_unmap = llvmpipe_transfer_unmap;
}
