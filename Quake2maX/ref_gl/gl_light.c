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
// r_light.c

#include "gl_local.h"

int	r_dlightframecount;

void vectoangles (vec3_t value1, vec3_t angles);

#define	DLIGHT_CUTOFF	0

/*
=============================================================================

DYNAMIC LIGHTS BLEND RENDERING

=============================================================================
*/

#define DLIGHTRADUIS 32.0

void R_RenderDlight (dlight_t *light)
{
	int		i, j;
	float	a;
	vec3_t	v;
	float	rad;

	rad = light->intensity * 0.35;

	VectorSubtract (light->origin, r_origin, v);
#if 0
	// FIXME?
	if (VectorLength (v) < rad)
	{	// view is inside the dlight
		V_AddBlend (light->color[0], light->color[1], light->color[2], light->intensity * 0.0003, v_blend);
		return;
	}
#endif

	qglBegin (GL_TRIANGLE_FAN);
	qglColor3f (light->color[0]*0.2, light->color[1]*0.2, light->color[2]*0.2);
	for (i=0 ; i<3 ; i++)
		v[i] = light->origin[i] - vpn[i]*rad;
	qglVertex3fv (v);
	qglColor3f (0,0,0);
	for (i=DLIGHTRADUIS ; i>=0 ; i--)
	{
		a = i/DLIGHTRADUIS * M_PI*2;
		for (j=0 ; j<3 ; j++)
			v[j] = light->origin[j] + vright[j]*cos(a)*rad
				+ vup[j]*sin(a)*rad;
		qglVertex3fv (v);
	}
	qglEnd ();
}

/*
=============
R_RenderDlights
=============
*/
void R_RenderDlights (void)
{
	int		i;
	dlight_t	*l;

	if (!gl_flashblend->value)
		return;

	r_dlightframecount = r_framecount + 1;	// because the count hasn't
											//  advanced yet for this frame
	qglDepthMask (0);
	qglDisable (GL_TEXTURE_2D);
	GL_ShadeModel (GL_SMOOTH);
	GLSTATE_ENABLE_BLEND
	GL_BlendFunction (GL_SRC_ALPHA, GL_ONE);

	l = r_newrefdef.dlights;
	for (i=0 ; i<r_newrefdef.num_dlights ; i++, l++)
		R_RenderDlight (l);

	qglColor3f (1,1,1);
	GLSTATE_DISABLE_BLEND
	qglEnable (GL_TEXTURE_2D);
	GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	qglDepthMask (1);

}


/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
=============
R_MarkLights
=============
*/
extern cvar_t *r_dlights_normal;
void R_MarkLights (dlight_t *light, int bit, mnode_t *node)
{
	cplane_t	*splitplane;
	float		dist;
	msurface_t	*surf;
	int		i, sidebit;
	
	if (node->contents != -1)
		return;

	splitplane = node->plane;
	dist = DotProduct (light->origin, splitplane->normal) - splitplane->dist;

	if (dist > light->intensity-DLIGHT_CUTOFF) 
	{
		R_MarkLights (light, bit, node->children[0]);
		return;
	}
	if (dist < -light->intensity+DLIGHT_CUTOFF) 
	{
		R_MarkLights (light, bit, node->children[1]);
		return;
	}

// mark the polygons
	surf = r_worldmodel->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		//Discoloda - start
		if (r_dlights_normal->value)
		{
			dist = DotProduct (light->origin, surf->plane->normal) - surf->plane->dist;	
			if (dist >= 0)
				sidebit = 0;
			else
				sidebit = SURF_PLANEBACK;

			if ( (surf->flags & SURF_PLANEBACK) != sidebit )
				continue;
		}
		//Discoloda - end

		if (surf->dlightframe != r_dlightframecount)
		{
			surf->dlightbits = bit;
			surf->dlightframe = r_dlightframecount;
		} else
			surf->dlightbits |= bit;
	}

	R_MarkLights (light, bit, node->children[0]);
	R_MarkLights (light, bit, node->children[1]);
}



