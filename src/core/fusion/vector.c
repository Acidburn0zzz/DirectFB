/*
   (c) Copyright 2000-2002  convergence integrated media GmbH.
   (c) Copyright 2002-2003  convergence GmbH.

   All rights reserved.

   Written by Denis Oliver Kropp <dok@directfb.org>,
              Andreas Hundt <andi@fischlustig.de> and
              Sven Neumann <sven@convergence.de>.

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

#include <core/coredefs.h>

#include <misc/memcpy.h>

#include <core/fusion/object.h>
#include <core/fusion/shmalloc.h>
#include <core/fusion/vector.h>


static inline bool ensure_capacity( FusionVector *vector )
{
     DFB_ASSERT( vector->capacity > 0 );

     if (!vector->elements) {
          vector->elements = SHMALLOC( vector->capacity * sizeof(void*) );
          if (!vector->elements)
               return false;
     }
     else if (vector->count == vector->capacity) {
          void *elements;
          int   capacity = vector->capacity << 1;

          elements = SHMALLOC( capacity * sizeof(void*) );
          if (!elements)
               return false;

          dfb_memcpy( elements, vector->elements,
                      vector->count * sizeof(void*) );

          vector->elements = elements;
          vector->capacity = capacity;
     }

     return true;
}

void
fusion_vector_init( FusionVector *vector, int capacity )
{
     DFB_ASSERT( vector != NULL );
     DFB_ASSERT( capacity > 0 );

     vector->magic    = FUSION_VECTOR_MAGIC;
     vector->elements = NULL;
     vector->count    = 0;
     vector->capacity = capacity;
}

void
fusion_vector_destroy( FusionVector *vector )
{
     DFB_ASSERT( vector != NULL );
     DFB_ASSERT( vector->magic == FUSION_VECTOR_MAGIC );
     DFB_ASSERT( vector->count == 0 || vector->elements != NULL );

     if (vector->elements) {
          SHFREE( vector->elements );
          vector->elements = NULL;
     }

     vector->magic = 0;
}

FusionResult
fusion_vector_add( FusionVector *vector,
                   void         *element )
{
     DFB_ASSERT( vector != NULL );
     DFB_ASSERT( vector->magic == FUSION_VECTOR_MAGIC );
     DFB_ASSERT( element != NULL );

     /* Make sure there's a free entry left. */
     if (!ensure_capacity( vector ))
          return FUSION_OUTOFSHAREDMEMORY;

     /* Add the element to the vector. */
     vector->elements[vector->count++] = element;

     return FUSION_SUCCESS;
}

FusionResult
fusion_vector_insert( FusionVector *vector,
                      void         *element,
                      int           index )
{
     DFB_ASSERT( vector != NULL );
     DFB_ASSERT( vector->magic == FUSION_VECTOR_MAGIC );
     DFB_ASSERT( element != NULL );
     DFB_ASSERT( index >= 0 );
     DFB_ASSERT( index <= vector->count );

     /* Make sure there's a free entry left. */
     if (!ensure_capacity( vector ))
          return FUSION_OUTOFSHAREDMEMORY;

     /* Move elements from insertion point one up. */
     memmove( &vector->elements[ index ],
              &vector->elements[ index + 1 ],
              (vector->count - index) * sizeof(void*) );

     /* Insert the element into the vector. */
     vector->elements[index] = element;

     /* Increase the element counter. */
     vector->count++;

     return FUSION_SUCCESS;
}

FusionResult
fusion_vector_remove( FusionVector *vector,
                      int           index )
{
     DFB_ASSERT( vector != NULL );
     DFB_ASSERT( vector->magic == FUSION_VECTOR_MAGIC );
     DFB_ASSERT( index >= 0 );
     DFB_ASSERT( index < vector->count );

     /* Move elements after this element one down. */
     memmove( &vector->elements[ index + 1 ],
              &vector->elements[ index ],
              (vector->count - index - 1) * sizeof(void*) );

     /* Decrease the element counter. */
     vector->count--;

     return FUSION_SUCCESS;
}

FusionResult
fusion_vector_remove_last( FusionVector *vector )
{
     DFB_ASSERT( vector != NULL );
     DFB_ASSERT( vector->magic == FUSION_VECTOR_MAGIC );
     DFB_ASSERT( vector->count > 0 );

     /* Decrease the element counter. */
     vector->count--;

     return FUSION_SUCCESS;
}

