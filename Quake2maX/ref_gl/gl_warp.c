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
// gl_warp.c -- sky and water polygons

#include "gl_local.h"

extern	model_t	*loadmodel;

char	skyname[MAX_QPATH];
float	skyrotate;
vec3_t	skyaxis;
image_t	*sky_images[6];

msurface_t	*warpface;

#define	SUBDIVIDE_SIZE	64
//#define	SUBDIVIDE_SIZE	1024

void SetVertexOverbrights (qboolean);

void BoundPoly (int numverts, float *verts, vec3_t mins, vec3_t maxs)
{
	int		i, j;
	float	*v;

	mins[0] = mins[1] = mins[2] = 9999;
	maxs[0] = maxs[1] = maxs[2] = -9999;
	v = verts;
	for (i=0 ; i<numverts ; i++)
		for (j=0 ; j<3 ; j++, v++)
		{
			if (*v < mins[j])
				mins[j] = *v;
			if (*v > maxs[j])
				maxs[j] = *v;
		}
}

void SubdividePolygon (int numverts, float *verts)
{
	int		i, j, k;
	vec3_t	mins, maxs;
	float	m;
	float	*v;
	vec3_t	front[64], back[64];
	int		f, b;
	float	dist[64];
	float	frac;
	glpoly_t	*poly;
	float	s, t;
	vec3_t	total;
	float	total_s, total_t;

	if (numverts > 60)
		ri.Sys_Error (ERR_DROP, "numverts = %i", numverts);

	BoundPoly (numverts, verts, mins, maxs);

	for (i=0 ; i<3 ; i++)
	{
		m = (mins[i] + maxs[i]) * 0.5;
		m = SUBDIVIDE_SIZE * floor (m/SUBDIVIDE_SIZE + 0.5);
		if (maxs[i] - m < 8)
			continue;
		if (m - mins[i] < 8)
			continue;

		// cut it
		v = verts + i;
		for (j=0 ; j<numverts ; j++, v+= 3)
			dist[j] = *v - m;

		// wrap cases
		dist[j] = dist[0];
		v-=i;
		VectorCopy (verts, v);

		f = b = 0;
		v = verts;
		for (j=0 ; j<numverts ; j++, v+= 3)
		{
			if (dist[j] >= 0)
			{
				VectorCopy (v, front[f]);
				f++;
			}
			if (dist[j] <= 0)
			{
				VectorCopy (v, back[b]);
				b++;
			}
			if (dist[j] == 0 || dist[j+1] == 0)
				continue;
			if ( (dist[j] > 0) != (dist[j+1] > 0) )
			{
				// clip point
				frac = dist[j] / (dist[j] - dist[j+1]);
				for (k=0 ; k<3 ; k++)
					front[f][k] = back[b][k] = v[k] + frac*(v[3+k] - v[k]);
				f++;
				b++;
			}
		}

		SubdividePolygon (f, front[0]);
		SubdividePolygon (b, back[0]);
		return;
	}

	// add a point in the center to help keep warp valid
	poly = Hunk_Alloc (sizeof(glpoly_t) + ((numverts-4)+2) * VERTEXSIZE*sizeof(float));
	poly->next = warpface->polys;
	warpface->polys = poly;
	poly->numverts = numverts+2;
	VectorClear (total);
	total_s = 0;
	total_t = 0;
	for (i=0 ; i<numverts ; i++, verts+= 3)
	{
		VectorCopy (verts, poly->verts[i+1]);
		s = DotProduct (verts, warpface->texinfo->vecs[0]);
		t = DotProduct (verts, warpface->texinfo->vecs[1]);

		total_s += s;
		total_t += t;
		VectorAdd (total, verts, total);

		poly->verts[i+1][3] = s;
		poly->verts[i+1][4] = t;
	}

	VectorScale (total, (1.0/numverts), poly->verts[0]);
	poly->verts[0][3] = total_s/numverts;
	poly->verts[0][4] = total_t/numverts;

	// copy first vertex to last
	memcpy (poly->verts[i+1], poly->verts[1], sizeof(poly->verts[0]));
}