/*
=============
R_PushDlights
=============
*/
void R_PushDlights (void)
{
	int		i;
	dlight_t	*l;

	if (gl_flashblend->value)
		return;

	r_dlightframecount = r_framecount + 1;	// because the count hasn't
											//  advanced yet for this frame
	l = r_newrefdef.dlights;
	for (i=0 ; i<r_newrefdef.num_dlights ; i++, l++)
		R_MarkLights ( l, 1<<i, r_worldmodel->nodes );
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

vec3_t			pointcolor;
cplane_t		*lightplane;		// used as shadow plane
vec3_t			lightspot;

int RecursiveLightPoint (mnode_t *node, vec3_t start, vec3_t end)
{
	float		front, back, frac;
	int			side;
	cplane_t	*plane;
	vec3_t		mid;
	msurface_t	*surf;
	int			s, t, ds, dt;
	int			i;
	mtexinfo_t	*tex;
	byte		*lightmap;
	int			maps;
	int			r;

	if (node->contents != -1)
		return -1;		// didn't hit anything
	
// calculate mid point

// FIXME: optimize for axial
	plane = node->plane;
	front = DotProduct (start, plane->normal) - plane->dist;
	back = DotProduct (end, plane->normal) - plane->dist;
	side = front < 0;

	if ( (back < 0) == side)
		return RecursiveLightPoint (node->children[side], start, end);
	
	frac = front / (front-back);
	mid[0] = start[0] + (end[0] - start[0])*frac;
	mid[1] = start[1] + (end[1] - start[1])*frac;
	mid[2] = start[2] + (end[2] - start[2])*frac;
	
// go down front side	
	r = RecursiveLightPoint (node->children[side], start, mid);
	if (r >= 0)
		return r;		// hit something
		
	if ( (back < 0) == side )
		return -1;		// didn't hit anuthing
		
// check for impact on this node
	VectorCopy (mid, lightspot);
	lightplane = plane;

	surf = r_worldmodel->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		if (surf->flags&(SURF_DRAWTURB|SURF_DRAWSKY)) 
			continue;	// no lightmaps

		tex = surf->texinfo;
		
		s = DotProduct (mid, tex->vecs[0]) + tex->vecs[0][3];
		t = DotProduct (mid, tex->vecs[1]) + tex->vecs[1][3];;

		if (s < surf->texturemins[0] ||
		t < surf->texturemins[1])
			continue;
		
		ds = s - surf->texturemins[0];
		dt = t - surf->texturemins[1];
		
		if ( ds > surf->extents[0] || dt > surf->extents[1] )
			continue;

		if (!surf->samples)
			return 0;

		ds >>= 4;
		dt >>= 4;

		lightmap = surf->samples;
		VectorCopy (vec3_origin, pointcolor);
		if (lightmap)
		{
			vec3_t scale;

			lightmap += 3*(dt * ((surf->extents[0]>>4)+1) + ds);

			for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
					maps++)
			{
				for (i=0 ; i<3 ; i++)
					scale[i] = gl_modulate->value*r_newrefdef.lightstyles[surf->styles[maps]].rgb[i];

				pointcolor[0] += lightmap[0] * scale[0] * (DIV255);
				pointcolor[1] += lightmap[1] * scale[1] * (DIV255);
				pointcolor[2] += lightmap[2] * scale[2] * (DIV255);
				lightmap += 3*((surf->extents[0]>>4)+1) *
						((surf->extents[1]>>4)+1);
			}
		}
		
		return 1;
	}

// go down back side
	return RecursiveLightPoint (node->children[!side], mid, end);
}

