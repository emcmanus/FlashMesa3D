/**********************************************************
 * Copyright 2009 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/


#include "svga_cmd.h"

#include "util/u_debug.h"
#include "util/u_memory.h"
#include "util/u_debug_stack.h"
#include "pipebuffer/pb_buffer.h"
#include "pipebuffer/pb_validate.h"

#include "svga_winsys.h"
#include "vmw_context.h"
#include "vmw_screen.h"
#include "vmw_buffer.h"
#include "vmw_surface.h"
#include "vmw_fence.h"

#define VMW_COMMAND_SIZE (64*1024)
#define VMW_SURFACE_RELOCS (1024)

#define VMW_MUST_FLUSH_STACK 8

struct vmw_svga_winsys_context
{
   struct svga_winsys_context base;

   struct vmw_winsys_screen *vws;

#ifdef DEBUG
   boolean must_flush;
   struct debug_stack_frame must_flush_stack[VMW_MUST_FLUSH_STACK];
#endif

   struct {
      uint8_t buffer[VMW_COMMAND_SIZE];
      uint32_t size;
      uint32_t used;
      uint32_t reserved;
   } command;

   struct {
      struct vmw_svga_winsys_surface *handles[VMW_SURFACE_RELOCS];
      uint32_t size;
      uint32_t used;
      uint32_t staged;
      uint32_t reserved;
   } surface;

   struct pb_validate *validate;

   uint32_t last_fence;
};


static INLINE struct vmw_svga_winsys_context *
vmw_svga_winsys_context(struct svga_winsys_context *swc)
{
   assert(swc);
   return (struct vmw_svga_winsys_context *)swc;
}


static enum pipe_error
vmw_swc_flush(struct svga_winsys_context *swc,
              struct pipe_fence_handle **pfence)
{
   struct vmw_svga_winsys_context *vswc = vmw_svga_winsys_context(swc);
   struct pipe_fence_handle *fence = NULL;
   unsigned i;
   enum pipe_error ret;

   ret = pb_validate_validate(vswc->validate);
   assert(ret == PIPE_OK);
   if(ret == PIPE_OK) {

      if (vswc->command.used)
         vmw_ioctl_command(vswc->vws,
                           vswc->command.buffer,
                           vswc->command.used,
                           &vswc->last_fence);

      fence = vmw_pipe_fence(vswc->last_fence);

      pb_validate_fence(vswc->validate, fence);
   }

   vswc->command.used = 0;
   vswc->command.reserved = 0;

   for(i = 0; i < vswc->surface.used + vswc->surface.staged; ++i) {
      struct vmw_svga_winsys_surface *vsurf =
	 vswc->surface.handles[i];
      p_atomic_dec(&vsurf->validated);
      vmw_svga_winsys_surface_reference(&vswc->surface.handles[i], NULL);
   }

   vswc->surface.used = 0;
   vswc->surface.reserved = 0;

#ifdef DEBUG
   vswc->must_flush = FALSE;
#endif

   if(pfence)
      *pfence = fence;

   return ret;
}


static void *
vmw_swc_reserve(struct svga_winsys_context *swc,
                uint32_t nr_bytes, uint32_t nr_relocs )
{
   struct vmw_svga_winsys_context *vswc = vmw_svga_winsys_context(swc);

#ifdef DEBUG
   /* Check if somebody forgot to check the previous failure */
   if(vswc->must_flush) {
      debug_printf("Forgot to flush:\n");
      debug_backtrace_dump(vswc->must_flush_stack, VMW_MUST_FLUSH_STACK);
      assert(!vswc->must_flush);
   }
#endif

   assert(nr_bytes <= vswc->command.size);
   if(nr_bytes > vswc->command.size)
      return NULL;

   if(vswc->command.used + nr_bytes > vswc->command.size ||
      vswc->surface.used + nr_relocs > vswc->surface.size) {
#ifdef DEBUG
      vswc->must_flush = TRUE;
      debug_backtrace_capture(vswc->must_flush_stack, 1,
                              VMW_MUST_FLUSH_STACK);
#endif
      return NULL;
   }

   assert(vswc->command.used + nr_bytes <= vswc->command.size);
   assert(vswc->surface.used + nr_relocs <= vswc->surface.size);

   vswc->command.reserved = nr_bytes;
   vswc->surface.reserved = nr_relocs;
   vswc->surface.staged = 0;

   return vswc->command.buffer + vswc->command.used;
}