/*
================
GL_SubdivideSurface

Breaks a polygon up along axial 64 unit
boundaries so that turbulent and sky warps
can be done reasonably.
================
*/
void GL_SubdivideSurface (msurface_t *fa)
{
	vec3_t		verts[64];
	int			numverts;
	int			i;
	int			lindex;
	float		*vec;

	warpface = fa;

	//
	// convert edges back to a normal polygon
	//
	numverts = 0;
	for (i=0 ; i<fa->numedges ; i++)
	{
		lindex = loadmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
			vec = loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position;
		else
			vec = loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position;
		VectorCopy (vec, verts[numverts]);
		numverts++;
	}

	SubdividePolygon (numverts, verts[0]);
}

//=========================================================



// speed up sin calculations - Ed
float	r_turbsin[] =
{
	#include "warpsin.h"
};
#define TURBSCALE (256.0 / (2 * M_PI))

// MrG - texture shader stuffs
#define DST_SIZE 16
unsigned int dst_texture = 0;
/*
===============
CreateDSTTex

Create the texture which warps texture shaders
===============
*/
void CreateDSTTex()
{
	signed char data[DST_SIZE][DST_SIZE][2];
	int x,y;

	for (x=0;x<DST_SIZE;x++)
		for (y=0;y<DST_SIZE;y++)
		{
			data[x][y][0]=rand()%255-128;
			data[x][y][1]=rand()%255-128;
		}

	qglGenTextures(1,&dst_texture);
	qglBindTexture(GL_TEXTURE_2D, dst_texture);
	qglTexImage2D(GL_TEXTURE_2D, 0, GL_DSDT8_NV, DST_SIZE, DST_SIZE, 0, GL_DSDT_NV,
				GL_BYTE, data);

	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	qglTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

/*
=============
EmitWaterPolys

Does a water warp on the pre-fragmented glpoly_t chain
=============
*/
void EmitWaterPolys (msurface_t *fa, qboolean light, float alpha)
{
	glpoly_t	*p, *bp;
	float		*v;
	int			i;
	float		s, t, os, ot;
	float		scroll, dstscroll;
	float		rdt = r_newrefdef.time;
	vec3_t		point;
	float		args[4] = {0.05,0,0,0.04};

	if (light)
		SetVertexOverbrights(true);

	if (gl_state.texshaders) 
	{
		qglActiveTextureARB(GL_TEXTURE0_ARB);
		if (!dst_texture)
			CreateDSTTex();

		GL_Bind(dst_texture);
		qglTexEnvi(GL_TEXTURE_SHADER_NV, GL_SHADER_OPERATION_NV, GL_TEXTURE_2D);
		
		qglActiveTextureARB(GL_TEXTURE1_ARB);
		GL_Bind(fa->texinfo->image->texnum);
		qglEnable(GL_TEXTURE_2D);

		qglTexEnvi(GL_TEXTURE_SHADER_NV, GL_SHADER_OPERATION_NV, GL_TEXTURE_2D);
		qglTexEnvi(GL_TEXTURE_SHADER_NV, GL_SHADER_OPERATION_NV, GL_OFFSET_TEXTURE_2D_NV);
		qglTexEnvi(GL_TEXTURE_SHADER_NV, GL_PREVIOUS_TEXTURE_INPUT_NV, GL_TEXTURE0_ARB);
		qglTexEnvfv(GL_TEXTURE_SHADER_NV, GL_OFFSET_TEXTURE_MATRIX_NV, &args[0]);

		// use this so that the new water isnt so bright anymore
		// We wont bother check for the extensions availabiliy, as the hardware required
		// to make it this far definately supports this as well
		qglTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);

		qglEnable(GL_TEXTURE_SHADER_NV);
		dstscroll = -64 * ( (r_newrefdef.time*0.15) - (int)(r_newrefdef.time*0.15) );
	}

	if (fa->texinfo->flags & SURF_FLOWING)
		scroll = -64 * ( (r_newrefdef.time*0.5) - (int)(r_newrefdef.time*0.5) );
	else
		scroll = 0;

	for (bp=fa->polys ; bp ; bp=bp->next)
	{
		p = bp;

		if (light)
			GL_ShadeModel (GL_SMOOTH);
		else
			qglColor4f( gl_state.inverse_intensity, gl_state.inverse_intensity, gl_state.inverse_intensity, alpha);

		qglBegin (GL_TRIANGLE_FAN);
		for (i=0,v=p->verts[0] ; i<p->numverts ; i++, v+=VERTEXSIZE)
		{
			os = v[3];
			ot = v[4];

			VectorCopy(v, point);

			s = os + 10*sin(cos(ot+rdt));
			s += scroll;
			s *= (1.0/64);

			t = ot + 10*cos(sin(os+rdt));
			t *= (1.0/64);

			if (light && p->vertexlight)
			{
				qglColor4ub( 
					p->vertexlight[i*3+0], 
					p->vertexlight[i*3+1], 
					p->vertexlight[i*3+2], 
					alpha*255);
			}

			if (gl_state.texshaders) 
			{ // MrG - texture shader waterwarp
				qglMTexCoord2fSGIS(GL_TEXTURE0_ARB,(v[3]+dstscroll)*0.015625,v[4]*0.015625);
				qglMTexCoord2fSGIS(GL_TEXTURE1_ARB,s,t);
			}
			else
				qglTexCoord2f (s, t);

			qglVertex3fv (point);
		}
		qglEnd ();

		if (light)
			GL_ShadeModel (GL_FLAT);
	}

	if (gl_state.texshaders) 
	{ // MrG - texture shader waterwarp
		qglDisable(GL_TEXTURE_2D);
		qglTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		qglActiveTextureARB(GL_TEXTURE0_ARB);
		qglDisable(GL_TEXTURE_SHADER_NV);
	}

	if (light)
		SetVertexOverbrights(false);
}

