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
// GL_RSURF.C: surface-related refresh code
#include <assert.h>

#include "gl_local.h"

static vec3_t	modelorg;		// relative to viewpoint

msurface_t	*r_alpha_surfaces;
msurface_t	*r_special_surfaces;
msurface_t	*outlinechain;  // q1 code: extern	msurface_t	*outlinechain;

#define DYNAMIC_LIGHT_WIDTH  128
#define DYNAMIC_LIGHT_HEIGHT 128

#define LIGHTMAP_BYTES 4

#define	BLOCK_WIDTH		128
#define	BLOCK_HEIGHT	128

#define	MAX_LIGHTMAPS	128

int		c_visible_lightmaps;
int		c_visible_textures;

#define GL_LIGHTMAP_FORMAT GL_RGBA

typedef struct
{
	int internal_format;
	int	current_lightmap_texture;

	msurface_t	*lightmap_surfaces[MAX_LIGHTMAPS];

	int			allocated[BLOCK_WIDTH];

	// the lightmap texture data needs to be kept in
	// main memory so texsubimage can update properly
	byte		lightmap_buffer[4*BLOCK_WIDTH*BLOCK_HEIGHT];
} gllightmapstate_t;

static gllightmapstate_t gl_lms;

rscript_t	*surfaceScript(msurface_t *surf);

static void		LM_InitBlock( void );
static void		LM_UploadBlock( qboolean dynamic );
static qboolean	LM_AllocBlock (int w, int h, int *x, int *y);
static void GL_BuildVertexLight (msurface_t *surf);

extern void R_SetCacheState( msurface_t *surf );
extern void R_BuildLightMap (msurface_t *surf, byte *dest, int stride);

void SetVertexOverbrights (qboolean);


//setup for a normal texture
__inline void Surf_EnableNormal(){
	//just uses normal modulate
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

//reset the settings to the defualt
__inline void Surf_Reset(){
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}
/*
=============================================================

	BRUSH MODELS

=============================================================
*/

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
image_t *R_TextureAnimation (mtexinfo_t *tex)
{
	int		c;

	if (!tex->next)
		return tex->image;

	c = currententity->frame % tex->numframes;
	while (c)
	{
		tex = tex->next;
		c--;
	}

	return tex->image;
}

void SetLightingMode (void)
{
	GL_SelectTexture( GL_TEXTURE0);

	if ( !gl_config.mtexcombine ) 
	{
		GL_TexEnv( GL_REPLACE );
		GL_SelectTexture( GL_TEXTURE1);

		if ( gl_lightmap->value )
			GL_TexEnv( GL_REPLACE );
		else 
			GL_TexEnv( GL_MODULATE );
	}
	else 
	{
		GL_TexEnv ( GL_COMBINE_EXT );
		qglTexEnvi ( GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_REPLACE );
		qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_TEXTURE );
		qglTexEnvi ( GL_TEXTURE_ENV, GL_COMBINE_ALPHA_EXT, GL_REPLACE );
		qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_EXT, GL_TEXTURE );

		GL_SelectTexture( GL_TEXTURE1 );
		GL_TexEnv ( GL_COMBINE_EXT );
		if ( gl_lightmap->value ) 
		{
			qglTexEnvi ( GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_REPLACE );
			qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_TEXTURE );
			qglTexEnvi ( GL_TEXTURE_ENV, GL_COMBINE_ALPHA_EXT, GL_REPLACE );
			qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_EXT, GL_TEXTURE );
		} 
		else 
		{
			qglTexEnvi ( GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE );
			qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_TEXTURE );
			qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE1_RGB_EXT, GL_PREVIOUS_EXT );

			qglTexEnvi ( GL_TEXTURE_ENV, GL_COMBINE_ALPHA_EXT, GL_MODULATE );
			qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_EXT, GL_TEXTURE );
			qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE1_ALPHA_EXT, GL_PREVIOUS_EXT );
		}

		if ( r_overbrightbits->value )
		{
			qglTexEnvi ( GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, r_overbrightbits->value );
		}
	}
}

#if 0
/*
=================
WaterWarpPolyVerts

Mangles the x and y coordinates in a copy of the poly
so that any drawing routine can be water warped
=================
*/
glpoly_t *WaterWarpPolyVerts (glpoly_t *p)
{
	int		i;
	float	*v, *nv;
	static byte	buffer[1024];
	glpoly_t *out;

	out = (glpoly_t *)buffer;

	out->numverts = p->numverts;
	v = p->verts[0];
	nv = out->verts[0];
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE, nv+=VERTEXSIZE)
	{
		nv[0] = v[0] + 4*sin(v[1]*0.05+r_newrefdef.time)*sin(v[2]*0.05+r_newrefdef.time);
		nv[1] = v[1] + 4*sin(v[0]*0.05+r_newrefdef.time)*sin(v[2]*0.05+r_newrefdef.time);

		nv[2] = v[2];
		nv[3] = v[3];
		nv[4] = v[4];
		nv[5] = v[5];
		nv[6] = v[6];
	}

	return out;
}

/*
================
DrawGLWaterPoly

Warp the vertex coordinates
================
*/
void DrawGLWaterPoly (glpoly_t *p)
{
	int		i;
	float	*v;

	p = WaterWarpPolyVerts (p);
	qglBegin (GL_TRIANGLE_FAN);
	v = p->verts[0];
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		qglTexCoord2f (v[3], v[4]);
		qglVertex3fv (v);
	}
	qglEnd ();
}
void DrawGLWaterPolyLightmap (glpoly_t *p)
{
	int		i;
	float	*v;

	p = WaterWarpPolyVerts (p);
	qglBegin (GL_TRIANGLE_FAN);
	v = p->verts[0];
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		qglTexCoord2f (v[5], v[6]);
		qglVertex3fv (v);
	}
	qglEnd ();
}
#endif

/*
================
DrawGLPoly
================
*/
void DrawGLPoly (msurface_t *fa, qboolean light, float alpha)
{
	int		i;
	float	*v;
	glpoly_t *p = fa->polys;

	if (light)
	{
		SetVertexOverbrights(true);
		GL_ShadeModel (GL_SMOOTH);
	}
	else
		qglColor4f( gl_state.inverse_intensity, gl_state.inverse_intensity, gl_state.inverse_intensity, alpha);

	qglBegin (GL_POLYGON);
	v = p->verts[0];
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		if (light && p->vertexlight)
		{
			qglColor4ub( 
				p->vertexlight[i*3+0], 
				p->vertexlight[i*3+1], 
				p->vertexlight[i*3+2], 
				alpha*255);
		}

		qglTexCoord2f (v[3], v[4]);
		qglVertex3fv (v);
	}
	qglEnd ();

	if (light)
	{
		SetVertexOverbrights(false);
		GL_ShadeModel (GL_FLAT);
	}
}