void R_StainNode (dstain_t *st, mnode_t *node) { 
	msurface_t *surf;
	float		dist;
	int			c;
	
	if (node->contents != -1)
		return;

	dist = DotProduct (st->origin, node->plane->normal) - node->plane->dist;
	if (dist > st->intensity) {
		R_StainNode (st, node->children[0]);
		return;
	}
	if (dist < -st->intensity) {
		R_StainNode (st, node->children[1]);
		return;
	}

	for (c = node->numsurfaces, surf = r_worldmodel->surfaces + node->firstsurface; c ; c--, surf++) 
	{
		int			i;
		mtexinfo_t	*tex;
		int			sd, td;
		float		fdist, frad, fminlight;
		vec3_t		impact, local;
		int			s, t;
		int			smax, tmax;
		byte		*pfBL;
		float		fsacc, ftacc;

		smax = (surf->extents[0]>>4)+1;
		tmax = (surf->extents[1]>>4)+1;
		tex = surf->texinfo;

		if ( (tex->flags & (SURF_SKY|SURF_TRANS33|SURF_TRANS66|SURF_WARP) ) )
			continue;

		frad = st->intensity;
		fdist = DotProduct (st->origin, surf->plane->normal) - surf->plane->dist;
		if(surf->flags & SURF_PLANEBACK) fdist *= -1;
		frad -= fabs(fdist);

		if (frad < 0)
			continue;
		fminlight = frad;

		for (i=0 ; i<3 ; i++)
			impact[i] = st->origin[i] - surf->plane->normal[i]*fdist;

		local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3] - surf->texturemins[0];
		local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3] - surf->texturemins[1];

		pfBL = surf->stains;
		surf->cached_light[0]=0;

		for (t = 0, ftacc = 0 ; t<tmax ; t++, ftacc += 16)
		{
			td = local[1] - ftacc;
			if ( td < 0 )
				td = -td;
			for ( s=0, fsacc = 0 ; s<smax ; s++, fsacc += 16, pfBL += 3)
			{
				sd = Q_ftol( local[0] - fsacc );

				if ( sd < 0 )
					sd = -sd;

				if (sd > td)
					fdist = sd + (td>>1);
				else
					fdist = td + (sd>>1);

				if ( fdist < fminlight ) 
				{
					float alpha, mult;
					int test;

					mult = frad / fdist;
						if (mult>5.0) mult=5.0;

					alpha = st->alpha*mult;
						if (alpha>255) alpha = 255;
						if (alpha>st->alpha) alpha = st->alpha;
						if (alpha<0) alpha = 0;
					alpha /= 255.0;

					for(i=0;i<3;i++) 
					{
						if (st->type == stain_add)
							test = pfBL[i] + (alpha * st->color[i]);
						else if (st->type == stain_modulate)
							test = (float)(1-alpha) * pfBL[i] + (float)(alpha) * st->color[i];
						else // if (st->type == stain_subtract) //default type
							test = pfBL[i] - (alpha * st->color[i]);

						if (test > 255)
							pfBL[i] = (byte)255;
						else if (test < 0)
							pfBL[i] = (byte)0;
						else
							pfBL[i] = (byte)test;
					}

				}
			}
		}
	}

	R_StainNode (st, node->children[0]);
	R_StainNode (st, node->children[1]);
}

/*
===============
R_LightPoint
===============
*/

void R_MaxColorVec (vec3_t color)
{
	int j;
	float lightest = 0;

	for (j=0;j<3;j++)
		if (color[j]>lightest)
			lightest= color[j];
	if (lightest>255)
		for (j=0;j<3;j++)
			color[j]*= 255/lightest;

	for (j=0;j<3;j++)
	{
		if (color[j]>1) color[j] = 1;
		if (color[j]<0) color[j] = 0;
	}
}


void R_LightPoint (vec3_t p, vec3_t color)
{
	vec3_t		end;
	float		r;
	int			lnum, i;
	dlight_t	*dl;
	float		light;
	vec3_t		dist;
	float		add;
	
	if (!r_worldmodel->lightdata)
	{
		color[0] = color[1] = color[2] = 1.0;
		return;
	}
	
	end[0] = p[0];
	end[1] = p[1];
	end[2] = p[2] - 2048;
	
	r = RecursiveLightPoint (r_worldmodel->nodes, p, end);
	
	if (r == -1)
	{
		VectorCopy (vec3_origin, color);
	}
	else
	{
		VectorCopy (pointcolor, color);
	}

	//this catches too bright modulated color
	for (i=0;i<3;i++)
		if (color[i]>1) color[i] = 1;

	//
	// add dynamic lights
	//
	light = 0;
	dl = r_newrefdef.dlights;
	for (lnum=0 ; lnum<r_newrefdef.num_dlights ; lnum++, dl++)
	{
		if (dl->spotlight) //spotlights
			continue;

		VectorSubtract (p, dl->origin, dist);

		add = dl->intensity - VectorLength(dist);
		add *= (DIV256);
		if (add > 0)
			VectorMA (color, add, dl->color, color);
	}
}