//===================================================================


vec3_t	skyclip[6] = {
	{1,1,0},
	{1,-1,0},
	{0,-1,1},
	{0,1,1},
	{1,0,1},
	{-1,0,1} 
};
int	c_sky;

// 1 = s, 2 = t, 3 = 2048
int	st_to_vec[6][3] =
{
	{3,-1,2},
	{-3,1,2},

	{1,3,2},
	{-1,-3,2},

	{-2,-1,3},		// 0 degrees yaw, look straight up
	{2,-1,-3}		// look straight down

//	{-1,2,3},
//	{1,2,-3}
};

// s = [0]/[2], t = [1]/[2]
int	vec_to_st[6][3] =
{
	{-2,3,1},
	{2,3,-1},

	{1,3,2},
	{-1,3,-2},

	{-2,-1,3},
	{-2,1,-3}

//	{-1,2,3},
//	{1,2,-3}
};

float	skymins[2][6], skymaxs[2][6];
float	sky_min, sky_max;

void DrawSkyPolygon (int nump, vec3_t vecs)
{
	int		i,j;
	vec3_t	v, av;
	float	s, t, dv;
	int		axis;
	float	*vp;

	c_sky++;

	// decide which face it maps to
	VectorCopy (vec3_origin, v);
	for (i=0, vp=vecs ; i<nump ; i++, vp+=3)
	{
		VectorAdd (vp, v, v);
	}
	av[0] = fabs(v[0]);
	av[1] = fabs(v[1]);
	av[2] = fabs(v[2]);
	if (av[0] > av[1] && av[0] > av[2])
	{
		if (v[0] < 0)
			axis = 1;
		else
			axis = 0;
	}
	else if (av[1] > av[2] && av[1] > av[0])
	{
		if (v[1] < 0)
			axis = 3;
		else
			axis = 2;
	}
	else
	{
		if (v[2] < 0)
			axis = 5;
		else
			axis = 4;
	}

	// project new texture coords
	for (i=0 ; i<nump ; i++, vecs+=3)
	{
		j = vec_to_st[axis][2];
		if (j > 0)
			dv = vecs[j - 1];
		else
			dv = -vecs[-j - 1];
		if (dv < 0.001)
			continue;	// don't divide by zero
		j = vec_to_st[axis][0];
		if (j < 0)
			s = -vecs[-j -1] / dv;
		else
			s = vecs[j-1] / dv;
		j = vec_to_st[axis][1];
		if (j < 0)
			t = -vecs[-j -1] / dv;
		else
			t = vecs[j-1] / dv;

		if (s < skymins[0][axis])
			skymins[0][axis] = s;
		if (t < skymins[1][axis])
			skymins[1][axis] = t;
		if (s > skymaxs[0][axis])
			skymaxs[0][axis] = s;
		if (t > skymaxs[1][axis])
			skymaxs[1][axis] = t;
	}
}