//============
//PGM
/*
================
DrawGLFlowingPoly -- version of DrawGLPoly that handles scrolling texture
================
*/
void DrawGLFlowingPoly (msurface_t *fa, qboolean light, float alpha)
{
	int		i;
	float	*v;
	glpoly_t *p;
	float	scroll;

	p = fa->polys;

	scroll = -64 * ( (r_newrefdef.time / 40.0) - (int)(r_newrefdef.time / 40.0) );
	if(scroll == 0.0)
		scroll = -64.0;

	if (light)
	{
		SetVertexOverbrights(true);
		GL_ShadeModel (GL_SMOOTH);
	}
	else
		qglColor4f( gl_state.inverse_intensity, gl_state.inverse_intensity, gl_state.inverse_intensity, alpha);

	qglBegin (GL_POLYGON);
	v = p->verts[0];
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		if (light && p->vertexlight)
		{
			qglColor4ub( 
				p->vertexlight[i*3+0], 
				p->vertexlight[i*3+1], 
				p->vertexlight[i*3+2], 
				alpha*255);
		}

		qglTexCoord2f ((v[3] + scroll), v[4]);
		qglVertex3fv (v);
	}
	qglEnd ();

	if (light)
	{
		SetVertexOverbrights(false);
		GL_ShadeModel (GL_FLAT);
	}
}
//PGM
//============

/*
** R_DrawTriangleOutlines
*/
 void R_DrawTriangleOutlines(msurface_t *surf)
{
     int        i;
     glpoly_t *p;

    if (!gl_showtris->value)
        return;

    // Guy: *\/\/\/ gl_showtris fix begin \/\/\/*
    qglDisable(GL_DEPTH_TEST);

    if (!surf)    // Guy: Called from non-multitexture mode; need to loop through surfaces defined by non-mtex functions
    {
        int j;

        qglDisable(GL_TEXTURE_2D);
        
        for (i = 0; i < MAX_LIGHTMAPS; i++)
        {
            for (surf = gl_lms.lightmap_surfaces[i]; surf != 0; surf = surf->lightmapchain)
            {
                for (p = surf->polys; p; p = p->chain)
                {
                    for (j = 2; j < p->numverts; j++)
                    {
                        qglBegin(GL_LINE_STRIP);
                            qglColor4f(1, 1, 1, 1);
                            qglVertex3fv(p->verts[0]);
                            qglVertex3fv(p->verts[j - 1]);
                            qglVertex3fv(p->verts[j]);
                            qglVertex3fv(p->verts[0]);
                        qglEnd();
                    }
                }
            }
        }

        qglEnable(GL_TEXTURE_2D);
    }
    
    else    // Guy: Called from multitexture mode; surface to be rendered in wireframe already passed in
    {
        float    tex_state0,
                 tex_state1;

        GL_SelectTexture(GL_TEXTURE0);
        qglGetTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &tex_state0);

        GL_SelectTexture(GL_TEXTURE1);
        qglGetTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &tex_state1);

        GL_EnableMultitexture(false);
        qglDisable(GL_TEXTURE_2D);

        for (p = surf->polys; p; p = p->chain)
        {
            for (i = 2; i < p->numverts; i++)
            {
                qglBegin(GL_LINE_STRIP);
                    qglColor4f(1, 1, 1, 1);
                    qglVertex3fv(p->verts[0]);
                    qglVertex3fv(p->verts[i - 1]);
                    qglVertex3fv(p->verts[i]);
                    qglVertex3fv(p->verts[0]);
                qglEnd();
            }
        }

        qglEnable(GL_TEXTURE_2D);
        GL_EnableMultitexture(true);
        
        GL_SelectTexture(GL_TEXTURE0);
        GL_TexEnv(tex_state0);

        GL_SelectTexture(GL_TEXTURE1);
        GL_TexEnv(tex_state1);
    }

qglEnable(GL_DEPTH_TEST);
// Guy: */\/\/\ gl_showtris fix end /\/\/\*
}

/*
** DrawGLPolyChain
*/
void DrawGLPolyChain( glpoly_t *p, float soffset, float toffset )
{
	if ( soffset == 0 && toffset == 0 )
	{
		for ( ; p != 0; p = p->chain )
		{
			float *v;
			int j;

			qglBegin (GL_POLYGON);
			v = p->verts[0];
			for (j=0 ; j<p->numverts ; j++, v+= VERTEXSIZE)
			{
				qglTexCoord2f (v[5], v[6] );
				qglVertex3fv (v);
			}
			qglEnd ();
		}
	}
	else
	{
		for ( ; p != 0; p = p->chain )
		{
			float *v;
			int j;

			qglBegin (GL_POLYGON);
			v = p->verts[0];
			for (j=0 ; j<p->numverts ; j++, v+= VERTEXSIZE)
			{
				qglTexCoord2f (v[5] - soffset, v[6] - toffset );
				qglVertex3fv (v);
			}
			qglEnd ();
		}
	}
}

