/*
   (c) Copyright 2001-2010  The world wide DirectFB Open Source Community (directfb.org)
   (c) Copyright 2000-2004  Convergence (integrated media) GmbH

   All rights reserved.

   Written by Denis Oliver Kropp <dok@directfb.org>,
              Andreas Hundt <andi@fischlustig.de>,
              Sven Neumann <neo@directfb.org>,
              Ville Syrjälä <syrjala@sci.fi> and
              Claudio Ciccani <klan@users.sf.net>.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/


//#define DIRECT_ENABLE_DEBUG

#include <config.h>

#include <core/layers.h>
#include <core/screens.h>

#include <misc/conf.h>


#include "drmkms_system.h"

D_DEBUG_DOMAIN( DRMKMS_Layer, "DRMKMS/Layer", "DRM/KMS Layer" );
D_DEBUG_DOMAIN( DRMKMS_Mode, "DRMKMS/Mode", "DRM/KMS Mode" );

/**********************************************************************************************************************/



static DFBResult
drmkmsInitLayer( CoreLayer                  *layer,
                 void                       *driver_data,
                 void                       *layer_data,
                 DFBDisplayLayerDescription *description,
                 DFBDisplayLayerConfig      *config,
                 DFBColorAdjustment         *adjustment )
{
     DRMKMSData *drmkms = driver_data;

     D_DEBUG_AT( DRMKMS_Layer, "%s()\n", __FUNCTION__ );

     description->type             = DLTF_GRAPHICS;
     description->caps             = DLCAPS_SURFACE;
     description->surface_caps     = DSCAPS_NONE;
     description->surface_accessor = CSAID_LAYER0;

     direct_snputs( description->name, "DRMKMS Layer", DFB_DISPLAY_LAYER_DESC_NAME_LENGTH );


     config->flags       = DLCONF_WIDTH | DLCONF_HEIGHT | DLCONF_PIXELFORMAT | DLCONF_BUFFERMODE;
     config->width       = dfb_config->mode.width  ?: drmkms->mode.hdisplay;
     config->height      = dfb_config->mode.height ?: drmkms->mode.vdisplay;

     config->pixelformat = dfb_config->mode.format ?: DSPF_ARGB;
     config->buffermode  = DLBM_FRONTONLY;

     return DFB_OK;
}

static DFBResult
drmkmsTestRegion( CoreLayer                  *layer,
                  void                       *driver_data,
                  void                       *layer_data,
                  CoreLayerRegionConfig      *config,
                  CoreLayerRegionConfigFlags *ret_failed )
{
     if (ret_failed)
          *ret_failed = DLCONF_NONE;

     return DFB_OK;
}

static DFBResult
drmkmsSetRegion( CoreLayer                  *layer,
                 void                       *driver_data,
                 void                       *layer_data,
                 void                       *region_data,
                 CoreLayerRegionConfig      *config,
                 CoreLayerRegionConfigFlags  updated,
                 CoreSurface                *surface,
                 CorePalette                *palette,
                 CoreSurfaceBufferLock      *left_lock,
                 CoreSurfaceBufferLock      *right_lock )
{
     int               ret;
     DRMKMSData       *drmkms = driver_data;
     DRMKMSDataShared *shared = drmkms->shared;

     D_DEBUG_AT( DRMKMS_Layer, "%s()\n", __FUNCTION__ );


     if (updated & (CLRCF_WIDTH | CLRCF_HEIGHT | CLRCF_BUFFERMODE | CLRCF_SOURCE))
     {
          ret = drmModeSetCrtc( drmkms->fd, drmkms->encoder->crtc_id, (u32)(long)left_lock->handle, config->source.x, config->source.y,
                                &drmkms->connector->connector_id, 1, &drmkms->mode );
          if (ret) {
               D_PERROR( "DirectFB/DRMKMS: drmModeSetCrtc() failed! (%d)\n", ret );
               D_DEBUG_AT( DRMKMS_Mode, " crtc_id: %d connector_id %d, mode %dx%d\n", drmkms->encoder->crtc_id, drmkms->connector->connector_id, drmkms->mode.hdisplay, drmkms->mode.vdisplay );
               return DFB_FAILURE;
          }

          shared->primary_dimension  = surface->config.size;
          shared->primary_rect       = config->source;
          shared->primary_fb         = (u32)(long)left_lock->handle;
     }


     return DFB_OK;
}

