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

// draw.c

#include "gl_local.h"

image_t		*draw_chars;

// vertex arrays
float	tex_array[MAX_ARRAY][2];
float	vert_array[MAX_ARRAY][3];
float	col_array[MAX_ARRAY][4];

extern	qboolean	scrap_dirty;
void Scrap_Upload (void);

void RefreshFont (void)
{

	draw_chars = GL_FindImage (va("fonts/%s.pcx", con_font->string), it_pic);
	if (!draw_chars)
	{
		draw_chars = GL_FindImage ("fonts/default.pcx", it_pic);
		ri.Cvar_Set( "con_font", "default" );
	}

	GL_Bind( draw_chars->texnum );
	
	con_font->modified = false;
}

/*
===============
Draw_InitLocal
===============
*/
void Draw_InitLocal (void)
{
	image_t	*Draw_FindPic (char *name);


	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	RefreshFont();
}



/*
================
Draw_Char

Draws one 8*8 graphics character with 0 being transparent.
It can be clipped to the top of the screen to allow the console to be
smoothly scrolled off.
================
*/

float	CharMapScale (void)
{
	return (draw_chars->width/128.0); //current width / original width
}

void Draw_Character (float x, float y, float frow, float fcol, float size, float scale,
					int red, int green, int blue, int alpha, qboolean italic)
{
	float italicAdd = 0;

	if (italic)
		italicAdd = scale*0.25;

	qglColor4ub( red, green, blue, alpha);

	qglTexCoord2f (fcol, frow);
	qglVertex2f (x+italicAdd, y);

	qglTexCoord2f (fcol + size, frow);
	qglVertex2f (x+scale+italicAdd, y);

	qglTexCoord2f (fcol + size, frow + size);
	qglVertex2f (x+scale-italicAdd, y+scale);

	qglTexCoord2f (fcol, frow + size);
	qglVertex2f (x-italicAdd, y+scale);
}

void Draw_ScaledChar (float x, float y, int num, float scale, 
	 int red, int green, int blue, int alpha, qboolean italic)
{
	int				row, col;
	float			frow, fcol, size, cscale;

	num &= 255;


	if (alpha >= 254)
		alpha = 254;
	else if (alpha <= 1)
		alpha = 1;

	if ( (num&127) == 32 )
		return;		// space

	if (y <= -8)
		return;			// totally off screen

	row = num>>4;
	col = num&15;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.0625;
	cscale = scale * FONT_SIZE;

	{
		GLSTATE_DISABLE_ALPHATEST
		GL_TexEnv( GL_MODULATE );
		GLSTATE_ENABLE_BLEND
		qglDepthMask   (false);
		qglEnable(GL_POLYGON_SMOOTH);
	}

	GL_Bind(draw_chars->texnum);


	qglBegin (GL_QUADS);
	{
		Draw_Character(x, y, frow, fcol, size, cscale,
						red, green, blue, alpha, italic);
	}
	qglEnd ();

	{
		qglDepthMask (true);
		GL_TexEnv( GL_REPLACE );
		GLSTATE_DISABLE_BLEND
		qglColor4f   (1,1,1,1);
		GLSTATE_ENABLE_ALPHATEST
		qglDisable(GL_POLYGON_SMOOTH);
	}
}

void Draw_Char (float x, float y, int num, int alpha)
{
	int				row, col;
	float			frow, fcol, size, scale;

	num &= 255;


	if (alpha >= 254)
		alpha = 254;
	else if (alpha <= 1)
		alpha = 1;

	if ( (num&127) == 32 )
		return;		// space

	if (y <= -FONT_SIZE)
		return;			// totally off screen

	row = num>>4;
	col = num&15;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.0625;
	scale = FONT_SIZE;

	{
		GLSTATE_DISABLE_ALPHATEST
		GL_TexEnv( GL_MODULATE );
		GLSTATE_ENABLE_BLEND
		qglDepthMask   (false);
		qglEnable(GL_POLYGON_SMOOTH);
	}

	GL_Bind(draw_chars->texnum);


	qglBegin (GL_QUADS);
	{
		Draw_Character(x, y, frow, fcol, size, scale,
						255, 255, 255, alpha, false);
	}
	qglEnd ();

	{
		qglDepthMask (true);
		GL_TexEnv( GL_REPLACE );
		GLSTATE_DISABLE_BLEND
		qglColor4f   (1,1,1,1);
		GLSTATE_ENABLE_ALPHATEST
		qglDisable(GL_POLYGON_SMOOTH);
	}
}