/*
** R_BlendLightMaps
**
** This routine takes all the given light mapped surfaces in the world and
** blends them into the framebuffer.
*/
void R_BlendLightmaps (void)
{
	int			i;
	msurface_t	*surf, *newdrawsurf = 0;

	// don't bother if we're set to fullbright
	if (r_fullbright->value)
		return;
	if (!r_worldmodel->lightdata)
		return;

	// don't bother writing Z
	qglDepthMask( 0 );

	/*
	** set the appropriate blending mode unless we're only looking at the
	** lightmaps.
	*/
	if (!gl_lightmap->value)
	{
		GLSTATE_ENABLE_BLEND

		if ( gl_saturatelighting->value )
		{
			GL_BlendFunction( GL_ONE, GL_ONE );
		}
		else
		{
			if ( gl_monolightmap->string[0] != '0' )
			{
				switch ( toupper( gl_monolightmap->string[0] ) )
				{
				case 'I':
					GL_BlendFunction (GL_ZERO, GL_SRC_COLOR );
					break;
				case 'L':
					GL_BlendFunction (GL_ZERO, GL_SRC_COLOR );
					break;
				case 'A':
				default:
					GL_BlendFunction( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
					break;
				}
			}
			else
			{
				GL_BlendFunction (GL_ZERO, GL_SRC_COLOR );
			}
		}
	}

	if ( currentmodel == r_worldmodel )
		c_visible_lightmaps = 0;

	/*
	** render static lightmaps first
	*/
	for ( i = 1; i < MAX_LIGHTMAPS; i++ )
	{
		if ( gl_lms.lightmap_surfaces[i] )
		{
			if (currentmodel == r_worldmodel)
				c_visible_lightmaps++;
			GL_Bind( gl_state.lightmap_textures + i);

			for ( surf = gl_lms.lightmap_surfaces[i]; surf != 0; surf = surf->lightmapchain )
			{
				if ( surf->polys )
					DrawGLPolyChain( surf->polys, 0, 0 );
			}
		}
	}

	/*
	** render dynamic lightmaps
	*/
	if ( gl_dynamic->value )
	{
		LM_InitBlock();

		GL_Bind( gl_state.lightmap_textures+0 );

		if (currentmodel == r_worldmodel)
			c_visible_lightmaps++;

		newdrawsurf = gl_lms.lightmap_surfaces[0];

		for ( surf = gl_lms.lightmap_surfaces[0]; surf != 0; surf = surf->lightmapchain )
		{
			int		smax, tmax;
			byte	*base;

			smax = (surf->extents[0]>>4)+1;
			tmax = (surf->extents[1]>>4)+1;

			if ( LM_AllocBlock( smax, tmax, &surf->dlight_s, &surf->dlight_t ) )
			{
				base = gl_lms.lightmap_buffer;
				base += ( surf->dlight_t * BLOCK_WIDTH + surf->dlight_s ) * LIGHTMAP_BYTES;

				R_BuildLightMap (surf, base, BLOCK_WIDTH*LIGHTMAP_BYTES);
			}
			else
			{
				msurface_t *drawsurf;

				// upload what we have so far
				LM_UploadBlock( true );

				// draw all surfaces that use this lightmap
				for ( drawsurf = newdrawsurf; drawsurf != surf; drawsurf = drawsurf->lightmapchain )
				{
					if ( drawsurf->polys )
						DrawGLPolyChain( drawsurf->polys, 
							              ( drawsurf->light_s - drawsurf->dlight_s ) * ( 1.0 / 128.0 ), 
										( drawsurf->light_t - drawsurf->dlight_t ) * ( 1.0 / 128.0 ) );
				}

				newdrawsurf = drawsurf;

				// clear the block
				LM_InitBlock();

				// try uploading the block now
				if ( !LM_AllocBlock( smax, tmax, &surf->dlight_s, &surf->dlight_t ) )
				{
					ri.Sys_Error( ERR_FATAL, "Consecutive calls to LM_AllocBlock(%d,%d) failed (dynamic)\n", smax, tmax );
				}

				base = gl_lms.lightmap_buffer;
				base += ( surf->dlight_t * BLOCK_WIDTH + surf->dlight_s ) * LIGHTMAP_BYTES;

				R_BuildLightMap (surf, base, BLOCK_WIDTH*LIGHTMAP_BYTES);
			}
		}

		/*
		** draw remainder of dynamic lightmaps that haven't been uploaded yet
		*/
		if ( newdrawsurf )
			LM_UploadBlock( true );

		for ( surf = newdrawsurf; surf != 0; surf = surf->lightmapchain )
		{
			if ( surf->polys )
				DrawGLPolyChain( surf->polys, ( surf->light_s - surf->dlight_s ) * ( 1.0 / 128.0 ), ( surf->light_t - surf->dlight_t ) * ( 1.0 / 128.0 ) );
		}
	}

	/*
	** restore state
	*/
	GLSTATE_DISABLE_BLEND
	GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	qglDepthMask( 1 );
}

/*
================
R_RenderBrushPoly
================
*/
void R_RenderBrushPoly (msurface_t *fa)
{
	int			maps;
	image_t		*image;
	qboolean is_dynamic = false;
	qboolean litPoly = gl_surftrans_light->value;

	c_brush_polys++;

	image = R_TextureAnimation (fa->texinfo);

	if (fa->flags & SURF_DRAWTURB)
	{	
		GL_Bind( image->texnum );

		// warp texture, no lightmaps
		GL_TexEnv( GL_MODULATE );
		qglColor4f( gl_state.inverse_intensity, 
			        gl_state.inverse_intensity,
					gl_state.inverse_intensity,
					1.0F );

		if (r_shaders->value && surfaceScript(fa))
			RS_DrawPolyNoLightMap(fa);
		else
			EmitWaterPolys (fa, litPoly, 1);

		GL_TexEnv( GL_REPLACE );

		return;
	}

	GL_Bind( image->texnum );
	GL_TexEnv( GL_REPLACE );

//======
//PGM

	if(fa->texinfo->flags & SURF_FLOWING) 
	{
		if (!image->script || !r_shaders->value)
		{
			GL_Bind( image->texnum );
			GL_TexEnv( GL_REPLACE );
			DrawGLFlowingPoly (fa, litPoly, 1);
		}
		else 
		{
			GL_TexEnv( GL_MODULATE );
			RS_DrawPolyNoLightMap(fa);
			GL_TexEnv( GL_REPLACE );
		}
	}
	else 
	{
		if (!image->script || !r_shaders->value)
		{
			GL_Bind( image->texnum );
			GL_TexEnv( GL_REPLACE );
			DrawGLPoly (fa, litPoly, 1);
		}
		else 
		{
			GL_TexEnv( GL_MODULATE );
			RS_DrawPolyNoLightMap(fa);
			GL_TexEnv( GL_REPLACE );
		}
	}
//PGM
//======

	/*
	** check for lightmap modification
	*/
	for ( maps = 0; maps < MAXLIGHTMAPS && fa->styles[maps] != 255; maps++ )
	{
		if ( r_newrefdef.lightstyles[fa->styles[maps]].white != fa->cached_light[maps] )
			goto dynamic;
	}

	// dynamic this frame or dynamic previously
	if ( ( fa->dlightframe == r_framecount ) )
	{
dynamic:
		if ( gl_dynamic->value )
		{
			if (!( fa->texinfo->flags & (SURF_SKY|SURF_TRANS33|SURF_TRANS66|SURF_WARP ) ) )
			{
				is_dynamic = true;
			}
		}
	}

	if ( is_dynamic )
	{
		if ( ( fa->styles[maps] >= 32 || fa->styles[maps] == 0 ) && ( fa->dlightframe != r_framecount ) )
		{
			unsigned	temp[34*34];
			int			smax, tmax;

			smax = (fa->extents[0]>>4)+1;
			tmax = (fa->extents[1]>>4)+1;

			R_BuildLightMap( fa, (void *)temp, smax*4 );
			R_SetCacheState( fa );

			GL_Bind( gl_state.lightmap_textures + fa->lightmaptexturenum );

			qglTexSubImage2D( GL_TEXTURE_2D, 0,
							  fa->light_s, fa->light_t, 
							  smax, tmax, 
							  GL_LIGHTMAP_FORMAT, 
							  GL_UNSIGNED_BYTE, temp );

			fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
			gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
		}
		else
		{
			fa->lightmapchain = gl_lms.lightmap_surfaces[0];
			gl_lms.lightmap_surfaces[0] = fa;
		}
	}
	else
	{
		fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
		gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
	}
}


/*
================
R_DrawAlphaSurfaces

Draw water surfaces and windows.
The BSP tree is waled front to back, so unwinding the chain
of alpha_surfaces will draw back to front, giving proper ordering.
================
*/

void drawAlphaSurface (msurface_t *s, float intens, float alpha, qboolean light)
{
	GL_TexEnv( GL_MODULATE );
	GLSTATE_ENABLE_BLEND

	GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_Bind(s->texinfo->image->texnum);
	qglColor4f (intens,intens,intens, alpha);

	if (s->flags & SURF_DRAWTURB)		
		EmitWaterPolys (s, light, alpha);
	else if(s->texinfo->flags & SURF_FLOWING)
		DrawGLFlowingPoly (s, light, alpha);
	else
		DrawGLPoly (s, light, alpha);
}

float SurfAlphaCalc (int flags)
{
	if (flags & SURF_TRANS33 && flags & SURF_TRANS66)
		return DIV254BY255;
	else if (flags & SURF_TRANS33)
		return 0.33333;
	else if (flags & SURF_TRANS66)
		return 0.66666;
	else
		return DIV254BY255;
}
void surf_ElementList(msurface_t *surf, qboolean ents);
void R_DrawAlphaSurfaces (qboolean elements)
{
	msurface_t	*s;
	qboolean transLit = gl_surftrans_light->value;

	// the textures are prescaled up for a better lighting range,
	// so scale it back down

	for (s=r_alpha_surfaces ; s ; s=s->texturechain)
	{
		c_brush_polys++;

		if (elements)
		{
			surf_ElementList(s, true);
			surf_ElementList(s, false);
		}

		qglLoadMatrixf (r_world_matrix);

		GL_BuildVertexLight(s);

		//moving trans brushes - spaz
		if (s->entity)
		{
			s->entity->angles[0] = -s->entity->angles[0];	// stupid quake bug
			s->entity->angles[2] = -s->entity->angles[2];	// stupid quake bug
				R_RotateForEntity (s->entity, true);
			s->entity->angles[0] = -s->entity->angles[0];	// stupid quake bug
			s->entity->angles[2] = -s->entity->angles[2];	// stupid quake bug
		}

		if (r_shaders->value && surfaceScript(s))
			RS_DrawPolyNoLightMap(s);
		else
			drawAlphaSurface(s, gl_state.inverse_intensity, SurfAlphaCalc(s->texinfo->flags), transLit);

	}

	GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_TexEnv( GL_REPLACE );
	qglColor4f (1,1,1,1);
	GLSTATE_DISABLE_BLEND

	r_alpha_surfaces = NULL;
}

void R_DrawSpecialSurfaces (void)
{
	msurface_t	*s;

	if (!r_shaders->value)
	{
		r_special_surfaces = NULL;
		return;
	}

	qglDepthMask(false);
	GL_ShadeModel (GL_SMOOTH);

	qglEnable(GL_POLYGON_OFFSET_FILL); 
	qglPolygonOffset(-3, -2); 

	for (s=r_special_surfaces ; s ; s=s->specialchain)
		RS_SpecialSurface(s);
	
	qglDisable(GL_POLYGON_OFFSET_FILL); 

	GLSTATE_DISABLE_BLEND
	GLSTATE_DISABLE_ALPHATEST

	qglDepthMask(true);

	r_special_surfaces = NULL;
}

// i think this is the outline stuff
void Surf_Outline (msurface_t *surfchain) //
{
	msurface_t *s, *removelink;
	float		*v;
	int			k;

	glColor4f(0,0,0,1);

	//GL_DisableTMU(GL_TEXTURE0_ARB);
	Surf_EnableNormal();
	qglEnable(GL_DEPTH_TEST);
	glBindTexture(GL_TEXTURE_2D, 0);
	qglCullFace (GL_FRONT);
	qglPolygonMode (GL_BACK, GL_LINE);

	qglDepthRange (0.02, 1.4);

	qglLineWidth (7.5f);
	qglEnable (GL_LINE_SMOOTH);

	for (s = surfchain; s; ){ // used to be surfchain for ( ; s ; s=s->texturechain)
								//for (i=0, image=gltextures ; i<numgltextures ; i++,image++)
								//for (i=0, v=poly->verts[0]; i<poly->numverts; i++, v+=VERTEXSIZE)
		// Outline the polys
		qglBegin(GL_POLYGON);
		v = s->polys->verts[0];
		for (k=0 ; k<s->polys->numverts ; k++, v+= VERTEXSIZE)
		{
			qglVertex3fv (v);
		}
		qglEnd ();

		removelink = s;
		s = s->outline;
		removelink->outline = NULL;
	}

	qglColor4f(1,1,1,1);
	//glCullFace (GL_FRONT);
	//glPolygonMode (GL_FRONT, GL_FILL);
	qglCullFace (GL_BACK);
	qglPolygonMode (GL_BACK, GL_FILL);
	Surf_Reset();
}
/*
===================================================================================================
DrawTextureChains
====================================================================================================
*/
void DrawTextureChains (void)
{
	int		i;
	msurface_t	*s;
	image_t		*image;

	c_visible_textures = 0;

//	GL_TexEnv( GL_REPLACE );

	if ( !qglSelectTextureSGIS && !qglActiveTextureARB )
	{
		for ( i = 0, image=gltextures ; i<numgltextures ; i++,image++)
		{
			if (!image->registration_sequence)
				continue;
			s = image->texturechain;
			if (!s)
				continue;
			c_visible_textures++;

			for ( ; s ; s=s->texturechain)
				R_RenderBrushPoly (s);

			image->texturechain = NULL;
		}
	}
	else
	{
		for ( i = 0, image=gltextures ; i<numgltextures ; i++,image++)
		{
			if (!image->registration_sequence)
				continue;
			if (!image->texturechain)
				continue;
			c_visible_textures++;

			for ( s = image->texturechain; s ; s=s->texturechain)
			{
				if ( !( s->flags & SURF_DRAWTURB ) )
					R_RenderBrushPoly (s);
			}
		}

		GL_EnableMultitexture( false );
		for ( i = 0, image=gltextures ; i<numgltextures ; i++,image++)
		{
			if (!image->registration_sequence)
				continue;
			s = image->texturechain;
			if (!s)
				continue;

			for ( ; s ; s=s->texturechain)
			{
				if ( s->flags & SURF_DRAWTURB )
					R_RenderBrushPoly (s);
			}

			image->texturechain = NULL;
		}
//		GL_EnableMultitexture( true );
	}

	GL_TexEnv( GL_REPLACE );
}

void RenderPolyFunc (msurface_t *surf, int nv, float *v, float scroll)
{
	glpoly_t *p;
	float *poly;
	int i;

	qglBegin (GL_POLYGON);
	for ( p = surf->polys; p; p = p->chain )
	{
		v = p->verts[0];
		for (i=0, poly=v ; i< nv; i++, poly+= VERTEXSIZE)
		{
			qglMTexCoord2fSGIS( GL_TEXTURE0, (poly[3]+scroll), poly[4]);
			qglMTexCoord2fSGIS( GL_TEXTURE1, poly[5], poly[6]);
			qglVertex3fv (poly);	
		}
		v = poly;
	}
	qglEnd ();
}

static void GL_RenderLightmappedPoly( msurface_t *surf )
{
	int		nv = surf->polys->numverts;
	int		map;
	float	*v;
	image_t *image = R_TextureAnimation( surf->texinfo );
	qboolean is_dynamic = false;
	unsigned lmtex = surf->lightmaptexturenum;
	glpoly_t *p;


	for ( map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255; map++ )
	{
		if ( r_newrefdef.lightstyles[surf->styles[map]].white != surf->cached_light[map] )
			goto dynamic;
	}

	// dynamic this frame or dynamic previously
	if ( ( surf->dlightframe == r_framecount ) )
	{
dynamic:
		if ( gl_dynamic->value )
		{
			if ( !(surf->texinfo->flags & (SURF_SKY|SURF_TRANS33|SURF_TRANS66|SURF_WARP ) ) )
			{
				is_dynamic = true;
			}
		}
	}

	if ( is_dynamic )
	{
		unsigned	temp[128*128];
		int			smax, tmax;

		if ( ( surf->styles[map] >= 32 || surf->styles[map] == 0 ) && ( surf->dlightframe != r_framecount ) )
		{
			smax = (surf->extents[0]>>4)+1;
			tmax = (surf->extents[1]>>4)+1;

			R_BuildLightMap( surf, (void *)temp, smax*4 );
			R_SetCacheState( surf );

			GL_MBind( GL_TEXTURE1, gl_state.lightmap_textures + surf->lightmaptexturenum );

			lmtex = surf->lightmaptexturenum;

			qglTexSubImage2D( GL_TEXTURE_2D, 0,
							  surf->light_s, surf->light_t, 
							  smax, tmax, 
							  GL_LIGHTMAP_FORMAT, 
							  GL_UNSIGNED_BYTE, temp );

		}
		else
		{
			smax = (surf->extents[0]>>4)+1;
			tmax = (surf->extents[1]>>4)+1;

			R_BuildLightMap( surf, (void *)temp, smax*4 );

			GL_MBind( GL_TEXTURE1, gl_state.lightmap_textures + 0 );

			lmtex = 0;

			qglTexSubImage2D( GL_TEXTURE_2D, 0,
							  surf->light_s, surf->light_t, 
							  smax, tmax, 
							  GL_LIGHTMAP_FORMAT, 
							  GL_UNSIGNED_BYTE, temp );

		}

		c_brush_polys++;

		GL_MBind( GL_TEXTURE1, gl_state.lightmap_textures + lmtex );

		if (surf->texinfo->flags & SURF_FLOWING)
		{
			float scroll;
		
			GL_MBind( GL_TEXTURE0, image->texnum );

			scroll = -64 * ( (r_newrefdef.time / 40.0) - (int)(r_newrefdef.time / 40.0) );
			if(scroll == 0.0)
				scroll = -64.0;

			RenderPolyFunc(surf, nv, v, scroll);

		}
		else
		{
			if (image->script && r_shaders->value) 
			{
				RS_DrawPoly(surf);
			}
			else
			{
				GL_MBind( GL_TEXTURE0, image->texnum );
				RenderPolyFunc(surf, nv, v, 0);
			}
		}
	}
	else
	{
		c_brush_polys++;

		GL_MBind( GL_TEXTURE1, gl_state.lightmap_textures + lmtex );

		if (surf->texinfo->flags & SURF_FLOWING)
		{
			float scroll;
			GL_MBind( GL_TEXTURE0, image->texnum );
		
			scroll = -64 * ( (r_newrefdef.time / 40.0) - (int)(r_newrefdef.time / 40.0) );
			if(scroll == 0.0)
				scroll = -64.0;

			RenderPolyFunc(surf, nv, v, scroll);
		}
		else
		{
			if (image->script && r_shaders->value) 
			{
				RS_DrawPoly(surf);
			}
			else
			{
				GL_MBind( GL_TEXTURE0, image->texnum );
				RenderPolyFunc(surf, nv, v, 0);
			}

		}
	}
}

void R_SurfLightPoint (vec3_t p, vec3_t color, qboolean baselight);
void GL_BuildVertexLightBase (glpoly_t *poly)
{
	vec3_t color, point;
	int	i;
	float *v;

	for (i=0, v=poly->verts[0]; i<poly->numverts; i++, v+=VERTEXSIZE)
	{
		VectorCopy(v, point);

		R_SurfLightPoint (point, color, true);
			
		R_MaxColorVec (color);

		poly->vertexlightbase[i*3+0] = (byte)(color[0]*255.0);
		poly->vertexlightbase[i*3+1] = (byte)(color[1]*255.0);
		poly->vertexlightbase[i*3+2] = (byte)(color[2]*255.0);
	}
}
void GL_ResetVertextLight (msurface_t *surf)
{
	glpoly_t *poly;

	if (!surf->polys)
		return;

	for (poly=surf->polys ; poly ; poly=poly->next)
		poly->vertexlightset = false;
}

void GL_BuildVertexLight (msurface_t *surf)
{
	vec3_t color, point;
	int	i;
	float *v;
	glpoly_t *poly;

	if (!surf->polys)
		return;

	for (poly=surf->polys ; poly ; poly=poly->next)
	{
		if (!poly->vertexlightbase)
			continue;

		if (!poly->vertexlightset)
		{
			GL_BuildVertexLightBase(poly);
			poly->vertexlightset = true;
		}

		for (i=0, v=poly->verts[0]; i<poly->numverts; i++, v+=VERTEXSIZE)
		{
			VectorCopy(v, point);

			R_SurfLightPoint (point, color, false);

			VectorSet(color,
				(float)poly->vertexlightbase[i*3+0]/255.0 + color[0],
				(float)poly->vertexlightbase[i*3+1]/255.0 + color[1],
				(float)poly->vertexlightbase[i*3+2]/255.0 + color[2]);
				
			R_MaxColorVec (color);

			poly->vertexlight[i*3+0] = (byte)(color[0]*255.0);
			poly->vertexlight[i*3+1] = (byte)(color[1]*255.0);
			poly->vertexlight[i*3+2] = (byte)(color[2]*255.0);
		}
	}
}

/*
=================--------------------------------------------------------------------------------
R_DrawInlineBModel
=================
*/


void R_DrawInlineBModel (entity_t *e)
{
	int			i, k;
	cplane_t	*pplane;
	float		dot;
	msurface_t	*psurf;
	dlight_t	*lt;
	psurf = &currentmodel->surfaces[currentmodel->firstmodelsurface];


	for (i=0 ; i<currentmodel->nummodelsurfaces ; i++, psurf++)
	{
			// find which side of the face we are on
		pplane = psurf->plane;
		if ( pplane->type < 3 )
		dot = modelorg[pplane->type] - pplane->dist;
		else
		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;
		// cull the polygon
		if (dot > BACKFACE_EPSILON)
			psurf->visframe = r_framecount;
	}
	// calculate dynamic lighting for bmodel
	if ( !gl_flashblend->value )
	{
		lt = r_newrefdef.dlights;
		if (currententity->angles[0] || currententity->angles[1] || currententity->angles[2])
		{
			vec3_t temp;
			vec3_t forward, right, up;
			AngleVectors (currententity->angles, forward, right, up);
			for (k=0 ; k<r_newrefdef.num_dlights ; k++, lt++)
			{
				VectorSubtract (lt->origin, currententity->origin, temp);
				lt->origin[0] = DotProduct (temp, forward);
				lt->origin[1] = -DotProduct (temp, right);
				lt->origin[2] = DotProduct (temp, up);
				R_MarkLights (lt, 1<<k, currentmodel->nodes + currentmodel->firstnode);
				VectorAdd (temp, currententity->origin, lt->origin);
			}
		} 
		else
		{
			for (k=0 ; k<r_newrefdef.num_dlights ; k++, lt++)
			{
				VectorSubtract (lt->origin, currententity->origin, lt->origin);
				R_MarkLights (lt, 1<<k, currentmodel->nodes + currentmodel->firstnode);
				VectorAdd (lt->origin, currententity->origin, lt->origin);
			}
		}
	}

	psurf = &currentmodel->surfaces[currentmodel->firstmodelsurface];

	if ( currententity->flags & RF_TRANSLUCENT )
	{
		GLSTATE_ENABLE_BLEND
		qglColor4f (1,1,1,0.25);
		GL_TexEnv( GL_MODULATE );
	}

	//
	// draw texture
	//
	for (i=0 ; i<currentmodel->nummodelsurfaces ; i++, psurf++)
	{
	// find which side of the node we are on
		pplane = psurf->plane;

		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;

	// draw the polygon  + outline
				if (r_outline->value||
		    ((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			if (psurf->texinfo->flags & (SURF_TRANS33|SURF_TRANS66) )
			{	// add to the translucent chain
				psurf->texturechain = r_alpha_surfaces;
				r_alpha_surfaces = psurf;
				psurf->entity = e;
			}
			else if ( qglMTexCoord2fSGIS && !(psurf->flags & SURF_DRAWTURB))
			{
				GL_RenderLightmappedPoly( psurf );
			}
			else
			{
				GL_BuildVertexLight(psurf);

				GL_EnableMultitexture( false );
				R_RenderBrushPoly( psurf );
				GL_EnableMultitexture( true );
			}
		}
	}

	if ( !(currententity->flags & RF_TRANSLUCENT))
	{
		if ( !qglMTexCoord2fSGIS )
			R_BlendLightmaps ();
	}
	else
	{
		GLSTATE_DISABLE_BLEND
		qglColor4f (1,1,1,1);
		GL_TexEnv( GL_REPLACE );
	}
		// outline stuff
	if (r_outline->value){
		Surf_Outline(outlinechain);
		outlinechain = NULL;
	}
}

/*
=================
R_DrawBrushModel
=================
*/
void R_DrawBrushModel (entity_t *e)
{
	vec3_t		mins, maxs;
	int			i;
	qboolean	rotated;

	if (currentmodel->nummodelsurfaces == 0)
		return;

	currententity = e;
	gl_state.currenttextures[0] = gl_state.currenttextures[1] = -1;

	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		rotated = true;
		for (i=0 ; i<3 ; i++)
		{
			mins[i] = e->origin[i] - currentmodel->radius;
			maxs[i] = e->origin[i] + currentmodel->radius;
		}
	}
	else
	{
		rotated = false;
		VectorAdd (e->origin, currentmodel->mins, mins);
		VectorAdd (e->origin, currentmodel->maxs, maxs);
	}

	if (R_CullBox (mins, maxs))
		return;

	qglColor3f (1,1,1);
	memset (gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));

	VectorSubtract (r_newrefdef.vieworg, e->origin, modelorg);
	if (rotated)
	{
		vec3_t	temp;
		vec3_t	forward, right, up;

		VectorCopy (modelorg, temp);
		AngleVectors (e->angles, forward, right, up);
		modelorg[0] = DotProduct (temp, forward);
		modelorg[1] = -DotProduct (temp, right);
		modelorg[2] = DotProduct (temp, up);
	}

    qglPushMatrix ();
e->angles[0] = -e->angles[0];	// stupid quake bug
e->angles[2] = -e->angles[2];	// stupid quake bug
	R_RotateForEntity (e, true);
e->angles[0] = -e->angles[0];	// stupid quake bug
e->angles[2] = -e->angles[2];	// stupid quake bug

	GL_EnableMultitexture( true );

// Vic - begin
	SetLightingMode();
// Vic - end

	R_DrawInlineBModel (e);
	GL_EnableMultitexture( false );

	qglPopMatrix ();
}

/*
=============================================================

	WORLD MODEL

=============================================================
*/

/*
================
R_RecursiveWorldNode
================
*/
void R_RecursiveWorldNode (mnode_t *node)
{
	int			c, side, sidebit;
	cplane_t	*plane;
	msurface_t	*surf, **mark;
	mleaf_t		*pleaf;
	float		dot;
	image_t		*image;

	if (node->contents == CONTENTS_SOLID)
		return;		// solid

	if (node->visframe != r_visframecount)
		return;
	if (R_CullBox (node->minmaxs, node->minmaxs+3))
		return;
	
// if a leaf node, draw stuff
	if (node->contents != -1)
	{
		pleaf = (mleaf_t *)node;

		// check for door connected areas
		if (r_newrefdef.areabits)
		{
			if (! (r_newrefdef.areabits[pleaf->area>>3] & (1<<(pleaf->area&7)) ) )
				return;		// not visible
		}

		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		if (c)
		{
			do
			{
				(*mark)->visframe = r_framecount;
				mark++;
			} while (--c);
		}

		return;
	}

// node is just a decision point, so go down the apropriate sides

// find which side of the node we are on
	plane = node->plane;

	switch (plane->type)
	{
	case PLANE_X:
		dot = modelorg[0] - plane->dist;
		break;
	case PLANE_Y:
		dot = modelorg[1] - plane->dist;
		break;
	case PLANE_Z:
		dot = modelorg[2] - plane->dist;
		break;
	default:
		dot = DotProduct (modelorg, plane->normal) - plane->dist;
		break;
	}

	if (dot >= 0)
	{
		side = 0;
		sidebit = 0;
	}
	else
	{
		side = 1;
		sidebit = SURF_PLANEBACK;
	}

// recurse down the children, front side first
	R_RecursiveWorldNode (node->children[side]);

	//	if (r_outline.value)
	//	R_RecursiveWorldNode (node->children[!side], modelorg);


	// draw stuff
	for ( c = node->numsurfaces, surf = r_worldmodel->surfaces + node->firstsurface; c ; c--, surf++)
	{
		if (surf->visframe != r_framecount)
			continue;

		if ( (surf->flags & SURF_PLANEBACK) != sidebit )
			continue;		// wrong side


		if (surf->texinfo->flags & SURF_SKY)
		{	// just adds to visible sky bounds
			R_AddSkySurface (surf);
		}

		if (r_outline->value){
			surf->outline=outlinechain;
			outlinechain = surf; 
		}

		if (surf->texinfo->flags & (SURF_TRANS33|SURF_TRANS66)) // used to be else if
		{	// add to the translucent chain
			surf->texturechain = r_alpha_surfaces;
			r_alpha_surfaces = surf;
			surf->entity = NULL;
		}
		else
		{
			GL_BuildVertexLight(surf);

			if ( qglMTexCoord2fSGIS && !( surf->flags & SURF_DRAWTURB ) )
			{
				GL_RenderLightmappedPoly( surf );
				
				surf->specialchain = r_special_surfaces;
				r_special_surfaces = surf;
			}
			else
			{
				// the polygon is visible, so add it to the texture
				// sorted chain
				// FIXME: this is a hack for animation
				
				image = R_TextureAnimation (surf->texinfo);
				surf->texturechain = image->texturechain;
				image->texturechain = surf;
			
				surf->specialchain = r_special_surfaces;
				r_special_surfaces = surf;

				if (qglMTexCoord2fSGIS)
				R_DrawTriangleOutlines(surf);    // Guy: gl_showtris fix
			}
		}
	}

	// recurse down the back side
	// if (r_outline->value)
	R_RecursiveWorldNode (node->children[!side]);
}

/*
=============
R_DrawWorld
=============
*/
void R_DrawWorld (void)
{
	entity_t	ent;

	if (!r_drawworld->value)
		return;

	if ( r_newrefdef.rdflags & RDF_NOWORLDMODEL )
		return;

	currentmodel = r_worldmodel;

	VectorCopy (r_newrefdef.vieworg, modelorg);

	// auto cycle the world frame for texture animation
	memset (&ent, 0, sizeof(ent));
	ent.frame = (int)(r_newrefdef.time*2);
	currententity = &ent;

	gl_state.currenttextures[0] = gl_state.currenttextures[1] = -1;

	qglColor3f (1,1,1);
	memset (gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));
	R_ClearSkyBox ();

	if ( qglMTexCoord2fSGIS )
	{
		GL_EnableMultitexture( true );

		SetLightingMode ();

		R_RecursiveWorldNode (r_worldmodel->nodes);

		GL_EnableMultitexture( false );
	}
	else
	{
		R_RecursiveWorldNode (r_worldmodel->nodes);
	}

	/*
	** theoretically nothing should happen in the next two functions
	** if multitexture is enabled
	*/
	DrawTextureChains ();
	R_BlendLightmaps ();
	
	R_DrawSkyBox ();

	if (!qglMTexCoord2fSGIS)
    R_DrawTriangleOutlines(NULL);
}