#define	ON_EPSILON		0.1			// point on plane side epsilon
#define	MAX_CLIP_VERTS	64
void ClipSkyPolygon (int nump, vec3_t vecs, int stage)
{
	float	*norm;
	float	*v;
	qboolean	front, back;
	float	d, e;
	float	dists[MAX_CLIP_VERTS];
	int		sides[MAX_CLIP_VERTS];
	vec3_t	newv[2][MAX_CLIP_VERTS];
	int		newc[2];
	int		i, j;

	if (nump > MAX_CLIP_VERTS-2)
		ri.Sys_Error (ERR_DROP, "ClipSkyPolygon: MAX_CLIP_VERTS");
	if (stage == 6)
	{	// fully clipped, so draw it
		DrawSkyPolygon (nump, vecs);
		return;
	}

	front = back = false;
	norm = skyclip[stage];
	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		d = DotProduct (v, norm);
		if (d > ON_EPSILON)
		{
			front = true;
			sides[i] = SIDE_FRONT;
		}
		else if (d < -ON_EPSILON)
		{
			back = true;
			sides[i] = SIDE_BACK;
		}
		else
			sides[i] = SIDE_ON;
		dists[i] = d;
	}

	if (!front || !back)
	{	// not clipped
		ClipSkyPolygon (nump, vecs, stage+1);
		return;
	}

	// clip it
	sides[i] = sides[0];
	dists[i] = dists[0];
	VectorCopy (vecs, (vecs+(i*3)) );
	newc[0] = newc[1] = 0;

	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		switch (sides[i])
		{
		case SIDE_FRONT:
			VectorCopy (v, newv[0][newc[0]]);
			newc[0]++;
			break;
		case SIDE_BACK:
			VectorCopy (v, newv[1][newc[1]]);
			newc[1]++;
			break;
		case SIDE_ON:
			VectorCopy (v, newv[0][newc[0]]);
			newc[0]++;
			VectorCopy (v, newv[1][newc[1]]);
			newc[1]++;
			break;
		}

		if (sides[i] == SIDE_ON || sides[i+1] == SIDE_ON || sides[i+1] == sides[i])
			continue;

		d = dists[i] / (dists[i] - dists[i+1]);
		for (j=0 ; j<3 ; j++)
		{
			e = v[j] + d*(v[j+3] - v[j]);
			newv[0][newc[0]][j] = e;
			newv[1][newc[1]][j] = e;
		}
		newc[0]++;
		newc[1]++;
	}

	// continue
	ClipSkyPolygon (newc[0], newv[0][0], stage+1);
	ClipSkyPolygon (newc[1], newv[1][0], stage+1);
}

/*
=================
R_AddSkySurface
=================
*/
void R_AddSkySurface (msurface_t *fa)
{
	int			i;
	vec3_t		verts[MAX_CLIP_VERTS];
	glpoly_t	*p;

	// calculate vertex values for sky box
	for (p=fa->polys ; p ; p=p->next)
	{
		for (i=0 ; i<p->numverts ; i++)
		{
			VectorSubtract (p->verts[i], r_origin, verts[i]);
		}
		ClipSkyPolygon (p->numverts, verts[0], 0);
	}
}


/*
==============
R_ClearSkyBox
==============
*/
void R_ClearSkyBox (void)
{
	int		i;

	for (i=0 ; i<6 ; i++)
	{
		skymins[0][i] = skymins[1][i] = 999999;
		skymaxs[0][i] = skymaxs[1][i] = -999999;
	}
}