void R_LightPointDynamics (vec3_t p, vec3_t color, m_dlight_t *list, int *amount, int max)
{
	vec3_t		end;
	float		r;
	int			lnum, i, m_dl;
	dlight_t	*dl;
	vec3_t		dist, dlColor;
	float		add;
	worldLight_t *wl;
	
	if (!r_worldmodel->lightdata)
	{
		color[0] = color[1] = color[2] = 1.0;
		return;
	}
	
	end[0] = p[0];
	end[1] = p[1];
	end[2] = p[2] - 2048;
	
	r = RecursiveLightPoint (r_worldmodel->nodes, p, end);
	
	if (r == -1)
	{
		VectorCopy (vec3_origin, color);
	}
	else
	{
		VectorCopy (pointcolor, color);
	}

	//this catches too bright modulated color
	for (i=0;i<3;i++)
		if (color[i]>1) color[i] = 1;

	//
	// add dynamic lights
	//
	m_dl = 0;
	dl = r_newrefdef.dlights;
	for (lnum=0 ; lnum<r_newrefdef.num_dlights ; lnum++, dl++)
	{
		if (dl->spotlight) //spotlights
			continue;

		VectorSubtract (dl->origin, p, dist);

		add = dl->intensity - VectorNormalize(dist);
		add *= (DIV256);
		if (add > 0)
		{
			float highest = -1;

			VectorScale(dl->color, add, dlColor);
			for (i=0;i<3;i++)
				if (highest<dlColor[i]) highest = dlColor[i];

			if (m_dl<max)
			{
				list[m_dl].strength = highest;
				VectorCopy(dist, list[m_dl].direction);
				VectorCopy(dlColor, list[m_dl].color);
				m_dl++;
			}
			else
			{
				float	least_val = 10;
				int		least_index = 0;

				for (i=0;i<m_dl;i++)
					if (list[i].strength<least_val)
					{

						least_val = list[i].strength;
						least_index = i;
					}

				VectorAdd (color, list[least_index].color, color);
				list[least_index].strength = highest;
				VectorCopy(dist, list[least_index].direction);
				VectorCopy(dlColor, list[least_index].color);
			}
		}
	}

	*amount = m_dl;
}

void R_SurfLightPoint (vec3_t p, vec3_t color, qboolean baselight)
{
	vec3_t		end;
	float		r;
	int			lnum, i;
	dlight_t	*dl;
	float		light;
	vec3_t		dist;
	float		add;
	
	if (!r_worldmodel->lightdata)
	{
		color[0] = color[1] = color[2] = 1.0;
		return;
	}
	
	if (baselight)
	{
		end[0] = p[0];
		end[1] = p[1];
		end[2] = p[2] - 2048;
		
		r = RecursiveLightPoint (r_worldmodel->nodes, p, end);
		
		if (r == -1)
			VectorCopy (vec3_origin, color);
		else
			VectorCopy (pointcolor, color);

		//this catches too bright modulated color
		for (i=0;i<3;i++)
			if (color[i]>1) color[i] = 1;
	}
	else
	{
		VectorClear(color);
		//
		// add dynamic lights
		//
		light = 0;
		dl = r_newrefdef.dlights;
		for (lnum=0 ; lnum<r_newrefdef.num_dlights ; lnum++, dl++)
		{
			if (dl->spotlight) //spotlights
				continue;

			VectorSubtract (p, dl->origin, dist);

			add = dl->intensity - VectorLength(dist);
			add *= (DIV256);
			if (add > 0)
			{
				VectorMA (color, add, dl->color, color);
			}
		}
	}
}

void R_ShadowLight (vec3_t pos, vec3_t lightAdd)
{
	int			lnum;
	dlight_t	*dl;
	float		len;
	vec3_t		dist, angle;
	float		add;

	VectorSet(lightAdd,
		0,
		0,
		-500
		);
	//
	// add dynamic light shadow angles
	//
	dl = r_newrefdef.dlights;
	for (lnum=0 ; lnum<r_newrefdef.num_dlights ; lnum++, dl++)
	{
		if (dl->spotlight) //spotlights
			continue;

		VectorSubtract (pos, dl->origin , dist);
		add = dl->intensity - VectorNormalize(dist);

		if (add > 0)
			VectorMA (lightAdd, add*10.0, dist, lightAdd);
	}
	
	len = VectorNormalize(lightAdd);

	//now rotate according to model yaw
	vectoangles (lightAdd, angle);

	//e->angles
	angle[YAW] = -(currententity->angles[YAW]-angle[YAW]);

	AngleVectors (angle, dist, NULL, NULL);
	VectorScale(dist, len, lightAdd);
}