/*
===============
R_MarkLeaves

Mark the leaves and nodes that are in the PVS for the current
cluster
===============
*/
void R_MarkLeaves (void)
{
	byte	*vis;
	byte	fatvis[MAX_MAP_LEAFS/8];
	mnode_t	*node;
	int		i, c;
	mleaf_t	*leaf;
	int		cluster;

	if (r_oldviewcluster == r_viewcluster && r_oldviewcluster2 == r_viewcluster2 && !r_novis->value && r_viewcluster != -1)
		return;

	// development aid to let you run around and see exactly where
	// the pvs ends
	if (gl_lockpvs->value)
		return;

	r_visframecount++;
	r_oldviewcluster = r_viewcluster;
	r_oldviewcluster2 = r_viewcluster2;

	if (r_novis->value || r_viewcluster == -1 || !r_worldmodel->vis)
	{
		// mark everything
		for (i=0 ; i<r_worldmodel->numleafs ; i++)
			r_worldmodel->leafs[i].visframe = r_visframecount;
		for (i=0 ; i<r_worldmodel->numnodes ; i++)
			r_worldmodel->nodes[i].visframe = r_visframecount;
		return;
	}

	vis = Mod_ClusterPVS (r_viewcluster, r_worldmodel);
	// may have to combine two clusters because of solid water boundaries
	if (r_viewcluster2 != r_viewcluster)
	{
		memcpy (fatvis, vis, (r_worldmodel->numleafs+7)/8);
		vis = Mod_ClusterPVS (r_viewcluster2, r_worldmodel);
		c = (r_worldmodel->numleafs+31)/32;
		for (i=0 ; i<c ; i++)
			((int *)fatvis)[i] |= ((int *)vis)[i];
		vis = fatvis;
	}
	
	for (i=0,leaf=r_worldmodel->leafs ; i<r_worldmodel->numleafs ; i++, leaf++)
	{
		cluster = leaf->cluster;
		if (cluster == -1)
			continue;
		if (vis[cluster>>3] & (1<<(cluster&7)))
		{
			node = (mnode_t *)leaf;
			do
			{
				if (node->visframe == r_visframecount)
					break;
				node->visframe = r_visframecount;
				node = node->parent;
			} while (node);
		}
	}

#if 0
	for (i=0 ; i<r_worldmodel->vis->numclusters ; i++)
	{
		if (vis[i>>3] & (1<<(i&7)))
		{
			node = (mnode_t *)&r_worldmodel->leafs[i];	// FIXME: cluster
			do
			{
				if (node->visframe == r_visframecount)
					break;
				node->visframe = r_visframecount;
				node = node->parent;
			} while (node);
		}
	}
#endif
}