static DFBResult
drmkmsFlipRegion( CoreLayer             *layer,
                  void                  *driver_data,
                  void                  *layer_data,
                  void                  *region_data,
                  CoreSurface           *surface,
                  DFBSurfaceFlipFlags    flags,
                  const DFBRegion       *left_update,
                  CoreSurfaceBufferLock *left_lock,
                  const DFBRegion       *right_update,
                  CoreSurfaceBufferLock *right_lock )
{
     int               ret;
     DRMKMSData       *drmkms = driver_data;
     DRMKMSDataShared *shared = drmkms->shared;
     DRMKMSPlaneData  *data   = layer_data;
     unsigned int      plane_mask = 1;
     unsigned int      buffer_index  = 0;

     D_DEBUG_AT( DRMKMS_Layer, "%s()\n", __FUNCTION__ );

     direct_mutex_lock( &drmkms->lock );

     if (data) {
          buffer_index = data->index+1;
          plane_mask = 1 << buffer_index;
     }

     while (drmkms->flip_pending & plane_mask) {
          D_DEBUG_AT( DRMKMS_Layer, "  -> waiting for pending flip (previous)\n" );

          direct_waitqueue_wait( &drmkms->wq_event, &drmkms->lock );
     }

     direct_mutex_unlock( &drmkms->lock );


     dfb_surface_ref( surface );
     drmkms->surface[buffer_index] = surface;
     drmkms->surfacebuffer_index[buffer_index] = left_lock->buffer->index;

     /* Task */
     direct_mutex_lock( &drmkms->task_lock );

     drmkms->pending_tasks[buffer_index] = left_lock->task;

     direct_mutex_unlock( &drmkms->task_lock );


     D_DEBUG_AT( DRMKMS_Layer, "  -> calling drmModePageFlip()\n" );

     ret = drmModePageFlip( drmkms->fd, drmkms->encoder->crtc_id, (u32)(long)left_lock->handle, DRM_MODE_PAGE_FLIP_EVENT, driver_data );
     if (ret) {
          D_PERROR( "DirectFB/DRMKMS: drmModePageFlip() failed!\n" );
          return DFB_FAILURE;
     }

     shared->primary_fb = (u32)(long)left_lock->handle;

     dfb_surface_flip( surface, false );


     direct_mutex_lock( &drmkms->lock );

     drmkms->flip_pending |= plane_mask;

     direct_waitqueue_broadcast( &drmkms->wq_flip );

     if ((flags & DSFLIP_WAITFORSYNC) == DSFLIP_WAITFORSYNC) {
          while (drmkms->flip_pending & plane_mask) {
               D_DEBUG_AT( DRMKMS_Layer, "  -> waiting for pending flip (WAITFORSYNC)\n" );

               direct_waitqueue_wait( &drmkms->wq_event, &drmkms->lock );
          }
     }

     direct_mutex_unlock( &drmkms->lock );

     return DFB_OK;
}


static int
drmkmsPlaneDataSize( void )
{
     return sizeof(DRMKMSPlaneData);
}

static DFBResult
drmkmsPlaneInitLayer( CoreLayer                  *layer,
                      void                       *driver_data,
                      void                       *layer_data,
                      DFBDisplayLayerDescription *description,
                      DFBDisplayLayerConfig      *config,
                      DFBColorAdjustment         *adjustment )
{
     DRMKMSData      *drmkms = driver_data;
     DRMKMSPlaneData *data   = layer_data;

     D_DEBUG_AT( DRMKMS_Layer, "%s()\n", __FUNCTION__ );

     data->index = drmkms->plane_index_count++;

     D_DEBUG_AT( DRMKMS_Layer, "  -> getting plane with index %d\n", data->index );

     data->plane = drmModeGetPlane(drmkms->fd, drmkms->plane_resources->planes[data->index]);

     D_DEBUG_AT( DRMKMS_Layer, "     ->  plane_id is %d\n", data->plane->plane_id );

     description->type             = DLTF_GRAPHICS;
     description->caps             = DLCAPS_SURFACE | DLCAPS_SCREEN_POSITION | DLCAPS_ALPHACHANNEL;
     description->surface_caps     = DSCAPS_NONE;
     description->surface_accessor = CSAID_LAYER0;

     snprintf( description->name, DFB_DISPLAY_LAYER_DESC_NAME_LENGTH, "DRMKMS Plane Layer %d", data->index );


     config->flags      = DLCONF_WIDTH | DLCONF_HEIGHT | DLCONF_PIXELFORMAT | DLCONF_BUFFERMODE;
     config->width      = dfb_config->mode.width  ?: drmkms->mode.hdisplay;
     config->height     = dfb_config->mode.height ?: drmkms->mode.vdisplay;

     config->pixelformat = dfb_config->mode.format ?: DSPF_ARGB;
     config->buffermode  = DLBM_FRONTONLY;


     return DFB_OK;
}