void MakeSkyVec (float s, float t, int axis, float *tex_s, float *tex_t, float *vec)
{
	vec3_t		v, b;
	int			j, k;

	b[0] = s * skydistance->value; // DMP skybox size changes
	b[1] = t * skydistance->value; // DMP
	b[2] = skydistance->value;  // DMP

	for (j=0 ; j<3 ; j++)
	{
		k = st_to_vec[axis][j];
		if (k < 0)
			v[j] = -b[-k - 1];
		else
			v[j] = b[k - 1];
	}

	// avoid bilerp seam
	s = (s+1)*0.5;
	t = (t+1)*0.5;

	if (s < sky_min)
		s = sky_min;
	else if (s > sky_max)
		s = sky_max;
	if (t < sky_min)
		t = sky_min;
	else if (t > sky_max)
		t = sky_max;

	t = 1.0 - t;

	*tex_s = s;
	*tex_t = t;
	VectorCopy(v, vec);
}

/*
==============
R_DrawSkyBox
==============
*/
int	skytexorder[6] = {0,2,1,3,4,5};
void R_DrawSkyBox (void)
{
	int		i, l;
	float	s ,t;
	vec3_t	point;
	rscript_t *rs = NULL;


	if (skyrotate)
	{	// check for no sky at all
		for (i=0 ; i<6 ; i++)
			if (skymins[0][i] < skymaxs[0][i]
			&& skymins[1][i] < skymaxs[1][i])
				break;
		if (i == 6)
			return;		// nothing visible
	}

	qglPushMatrix ();
	qglTranslatef (r_origin[0], r_origin[1], r_origin[2]);
	qglRotatef (r_newrefdef.time * skyrotate, skyaxis[0], skyaxis[1], skyaxis[2]);

	for (i=0 ; i<6 ; i++)
	{
		if (skyrotate)
		{	// hack, forces full sky to draw when rotating
			skymins[0][i] = -1;
			skymins[1][i] = -1;
			skymaxs[0][i] = 1;
			skymaxs[1][i] = 1;
		}

		if (skymins[0][i] >= skymaxs[0][i]
		|| skymins[1][i] >= skymaxs[1][i])
			continue;

		GL_ShadeModel (GL_SMOOTH);
		
		if (r_shaders->value)
			rs=RS_FindScript(sky_images[skytexorder[i]]->bare_name);

		if (!rs)
		{
			GL_Bind (sky_images[skytexorder[i]]->texnum);

			qglBegin (GL_QUADS);

			MakeSkyVec (skymins[0][i], skymins[1][i], i, &s, &t, point);
			qglTexCoord2f (s, t);
			qglVertex3fv (point);

			MakeSkyVec (skymins[0][i], skymaxs[1][i], i, &s, &t, point);
			qglTexCoord2f (s, t);
			qglVertex3fv (point);

			MakeSkyVec (skymaxs[0][i], skymaxs[1][i], i, &s, &t, point);
			qglTexCoord2f (s, t);
			qglVertex3fv (point);

			MakeSkyVec (skymaxs[0][i], skymins[1][i], i, &s, &t, point);
			qglTexCoord2f (s, t);
			qglVertex3fv (point);

			qglEnd ();
		}
		else
		{
			float	txm,tym, alpha,s,t;
			float xoff, yoff;
			rs_stage_t *stage;

			RS_ReadyScript(rs);

			stage=rs->stage;
			while (stage) 
			{
				if (stage->colormap.enabled)
					qglDisable (GL_TEXTURE_2D);
				else if (stage->dynamic)
					GL_Bind (r_dynamicimage->texnum);
				else if (stage->anim_count)
					GL_Bind(RS_Animate(stage));
				else
					GL_Bind (stage->texture->texnum);

				if (stage->envmap)
				{
					GL_Spheremap(true);
				}

				if (stage->blendfunc.blend) 
				{
					GL_BlendFunction(stage->blendfunc.source,stage->blendfunc.dest);
					GLSTATE_ENABLE_BLEND
				} 
				else 
				{
					GLSTATE_DISABLE_BLEND
				}

				if (stage->alphashift.min || stage->alphashift.speed) 
				{
					alpha=0;

					if (!stage->alphashift.speed && stage->alphashift.min > 0) 
					{
						alpha=stage->alphashift.min;
					} 
					else if (stage->alphashift.speed) 
					{
						alpha=sin(rs_realtime * stage->alphashift.speed);
						alpha=(alpha+1)*0.5f;
						if (alpha > stage->alphashift.max) alpha=stage->alphashift.max;
						if (alpha < stage->alphashift.min) alpha=stage->alphashift.min;
					}
				} 
				else
					alpha=1.0f;

				if (stage->alphamask) 
				{
					GLSTATE_ENABLE_ALPHATEST
				} 
				else 
				{
					GLSTATE_DISABLE_ALPHATEST
				}

				{
					float red = 1, green = 1, blue = 1;
					
					if (stage->colormap.enabled)
					{
						red = stage->colormap.red/255.0;
						green = stage->colormap.green/255.0;
						blue = stage->colormap.blue/255.0;
					}

					qglColor4f(red,green,blue, alpha);
				}


				qglBegin (GL_QUADS);

				MakeSkyVec (skymins[0][i], skymins[1][i], i, &s, &t, point);
				RS_SetTexcoords2D (stage, &s, &t);
				qglTexCoord2f (s, t);
				qglVertex3fv (point);

				MakeSkyVec (skymins[0][i], skymaxs[1][i], i, &s, &t, point);
				RS_SetTexcoords2D (stage, &s, &t);
				qglTexCoord2f (s, t);
				qglVertex3fv (point);

				MakeSkyVec (skymaxs[0][i], skymaxs[1][i], i, &s, &t, point);
				RS_SetTexcoords2D (stage, &s, &t);
				qglTexCoord2f (s, t);
				qglVertex3fv (point);

				MakeSkyVec (skymaxs[0][i], skymins[1][i], i, &s, &t, point);
				RS_SetTexcoords2D (stage, &s, &t);
				qglTexCoord2f (s, t);
				qglVertex3fv (point);

				qglEnd();

				qglColor4f(1,1,1,1);
				if (stage->colormap.enabled)
					qglEnable (GL_TEXTURE_2D);
				GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				GLSTATE_DISABLE_TEXGEN

				stage=stage->next;
			}
			GLSTATE_ENABLE_ALPHATEST
			GLSTATE_DISABLE_BLEND

		}
	}

	qglPopMatrix ();
}