/*
=============================================================================

  LIGHTMAP ALLOCATION

=============================================================================
*/

static void LM_InitBlock( void )
{
	memset( gl_lms.allocated, 0, sizeof( gl_lms.allocated ) );
}

static void LM_UploadBlock( qboolean dynamic )
{
	int texture;
	int height = 0;

	if ( dynamic )
	{
		texture = 0;
	}
	else
	{
		texture = gl_lms.current_lightmap_texture;
	}

	GL_Bind( gl_state.lightmap_textures + texture );
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	if ( dynamic )
	{
		int i;

		for ( i = 0; i < BLOCK_WIDTH; i++ )
		{
			if ( gl_lms.allocated[i] > height )
				height = gl_lms.allocated[i];
		}

		qglTexSubImage2D( GL_TEXTURE_2D, 
						  0,
						  0, 0,
						  BLOCK_WIDTH, height,
						  GL_LIGHTMAP_FORMAT,
						  GL_UNSIGNED_BYTE,
						  gl_lms.lightmap_buffer );
	}
	else
	{
		qglTexImage2D( GL_TEXTURE_2D, 
					   0, 
					   gl_lms.internal_format,
					   BLOCK_WIDTH, BLOCK_HEIGHT, 
					   0, 
					   GL_LIGHTMAP_FORMAT, 
					   GL_UNSIGNED_BYTE, 
					   gl_lms.lightmap_buffer );
		if ( ++gl_lms.current_lightmap_texture == MAX_LIGHTMAPS )
			ri.Sys_Error( ERR_DROP, "LM_UploadBlock() - MAX_LIGHTMAPS exceeded\n" );
	}
}