//===================================================================

static float s_blocklights[146*146*3];
/*
===============
R_AddDynamicLights
===============
*/

void R_AddDynamicLights (msurface_t *surf)
{
	int			lnum;
	int			sd, td;
	float		fdist, frad, fminlight;
	vec3_t		impact, local, dlorigin, temp;
	int			s, t;
	int			i;
	int			smax, tmax;
	mtexinfo_t	*tex;
	dlight_t	*dl;
	float		*pfBL;
	float		fsacc, ftacc;
	qboolean rotated = false;
	vec3_t	forward, right, up;

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	tex = surf->texinfo;

	if (currententity->angles[0] || currententity->angles[1] || currententity->angles[2])
	{
		rotated = true;
		AngleVectors (currententity->angles, forward, right, up);
	}

	for (lnum=0 ; lnum<r_newrefdef.num_dlights ; lnum++)
	{
		if ( !(surf->dlightbits & (1<<lnum) ) )
			continue;		// not lit by this light

		dl = &r_newrefdef.dlights[lnum];

		if (dl->spotlight) //spotlights
			continue;

		frad = dl->intensity;

		VectorCopy ( dl->origin, dlorigin );
		VectorSubtract (dlorigin, currententity->origin, dlorigin);

		if (rotated)
		{
			VectorCopy (dlorigin, temp);
			dlorigin[0] = DotProduct (temp, forward);
			dlorigin[1] = -DotProduct (temp, right);
			dlorigin[2] = DotProduct (temp, up);
		}

		fdist = DotProduct (dlorigin, surf->plane->normal) -
				surf->plane->dist;
		frad -= fabs(fdist);
		// rad is now the highest intensity on the plane

		fminlight = DLIGHT_CUTOFF;	// FIXME: make configurable?
		if (frad < fminlight)
			continue;
		fminlight = frad - fminlight;

		for (i=0 ; i<3 ; i++)
		{
			impact[i] = dlorigin[i] -
					surf->plane->normal[i]*fdist;
		}

		local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3] - surf->texturemins[0];
		local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3] - surf->texturemins[1];

		pfBL = s_blocklights;
		for (t = 0, ftacc = 0 ; t<tmax ; t++, ftacc += 16)
		{
			td = local[1] - ftacc;
			if ( td < 0 )
				td = -td;

			for ( s=0, fsacc = 0 ; s<smax ; s++, fsacc += 16, pfBL += 3)
			{
				sd = Q_ftol( local[0] - fsacc );

				if ( sd < 0 )
					sd = -sd;

				if (sd > td)
					fdist = sd + (td>>1);
				else
					fdist = td + (sd>>1);

				if ( fdist < fminlight )
				{
					pfBL[0] += ( frad - fdist ) * dl->color[0];
					pfBL[1] += ( frad - fdist ) * dl->color[1];
					pfBL[2] += ( frad - fdist ) * dl->color[2];
				}
			}
		}
	}
}

void R_AddStains (msurface_t *surf)
{
	int			s, t, i;
	int			smax, tmax;

	float		*pfBL;
	byte		*pfBS;
	vec3_t		scale;

	for (i=0 ; i<3 ; i++)
		scale[i] = gl_modulate->value;

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;

	pfBL = s_blocklights;
	pfBS = surf->stains;

	for (t = 0; t<tmax; t++)
		for ( s=0; s<smax; s++, pfBL += 3, pfBS +=3)
			for (i=0;i<3;i++)
			{
				if (pfBL[i]>pfBS[i]*scale[i])
					pfBL[i]=pfBS[i]*scale[i];
			}
}

/*
** R_SetCacheState
*/
void R_SetCacheState( msurface_t *surf )
{
	int maps;

	for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
		 maps++)
	{
		surf->cached_light[maps] = r_newrefdef.lightstyles[surf->styles[maps]].white;
	}
}

