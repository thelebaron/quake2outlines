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
// gl_mesh.c: triangle model functions

#include "gl_local.h"
#include "vlights.h"

/*
=============================================================

  ALIAS MODELS

=============================================================
*/

#define NUMVERTEXNORMALS	162

float	r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

typedef float vec4_t[4];

static	vec4_t	s_lerped[MAX_VERTS];

extern	int				r_viewport[4];
extern	vec3_t			lightspot;

vec3_t	viewdir;
vec3_t	lightdir;
vec3_t	shadevector;
vec3_t	shadelight;

#define MAX_MODEL_DLIGHTS 128
m_dlight_t model_dlights[MAX_MODEL_DLIGHTS];
int model_dlights_num;

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16
float	r_avertexnormal_dots[SHADEDOT_QUANT][256] =
#include "anormtab.h"
;

float	*shadedots = r_avertexnormal_dots[0];
void vectoangles (vec3_t value1, vec3_t angles);

void GL_LerpVerts( int nverts, dtrivertx_t *v, dtrivertx_t *ov, dtrivertx_t *verts, float *lerp, float move[3], float frontv[3], float backv[3], float normalscale )
{
	int i;

	for (i=0 ; i < nverts; i++, v++, ov++, lerp+=4 )
	{
		float *normal = r_avertexnormals[verts[i].lightnormalindex];

		lerp[0] = move[0] + ov->v[0]*backv[0] + v->v[0]*frontv[0] + normal[0] * normalscale;
		lerp[1] = move[1] + ov->v[1]*backv[1] + v->v[1]*frontv[1] + normal[1] * normalscale;
		lerp[2] = move[2] + ov->v[2]*backv[2] + v->v[2]*frontv[2] + normal[2] * normalscale; 
	}
}

/*============================
Discoloda 's cellshading outline routine
=============================*/

#define OUTLINEDROPOFF 1000.0 //distance away for it to stop
void GL_DrawOutLine (dmdl_t *paliashdr, int posenum, float width, qboolean mirrormodel) 
{
	dtrivertx_t	*verts;
	int		*order;
	int		count;
	float	strength, len;
	daliasframe_t	*frame;
	qboolean translucent = false;

	frame = (daliasframe_t *)((byte *)paliashdr + paliashdr->ofs_frames 
		+ currententity->frame * paliashdr->framesize);
	verts = frame->verts;

	order = (int *)((byte *)paliashdr + paliashdr->ofs_glcmds);

	if (currententity->flags & RF_TRANSLUCENT)
		return;

	qglPolygonMode (GL_FRONT_AND_BACK, GL_LINE);
	if (mirrormodel)
		qglCullFace (GL_FRONT);
	else
		qglCullFace (GL_BACK);

	//this makes long distance make line smaller, but never gone...
	{	
		vec3_t length;
		VectorSubtract(r_newrefdef.vieworg, currententity->origin, length);
		len = VectorNormalize(length);

		strength = (OUTLINEDROPOFF-len)/OUTLINEDROPOFF;
		if (strength>1)	strength=1;
		if (strength<0)	strength=0;
	}

	qglColor4f (0,0,0,1);
	qglLineWidth(width*strength);

	if (translucent)
	{
		qglDisable (GL_TEXTURE_2D);
		GLSTATE_ENABLE_BLEND
	}
	//Now Draw...
	while (1)
	{
		// get the vertex count and primitive type
		count = *order++;
		if (!count)
			break;		// done

		if (count < 0)
		{
			count = -count;
			qglBegin (GL_TRIANGLE_FAN);
		}
		else
			qglBegin (GL_TRIANGLE_STRIP);

		{
			do
			{
				qglVertex3fv (s_lerped[order[2]]);
				order += 3;
			} while (--count);
		}
		qglEnd ();
	}

	if (translucent)
	{
		qglEnable (GL_TEXTURE_2D);
		GLSTATE_DISABLE_BLEND
	}

	if (!mirrormodel)
		qglCullFace(GL_FRONT);

	qglPolygonMode (GL_FRONT_AND_BACK, GL_FILL);

	qglLineWidth(1);
}

/*
=============
GL_DrawAliasFrameLerp

interpolates between two frames and origins
FIXME: batch lerp all vertexes
=============
*/

float  mirrorValue (float value, qboolean mirrormodel)
{
	if (mirrormodel)
	{
		if (value>1)
			return 0;
		else if (value<0)
			return 1;
		else
			return 1-value;
	}
	return value;
}

float calcEntAlpha (float alpha, vec3_t point)
{
	float newAlpha;
	vec3_t vert_len;

	newAlpha = alpha;

	if (!(currententity->renderfx&RF2_CAMERAMODEL) || !(currententity->flags&RF_TRANSLUCENT))
	{
		if (newAlpha<0) newAlpha = 0;
		if (newAlpha>1) newAlpha = 1;
		return newAlpha;
	}

	VectorSubtract(r_newrefdef.vieworg, point, vert_len);
	newAlpha *= VectorLength(vert_len)/cl_3dcam_dist->value;
	if (newAlpha>alpha)	newAlpha = alpha;

	if (newAlpha<0) newAlpha = 0;
	if (newAlpha>1) newAlpha = 1;

	return newAlpha;
}

void capColorVec(vec3_t color)
{
	int i;

	for (i=0;i<3;i++)
	{
		if (color[i]>1) color[i] = 1;
		if (color[i]<0) color[i] = 0;
	}
}

void SetVertexOverbrights (qboolean toggle)
{
	if (!r_overbrightbits->value || !gl_config.mtexcombine)
		return;

	if (toggle)//turn on
	{
		qglTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);
		qglTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE);
		qglTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, r_overbrightbits->value);
		qglTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_MODULATE);
		
		GL_TexEnv( GL_COMBINE_EXT );
	}
	else //turn off
	{
		GL_TexEnv( GL_MODULATE );
		qglTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 1);
	}
}

void SetShellBlend (qboolean toggle)
{
	if (toggle)//turn on
	{
		if (currententity->flags & RF_TRANSLUCENT)
			GLSTATE_ENABLE_BLEND

		//shells never use linked skins!
		if (!currententity->skin)
		{
			if (currententity->flags & RF_TRANSLUCENT)
				GL_Stencil(true);

			qglDisable(GL_TEXTURE_2D);
		}
		else
			GL_Bind(currententity->skin->texnum);
	}
	else //turn off
	{
		if (currententity->flags & RF_TRANSLUCENT)
			GLSTATE_DISABLE_BLEND

		if (!currententity->skin)
		{
			if (currententity->flags & RF_TRANSLUCENT)
				GL_Stencil(false);

			qglEnable( GL_TEXTURE_2D );
		}
	}
}

void lightAliasModel (vec3_t baselight, dtrivertx_t *verts, dtrivertx_t *ov, float backlerp, vec3_t lightOut)
{
	int i;
	float l;


	if (r_model_lightlerp->value)
	{
		l = 2.0 * VLight_LerpLight( verts->lightnormalindex,
								ov->lightnormalindex,
								backlerp, lightdir, currententity->angles, false );

		VectorScale(baselight, l, lightOut);
		
		if (model_dlights_num)
			for (i=0;i<model_dlights_num;i++)
			{
			

				l = 2.0*VLight_LerpLight( verts->lightnormalindex,
										ov->lightnormalindex,
										backlerp, model_dlights[i].direction, currententity->angles, true );

				VectorMA(lightOut, l, model_dlights[i].color, lightOut);
			}

	}
	else
	{
		l = shadedots[verts->lightnormalindex];
		VectorScale(baselight, l, lightOut);
	}

	for (i=0;i<3;i++)
	{
		if (lightOut[i]<0) lightOut[i] = 0;
		if (lightOut[i]>1) lightOut[i] = 1;
	}
}