// returns a texture number and the position inside it
static qboolean LM_AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;

	best = BLOCK_HEIGHT;

	for (i=0 ; i<BLOCK_WIDTH-w ; i++)
	{
		best2 = 0;

		for (j=0 ; j<w ; j++)
		{
			if (gl_lms.allocated[i+j] >= best)
				break;
			if (gl_lms.allocated[i+j] > best2)
				best2 = gl_lms.allocated[i+j];
		}
		if (j == w)
		{	// this is a valid spot
			*x = i;
			*y = best = best2;
		}
	}

	if (best + h > BLOCK_HEIGHT)
		return false;

	for (i=0 ; i<w ; i++)
		gl_lms.allocated[*x + i] = best + h;

	return true;
}

/*
================
GL_BuildPolygonFromSurface
================
*/
void GL_BuildPolygonFromSurface(msurface_t *fa)
{
	int			i, lindex, lnumverts;
	medge_t		*pedges, *r_pedge;
	int			vertpage;
	float		*vec;
	float		s, t;
	glpoly_t	*poly;
	vec3_t		total;

// reconstruct the polygon
	pedges = currentmodel->edges;
	lnumverts = fa->numedges;
	vertpage = 0;

	VectorClear (total);
	//
	// draw texture
	//
	poly = Hunk_Alloc (sizeof(glpoly_t) + (lnumverts-4) * VERTEXSIZE*sizeof(float));
	poly->next = fa->polys;
	poly->flags = fa->flags;
	fa->polys = poly;
	poly->numverts = lnumverts;

	for (i=0 ; i<lnumverts ; i++)
	{
		lindex = currentmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &pedges[lindex];
			vec = currentmodel->vertexes[r_pedge->v[0]].position;
		}
		else
		{
			r_pedge = &pedges[-lindex];
			vec = currentmodel->vertexes[r_pedge->v[1]].position;
		}
		/*
		s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s /= fa->texinfo->image->width;

		t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t /= fa->texinfo->image->height;
		*/
		s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s /= fa->texinfo->texWidth;

		t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t /= fa->texinfo->texHeight;

		VectorAdd (total, vec, total);
		VectorCopy (vec, poly->verts[i]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;

		//
		// lightmap texture coordinates
		//
		s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s -= fa->texturemins[0];
		s += fa->light_s*16;
		s += 8;
		s /= BLOCK_WIDTH*16; //fa->texinfo->texture->width;

		t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t -= fa->texturemins[1];
		t += fa->light_t*16;
		t += 8;
		t /= BLOCK_HEIGHT*16; //fa->texinfo->texture->height;

		poly->verts[i][5] = s;
		poly->verts[i][6] = t;
	}

	poly->numverts = lnumverts;

}