/*
===============
R_BuildLightMap

Combine and scale multiple lightmaps into the floating format in blocklights
===============
*/
void R_BuildLightMap (msurface_t *surf, byte *dest, int stride)
{
	int			smax, tmax;
	int			r, g, b, a, max;
	int			i, j, size;
	byte		*lightmap;
	float		scale[4];
	int			nummaps;
	float		*bl;
	lightstyle_t	*style;
	int monolightmap;

	if ( surf->texinfo->flags & (SURF_SKY|SURF_TRANS33|SURF_TRANS66|SURF_WARP) )
		ri.Sys_Error (ERR_DROP, "R_BuildLightMap called for non-lit surface");

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	size = smax*tmax;
	if (size > ((sizeof(s_blocklights)>>4)*8) )
		ri.Sys_Error (ERR_DROP, "Bad s_blocklights size");

// set to full bright if no light data
	if (!surf->samples)
	{
		int maps;

		for (i=0 ; i<size*3 ; i++)
			s_blocklights[i] = 255;
		for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
			 maps++)
		{
			style = &r_newrefdef.lightstyles[surf->styles[maps]];
		}
		goto store;
	}

	// count the # of maps
	for ( nummaps = 0 ; nummaps < MAXLIGHTMAPS && surf->styles[nummaps] != 255 ;
		 nummaps++)
		;

	lightmap = surf->samples;

	// add all the lightmaps
	if ( nummaps == 1 )
	{
		int maps;

		for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
			 maps++)
		{
			bl = s_blocklights;

			for (i=0 ; i<3 ; i++)
				scale[i] = gl_modulate->value*r_newrefdef.lightstyles[surf->styles[maps]].rgb[i];

			if ( scale[0] == 1.0F &&
				 scale[1] == 1.0F &&
				 scale[2] == 1.0F )
			{
				for (i=0 ; i<size ; i++, bl+=3)
				{
					bl[0] = lightmap[i*3+0];
					bl[1] = lightmap[i*3+1];
					bl[2] = lightmap[i*3+2];
				}
			}
			else
			{
				for (i=0 ; i<size ; i++, bl+=3)
				{
					bl[0] = lightmap[i*3+0] * scale[0];
					bl[1] = lightmap[i*3+1] * scale[1];
					bl[2] = lightmap[i*3+2] * scale[2];
				}
			}
			lightmap += size*3;		// skip to next lightmap
		}
	}
	else
	{
		int maps;

		memset( s_blocklights, 0, sizeof( s_blocklights[0] ) * size * 3 );

		for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
			 maps++)
		{
			bl = s_blocklights;

			for (i=0 ; i<3 ; i++)
				scale[i] = gl_modulate->value*r_newrefdef.lightstyles[surf->styles[maps]].rgb[i];

			if ( scale[0] == 1.0F &&
				 scale[1] == 1.0F &&
				 scale[2] == 1.0F )
			{
				for (i=0 ; i<size ; i++, bl+=3 )
				{
					bl[0] += lightmap[i*3+0];
					bl[1] += lightmap[i*3+1];
					bl[2] += lightmap[i*3+2];
				}
			}
			else
			{
				for (i=0 ; i<size ; i++, bl+=3)
				{
					bl[0] += lightmap[i*3+0] * scale[0];
					bl[1] += lightmap[i*3+1] * scale[1];
					bl[2] += lightmap[i*3+2] * scale[2];
				}
			}
			lightmap += size*3;		// skip to next lightmap
		}
	}

// add all the dynamic lights
	if (surf->dlightframe == r_framecount)
		R_AddDynamicLights (surf);

//add stains...
	if (!(r_newrefdef.rdflags & RDF_NOWORLDMODEL)) // no stains in menu -crashy :p
		if (surf->stains && r_stainmap->value)
			R_AddStains (surf);