/*
=============
Draw_FindPic
=============
*/
image_t	*Draw_FindPic (char *name)
{
	image_t *gl;
	char	fullname[MAX_QPATH];

	if (name[0] != '/' && name[0] != '\\')
	{
		Com_sprintf (fullname, sizeof(fullname), "pics/%s.pcx", name);
		gl = GL_FindImage (fullname, it_pic);
	}
	else
		gl = GL_FindImage (name+1, it_pic);

	return gl;
}

/*
=============
Draw_GetPicSize
=============
*/


void ShaderResizePic( image_t *gl, int *w, int *h)
{
	rscript_t *rs = NULL;

	if (r_shaders->value)
		rs=RS_FindScript(gl->bare_name);
	if (!rs)
		return;
	if (!rs->picsize.enable)
		return;

return;

	*w = rs->picsize.width;
	*h = rs->picsize.height;
}

void Draw_GetPicSize (int *w, int *h, char *pic)
{
	image_t *gl;

	gl = Draw_FindPic (pic);
	if (!gl)
	{
		*w = *h = -1;
		return;
	}
	*w = gl->width;
	*h = gl->height;
	ShaderResizePic(gl, w, h);
}

void Draw_ShaderPic (image_t *gl)
{
	rscript_t *rs = NULL;
	float	txm,tym, alpha,s,t;
	rs_stage_t *stage;

	if (r_shaders->value)
		rs=RS_FindScript(gl->bare_name);

	if (!rs) 
	{
		GLSTATE_DISABLE_ALPHATEST
		GLSTATE_ENABLE_BLEND
		GL_TexEnv( GL_MODULATE );
		qglColor4f(1,1,1,DIV254BY255);
		VA_SetElem4(col_array[0], 1,1,1,1);
		VA_SetElem4(col_array[1], 1,1,1,1);
		VA_SetElem4(col_array[2], 1,1,1,1);
		VA_SetElem4(col_array[3], 1,1,1,1);
		GL_Bind (gl->texnum);
		VA_SetElem2(tex_array[0],gl->sl, gl->tl);
		VA_SetElem2(tex_array[1],gl->sh, gl->tl);
		VA_SetElem2(tex_array[2],gl->sh, gl->th);
		VA_SetElem2(tex_array[3],gl->sl, gl->th);
		qglDrawArrays (GL_QUADS, 0, 4);
	} 
	else 
	{
		RS_ReadyScript(rs);

		stage=rs->stage;
		while (stage) 
		{
			float red = 1, green = 1, blue = 1;

			if (stage->blendfunc.blend) 
			{
				GLSTATE_ENABLE_BLEND
				GL_BlendFunction(stage->blendfunc.source,stage->blendfunc.dest);
			}
			else
			{
				GLSTATE_DISABLE_BLEND
			}

			alpha=1.0f;
			if (stage->alphashift.min || stage->alphashift.speed) 
			{
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

			if (stage->alphamask) 
			{
				GLSTATE_ENABLE_ALPHATEST
			} 
			else 
			{
				GLSTATE_DISABLE_ALPHATEST
			}

			if (stage->colormap.enabled)
			{
				red = stage->colormap.red/255.0;
				green = stage->colormap.green/255.0;
				blue = stage->colormap.blue/255.0;
			}

			qglColor4f(red,green,blue, alpha);
			VA_SetElem4(col_array[0], red,green,blue, alpha);
			VA_SetElem4(col_array[1], red,green,blue, alpha);
			VA_SetElem4(col_array[2], red,green,blue, alpha);
			VA_SetElem4(col_array[3], red,green,blue, alpha);

			if (stage->colormap.enabled)
				qglDisable (GL_TEXTURE_2D);
			else if (stage->anim_count)
				GL_Bind(RS_Animate(stage));
			else
				GL_Bind (stage->texture->texnum);

			s = 0; t = 1;
			RS_SetTexcoords2D (stage, &s, &t);
			VA_SetElem2(tex_array[3],s, t);
			s = 0; t = 0;
			RS_SetTexcoords2D (stage, &s, &t);
			VA_SetElem2(tex_array[0],s, t);
			s = 1; t = 0;
			RS_SetTexcoords2D (stage, &s, &t);
			VA_SetElem2(tex_array[1],s, t);
			s = 1; t = 1;
			RS_SetTexcoords2D (stage, &s, &t);
			VA_SetElem2(tex_array[2],s, t);

			qglDrawArrays(GL_QUADS,0,4);

			qglColor4f(1,1,1,1);
			if (stage->colormap.enabled)
				qglEnable (GL_TEXTURE_2D);

			stage=stage->next;
		}

		qglColor4f(1,1,1,1);
		GLSTATE_ENABLE_ALPHATEST
		GLSTATE_DISABLE_BLEND
		GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
}

float CalcFov (float fov_x, float width, float height)
{
	float	a;
	float	x;

	if (fov_x < 1 || fov_x > 179)
		ri.Sys_Error (ERR_DROP, "Bad fov: %f", fov_x);

	x = width/tan(fov_x/360*M_PI);
	a = atan (height/x)*360/M_PI;
	return a;
}

void *RS_AnimateSkin (rs_stage_t *stage);
qboolean Draw_Shader_Model (image_t *gl, int x, int y, int w, int h)
{
	refdef_t refdef;
	char scratch[MAX_QPATH];
	int maxframe = 29;
	entity_t entity[64], *ent;
	float alpha;
	rscript_t *rs = NULL;
	rs_stage_t *stage;

	if (r_shaders->value)
		rs=RS_FindScript(gl->bare_name);
	if (!rs)
		return false;
	if (!rs->model)
		return false;
	//dont draw if x or y are off the screen
	if (x > vid.width || y > vid.height ||x<0 || y<0)
		return false;

	RS_ReadyScript(rs);
	stage=rs->stage;

	memset( &refdef, 0, sizeof( refdef ) );

	refdef.x = x;
	refdef.y = y;
	refdef.width = w;
	refdef.height = h;

	refdef.fov_x = 50;
	refdef.fov_y = CalcFov( refdef.fov_x, refdef.width, refdef.height );
	refdef.time = rs_realtime;

	refdef.num_entities = 0;
	refdef.entities = &entity[0];

	while (stage) 
	{
		memset( &entity[refdef.num_entities], 0, sizeof( entity[refdef.num_entities] ) );
		ent = &entity[refdef.num_entities];

		ent->model = R_RegisterModel( stage->model );

		if (stage->anim_count)
			ent->skin = RS_AnimateSkin (stage);
		else
			ent->skin = stage->texture;

		alpha=1.0f;
		if (stage->alphashift.min || stage->alphashift.speed) 
		{
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
		ent->alpha = alpha;

		ent->flags = RF_FULLBRIGHT|RF_DEPTHHACK;

		VectorCopy( stage->origin, ent->origin );
		VectorCopy( ent->origin, ent->oldorigin );

		ent->angles[0] = anglemod( stage->angle[0] );
		ent->angles[1] = anglemod( stage->angle[1] + stage->rot_speed * rs_realtime);
		ent->angles[2] = anglemod( stage->angle[2] );
		
		if (stage->frames.enabled)
		{
			int framecount, framenum, frametime;
			float backlerp, realtime = rs_realtime * stage->frames.speed * 10.0;
			frametime = realtime;

			backlerp = 1 - (realtime - frametime);

			if (stage->frames.start == stage->frames.end)
			{
				ent->frame = stage->frames.start;
				ent->oldframe = stage->frames.end;
				ent->backlerp = 0.0;
			}
			else if (stage->frames.start < stage->frames.end) //frames positive
			{
				framecount = stage->frames.end - stage->frames.start + 1;
				framenum =  stage->frames.start + frametime % framecount;

				ent->frame = framenum;
				ent->oldframe = (framenum == stage->frames.start)? stage->frames.end: framenum-1;
				ent->backlerp = backlerp;
			}
			else //frames negative
			{
				framecount = stage->frames.start - stage->frames.end + 1;
				framenum = stage->frames.start - frametime % framecount;

				ent->frame = framenum;
				ent->oldframe = (framenum == stage->frames.start)? stage->frames.end: framenum+1;
				ent->backlerp = backlerp;

			}
		}
		else
		{
			ent->frame = 0;
			ent->oldframe = 0;
			ent->backlerp = 0.0;
		}

		// scale
		ent->scale = 1;
		if (stage->scale.scaleX) 
		{
			switch (stage->scale.typeX) 
			{
			case 0:	// static
				ent->scale *= stage->scale.scaleX;
				break;
			case 1:	// sine
				ent->scale *= stage->scale.scaleX*sin(rs_realtime*0.05);
				break;
			case 2:	// cosine
				ent->scale *= stage->scale.scaleX*cos(rs_realtime*0.05);
				break;
			}
		}

		refdef.num_entities++;
		stage = stage->next;
	}

	refdef.areabits = 0;
	refdef.lightstyles = 0;
	refdef.rdflags = RDF_NOWORLDMODEL;

	R_RenderFrame( &refdef );

	return true;
}

/*
=============
Draw_StretchPic
=============
*/
void Draw_StretchPic (int x, int y, int w, int h, char *pic)
{
	image_t *gl;

	gl = Draw_FindPic (pic);
	if (!gl)
	{
		ri.Con_Printf (PRINT_ALL, "Can't find pic: %s\n", pic);
		return;
	}

	if (Draw_Shader_Model (gl, x, y, w, h))
		return;

	if (scrap_dirty)
		Scrap_Upload ();

	VA_SetElem2(vert_array[0],x, y);
	VA_SetElem2(vert_array[1],x+w, y);
	VA_SetElem2(vert_array[2],x+w, y+h);
	VA_SetElem2(vert_array[3],x, y+h);

	Draw_ShaderPic(gl);
}

/*
=============
Draw_ScaledPic
=============
*/

void Draw_ScaledPic (int x, int y, float scale, char *pic)
{
	int w, h;
	float xoff, yoff;
	image_t *gl;

	gl = Draw_FindPic (pic);
	if (!gl)
	{
		ri.Con_Printf (PRINT_ALL, "Can't find pic: %s\n", pic);
		return;
	}

	w = gl->width;
	h = gl->height;

	xoff = (gl->width*scale-gl->width);
	yoff = (gl->height*scale-gl->height);

	ShaderResizePic( gl, &w, &h);

	if (Draw_Shader_Model (gl, x, y, w+xoff, h+yoff))
		return;

	if (scrap_dirty)
		Scrap_Upload ();

	VA_SetElem2(vert_array[0],x, y);
	VA_SetElem2(vert_array[1],x+w+xoff, y);
	VA_SetElem2(vert_array[2],x+w+xoff, y+h+yoff);
	VA_SetElem2(vert_array[3],x, y+h+yoff);

	Draw_ShaderPic(gl);
}

/*
=============
Draw_Pic
=============
*/

void Draw_Pic (int x, int y, char *pic)
{
	image_t *gl;
	int w, h;

	gl = Draw_FindPic (pic);

	if (!gl)
	{
		ri.Con_Printf (PRINT_ALL, "Can't find pic: %s\n", pic);
		return;
	}
	
	w = gl->width;
	h = gl->height;

	ShaderResizePic( gl, &w, &h);

	if (Draw_Shader_Model (gl, x, y, w, h))
		return;

	if (scrap_dirty)
		Scrap_Upload ();

	VA_SetElem2(vert_array[0],x, y);
	VA_SetElem2(vert_array[1],x+w, y);
	VA_SetElem2(vert_array[2],x+w, y+h);
	VA_SetElem2(vert_array[3],x, y+h);

	Draw_ShaderPic(gl);
}



/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear (int x, int y, int w, int h, char *pic)
{
	image_t	*image;

	image = Draw_FindPic (pic);
	if (!image)
	{
		ri.Con_Printf (PRINT_ALL, "Can't find pic: %s\n", pic);
		return;
	}

	GL_Bind (image->texnum);
	qglBegin (GL_QUADS);
	qglTexCoord2f (x/64.0, y/64.0);
	qglVertex2f (x, y);
	qglTexCoord2f ( (x+w)/64.0, y/64.0);
	qglVertex2f (x+w, y);
	qglTexCoord2f ( (x+w)/64.0, (y+h)/64.0);
	qglVertex2f (x+w, y+h);
	qglTexCoord2f ( x/64.0, (y+h)/64.0 );
	qglVertex2f (x, y+h);
	qglEnd ();
}


/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c)
{
	union
	{
		unsigned	c;
		byte		v[4];
	} color;

	if ( (unsigned)c > 255)
		ri.Sys_Error (ERR_FATAL, "Draw_Fill: bad color");

	qglDisable (GL_TEXTURE_2D);

	color.c = d_8to24table[c];
	qglColor3f (color.v[0]*DIV255,
				color.v[1]*DIV255,
				color.v[2]*DIV255);

	qglBegin (GL_QUADS);

	qglVertex2f (x,y);
	qglVertex2f (x+w, y);
	qglVertex2f (x+w, y+h);
	qglVertex2f (x, y+h);

	qglEnd ();
	qglColor3f (1,1,1);
	qglEnable (GL_TEXTURE_2D);
}

//=============================================================================

/*
================
Draw_FadeBox

================
*/
void Draw_FadeBox (int x, int y, int w, int h, float r, float g, float b, float alpha)
{

	GLSTATE_DISABLE_ALPHATEST
	GL_TexEnv( GL_MODULATE );
	GLSTATE_ENABLE_BLEND
	qglDisable (GL_TEXTURE_2D);
	qglColor4f (r, g, b, alpha);

	qglBegin (GL_QUADS);
	{
		qglVertex2f (x,y);
		qglVertex2f (w+x, y);
		qglVertex2f (w+x, h+y);
		qglVertex2f (x, h+y);
	}
	qglEnd ();

	qglColor4f (1,1,1,1);
	qglEnable (GL_TEXTURE_2D);
	GLSTATE_DISABLE_BLEND
	GL_TexEnv( GL_REPLACE );
	GLSTATE_ENABLE_ALPHATEST
}


//====================================================================


/*
=============
Draw_StretchRaw
=============
*/
extern unsigned	r_rawpalette[256];

void Draw_StretchRaw (int x, int y, int w, int h, int cols, int rows, byte *data)
{
	unsigned	image32[256*256];
	unsigned char image8[256*256];
	int			i, j, trows;
	byte		*source;
	int			frac, fracstep;
	float		hscale;
	int			row;
	float		t;

	GL_Bind (0);

	if (rows<=256)
	{
		hscale = 1;
		trows = rows;
	}
	else
	{
		hscale = rows*DIV256;
		trows = 256;
	}
	t = rows*hscale / 256;

	if ( !qglColorTableEXT )
	{
		unsigned *dest;

		for (i=0 ; i<trows ; i++)
		{
			row = (int)(i*hscale);
			if (row > rows)
				break;
			source = data + cols*row;
			dest = &image32[i*256];
			fracstep = cols*0x10000*DIV256;
			frac = fracstep >> 1;
			for (j=0 ; j<256 ; j++)
			{
				dest[j] = r_rawpalette[source[frac>>16]];
				frac += fracstep;
			}
		}

		qglTexImage2D (GL_TEXTURE_2D, 0, gl_tex_solid_format, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, image32);
	}
	else
	{
		unsigned char *dest;

		for (i=0 ; i<trows ; i++)
		{
			row = (int)(i*hscale);
			if (row > rows)
				break;
			source = data + cols*row;
			dest = &image8[i*256];
			fracstep = cols*0x10000*DIV256;
			frac = fracstep >> 1;
			for (j=0 ; j<256 ; j++)
			{
				dest[j] = source[frac>>16];
				frac += fracstep;
			}
		}

		qglTexImage2D( GL_TEXTURE_2D, 
			           0, 
					   GL_COLOR_INDEX8_EXT, 
					   256, 256, 
					   0, 
					   GL_COLOR_INDEX, 
					   GL_UNSIGNED_BYTE, 
					   image8 );
	}
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	qglBegin (GL_QUADS);
	qglTexCoord2f (0, 0);
	qglVertex2f (x, y);
	qglTexCoord2f (1, 0);
	qglVertex2f (x+w, y);
	qglTexCoord2f (1, t);
	qglVertex2f (x+w, y+h);
	qglTexCoord2f (0, t);
	qglVertex2f (x, y+h);
	qglEnd ();
}