/*
========================
GL_CreateSurfaceLightmap
========================
*/
void GL_CreateSurfaceLightmap (msurface_t *surf)
{
	int		smax, tmax;
	byte	*base;

	//ill check, but they're not gonna make it...
	if (surf->flags & (SURF_DRAWSKY|SURF_DRAWTURB))
		return;

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;

	if ( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ) )
	{
		LM_UploadBlock( false );
		LM_InitBlock();
		if ( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ) )
		{
			ri.Sys_Error( ERR_FATAL, "Consecutive calls to LM_AllocBlock(%d,%d) failed\n", smax, tmax );
		}
	}

	surf->lightmaptexturenum = gl_lms.current_lightmap_texture;

	base = gl_lms.lightmap_buffer;
	base += (surf->light_t * BLOCK_WIDTH + surf->light_s) * LIGHTMAP_BYTES;

	R_SetCacheState( surf );
	R_BuildLightMap (surf, base, BLOCK_WIDTH*LIGHTMAP_BYTES);
}

void GL_CreateSurfaceStainmap (msurface_t *surf)
{
	int		size, smax, tmax;

	if (surf->flags & (SURF_DRAWSKY|SURF_DRAWTURB))
		return;

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	size = smax*tmax*3*sizeof(byte);
	
	surf->stains = malloc(size);
	memset(surf->stains, 255, size);
}