static void
vmw_swc_surface_relocation(struct svga_winsys_context *swc,
                           uint32 *where,
                           struct svga_winsys_surface *surface,
                           unsigned flags)
{
   struct vmw_svga_winsys_context *vswc = vmw_svga_winsys_context(swc);
   struct vmw_svga_winsys_surface *vsurf;

   if(!surface) {
      *where = SVGA3D_INVALID_ID;
      return;
   }

   assert(vswc->surface.staged < vswc->surface.reserved);

   vsurf = vmw_svga_winsys_surface(surface);

   *where = vsurf->sid;

   vmw_svga_winsys_surface_reference(&vswc->surface.handles[vswc->surface.used + vswc->surface.staged], vsurf);
   p_atomic_inc(&vsurf->validated);
   ++vswc->surface.staged;
}


static void
vmw_swc_region_relocation(struct svga_winsys_context *swc,
                          struct SVGAGuestPtr *where,
                          struct svga_winsys_buffer *buffer,
                          uint32 offset,
                          unsigned flags)
{
   struct vmw_svga_winsys_context *vswc = vmw_svga_winsys_context(swc);
   struct SVGAGuestPtr ptr;
   struct pb_buffer *buf = vmw_pb_buffer(buffer);
   enum pipe_error ret;

   if(!vmw_gmr_bufmgr_region_ptr(buf, &ptr))
      assert(0);

   ptr.offset += offset;

   *where = ptr;

   ret = pb_validate_add_buffer(vswc->validate, buf, flags);
   /* TODO: Update pipebuffer to reserve buffers and not fail here */
   assert(ret == PIPE_OK);
}


static void
vmw_swc_commit(struct svga_winsys_context *swc)
{
   struct vmw_svga_winsys_context *vswc = vmw_svga_winsys_context(swc);

   assert(vswc->command.reserved);
   assert(vswc->command.used + vswc->command.reserved <= vswc->command.size);
   vswc->command.used += vswc->command.reserved;
   vswc->command.reserved = 0;

   assert(vswc->surface.staged <= vswc->surface.reserved);
   assert(vswc->surface.used + vswc->surface.staged <= vswc->surface.size);
   vswc->surface.used += vswc->surface.staged;
   vswc->surface.staged = 0;
   vswc->surface.reserved = 0;
}


static void
vmw_swc_destroy(struct svga_winsys_context *swc)
{
   struct vmw_svga_winsys_context *vswc = vmw_svga_winsys_context(swc);
   unsigned i;
   for(i = 0; i < vswc->surface.used; ++i) {
      p_atomic_dec(&vswc->surface.handles[i]->validated);
      vmw_svga_winsys_surface_reference(&vswc->surface.handles[i], NULL);
   }
   pb_validate_destroy(vswc->validate);
   vmw_ioctl_context_destroy(vswc->vws, swc->cid);
   FREE(vswc);
}


struct svga_winsys_context *
vmw_svga_winsys_context_create(struct svga_winsys_screen *sws)
{
   struct vmw_winsys_screen *vws = vmw_winsys_screen(sws);
   struct vmw_svga_winsys_context *vswc;

   vswc = CALLOC_STRUCT(vmw_svga_winsys_context);
   if(!vswc)
      return NULL;

   vswc->base.destroy = vmw_swc_destroy;
   vswc->base.reserve = vmw_swc_reserve;
   vswc->base.surface_relocation = vmw_swc_surface_relocation;
   vswc->base.region_relocation = vmw_swc_region_relocation;
   vswc->base.commit = vmw_swc_commit;
   vswc->base.flush = vmw_swc_flush;

   vswc->base.cid = vmw_ioctl_context_create(vws);

   vswc->vws = vws;

   vswc->command.size = VMW_COMMAND_SIZE;
   vswc->surface.size = VMW_SURFACE_RELOCS;

   vswc->validate = pb_validate_create();
   if(!vswc->validate) {
      FREE(vswc);
      return NULL;
   }

   return &vswc->base;
}


struct pipe_context *
vmw_svga_context_create(struct pipe_screen *screen)
{
   return svga_context_create(screen);
}
