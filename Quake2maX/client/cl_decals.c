/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
#include "client.h"

#define MAX_DECAL_VERTS			384
#define MAX_DECAL_FRAGMENTS		128

cdecal_t	cl_decals[MAX_DECALS];

/*
=================
CL_ClearDecals
=================
*/
void CL_ClearDecals (void)
{
	memset ( cl_decals, 0, sizeof(cl_decals) );
}

/*
=================
CL_AllocDecal
=================
*/
cdecal_t *CL_AllocDecal (void)
{
	int			i;
	int			time;
	cdecal_t	*free, *dl;

// find free or the oldest decal
	time = cl.time;
	for ( i = 0, dl = cl_decals, free = dl; i < MAX_DECALS; i++, dl++ ) {
		if ( dl->die > cl.time ) {
			if ( dl->start >= time ) {
				continue;
			}

			free = dl;
			time = dl->start;
			continue;
		}

		free = dl;
		goto done;
	}

done:
	memset ( free, 0, sizeof (*free) );

	return free;
}

/*
=================
CL_SpawnDecal
=================
*/
void CL_SpawnDecal ( vec3_t origin, vec3_t dir, float orient, float radius,
				 float r, float g, float b, float a, float die, float fadetime, qboolean fadealpha, struct shader_s *shader )
{
	int i, j;
	mat3_t axis;
	vec3_t verts[MAX_DECAL_VERTS];
	byte_vec4_t color;
	fragment_t *fr, fragments[MAX_DECAL_FRAGMENTS];
	int numfragments;
	cdecal_t *dl;

	// invalid decal
	if ( radius <= 0 || VectorCompare (dir, vec3_origin) ) {
		return;
	}

	// calculate orientation matrix
	VectorNormalize2 ( dir, axis[0] );
	PerpendicularVector ( axis[1], axis[0] );
	RotatePointAroundVector ( axis[2], axis[0], axis[1], orient );
	CrossProduct ( axis[0], axis[2], axis[1] );

	numfragments = R_GetClippedFragments ( origin, radius, axis, // clip it
		MAX_DECAL_VERTS, verts, MAX_DECAL_FRAGMENTS, fragments );

	// no valid fragments
	if ( !numfragments ) {
		return;
	}

	color[0] = FloatToByte ( r );
	color[1] = FloatToByte ( g );
	color[2] = FloatToByte ( b );
	color[3] = FloatToByte ( a );

	VectorScale ( axis[1], 0.5f / radius, axis[1] );
	VectorScale ( axis[2], 0.5f / radius, axis[2] );

	for ( i = 0, fr = fragments; i < numfragments; i++, fr++ ) {
		if ( fr->numverts > MAX_POLY_VERTS ) {
			fr->numverts = MAX_POLY_VERTS;
		} else if ( fr->numverts <= 0 ) {
			continue;
		}

		// allocate decal
		dl = CL_AllocDecal ();
		dl->start = cl.time;
		dl->die = cl.time + die * 1000;
		dl->fadetime = cl.time + (die - min(fadetime, die)) * 1000;
		dl->fadefreq = 0.001f / min(fadetime, die);
		dl->fadealpha = fadealpha;
		dl->shader = shader;
		dl->color[0] = r; 
		dl->color[1] = g;
		dl->color[2] = b;
		dl->color[3] = a;
		dl->poly.numverts = fr->numverts;
		dl->poly.colors = dl->colors;
		dl->poly.verts = dl->verts;
		dl->poly.stcoords = dl->stcoords;
		dl->poly.shader = dl->shader;

		for ( j = 0; j < fr->numverts; j++ ) {
			vec3_t v;
			
			VectorCopy ( verts[fr->firstvert+j], dl->verts[j] );
			VectorSubtract ( dl->verts[j], origin, v );
			dl->stcoords[j][0] = DotProduct ( v, axis[1] ) + 0.5f;
			dl->stcoords[j][1] = DotProduct ( v, axis[2] ) + 0.5f;
			*(int *)dl->colors[j] = *(int *)color;
		}
	}
}

/*
=================
CL_AddDecals
=================
*/
void CL_AddDecals (void)
{
	int			i, j;
	float		fade;
	cdecal_t	*dl;
	byte_vec4_t color;

	for ( i = 0, dl = cl_decals; i < MAX_DECALS; i++, dl++ ) {
		if ( dl->die <= cl.time ) {
			continue;
		}

		if ( dl->fadetime < cl.time ) {
			fade = (dl->die - cl.time) * dl->fadefreq;

			if ( dl->fadealpha ) {
				color[0] = FloatToByte ( dl->color[0] );
				color[1] = FloatToByte ( dl->color[1] );
				color[2] = FloatToByte ( dl->color[2] );
				color[3] = FloatToByte ( dl->color[3]*fade );
			} else {
				color[0] = FloatToByte ( dl->color[0]*fade );
				color[1] = FloatToByte ( dl->color[1]*fade );
				color[2] = FloatToByte ( dl->color[2]*fade );
				color[3] = FloatToByte ( dl->color[3] );
			}

			for ( j = 0; j < dl->poly.numverts; j++ ) {
				*(int *)dl->colors[j] = *(int *)color;
			}
		}

		V_AddPoly ( &dl->poly );
	}
}