void GL_CreateVertexLightmap (msurface_t *surf)
{
	glpoly_t *poly;
	int		size;
	for (poly=surf->polys ; poly ; poly=poly->next)
	{
		size = sizeof(byte)*3*(poly->numverts);
		poly->vertexlight = Hunk_Alloc(size);
		poly->vertexlightbase = Hunk_Alloc(size);
		memset(poly->vertexlight, 0, size);
		memset(poly->vertexlightbase, 0, size);
		poly->vertexlightset = false;
	}	
}


void GL_FindPolyCenters (msurface_t *surf)
{
	int i;
	float *v;
	vec3_t average;
	glpoly_t *poly;

	for (poly=surf->polys ; poly ; poly=poly->next)
	{
		VectorClear(average);

		for (i=0,v=poly->verts[0] ; i<poly->numverts ; i++, v+=VERTEXSIZE)
			VectorAdd(average, v, average);

		VectorScale(average, 1.0/(float)poly->numverts, poly->center);
	}
}

/*
==================
GL_BeginBuildingLightmaps

==================
*/

/* PixelInternalFormat */
/*      GL_ALPHA4 */
/*      GL_ALPHA8 */
/*      GL_ALPHA12 */
/*      GL_ALPHA16 */
/*      GL_LUMINANCE4 */
/*      GL_LUMINANCE8 */
/*      GL_LUMINANCE12 */
/*      GL_LUMINANCE16 */
/*      GL_LUMINANCE4_ALPHA4 */
/*      GL_LUMINANCE6_ALPHA2 */
/*      GL_LUMINANCE8_ALPHA8 */
/*      GL_LUMINANCE12_ALPHA4 */
/*      GL_LUMINANCE12_ALPHA12 */
/*      GL_LUMINANCE16_ALPHA16 */
/*      GL_INTENSITY */
/*      GL_INTENSITY4 */
/*      GL_INTENSITY8 */
/*      GL_INTENSITY12 */
/*      GL_INTENSITY16 */
/*      GL_R3_G3_B2 */
/*      GL_RGB4 */
/*      GL_RGB5 */
/*      GL_RGB8 */
/*      GL_RGB10 */
/*      GL_RGB12 */
/*      GL_RGB16 */
/*      GL_RGBA2 */
/*      GL_RGBA4 */
/*      GL_RGB5_A1 */
/*      GL_RGBA8 */
/*      GL_RGB10_A2 */
/*      GL_RGBA12 */
/*      GL_RGBA16 */


void GL_BeginBuildingLightmaps (model_t *m)
{
	static lightstyle_t	lightstyles[MAX_LIGHTSTYLES];
	int				i;
	unsigned		dummy[128*128];

	memset( gl_lms.allocated, 0, sizeof(gl_lms.allocated) );

	r_framecount = 1;		// no dlightcache

	GL_EnableMultitexture( true );
	GL_SelectTexture( GL_TEXTURE1);

	/*
	** setup the base lightstyles so the lightmaps won't have to be regenerated
	** the first time they're seen
	*/
	for (i=0 ; i<MAX_LIGHTSTYLES ; i++)
	{
		lightstyles[i].rgb[0] = 1;
		lightstyles[i].rgb[1] = 1;
		lightstyles[i].rgb[2] = 1;
		lightstyles[i].white = 3;
	}
	r_newrefdef.lightstyles = lightstyles;

	if (!gl_state.lightmap_textures)
	{
		gl_state.lightmap_textures	= TEXNUM_LIGHTMAPS;
//		gl_state.lightmap_textures	= gl_state.texture_extension_number;
//		gl_state.texture_extension_number = gl_state.lightmap_textures + MAX_LIGHTMAPS;
	}

	gl_lms.current_lightmap_texture = 1;

	/*
	** if mono lightmaps are enabled and we want to use alpha
	** blending (a,1-a) then we're likely running on a 3DLabs
	** Permedia2.  In a perfect world we'd use a GL_ALPHA lightmap
	** in order to conserve space and maximize bandwidth, however 
	** this isn't a perfect world.
	**
	** So we have to use alpha lightmaps, but stored in GL_RGBA format,
	** which means we only get 1/16th the color resolution we should when
	** using alpha lightmaps.  If we find another board that supports
	** only alpha lightmaps but that can at least support the GL_ALPHA
	** format then we should change this code to use real alpha maps.
	*/
	if ( toupper( gl_monolightmap->string[0] ) == 'A' )
	{
		gl_lms.internal_format = gl_tex_alpha_format;
	}
	/*
	** try to do hacked colored lighting with a blended texture
	*/
	else if ( toupper( gl_monolightmap->string[0] ) == 'C' )
	{
		gl_lms.internal_format = gl_tex_alpha_format;
	}
	else if ( toupper( gl_monolightmap->string[0] ) == 'I' )
	{
		gl_lms.internal_format = GL_INTENSITY8;
	}
	else if ( toupper( gl_monolightmap->string[0] ) == 'L' ) 
	{
		gl_lms.internal_format = GL_LUMINANCE8;
	}
	else
	{
		gl_lms.internal_format = gl_tex_solid_format;
	}

	/*
	** initialize the dynamic lightmap texture
	*/
	GL_Bind( gl_state.lightmap_textures + 0 );
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	qglTexImage2D( GL_TEXTURE_2D, 
				   0, 
				   gl_lms.internal_format,
				   BLOCK_WIDTH, BLOCK_HEIGHT, 
				   0, 
				   GL_LIGHTMAP_FORMAT, 
				   GL_UNSIGNED_BYTE, 
				   dummy );
}

/*
=======================
GL_EndBuildingLightmaps
=======================
*/
void GL_EndBuildingLightmaps (void)
{
	LM_UploadBlock( false );
	GL_EnableMultitexture( false );
}