void GL_DrawAliasFrameLerpShell (dmdl_t *paliashdr, float backlerp)
{
	daliasframe_t	*frame, *oldframe;
	dtrivertx_t	*v, *ov, *verts;
	int		*order, *tmp_order;
	int		count;
	float	frontlerp;
	float	alpha;
	vec3_t	move, delta, vectors[3];
	vec3_t	frontv, backv, viewdir, lightcolor;
	int		i, tmp_count;
	int		index_xyz;
	int		va = 0; 
	float mode, basealpha;
	float	*lerp;
	qboolean depthmaskrscipt = false, is_trans = false;
	rscript_t *rs = NULL;
	float	txm,tym;
	float *normal;
	rs_stage_t *stage, *laststage = NULL;

	frame = (daliasframe_t *)((byte *)paliashdr + paliashdr->ofs_frames 
		+ currententity->frame * paliashdr->framesize);
	verts = v = frame->verts;

	oldframe = (daliasframe_t *)((byte *)paliashdr + paliashdr->ofs_frames 
		+ currententity->oldframe * paliashdr->framesize);
	ov = oldframe->verts;

	order = (int *)((byte *)paliashdr + paliashdr->ofs_glcmds);


	if (currententity->flags&RF_VIEWERMODEL)
		return;

	if (currententity->flags & RF_TRANSLUCENT)
		basealpha = alpha = currententity->alpha;
	else
		basealpha = alpha = 1.0;

	SetShellBlend (true);

	frontlerp = 1.0 - backlerp;

	// move should be the delta back to the previous frame * backlerp
	VectorSubtract (currententity->oldorigin, currententity->origin, delta);
	AngleVectors (currententity->angles, vectors[0], vectors[1], vectors[2]);

	VectorSubtract(currententity->origin, r_newrefdef.vieworg, viewdir);
	VectorNormalize ( viewdir );

	SetVertexOverbrights( true );

	qglEnableClientState( GL_COLOR_ARRAY );

	if (r_shaders->value)
		rs=(rscript_t *)currententity->script;

	move[0] = DotProduct (delta, vectors[0]);	// forward
	move[1] = -DotProduct (delta, vectors[1]);	// left
	move[2] = DotProduct (delta, vectors[2]);	// up

	VectorAdd (move, oldframe->translate, move);

	for (i=0 ; i<3 ; i++)
	{
		move[i] = backlerp*move[i] + frontlerp*frame->translate[i];
	}

	for (i=0 ; i<3 ; i++)
	{
		frontv[i] = frontlerp*frame->scale[i];
		backv[i] = backlerp*oldframe->scale[i];
	}

	lerp = s_lerped[0];

	if (currententity->flags & RF_WEAPONMODEL)
		GL_LerpVerts( paliashdr->num_xyz, v, ov, verts, lerp, move, frontv, backv, POWERSUIT_SCALE*0.2 );
	else
		GL_LerpVerts( paliashdr->num_xyz, v, ov, verts, lerp, move, frontv, backv, POWERSUIT_SCALE*0.75 );

	qglScalef( currententity->scale, currententity->scale, currententity->scale );

	if (!rs) 
	{
		while (1)
		{
			// get the vertex count and primitive type
			count = *order++;
			va=0;
			if (!count)
				break;		// done
			if (count < 0) 
			{
				count = -count;
				mode=GL_TRIANGLE_FAN;
			} 
			else
				mode=GL_TRIANGLE_STRIP;

			do 
			{
				// texture coordinates come from the draw list
				index_xyz = order[2];

				VA_SetElem2(tex_array[va],((float *)order)[0], ((float *)order)[1]);
				VA_SetElem3(vert_array[va],s_lerped[index_xyz][0],s_lerped[index_xyz][1],s_lerped[index_xyz][2]);
				VA_SetElem4(col_array[va],shadelight[0], shadelight[1], shadelight[2], calcEntAlpha(alpha, s_lerped[index_xyz]));
				va++;
				order += 3;
			} while (--count);

			qglDrawArrays(mode,0,va);
		}
	}
	else 
	{
		RS_ReadyScript(rs);

		if (rs->stage && rs->stage->has_alpha)
		{
			is_trans = true;
			depthmaskrscipt = true;
		}

		if (depthmaskrscipt)
			qglDepthMask(false);

		while (1)
		{
			// get the vertex count and primitive type
			count = *order++;
			if (!count)
				break;		// done
			if (count < 0) 
			{
				count = -count;
				mode=GL_TRIANGLE_FAN;
			} else
				mode=GL_TRIANGLE_STRIP;

			stage=rs->stage;
			tmp_count=count;
			tmp_order=order;

			while (stage) 
			{
				count=tmp_count;
				order=tmp_order;
				va=0;

				if (stage->detail && !rs_detail->value)
					continue;

				if (stage->colormap.enabled)
					qglDisable (GL_TEXTURE_2D);
				else if (stage->dynamic)
					GL_Bind (r_dynamicimage->texnum);
				else if (stage->anim_count)
					GL_Bind(RS_Animate(stage));
				else
					GL_Bind (stage->texture->texnum);

				if (stage->blendfunc.blend)
				{
					GL_BlendFunction(stage->blendfunc.source,stage->blendfunc.dest);
					GLSTATE_ENABLE_BLEND
				}
				else if (basealpha==1.0f)
				{
					GLSTATE_DISABLE_BLEND
				}
				else
				{
					GLSTATE_ENABLE_BLEND
					GL_BlendFunction(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
					alpha = basealpha;
				}

				if (stage->alphashift.min || stage->alphashift.speed)
				{
					if (!stage->alphashift.speed && stage->alphashift.min > 0)
					{
						alpha=basealpha*stage->alphashift.min;
					} 
					else if (stage->alphashift.speed)
					{
						alpha=basealpha*sin(rs_realtime * stage->alphashift.speed);
						if (alpha < 0) alpha=-alpha*basealpha;
						if (alpha > stage->alphashift.max) alpha=basealpha*stage->alphashift.max;
						if (alpha < stage->alphashift.min) alpha=basealpha*stage->alphashift.min;
					}
				} 
				else
					alpha=basealpha;

				if (stage->alphamask) 
				{
					GLSTATE_ENABLE_ALPHATEST
				} 
				else 
				{
					GLSTATE_DISABLE_ALPHATEST
				}

				do 
				{
					float os = ((float *)order)[0];
					float ot = ((float *)order)[1];
					vec3_t normal;
					int k;

					// texture coordinates come from the draw list
					index_xyz = order[2];

					for (k=0;k<3;k++)
						normal[k] = r_avertexnormals[verts[index_xyz].lightnormalindex][k] + 
						( r_avertexnormals[ov[index_xyz].lightnormalindex][k] - 
						r_avertexnormals[verts[index_xyz].lightnormalindex][k] ) * backlerp;
					VectorNormalize ( normal );

					if (stage->envmap)
					{
						vec3_t envmapvec, normal;

						VectorAdd(currententity->origin, s_lerped[index_xyz], envmapvec);
						RS_SetEnvmap (envmapvec, &os, &ot);

						os-=DotProduct (normal , vectors[1] );
						ot+=DotProduct (normal, vectors[2] );
					}

					RS_SetTexcoords2D (stage, &os, &ot);

					VA_SetElem2(tex_array[va], os, ot);
					VA_SetElem3(vert_array[va],s_lerped[index_xyz][0],s_lerped[index_xyz][1],s_lerped[index_xyz][2]);

					{
						float red = 1, green = 1, blue = 1, nAlpha;
						
						nAlpha = RS_AlphaFuncAlias (stage->alphafunc, 
							calcEntAlpha(alpha, s_lerped[index_xyz]), normal, s_lerped[index_xyz]);

						if (stage->lightmap)
						{
							red = shadelight[0];
							green = shadelight[1];
							blue = shadelight[2];
						}
						if (stage->colormap.enabled)
						{
							red *= stage->colormap.red/255.0;
							green *= stage->colormap.green/255.0;
							blue *= stage->colormap.blue/255.0;
						}

						VA_SetElem4(col_array[va], red, green, blue, nAlpha);
					}

					va++;
					order += 3;
				} 
				while (--count);

				qglDrawArrays(mode,0,va);

				qglColor4f(1,1,1,1);
				if (stage->colormap.enabled)
					qglEnable (GL_TEXTURE_2D);
				GLSTATE_DISABLE_ALPHATEST
				GLSTATE_DISABLE_BLEND
				GLSTATE_DISABLE_TEXGEN

				laststage = stage;
				stage=stage->next;
			}
		}

		if (depthmaskrscipt)
			qglDepthMask(true);
	}
		
	SetShellBlend (false);
	
	SetVertexOverbrights( false );

	qglDisableClientState( GL_COLOR_ARRAY );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
}

#define CELTEXMIN 0.5/32.0
#define CELTEXMAX 31.5/32.0

float celTexCoord (dtrivertx_t *verts, dtrivertx_t *ov, float backlerp)
{
	float	shadeCoord;
	int		i, highest = 0;
	vec3_t	lightcolor;

	lightAliasModel (shadelight, verts, ov, backlerp, lightcolor);

	for (i=0; i<3; i++)
		if (lightcolor[i]>lightcolor[highest])
			highest = i;

	capColorVec(lightcolor);

	shadeCoord = lightcolor[highest];
	if (shadeCoord<CELTEXMIN) shadeCoord = CELTEXMIN;
	if (shadeCoord>CELTEXMAX) shadeCoord = CELTEXMAX;

	return shadeCoord;
}

void GL_DrawAliasFrameLerp (dmdl_t *paliashdr, float backlerp, qboolean mirrormodel)
{
	float 	l;
	daliasframe_t	*frame, *oldframe;
	dtrivertx_t	*v, *ov, *verts;
	int		*order, *startorder, *tmp_order;
	int		count;
	float	frontlerp;
	float	alpha, basealpha;
	vec3_t	move, delta, vectors[3];
	vec3_t	frontv, backv;
	int		i, tmp_count;
	int		index_xyz;
	float	*lerp;
	int		va = 0; float mode;
	vec3_t lightcolor;
	qboolean depthmaskrscipt = false, is_trans = false;
	rscript_t *rs = NULL;
	float	txm,tym;
	float *normal;
	rs_stage_t *stage, *laststage = NULL;

	frame = (daliasframe_t *)((byte *)paliashdr + paliashdr->ofs_frames 
		+ currententity->frame * paliashdr->framesize);
	verts = v = frame->verts;

	oldframe = (daliasframe_t *)((byte *)paliashdr + paliashdr->ofs_frames 
		+ currententity->oldframe * paliashdr->framesize);
	ov = oldframe->verts;

	startorder = order = (int *)((byte *)paliashdr + paliashdr->ofs_glcmds);

//	glTranslatef (frame->translate[0], frame->translate[1], frame->translate[2]);
//	glScalef (frame->scale[0], frame->scale[1], frame->scale[2]);

	if (currententity->flags & RF_TRANSLUCENT)
	{
		is_trans = true;
		basealpha = alpha = currententity->alpha;
	}
	else
		basealpha = alpha = 1.0;

	frontlerp = 1.0 - backlerp;

	// move should be the delta back to the previous frame * backlerp
	VectorSubtract (currententity->oldorigin, currententity->origin, delta);
	AngleVectors (currententity->angles, vectors[0], vectors[1], vectors[2]);

	move[0] = DotProduct (delta, vectors[0]);	// forward
	move[1] = -DotProduct (delta, vectors[1]);	// left
	move[2] = DotProduct (delta, vectors[2]);	// up

	VectorAdd (move, oldframe->translate, move);

	for (i=0 ; i<3 ; i++)
	{
		move[i] = backlerp*move[i] + frontlerp*frame->translate[i];
	}

	for (i=0 ; i<3 ; i++)
	{
		frontv[i] = frontlerp*frame->scale[i];
		backv[i] = backlerp*oldframe->scale[i];
	}

	lerp = s_lerped[0];

	
	GL_LerpVerts( paliashdr->num_xyz, v, ov, verts, lerp, move, frontv, backv, 0 );
	qglScalef( currententity->scale, currententity->scale, currententity->scale );
	
	if (currententity->flags&RF_VIEWERMODEL)
		return;

	VectorSubtract(currententity->origin, lightspot, lightdir);
	VectorNormalize ( lightdir );

	VectorSubtract(currententity->origin, r_newrefdef.vieworg, viewdir);
	VectorNormalize ( viewdir );

	SetVertexOverbrights( true );

	qglEnableClientState( GL_COLOR_ARRAY );

	if (r_shaders->value)
		rs=(rscript_t *)currententity->script;


	//set base light color
	VectorCopy(shadelight, lightcolor);
	for (i=0;i<model_dlights_num;i++)
		VectorAdd(lightcolor, model_dlights[i].color, lightcolor);
	VectorNormalize(lightcolor);


	if (!rs) 
	{
		while (1)
		{
			// get the vertex count and primitive type
			count = *order++;
			va=0;
			if (!count)
				break;		// done
			if (count < 0) 
			{
				count = -count;
				mode=GL_TRIANGLE_FAN;
			} 
			else
				mode=GL_TRIANGLE_STRIP;

			do 
			{
				// texture coordinates come from the draw list
				index_xyz = order[2];
				l = shadedots[verts[index_xyz].lightnormalindex];

				if (!r_celshading->value || is_trans)
					lightAliasModel (shadelight, &verts[index_xyz], &ov[index_xyz], backlerp, lightcolor);

				VA_SetElem2(tex_array[va],((float *)order)[0], ((float *)order)[1]);
				VA_SetElem3(vert_array[va],s_lerped[index_xyz][0],s_lerped[index_xyz][1],s_lerped[index_xyz][2]);
				VA_SetElem4(col_array[va],lightcolor[0], lightcolor[1], lightcolor[2], calcEntAlpha(alpha, s_lerped[index_xyz]));
				va++;
				order += 3;
			} while (--count);

			qglDrawArrays(mode,0,va);
		}
	}
	else 
	{
		RS_ReadyScript(rs);

		if (rs->stage && rs->stage->has_alpha)
		{
			is_trans = true;
			depthmaskrscipt = true;
		}

		if (depthmaskrscipt)
			qglDepthMask(false);

		while (1)
		{
			// get the vertex count and primitive type
			count = *order++;
			if (!count)
				break;		// done
			if (count < 0) 
			{
				count = -count;
				mode=GL_TRIANGLE_FAN;
			} else
				mode=GL_TRIANGLE_STRIP;

			stage=rs->stage;
			tmp_count=count;
			tmp_order=order;

			while (stage) 
			{
				count=tmp_count;
				order=tmp_order;
				va=0;

				if (stage->detail && !rs_detail->value)
					continue;

				if (stage->colormap.enabled)
					qglDisable (GL_TEXTURE_2D);
				else if (stage->dynamic)
					GL_Bind (r_dynamicimage->texnum);
				else if (stage->anim_count)
					GL_Bind(RS_Animate(stage));
				else
					GL_Bind (stage->texture->texnum);

				if (stage->blendfunc.blend)
				{
					GL_BlendFunction(stage->blendfunc.source,stage->blendfunc.dest);
					GLSTATE_ENABLE_BLEND
				}
				else if (basealpha==1.0f)
				{
					GLSTATE_DISABLE_BLEND
				}
				else
				{
					GLSTATE_ENABLE_BLEND
					GL_BlendFunction(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
					alpha = basealpha;
				}

				if (stage->alphashift.min || stage->alphashift.speed)
				{
					if (!stage->alphashift.speed && stage->alphashift.min > 0)
					{
						alpha=basealpha*stage->alphashift.min;
					} 
					else if (stage->alphashift.speed)
					{
						alpha=basealpha*sin(rs_realtime * stage->alphashift.speed);
						if (alpha < 0) alpha=-alpha*basealpha;
						if (alpha > stage->alphashift.max) alpha=basealpha*stage->alphashift.max;
						if (alpha < stage->alphashift.min) alpha=basealpha*stage->alphashift.min;
					}
				} 
				else
					alpha=basealpha;

				if (stage->alphamask) 
				{
					GLSTATE_ENABLE_ALPHATEST
				} 
				else 
				{
					GLSTATE_DISABLE_ALPHATEST
				}

				do 
				{
					float os = ((float *)order)[0];
					float ot = ((float *)order)[1];
					vec3_t normal;
					int k;

					// texture coordinates come from the draw list
					index_xyz = order[2];
					l = shadedots[verts[index_xyz].lightnormalindex];
	
					for (k=0;k<3;k++)
						normal[k] = r_avertexnormals[verts[index_xyz].lightnormalindex][k] + 
						( r_avertexnormals[ov[index_xyz].lightnormalindex][k] - 
						r_avertexnormals[verts[index_xyz].lightnormalindex][k] ) * backlerp;
					VectorNormalize ( normal );

					if (stage->envmap)
					{
						vec3_t envmapvec;

						VectorAdd(currententity->origin, s_lerped[index_xyz], envmapvec);
						RS_SetEnvmap (envmapvec, &os, &ot);

						os -= DotProduct (normal , vectors[1] );
						ot += DotProduct (normal, vectors[2] );
					}

					RS_SetTexcoords2D(stage, &os, &ot);

					VA_SetElem2(tex_array[va], os, ot);
					VA_SetElem3(vert_array[va],s_lerped[index_xyz][0],s_lerped[index_xyz][1],s_lerped[index_xyz][2]);

					{
						float red = 1, green = 1, blue = 1, nAlpha;
						
						nAlpha = RS_AlphaFuncAlias (stage->alphafunc, 
							calcEntAlpha(alpha, s_lerped[index_xyz]), normal, s_lerped[index_xyz]);

						if (stage->lightmap)
						{
							if (!r_celshading->value || is_trans)
								lightAliasModel (shadelight, &verts[index_xyz], &ov[index_xyz], backlerp, lightcolor);

							red = lightcolor[0];
							green = lightcolor[1];
							blue = lightcolor[2];
						}
						if (stage->colormap.enabled)
						{
							red *= stage->colormap.red/255.0;
							green *= stage->colormap.green/255.0;
							blue *= stage->colormap.blue/255.0;
						}

						VA_SetElem4(col_array[va], red, green, blue, nAlpha);
					}

					va++;
					order += 3;
				} 
				while (--count);

				qglDrawArrays(mode,0,va);

				qglColor4f(1,1,1,1);
				if (stage->colormap.enabled)
					qglEnable (GL_TEXTURE_2D);

				laststage = stage;
				stage=stage->next;
			}
		}

		if (depthmaskrscipt)
			qglDepthMask(true);
	}

	GLSTATE_DISABLE_ALPHATEST
	GLSTATE_DISABLE_BLEND
	GLSTATE_DISABLE_TEXGEN

	order = startorder;

	if (r_celshading->value && !is_trans)
	{
		qglDepthMask (false);
		qglEnable( GL_TEXTURE_2D );	
		GLSTATE_ENABLE_BLEND
		GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		GL_Bind(r_celtexture->texnum);

		VectorCopy(shadelight, lightcolor);

		while (1)
		{
			// get the vertex count and primitive type
			count = *order++;
			va=0;
			if (!count)
				break;		// done
			if (count < 0) 
			{
				count = -count;
				mode=GL_TRIANGLE_FAN;
			} 
			else
				mode=GL_TRIANGLE_STRIP;

			do 
			{
				// texture coordinates come from the draw list
				index_xyz = order[2];

				VA_SetElem2(tex_array[va], celTexCoord (&verts[index_xyz], &ov[index_xyz], backlerp), 0);
				VA_SetElem3(vert_array[va],s_lerped[index_xyz][0],s_lerped[index_xyz][1],s_lerped[index_xyz][2]);
				VA_SetElem4(col_array[va], 1.0f, 1.0f, 1.0f, 1.0f);

				va++;
				order += 3;
			} while (--count);

				qglDrawArrays(mode,0,va);
		}

		GLSTATE_DISABLE_BLEND
		qglDepthMask (true);

	}

	qglDisableClientState( GL_COLOR_ARRAY );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );

	if (r_celshading->value && !is_trans)
		GL_DrawOutLine (paliashdr, currententity->frame, r_celshading_width->value, mirrormodel);

	SetVertexOverbrights(false);
}

/*
=============
GL_DrawAliasShadow
=============
*/

#define GetDistance(normal,v1) -((normal[0] * v1[0]) + (normal[1] * v1[1]) + (normal[2] * v1[2]));

void CastVolumeShadow(dmdl_t *hdr, vec3_t light, float projectdistance)
{
	dtriangle_t *ot, *tris;
	int i, j, k;
	BOOL trianglefacinglight[MAX_TRIANGLES];
	vec3_t v0,v1,v2,v3;

	float	norm[3], dist, dot1;
	float	tnorm[3], tdist, dot2, blah[3];
	vec3_t	normvec;

	daliasframe_t	*frame, *oldframe;
	dtrivertx_t		*ov, *verts;

	frame = (daliasframe_t *)((byte *)hdr + hdr->ofs_frames 
		+ currententity->frame * hdr->framesize);
	verts = frame->verts;

	ot = tris = (dtriangle_t *)((unsigned char*)hdr + hdr->ofs_tris);

	for (i=0; i<hdr->num_tris; i++, tris++) 
	{
		for (j=0; j<3; j++) 
		{
			v0[j] = s_lerped[tris->index_xyz[0]][j];
			v1[j] = s_lerped[tris->index_xyz[1]][j];
			v2[j] = s_lerped[tris->index_xyz[2]][j];
		}

		trianglefacinglight[i] =
			(light[0] - v0[0]) * ((v0[1] - v1[1]) * (v2[2] - v1[2]) - (v0[2] - v1[2]) * (v2[1] - v1[1]))
			+ (light[1] - v0[1]) * ((v0[2] - v1[2]) * (v2[0] - v1[0]) - (v0[0] - v1[0]) * (v2[2] - v1[2]))
			+ (light[2] - v0[2]) * ((v0[0] - v1[0]) * (v2[1] - v1[1]) - (v0[1] - v1[1]) * (v2[0] - v1[0])) > 0;
	}

	qglBegin(GL_QUADS);
	for (i=0, tris=ot; i<hdr->num_tris; i++, tris++) 
	{
		if (!trianglefacinglight[i])
			continue;

		if (!trianglefacinglight[currentmodel->edge_tri[i][0]]) 
		{
			for (j=0; j<3; j++) 
			{
				v0[j]=s_lerped[tris->index_xyz[1]][j];
				v1[j]=s_lerped[tris->index_xyz[0]][j];

				v2[j]=v1[j]+((v1[j]-light[j]) * projectdistance);
				v3[j]=v0[j]+((v0[j]-light[j]) * projectdistance);
			}

			qglVertex3fv(v0);
			qglVertex3fv(v1);
			qglVertex3fv(v2);
			qglVertex3fv(v3);
		}

		if (!trianglefacinglight[currentmodel->edge_tri[i][1]]) 
		{
			for (j=0; j<3; j++) {
				v0[j]=s_lerped[tris->index_xyz[2]][j];
				v1[j]=s_lerped[tris->index_xyz[1]][j];

				v2[j]=v1[j]+((v1[j]-light[j]) * projectdistance);
				v3[j]=v0[j]+((v0[j]-light[j]) * projectdistance);
			}

			qglVertex3fv(v0);
			qglVertex3fv(v1);
			qglVertex3fv(v2);
			qglVertex3fv(v3);
		}

		if (!trianglefacinglight[currentmodel->edge_tri[i][2]]) 
		{
			for (j=0; j<3; j++) 
			{
				v0[j]=s_lerped[tris->index_xyz[0]][j];
				v1[j]=s_lerped[tris->index_xyz[2]][j];

				v2[j]=v1[j]+((v1[j]-light[j]) * projectdistance);
				v3[j]=v0[j]+((v0[j]-light[j]) * projectdistance);
			}

			qglVertex3fv(v0);
			qglVertex3fv(v1);
			qglVertex3fv(v2);
			qglVertex3fv(v3);
		}
	}
	qglEnd();

	// cap the volume
	qglBegin(GL_TRIANGLES);
	for (i=0, tris=ot; i<hdr->num_tris; i++, tris++) 
	{
		if (trianglefacinglight[i]) 
		{
			for (j=0; j<3; j++) 
			{
				v0[j]=s_lerped[tris->index_xyz[0]][j];
				v1[j]=s_lerped[tris->index_xyz[1]][j];
				v2[j]=s_lerped[tris->index_xyz[2]][j];
			}
			qglVertex3fv(v0);
			qglVertex3fv(v1);
			qglVertex3fv(v2);

			continue;
		}

		for (j=0; j<3; j++) 
		{
			v0[j]=s_lerped[tris->index_xyz[0]][j];
			v1[j]=s_lerped[tris->index_xyz[1]][j];
			v2[j]=s_lerped[tris->index_xyz[2]][j];

			v0[j]=v0[j]+((v0[j]-light[j]) * projectdistance);
			v1[j]=v1[j]+((v1[j]-light[j]) * projectdistance);
			v2[j]=v2[j]+((v2[j]-light[j]) * projectdistance);
		}

		qglVertex3fv(v0);
		qglVertex3fv(v1);
		qglVertex3fv(v2);
	}
	qglEnd();
}

qboolean TracePoints (vec3_t start, vec3_t end, void *surf);

#define DEFAULT_LIGHT 500
void GL_DrawAliasShadow (entity_t *e, dmdl_t *paliashdr, int posenum, qboolean mirrored)
{
	int		*order;
	vec3_t	point, proj, light, norm, temp;
	float	height, lheight;
	int		count;
	int		va = 0, i, o; 
	float mode, projected_distance = 25;
	dtriangle_t *t, *tris;
	daliasframe_t	*frame, *oldframe;
	dtrivertx_t		*ov, *verts;
	dlight_t *l;
	worldLight_t *wl;
	float cost, sint;
	float is, it, dist;
	int countlights = 0;

	if (currentmodel->noshadow)
		return;

	lheight = currententity->origin[2] - lightspot[2];

	l=r_newrefdef.dlights;

	t=tris=(dtriangle_t *)((unsigned char*)paliashdr + paliashdr->ofs_tris);

	frame = (daliasframe_t *)((byte *)paliashdr + paliashdr->ofs_frames 
		+ currententity->frame * paliashdr->framesize);
	verts = frame->verts;
	oldframe = (daliasframe_t *)((byte *)paliashdr + paliashdr->ofs_frames 
		+ currententity->oldframe * paliashdr->framesize);

	height = 0;

	order = (int *)((byte *)paliashdr + paliashdr->ofs_glcmds);

	cost = cos(-currententity->angles[1] / 180 * M_PI), sint = sin(-currententity->angles[1] / 180 * M_PI);

	height = -lheight + 0.1f;
	/*	OLD SHADOW CODE!!!
	while (1)
	{
		// get the vertex count and primitive type
		count = *order++;
		if (!count)
			break;		// done
		if (count < 0)
		{
			count = -count;
			qglBegin (GL_TRIANGLE_FAN);
		}
		else
			qglBegin (GL_TRIANGLE_STRIP);

		do
		{
			memcpy( point, s_lerped[order[2]], sizeof( point )  );

			point[0] -= shadevector[0]*(point[2]+lheight);
			point[1] -= shadevector[1]*(point[2]+lheight);
			point[2] = height;

			qglVertex3fv (point);

			order += 3;

		} while (--count);

		qglEnd ();
	}
	*/

	if (gl_shadows->value && !gl_stencil->value) 
	{
		qglColor3f(0,0,1);

		if (gl_shadows->value>1)
		{
			for (i=0; i<r_newrefdef.num_dlights; i++, l++) 
			{
				if ((l->origin[0] == currententity->origin[0]) &&
					(l->origin[1] == currententity->origin[1]) &&
					(l->origin[2] == currententity->origin[2]))
					continue;

				VectorSubtract(currententity->origin, l->origin, temp);
				dist = sqrt(DotProduct(temp,temp));

				if (dist > 384)
					continue;

				for (o=0; o<3; o++)
					light[o] = -currententity->origin[o] + l->origin[o];	// lights origin in relation to the entity

				is = light[0], it = light[1];
				light[0] = (cost * (is - 0) + sint * (0 - it) + 0);
				light[1] = (cost * (it - 0) + sint * (is - 0) + 0);
				light[2]+=8;

				if (projected_distance < 0.01f)
					continue;

				CastVolumeShadow(paliashdr,light, projected_distance);
				countlights++;
			}
			
			for (i=0; i<r_numWorldLights; i++) 
			{	
				msurface_t *surf;
				int sidebit;

				wl = &r_worldLights[i];
				surf = wl->surf;

				VectorSubtract(currententity->origin, wl->origin, temp);
				dist = VectorNormalize(temp);

				if (dist > wl->intensity)
					continue;

				if (surf)
				{
					dist = DotProduct (currententity->origin, surf->plane->normal) - surf->plane->dist;

					if (dist >= 0)
						sidebit = 0;
					else
						sidebit = SURF_PLANEBACK;

					if ( (surf->flags & SURF_PLANEBACK) != sidebit )
						continue;
				}

				if (!TracePoints(currententity->origin, wl->origin, wl->surf))
						continue;

				for (o=0; o<3; o++)
					light[o] = -currententity->origin[o] + wl->origin[o];	// lights origin in relation to the entity

				is = light[0], it = light[1];
				light[0] = (cost * (is - 0) + sint * (0 - it) + 0);
				light[1] = (cost * (it - 0) + sint * (is - 0) + 0);
				light[2]+=8;

				if (projected_distance < 0.01f)
					continue;

				CastVolumeShadow(paliashdr,light, projected_distance);
				countlights++;
			}
		}

		if (!countlights)
		{
			VectorSet(light, 0,0,DEFAULT_LIGHT);
			is = light[0], it = light[1];
			light[0] = (cost * (is - 0) + sint * (0 - it) + 0);
			light[1] = (cost * (it - 0) + sint * (is - 0) + 0);
			light[2]+=8;

			CastVolumeShadow(paliashdr,light, projected_distance);
		}

		qglColor3f(1,1,1);
	}
	else if (gl_shadows->value) 
	{
		qglColorMask(0,0,0,0);
		qglEnable(GL_STENCIL_TEST);

		qglDepthMask(0);
		qglDepthFunc( GL_LESS );
		qglStencilFunc( GL_ALWAYS, 0, 0xFF);

		if (gl_shadows->value>1)
		{
			for (i=0; i<r_newrefdef.num_dlights; i++, l++) 
			{
				if ((l->origin[0] == currententity->origin[0]) &&
					(l->origin[1] == currententity->origin[1]) &&
					(l->origin[2] == currententity->origin[2]))
					continue;

	//				VectorSubtract(a, b, temp);
	//				dist = sqrt(DotProduct(temp, temp));

				VectorSubtract(currententity->origin, l->origin, temp);
				dist = sqrt(DotProduct(temp,temp));

				if (dist > 384)
					continue;

	//				projected_distance = (384 - dist) / (384 / 4);

				for (o=0; o<3; o++)
					light[o] = -currententity->origin[o] + l->origin[o];	// lights origin in relation to the entity

				is = light[0], it = light[1];
				light[0] = (cost * (is - 0) + sint * (0 - it) + 0);
				light[1] = (cost * (it - 0) + sint * (is - 0) + 0);
				light[2]+=8;

				// increment stencil if backface is behind depthbuffer
				qglCullFace(GL_BACK); // quake is backwards, this culls front faces
				qglStencilOp(GL_KEEP, GL_INCR, GL_KEEP);
				CastVolumeShadow(paliashdr,light, projected_distance);

				// decrement stencil if frontface is behind depthbuffer
				qglCullFace(GL_FRONT); // quake is backwards, this culls back faces
				qglStencilOp(GL_KEEP, GL_DECR, GL_KEEP);
				CastVolumeShadow(paliashdr,light, projected_distance);

				countlights++;
			}
			
			for (i=0; i<r_numWorldLights; i++) 
			{
				msurface_t *surf;
				int sidebit;

				wl = &r_worldLights[i];
				surf = wl->surf;

				VectorSubtract(wl->origin, currententity->origin, temp);
				dist = VectorNormalize(temp);

				if (dist > wl->intensity)
					continue;

				if (surf)
				{
					dist = DotProduct (currententity->origin, surf->plane->normal) - surf->plane->dist;

					if (dist >= 0)
						sidebit = 0;
					else
						sidebit = SURF_PLANEBACK;

					if ( (surf->flags & SURF_PLANEBACK) != sidebit )
						continue;
				}

				if (!TracePoints(currententity->origin, wl->origin, wl->surf))
						continue;

				for (o=0; o<3; o++)
					light[o] = -currententity->origin[o] + wl->origin[o];	// lights origin in relation to the entity

				is = light[0], it = light[1];
				light[0] = (cost * (is - 0) + sint * (0 - it) + 0);
				light[1] = (cost * (it - 0) + sint * (is - 0) + 0);
				light[2]+=8;

				// increment stencil if backface is behind depthbuffer
				qglCullFace(GL_BACK); // quake is backwards, this culls front faces
				qglStencilOp(GL_KEEP, GL_INCR, GL_KEEP);
				CastVolumeShadow(paliashdr,light, projected_distance);

				// decrement stencil if frontface is behind depthbuffer
				qglCullFace(GL_FRONT); // quake is backwards, this culls back faces
				qglStencilOp(GL_KEEP, GL_DECR, GL_KEEP);
				CastVolumeShadow(paliashdr,light, projected_distance);

				countlights++;
			}
			
		}

		if (!countlights) //if no lights, just go straight down
		{
			VectorSet(light, 0,0,DEFAULT_LIGHT);
			is = light[0], it = light[1];
			light[0] = (cost * (is - 0) + sint * (0 - it) + 0);
			light[1] = (cost * (it - 0) + sint * (is - 0) + 0);
			light[2]+=8;

			// increment stencil if backface is behind depthbuffer
			qglCullFace(GL_BACK); // quake is backwards, this culls front faces
			qglStencilOp(GL_KEEP, GL_INCR, GL_KEEP);
			CastVolumeShadow(paliashdr,light, projected_distance);

			// decrement stencil if frontface is behind depthbuffer
			qglCullFace(GL_FRONT); // quake is backwards, this culls back faces
			qglStencilOp(GL_KEEP, GL_DECR, GL_KEEP);
			CastVolumeShadow(paliashdr,light, projected_distance);
		}

		qglDisable(GL_STENCIL_TEST);
		qglColorMask(1,1,1,1);

		qglDepthMask(1);
		qglDepthFunc(GL_LEQUAL);
	}

}

/*
** R_CullAliasModel
*/
static qboolean R_CullAliasModel( vec3_t bbox[8], entity_t *e )
{
	int i;
	vec3_t		mins, maxs;
	dmdl_t		*paliashdr;
	vec3_t		vectors[3];
	vec3_t		thismins, oldmins, thismaxs, oldmaxs;
	daliasframe_t *pframe, *poldframe;
	vec3_t angles;

	paliashdr = (dmdl_t *)currentmodel->extradata;

	if ( ( e->frame >= paliashdr->num_frames ) || ( e->frame < 0 ) )
	{
		ri.Con_Printf (PRINT_ALL, "R_CullAliasModel %s: no such frame %d\n", 
			currentmodel->name, e->frame);
		e->frame = 0;
	}
	if ( ( e->oldframe >= paliashdr->num_frames ) || ( e->oldframe < 0 ) )
	{
		ri.Con_Printf (PRINT_ALL, "R_CullAliasModel %s: no such oldframe %d\n", 
			currentmodel->name, e->oldframe);
		e->oldframe = 0;
	}

	pframe = ( daliasframe_t * ) ( ( byte * ) paliashdr + 
		                              paliashdr->ofs_frames +
									  e->frame * paliashdr->framesize);

	poldframe = ( daliasframe_t * ) ( ( byte * ) paliashdr + 
		                              paliashdr->ofs_frames +
									  e->oldframe * paliashdr->framesize);

	/*
	** compute axially aligned mins and maxs
	*/
	if ( pframe == poldframe )
	{
		for ( i = 0; i < 3; i++ )
		{
			mins[i] = pframe->translate[i];
			maxs[i] = mins[i] + pframe->scale[i]*255;
		}
	}
	else
	{
		for ( i = 0; i < 3; i++ )
		{
			thismins[i] = pframe->translate[i];
			thismaxs[i] = thismins[i] + pframe->scale[i]*255;

			oldmins[i]  = poldframe->translate[i];
			oldmaxs[i]  = oldmins[i] + poldframe->scale[i]*255;

			if ( thismins[i] < oldmins[i] )
				mins[i] = thismins[i];
			else
				mins[i] = oldmins[i];

			if ( thismaxs[i] > oldmaxs[i] )
				maxs[i] = thismaxs[i];
			else
				maxs[i] = oldmaxs[i];
		}
	}

	/*
	** compute a full bounding box
	*/
	for ( i = 0; i < 8; i++ )
	{
		vec3_t   tmp;

		if ( i & 1 )
			tmp[0] = mins[0];
		else
			tmp[0] = maxs[0];

		if ( i & 2 )
			tmp[1] = mins[1];
		else
			tmp[1] = maxs[1];

		if ( i & 4 )
			tmp[2] = mins[2];
		else
			tmp[2] = maxs[2];

		VectorCopy( tmp, bbox[i] );
	}

	/*
	** rotate the bounding box
	*/
	VectorCopy( e->angles, angles );
	angles[YAW] = -angles[YAW];
	AngleVectors( angles, vectors[0], vectors[1], vectors[2] );

	for ( i = 0; i < 8; i++ )
	{
		vec3_t tmp;

		VectorCopy( bbox[i], tmp );

		bbox[i][0] = DotProduct( vectors[0], tmp );
		bbox[i][1] = -DotProduct( vectors[1], tmp );
		bbox[i][2] = DotProduct( vectors[2], tmp );

		VectorAdd( e->origin, bbox[i], bbox[i] );
	}

	{
		int p, f, aggregatemask = ~0;

		for ( p = 0; p < 8; p++ )
		{
			int mask = 0;

			for ( f = 0; f < 4; f++ )
			{
				float dp = DotProduct( frustum[f].normal, bbox[p] );

				if ( ( dp - frustum[f].dist ) < 0 )
				{
					mask |= ( 1 << f );
				}
			}

			aggregatemask &= mask;
		}

		if ( aggregatemask )
		{
			return true;
		}

		return false;
	}
}

static void R_FindBBox( vec3_t bbox[8], entity_t *e )
{
	int i;
	vec3_t		mins, maxs;
	dmdl_t		*paliashdr;
	vec3_t		vectors[3];
	vec3_t		thismins, oldmins, thismaxs, oldmaxs;
	daliasframe_t *pframe, *poldframe;
	vec3_t angles;

	paliashdr = (dmdl_t *)currentmodel->extradata;

	if ( ( e->frame >= paliashdr->num_frames ) || ( e->frame < 0 ) )
	{
		ri.Con_Printf (PRINT_ALL, "R_CullAliasModel %s: no such frame %d\n", 
			currentmodel->name, e->frame);
		e->frame = 0;
	}
	if ( ( e->oldframe >= paliashdr->num_frames ) || ( e->oldframe < 0 ) )
	{
		ri.Con_Printf (PRINT_ALL, "R_CullAliasModel %s: no such oldframe %d\n", 
			currentmodel->name, e->oldframe);
		e->oldframe = 0;
	}

	pframe = ( daliasframe_t * ) ( ( byte * ) paliashdr + 
		                              paliashdr->ofs_frames +
									  e->frame * paliashdr->framesize);

	poldframe = ( daliasframe_t * ) ( ( byte * ) paliashdr + 
		                              paliashdr->ofs_frames +
									  e->oldframe * paliashdr->framesize);

	/*
	** compute axially aligned mins and maxs
	*/
	if ( pframe == poldframe )
	{
		for ( i = 0; i < 3; i++ )
		{
			mins[i] = pframe->translate[i];
			maxs[i] = mins[i] + pframe->scale[i]*255;
		}
	}
	else
	{
		for ( i = 0; i < 3; i++ )
		{
			thismins[i] = pframe->translate[i];
			thismaxs[i] = thismins[i] + pframe->scale[i]*255;

			oldmins[i]  = poldframe->translate[i];
			oldmaxs[i]  = oldmins[i] + poldframe->scale[i]*255;

			if ( thismins[i] < oldmins[i] )
				mins[i] = thismins[i];
			else
				mins[i] = oldmins[i];

			if ( thismaxs[i] > oldmaxs[i] )
				maxs[i] = thismaxs[i];
			else
				maxs[i] = oldmaxs[i];
		}
	}

	/*
	** compute a full bounding box
	*/
	for ( i = 0; i < 8; i++ )
	{
		vec3_t   tmp;

		if ( i & 1 )
			tmp[0] = mins[0];
		else
			tmp[0] = maxs[0];

		if ( i & 2 )
			tmp[1] = mins[1];
		else
			tmp[1] = maxs[1];

		if ( i & 4 )
			tmp[2] = mins[2];
		else
			tmp[2] = maxs[2];

		VectorCopy( tmp, bbox[i] );
	}

	/*
	** rotate the bounding box
	*/
	VectorCopy( e->angles, angles );
	angles[YAW] = -angles[YAW];
	AngleVectors( angles, vectors[0], vectors[1], vectors[2] );

	for ( i = 0; i < 8; i++ )
	{
		vec3_t tmp;

		VectorCopy( bbox[i], tmp );

		bbox[i][0] = DotProduct( vectors[0], tmp );
		bbox[i][1] = -DotProduct( vectors[1], tmp );
		bbox[i][2] = DotProduct( vectors[2], tmp );

		VectorAdd( e->origin, bbox[i], bbox[i] );
	}
}

/*
=================
R_DrawAliasModel

=================
*/
void GL_FlipModel (qboolean on)
{
	extern void MYgluPerspective( GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar );

	if (on)
	{
		qglMatrixMode( GL_PROJECTION );
		qglPushMatrix();
		qglLoadIdentity();
		qglScalef( -1, 1, 1 );
		MYgluPerspective( r_newrefdef.fov_y, ( float ) r_newrefdef.width / r_newrefdef.height,  4,  4096);
		qglMatrixMode( GL_MODELVIEW );

		qglCullFace( GL_BACK );
	}
	else
	{
		qglMatrixMode( GL_PROJECTION );
		qglPopMatrix();
		qglMatrixMode( GL_MODELVIEW );
		qglCullFace( GL_FRONT );
	}
}

void R_ShadowLight (vec3_t pos, vec3_t lightAdd);
void R_LightPointDynamics (vec3_t p, vec3_t color, m_dlight_t *list, int *amount, int max);
void setShadeLight (void)
{
	int i;

	if ( currententity->flags & ( RF_SHELL_HALF_DAM | RF_SHELL_GREEN | RF_SHELL_RED | RF_SHELL_BLUE | RF_SHELL_DOUBLE ) )
	{
		VectorClear (shadelight);
		if (currententity->flags & RF_SHELL_HALF_DAM)
		{
				shadelight[0] = 0.56;
				shadelight[1] = 0.59;
				shadelight[2] = 0.45;
		}
		if ( currententity->flags & RF_SHELL_DOUBLE )
		{
			shadelight[0] = 0.9;
			shadelight[1] = 0.7;
		}
		if ( currententity->flags & RF_SHELL_RED )
			shadelight[0] = 1.0;
		if ( currententity->flags & RF_SHELL_GREEN )
			shadelight[1] = 1.0;
		if ( currententity->flags & RF_SHELL_BLUE )
			shadelight[2] = 1.0;
	}
	else if ( currententity->flags & RF_FULLBRIGHT )
	{
		for (i=0 ; i<3 ; i++)
			shadelight[i] = 1.0;
	}
	else
	{
		//Set up basic lighting...
		if (r_model_lightlerp->value && r_model_dlights->value)
		{
			int max = r_model_dlights->value;

			if (max<0)max=0;
			if (max>MAX_MODEL_DLIGHTS)max=MAX_MODEL_DLIGHTS;

			R_LightPointDynamics (currententity->origin, shadelight, model_dlights, 
				&model_dlights_num, max);
		}
		else
		{
			R_LightPoint (currententity->origin, shadelight);
			model_dlights_num = 0;
		}

		// player lighting hack for communication back to server
		// big hack!
		if ( currententity->flags & RF_WEAPONMODEL )
		{
			// pick the greatest component, which should be the same
			// as the mono value returned by software
			if (shadelight[0] > shadelight[1])
			{
				if (shadelight[0] > shadelight[2])
					r_lightlevel->value = 150*shadelight[0];
				else
					r_lightlevel->value = 150*shadelight[2];
			}
			else
			{
				if (shadelight[1] > shadelight[2])
					r_lightlevel->value = 150*shadelight[1];
				else
					r_lightlevel->value = 150*shadelight[2];
			}

		}
		
		if ( gl_monolightmap->string[0] != '0' )
		{
			float s = shadelight[0];

			if ( s < shadelight[1] )
				s = shadelight[1];
			if ( s < shadelight[2] )
				s = shadelight[2];

			shadelight[0] = s;
			shadelight[1] = s;
			shadelight[2] = s;
		}
	}

	if ( currententity->flags & RF_MINLIGHT )
	{
		for (i=0 ; i<3 ; i++)
			if (shadelight[i] > 0.1)
				break;
		if (i == 3)
		{
			shadelight[0] = 0.1;
			shadelight[1] = 0.1;
			shadelight[2] = 0.1;
		}
	}

	if ( currententity->flags & RF_GLOW )
	{	// bonus items will pulse with time
		float	scale;
		float	min;

		scale = 0.2 * sin(r_newrefdef.time*7);
		for (i=0 ; i<3 ; i++)
		{
			min = shadelight[i] * 0.8;
			shadelight[i] += scale;
			if (shadelight[i] < min)
				shadelight[i] = min;
		}
	}

	// =================
	// PGM	ir goggles color override
	if (r_newrefdef.rdflags & RDF_IRGOGGLES)
	{
		if (currententity->flags & RF_IR_VISIBLE)
		{
			shadelight[0] = 1.0;
			shadelight[1] = 0.25;
			shadelight[2] = 0.25;
		}
	}

	// PGM	
	// =================

	shadedots = r_avertexnormal_dots[((int)(currententity->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1)];
}

void setBlendModeOn (image_t *skin)
{
	GL_TexEnv( GL_MODULATE );

	if (skin)
		GL_Bind(skin->texnum);

	GL_ShadeModel (GL_SMOOTH);

	if ( currententity->flags & RF_TRANSLUCENT )
	{
		qglDepthMask   (false);

		if (currententity->flags&RF_TRANS_ADDITIVE)
		{ 
			GL_BlendFunction   (GL_SRC_ALPHA, GL_ONE);	
			qglColor4ub(255, 255, 255, 255);
			GL_ShadeModel (GL_FLAT);
		}
		else
			GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		
		GLSTATE_ENABLE_BLEND
	}
}

void setBlendModeOff ()
{
	if ( currententity->flags & RF_TRANSLUCENT )
	{
		qglDepthMask   (true);
		GLSTATE_DISABLE_BLEND
		GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
}

void R_DrawAliasShadow (entity_t *e)
{
	dmdl_t		*paliashdr;
	vec3_t		bbox[8];
	qboolean	mirrormodel = false;
	float		an;
	daliasframe_t	*frame, *oldframe;
	dtrivertx_t	*v, *ov, *verts;
	int		*order;
	int		count;
	float	frontlerp;
	float	alpha;
	vec3_t	move, delta, vectors[3];
	vec3_t	frontv, backv;
	int		i;

	if ( !( e->flags & RF_VIEWERMODEL || e->renderfx & RF2_CAMERAMODEL) )
	{
		if ( R_CullAliasModel( bbox, e ) )
			return;
	}

	if ( e->flags & RF_WEAPONMODEL )
	{
		if ( r_lefthand->value == 2 )
			return;
		else if (r_lefthand->value==1)
			mirrormodel = true;
	}
	else if (e->renderfx & RF2_CAMERAMODEL)
	{
		if (r_lefthand->value==1)
			mirrormodel = true;
	}
	else if (currententity->flags&RF_MIRRORMODEL)
		mirrormodel = true;

	paliashdr = (dmdl_t *)currentmodel->extradata;

	if (currententity->flags&RF_TRANSLUCENT)
		qglColor4f (0,0,0,0.3*currententity->alpha);
	else
		qglColor4f (0,0,0,0.3);

	frame = (daliasframe_t *)((byte *)paliashdr + paliashdr->ofs_frames 
		+ currententity->frame * paliashdr->framesize);
	verts = v = frame->verts;

	oldframe = (daliasframe_t *)((byte *)paliashdr + paliashdr->ofs_frames 
		+ currententity->oldframe * paliashdr->framesize);
	ov = oldframe->verts;

	order = (int *)((byte *)paliashdr + paliashdr->ofs_glcmds);

	frontlerp = 1.0 - currententity->backlerp;

	// move should be the delta back to the previous frame * backlerp
	VectorSubtract (currententity->oldorigin, currententity->origin, delta);
	AngleVectors (currententity->angles, vectors[0], vectors[1], vectors[2]);

	move[0] = DotProduct (delta, vectors[0]);	// forward
	move[1] = -DotProduct (delta, vectors[1]);	// left
	move[2] = DotProduct (delta, vectors[2]);	// up

	VectorAdd (move, oldframe->translate, move);

	for (i=0 ; i<3 ; i++)
	{
		move[i] = currententity->backlerp*move[i] + frontlerp*frame->translate[i];
		frontv[i] = frontlerp*frame->scale[i];
		backv[i] = currententity->backlerp*oldframe->scale[i];
	}
	
	GL_LerpVerts( paliashdr->num_xyz, v, ov, verts, s_lerped[0], move, frontv, backv, 0 );

	if (mirrormodel)
		GL_FlipModel(true);

	if (gl_shadows->value)
	{

		qglDepthMask(false);
		qglDisable (GL_TEXTURE_2D);
		GLSTATE_ENABLE_BLEND

		GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		qglColor4f(0,0,0, 0.3);


		qglPushMatrix ();
		{
			vec3_t end;

			R_RotateForEntity (e, false);
/*
			an = currententity->angles[1]/180*M_PI;
			shadevector[0] = cos(-an);
			shadevector[1] = sin(-an);
			shadevector[2] = 1;
			VectorNormalize (shadevector);

			VectorSet(end,
				currententity->origin[0],
				currententity->origin[1],
				currententity->origin[2] - 2048
			);
			RecursiveLightPoint (r_worldmodel->nodes, currententity->origin, end);
//*/
			GL_DrawAliasShadow( e, paliashdr, currententity->frame, mirrormodel );
		}
		qglPopMatrix ();

		GLSTATE_DISABLE_BLEND
		qglEnable (GL_TEXTURE_2D);
	}

	if (mirrormodel)
		GL_FlipModel(false);


	qglScalef (1,1,1);
	qglColor3f (1,1,1);
	GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void R_DrawAliasModel (entity_t *e)
{
	dmdl_t		*paliashdr;
	vec3_t		bbox[8];
	image_t		*skin;
	qboolean	mirrormodel = false;

	if ( !( e->flags & RF_WEAPONMODEL || e->flags & RF_VIEWERMODEL || e->renderfx & RF2_CAMERAMODEL) )
	{
		if ( R_CullAliasModel( bbox, e ) )
			return;
	}

	if ( e->flags & RF_WEAPONMODEL )
	{
		if ( r_lefthand->value == 2 )
			return;
		else if (r_lefthand->value==1)
			mirrormodel = true;
	}
	else if (e->renderfx & RF2_CAMERAMODEL)
	{
		if (r_lefthand->value==1)
			mirrormodel = true;
	}
	else if (currententity->flags&RF_MIRRORMODEL)
		mirrormodel = true;

	paliashdr = (dmdl_t *)currentmodel->extradata;

	//
	// get lighting information
	//
	// PMM - rewrote, reordered to handle new shells & mixing
	// PMM - 3.20 code .. replaced with original way of doing it to keep mod authors happy
	//
	setShadeLight();

	//
	// locate the proper data
	//

	c_alias_polys += paliashdr->num_tris;

	//
	// draw all the triangles
	//
	if (currententity->flags & RF_DEPTHHACK) // hack the depth range to prevent view model from poking into walls
	{
		if (r_newrefdef.rdflags & RDF_NOWORLDMODEL)
			qglDepthRange (gldepthmin, gldepthmin + 0.01*(gldepthmax-gldepthmin));
		else
			qglDepthRange (gldepthmin, gldepthmin + 0.5*(gldepthmax-gldepthmin));
	}

	if (mirrormodel)
		GL_FlipModel(true);

    qglPushMatrix ();
	e->angles[PITCH] = -e->angles[PITCH];	// sigh.
	R_RotateForEntity (e, true);
	e->angles[PITCH] = -e->angles[PITCH];	// sigh.

	// select skin
	if (currententity->skin)
		skin = currententity->skin;	// custom player skin
	else
	{
		if (currententity->skinnum >= MAX_MD2SKINS)
			skin = currentmodel->skins[0];
		else
		{
			skin = currentmodel->skins[currententity->skinnum];
			if (!skin)
				skin = currentmodel->skins[0];
		}
	}
	if (!skin)
		skin = r_notexture;	// fallback...

	// draw it

	if ( (currententity->frame >= paliashdr->num_frames) 
		|| (currententity->frame < 0) )
	{
		ri.Con_Printf (PRINT_ALL, "R_DrawAliasModel %s: no such frame %d\n",
			currentmodel->name, currententity->frame);
		currententity->frame = 0;
		currententity->oldframe = 0;
	}

	if ( (currententity->oldframe >= paliashdr->num_frames)
		|| (currententity->oldframe < 0))
	{
		ri.Con_Printf (PRINT_DEVELOPER, "R_DrawAliasModel %s: no such oldframe %d\n",
			currentmodel->name, currententity->oldframe);
		currententity->frame = 0;
		currententity->oldframe = 0;
	}

	if ( !r_lerpmodels->value )
		currententity->backlerp = 0;

	setBlendModeOn(skin);

	if ( currententity->flags & ( RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM) ) 
		GL_DrawAliasFrameLerpShell(paliashdr,currententity->backlerp);
	else 
		GL_DrawAliasFrameLerp (paliashdr, currententity->backlerp, mirrormodel);

	GL_TexEnv( GL_REPLACE );
	GL_ShadeModel (GL_FLAT);

	qglPopMatrix ();


	if (mirrormodel)
		GL_FlipModel(false);

	setBlendModeOff();

	if (currententity->flags & RF_DEPTHHACK)
		qglDepthRange (gldepthmin, gldepthmax);

	qglScalef (1,1,1);
	qglColor4f (1,1,1,1);
}