/*
============
R_SetSky
============
*/
// 3dstudio environment map names
char	*suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
void R_SetSky (char *name, float rotate, vec3_t axis)
{
	int		i;
	char	pathname[MAX_QPATH];

	strncpy (skyname, name, sizeof(skyname)-1);
	skyrotate = rotate;
	VectorCopy (axis, skyaxis);

	for (i=0 ; i<6 ; i++)
	{
		Com_sprintf (pathname, sizeof(pathname), "env/%s%s.pcx", skyname, suf[i]);
		sky_images[i] = GL_FindImage (pathname, it_sky);

		if (!sky_images[i])
			sky_images[i] = r_notexture;

		sky_min = DIV512;
		sky_max = 511.0*DIV512;
	}
}

/*
	Surface Subdivision
	Modified By Robert 'Heffo' Heffernan
	6/1/2002 12:13pm GMT +10
*/
#define	BLOCK_WIDTH		128
#define	BLOCK_HEIGHT	128

void SubdivideLightmappedPolygon (int numverts, float *verts, float subdivide_size)
{
	int		i, j, k;
	vec3_t	mins, maxs;
	float	m;
	float	*v;
	vec3_t	front[64], back[64];
	int		f, b;
	float	dist[64];
	float	frac;
	glpoly_t	*poly;
	float	s, t;
	vec3_t	total;
	float	total_s, total_t, total_u, total_v;

	if (numverts > 60)
		ri.Sys_Error (ERR_DROP, "numverts = %i", numverts);

	BoundPoly (numverts, verts, mins, maxs);

	for (i=0 ; i<3 ; i++)
	{
		m = (mins[i] + maxs[i]) * 0.5;
		m = subdivide_size * floor (m/subdivide_size + 0.5);
		if (maxs[i] - m < 8)
			continue;
		if (m - mins[i] < 8)
			continue;

		// cut it
		v = verts + i;
		for (j=0 ; j<numverts ; j++, v+= 3)
			dist[j] = *v - m;

		// wrap cases
		dist[j] = dist[0];
		v-=i;
		VectorCopy (verts, v);

		f = b = 0;
		v = verts;
		for (j=0 ; j<numverts ; j++, v+= 3)
		{
			if (dist[j] >= 0)
			{
				VectorCopy (v, front[f]);
				f++;
			}
			if (dist[j] <= 0)
			{
				VectorCopy (v, back[b]);
				b++;
			}
			if (dist[j] == 0 || dist[j+1] == 0)
				continue;
			if ( (dist[j] > 0) != (dist[j+1] > 0) )
			{
				// clip point
				frac = dist[j] / (dist[j] - dist[j+1]);
				for (k=0 ; k<3 ; k++)
					front[f][k] = back[b][k] = v[k] + frac*(v[3+k] - v[k]);
				f++;
				b++;
			}
		}

		SubdivideLightmappedPolygon (f, front[0], subdivide_size);
		SubdivideLightmappedPolygon (b, back[0], subdivide_size);
		return;
	}

	// add a point in the center to help keep warp valid
	poly = Hunk_Alloc (sizeof(glpoly_t) + ((numverts-4)+2) * VERTEXSIZE*sizeof(float));
	poly->next = warpface->polys;
	warpface->polys = poly;
	poly->numverts = numverts+2;
	VectorClear (total);
	total_s = 0; total_t = 0;
	total_u = 0; total_v = 0;

	for (i=0 ; i<numverts ; i++, verts+= 3)
	{
		VectorCopy (verts, poly->verts[i+1]);
		s = DotProduct (verts, warpface->texinfo->vecs[0]) + warpface->texinfo->vecs[0][3];
		s /= warpface->texinfo->image->width;

		t = DotProduct (verts, warpface->texinfo->vecs[1]) + warpface->texinfo->vecs[1][3];
		t /= warpface->texinfo->image->height;

		total_s += s;
		total_t += t;
		VectorAdd (total, verts, total);

		poly->verts[i+1][3] = s;
		poly->verts[i+1][4] = t;

		//
		// lightmap texture coordinates
		//
		s = DotProduct (verts, warpface->texinfo->vecs[0]) + warpface->texinfo->vecs[0][3];
		s -= warpface->texturemins[0];
		s += warpface->light_s*16;
		s += 8;
		s /= BLOCK_WIDTH*16; //fa->texinfo->texture->width;

		t = DotProduct (verts, warpface->texinfo->vecs[1]) + warpface->texinfo->vecs[1][3];
		t -= warpface->texturemins[1];
		t += warpface->light_t*16;
		t += 8;
		t /= BLOCK_HEIGHT*16; //fa->texinfo->texture->height;

		total_u += s;
		total_v += t;

		poly->verts[i+1][5] = s;
		poly->verts[i+1][6] = t;
	}

	VectorScale (total, (1.0/numverts), poly->verts[0]);
	poly->verts[0][3] = total_s/numverts;
	poly->verts[0][4] = total_t/numverts;

	poly->verts[0][5] = total_u/numverts;
	poly->verts[0][6] = total_v/numverts;

	// copy first vertex to last
	memcpy (poly->verts[i+1], poly->verts[1], sizeof(poly->verts[0]));
}

/*
================
GL_SubdivideLightmappedSurface

Breaks a polygon up along axial arbitary ^2 unit
boundaries so that vertex warps
can be done without looking like shit.
================
*/
void GL_SubdivideLightmappedSurface (msurface_t *fa, float subdivide_size)
{
	vec3_t		verts[64];
	int			numverts;
	int			i;
	int			lindex;
	float		*vec;

	warpface = fa;

	//
	// convert edges back to a normal polygon
	//
	numverts = 0;
	for (i=0 ; i<fa->numedges ; i++)
	{
		lindex = loadmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
			vec = loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position;
		else
			vec = loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position;
		VectorCopy (vec, verts[numverts]);
		numverts++;
	}

	SubdivideLightmappedPolygon (numverts, verts[0], subdivide_size);
}