// put into texture format
store:
	stride -= (smax<<2);
	bl = s_blocklights;

	monolightmap = gl_monolightmap->string[0];

	if ( monolightmap == '0' )
	{
		for (i=0 ; i<tmax ; i++, dest += stride)
		{
			for (j=0 ; j<smax ; j++)
			{
				
				r = Q_ftol( bl[0] );
				g = Q_ftol( bl[1] );
				b = Q_ftol( bl[2] );

				// catch negative lights
				if (r < 0)
					r = 0;
				if (g < 0)
					g = 0;
				if (b < 0)
					b = 0;

				/*
				** determine the brightest of the three color components
				*/
				if (r > g)
					max = r;
				else
					max = g;
				if (b > max)
					max = b;

				/*
				** alpha is ONLY used for the mono lightmap case.  For this reason
				** we set it to the brightest of the color components so that 
				** things don't get too dim.
				*/
				a = max;

				/*
				** rescale all the color components if the intensity of the greatest
				** channel exceeds 1.0
				*/
				if (max > 255)
				{
					float t = 255.0F / max;

					r = r*t;
					g = g*t;
					b = b*t;
					a = a*t;
				}

				dest[0] = r;
				dest[1] = g;
				dest[2] = b;
				dest[3] = a;

				bl += 3;
				dest += 4;
			}
		}
	}
	else
	{
		for (i=0 ; i<tmax ; i++, dest += stride)
		{
			for (j=0 ; j<smax ; j++)
			{
				
				r = Q_ftol( bl[0] );
				g = Q_ftol( bl[1] );
				b = Q_ftol( bl[2] );

				// catch negative lights
				if (r < 0)
					r = 0;
				if (g < 0)
					g = 0;
				if (b < 0)
					b = 0;

				/*
				** determine the brightest of the three color components
				*/
				if (r > g)
					max = r;
				else
					max = g;
				if (b > max)
					max = b;

				/*
				** alpha is ONLY used for the mono lightmap case.  For this reason
				** we set it to the brightest of the color components so that 
				** things don't get too dim.
				*/
				a = max;

				/*
				** rescale all the color components if the intensity of the greatest
				** channel exceeds 1.0
				*/
				if (max > 255)
				{
					float t = 255.0F / max;

					r = r*t;
					g = g*t;
					b = b*t;
					a = a*t;
				}

				/*
				** So if we are doing alpha lightmaps we need to set the R, G, and B
				** components to 0 and we need to set alpha to 1-alpha.
				*/
				switch ( monolightmap )
				{
				case 'L':
				case 'I':
					r = a;
					g = b = 0;
					break;
				case 'C':
					// try faking colored lighting
					a = 255 - ((r+g+b)/3);
					r *= a*DIV255;
					g *= a*DIV255;
					b *= a*DIV255;
					break;
				case 'A':
				default:
					r = g = b = 0;
					a = 255 - a;
					break;
				}

				dest[0] = r;
				dest[1] = g;
				dest[2] = b;
				dest[3] = a;

				bl += 3;
				dest += 4;
			}
		}
	}
}

dstain_t tempStain;
void	R_AddStain (vec3_t org, float intensity, float r, float g, float b, float a, staintype_t type)
{
	if (!r_stainmap->value)
		return;

	tempStain.origin[0]=org[0];
	tempStain.origin[1]=org[1];
	tempStain.origin[2]=org[2];
	tempStain.color[0]=r;
	tempStain.color[1]=g;
	tempStain.color[2]=b;
	tempStain.alpha = a;
	tempStain.intensity=intensity;
	tempStain.type = type;

	//now add the stain...
	R_StainNode (&tempStain, r_worldmodel->nodes);
	//also stain bmodels
/*	{
		entity_t *ent;
		int i;

		for (i=0;i<r_newrefdef.num_entities; i++)
		{

			ent = &r_newrefdef.entities[i];

			if (ent->model && ent->model->type==mod_brush)
			{

			}
		}
	}*/
}

//bleh, need this
void vectoangles (vec3_t value1, vec3_t angles)
{
	float	forward;
	float	yaw, pitch;
	
	if (value1[1] == 0 && value1[0] == 0)
	{
		yaw = 0;
		if (value1[2] > 0)
			pitch = 90;
		else
			pitch = 270;
	}
	else
	{
	// PMM - fixed to correct for pitch of 0
		if (value1[0])
			yaw = (atan2(value1[1], value1[0]) * 180 / M_PI);
		else if (value1[1] > 0)
			yaw = 90;
		else
			yaw = 270;

		if (yaw < 0)
			yaw += 360;

		forward = sqrt (value1[0]*value1[0] + value1[1]*value1[1]);
		pitch = (atan2(value1[2], forward) * 180 / M_PI);
		if (pitch < 0)
			pitch += 360;
	}

	angles[PITCH] = -pitch;
	angles[YAW] = yaw;
	angles[ROLL] = 0;
}