static DFBResult
drmkmsPlaneTestRegion( CoreLayer                  *layer,
                       void                       *driver_data,
                       void                       *layer_data,
                       CoreLayerRegionConfig      *config,
                       CoreLayerRegionConfigFlags *ret_failed )
{
     if (ret_failed)
          *ret_failed = DLCONF_NONE;

     return DFB_OK;
}

static DFBResult
drmkmsPlaneSetRegion( CoreLayer                  *layer,
                      void                       *driver_data,
                      void                       *layer_data,
                      void                       *region_data,
                      CoreLayerRegionConfig      *config,
                      CoreLayerRegionConfigFlags  updated,
                      CoreSurface                *surface,
                      CorePalette                *palette,
                      CoreSurfaceBufferLock      *left_lock,
                      CoreSurfaceBufferLock      *right_lock )
{
     int              ret;
     DRMKMSData      *drmkms = driver_data;
     DRMKMSPlaneData *data   = layer_data;

     D_DEBUG_AT( DRMKMS_Layer, "%s()\n", __FUNCTION__ );
     if (updated & (CLRCF_WIDTH | CLRCF_HEIGHT | CLRCF_BUFFERMODE | CLRCF_DEST | CLRCF_SOURCE))
     {
          if (data->enabled && drmkms->shared->reinit_planes)
               drmModeSetPlane(drmkms->fd, data->plane->plane_id, drmkms->encoder->crtc_id, 0,
                                     /* plane_flags */ 0 ,0 ,0 ,0 ,0 ,0 ,0 ,0 ,0);

          ret = drmModeSetPlane(drmkms->fd, data->plane->plane_id, drmkms->encoder->crtc_id, (u32)(long)left_lock->handle,
                                /* plane_flags */ 0, config->dest.x, config->dest.y, config->dest.w, config->dest.h,
                                config->source.x << 16, config->source.y <<16, config->source.w << 16, config->source.h << 16);

          if (ret) {
               D_WARN( "DirectFB/DRMKMS: drmModeSetPlane() failed! (%d)\n", ret );
               return DFB_FAILURE;
          }

     }

     data->enabled = true;

     return DFB_OK;
}

static DFBResult
drmkmsPlaneRemoveRegion( CoreLayer             *layer,
                          void                  *driver_data,
                          void                  *layer_data,
                          void                  *region_data )
{
     DFBResult        ret;
     DRMKMSData      *drmkms = driver_data;
     DRMKMSPlaneData *data   = layer_data;

     D_DEBUG_AT( DRMKMS_Layer, "%s()\n", __FUNCTION__ );

     if (data->enabled) {
          ret = drmModeSetPlane(drmkms->fd, data->plane->plane_id, drmkms->encoder->crtc_id, 0,
                                          /* plane_flags */ 0 ,0 ,0 ,0 ,0 ,0 ,0 ,0 ,0);

          if (ret) {
               D_PERROR( "DRMKMS/Layer/Remove: Failed setting plane configuration!\n" );
               return ret;
          }
     }

     data->enabled = false;

     return DFB_OK;
}


static const DisplayLayerFuncs _drmkmsLayerFuncs = {
     .InitLayer     = drmkmsInitLayer,
     .TestRegion    = drmkmsTestRegion,
     .SetRegion     = drmkmsSetRegion,
     .FlipRegion    = drmkmsFlipRegion
};

static const DisplayLayerFuncs _drmkmsPlaneLayerFuncs = {
     .LayerDataSize = drmkmsPlaneDataSize,
     .InitLayer     = drmkmsPlaneInitLayer,
     .TestRegion    = drmkmsPlaneTestRegion,
     .SetRegion     = drmkmsPlaneSetRegion,
     .RemoveRegion  = drmkmsPlaneRemoveRegion,
     .FlipRegion    = drmkmsFlipRegion
};

const DisplayLayerFuncs *drmkmsLayerFuncs = &_drmkmsLayerFuncs;
const DisplayLayerFuncs *drmkmsPlaneLayerFuncs = &_drmkmsPlaneLayerFuncs;
