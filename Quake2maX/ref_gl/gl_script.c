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

// gl_script.c - scripted texture rendering - MrG

#include "gl_local.h"
#include <io.h>

void CIN_FreeCin (int texnum);

extern float	r_turbsin[];

#define		TURBSCALE (256.0 / (2 * M_PI))

#define		TOK_DELIMINATORS "\r\n\t "

float		rs_realtime = 0;
rscript_t	*rs_rootscript = NULL;

void RS_ListScripts (void)
{
	rscript_t	*rs;
	int			i;

	for (i = 0, rs = rs_rootscript; rs; rs = rs->next, i++) 
		ri.Con_Printf (PRINT_ALL, ">> %s\n", rs->name);
}

int	RS_Random(rs_stage_t *stage, msurface_t *surf)
{
	random_stage_t	*randStage = stage->rand_stage;
	int number = 0, i;
	glpoly_t *poly;

	for (poly=surf->polys ; poly ; poly=poly->next)
		number += poly->center[0] + poly->center[1] + poly->center[2];

	for (i=0; i<(number%stage->rand_count) && randStage; randStage = randStage->next)
		;

	return  randStage->texture->texnum;
}

int RS_Animate (rs_stage_t *stage)
{
	anim_stage_t	*anim = stage->last_anim;
	float			time = rs_realtime * 1000 - 
		(stage->last_anim_time + stage->anim_delay);

	while (stage->last_anim_time < rs_realtime) 
	{
		anim = anim->next;
		if (!anim)
			anim = stage->anim_stage;
		stage->last_anim_time += stage->anim_delay;
	}

	stage->last_anim = anim;

	return anim->texture->texnum;
}
void *RS_AnimateSkin (rs_stage_t *stage)
{
	anim_stage_t	*anim = stage->last_anim;
	float			time = rs_realtime * 1000 - 
		(stage->last_anim_time + stage->anim_delay);

	while (stage->last_anim_time < rs_realtime) 
	{
		anim = anim->next;
		if (!anim)
			anim = stage->anim_stage;
		stage->last_anim_time += stage->anim_delay;
	}

	stage->last_anim = anim;

	return anim->texture;
}

float cutDot (vec3_t vec1, vec3_t vec2)
{
	float dot = DotProduct(vec1, vec2);
	if (dot>1) return 1;
	if (dot<-1) return -1;
	return dot;
}

void RS_ResetScript (rscript_t *rs)
{
	rs_stage_t		*stage = rs->stage, *tmp_stage;
	anim_stage_t	*anim, *tmp_anim;
	random_stage_t	*randStage, *tmp_rand;

	rs->name[0] = 0;
	
	while (stage != NULL)
	{
		if (stage->anim_count) 
		{
			anim = stage->anim_stage;
			while (anim != NULL) 
			{
				tmp_anim = anim;
				if (anim->texture)
					if (anim->texture->is_cin)
						CIN_FreeCin(anim->texture->texnum);
				anim = anim->next;
				free (tmp_anim);
			}
		}
		if (stage->rand_count) 
		{
			randStage = stage->rand_stage;
			while (randStage != NULL) 
			{
				tmp_rand = randStage;
				if (randStage->texture)
					if (randStage->texture->is_cin)
						CIN_FreeCin(randStage->texture->texnum);
				randStage = randStage->next;
				free (tmp_rand);
			}
		}

		tmp_stage = stage;
		stage = stage->next;

		free (tmp_stage);
	}

	rs->picsize.enable = false;
	rs->picsize.width = 0;
	rs->picsize.height = 0;
	rs->model = false;
	rs->mirror = false;
	rs->stage = NULL;
	rs->dontflush = false;
	rs->subdivide = 0;
	rs->warpdist = 0;
	rs->warpsmooth = 0;
	rs->warpspeed = 0;
	rs->ready = false;

	
}

void RS_ClearStage (rs_stage_t *stage)
{
	anim_stage_t	*anim = stage->anim_stage, *tmp_anim;
	random_stage_t	*randStage = stage->rand_stage, *tmp_rand;


	while (anim != NULL) 
	{
		tmp_anim = anim;
		anim = anim->next;
		free (tmp_anim);
	}
	while (randStage != NULL) 
	{
		tmp_rand = randStage;
		randStage = randStage->next;
		free (tmp_rand);
	}

	stage->last_anim = 0;
	stage->last_anim_time = 0;
	stage->anim_count = 0;

	stage->anim_delay = 0;
	stage->anim_stage = NULL;
	
	stage->rand_count = 0;
	stage->rand_stage = NULL;

	stage->has_alpha = false;
	stage->alphamask = false;

	stage->alphafunc = 0;

	stage->alphashift.max = 0;
	stage->alphashift.min = 0;
	stage->alphashift.speed = 0;

	stage->blendfunc.blend = false;
	stage->blendfunc.dest = stage->blendfunc.source = 0;

	stage->colormap.enabled = false;
	stage->colormap.red = 0;
	stage->colormap.green = 0;
	stage->colormap.blue = 0;

	stage->scale.scaleX = 0;
	stage->scale.scaleY = 0;
	stage->scale.typeX = 0;
	stage->scale.typeY = 0;

	stage->scroll.speedX = 0;
	stage->scroll.speedY = 0;
	stage->scroll.typeX = 0;
	stage->scroll.typeY = 0;

	stage->frames.enabled = false;
	stage->frames.start = 0;
	stage->frames.end = 0;

	stage->rot_speed = 0;
	
	VectorClear(stage->origin);
	VectorClear(stage->angle);

	stage->texture = NULL;


	stage->depthhack = 0;
	stage->envmap = false;
	stage->dynamic = false;
	stage->detail = false;

	stage->lightmap = true;

	stage->next = NULL;
}

rscript_t *RS_NewScript (char *name)
{
	rscript_t	*rs;

	if (!rs_rootscript) 
	{
		rs_rootscript = (rscript_t *)malloc(sizeof(rscript_t));
		rs = rs_rootscript;
	} 
	else 
	{
		rs = rs_rootscript;

		while (rs->next != NULL)
			rs = rs->next;

		rs->next = (rscript_t *)malloc(sizeof(rscript_t));
		rs = rs->next;
	}

	strncpy (rs->name, name, sizeof(rs->name));

	rs->stage = NULL;
	rs->next = NULL;
	rs->dontflush = false;
	rs->subdivide = 0;
	rs->warpdist = 0.0f;
	rs->warpsmooth = 0.0f;
	rs->ready = false;
	rs->mirror = false;
	rs->model = false;
	rs->picsize.enable = false;
	rs->picsize.width = 0;
	rs->picsize.height = 0;

	return rs;
}

rs_stage_t *RS_NewStage (rscript_t *rs)
{
	rs_stage_t	*stage;

	if (rs->stage == NULL) 
	{
		rs->stage = (rs_stage_t *)malloc(sizeof(rs_stage_t));
		stage = rs->stage;
	} 
	else
	{
		stage = rs->stage;
		while (stage->next != NULL)
			stage = stage->next;

		stage->next = (rs_stage_t *)malloc(sizeof(rs_stage_t));
		stage = stage->next;
	}

	strncpy (stage->name, "***r_notexture***", sizeof(stage->name));

	stage->rand_stage = NULL;
	stage->anim_stage = NULL;
	stage->next = NULL;
	stage->last_anim = NULL;

	RS_ClearStage (stage);

	return stage;
}

void RS_FreeAllScripts (void)
{
	rscript_t	*rs = rs_rootscript, *tmp_rs;

	while (rs != NULL) 
	{
		tmp_rs = rs->next;
		RS_ResetScript(rs);
		free (rs);
		rs = tmp_rs;
	}
}

void RS_ReloadImageScriptLinks (void)
{
	image_t		*image;
	int	i;

	for (i=0, image=gltextures ; i<numgltextures ; i++,image++)
		image->script = RS_FindScript(image->bare_name);

}

void RS_FreeScript(rscript_t *rs)
{
	rscript_t	*tmp_rs;

	if (!rs)
		return;

	if (rs_rootscript == rs) 
	{
		rs_rootscript = rs_rootscript->next;
		RS_ResetScript(rs);
		free (rs);
		return;
	}

	tmp_rs = rs_rootscript;
	while (tmp_rs->next != rs)
		tmp_rs = tmp_rs->next;
	tmp_rs->next = rs->next;

	RS_ResetScript (rs);

	free (rs);
}

void RS_FreeUnmarked (void)
{
	rscript_t	*rs = rs_rootscript, *tmp_rs;

	while (rs != NULL) 
	{
		tmp_rs = rs->next;

		if (!rs->dontflush)
			RS_FreeScript(rs);

		rs = tmp_rs;
	}
}

rscript_t *RS_FindScript(char *name)
{
	rscript_t	*rs = rs_rootscript;

	while (rs != NULL) 
	{
		if (!_stricmp(rs->name, name))
		{
			if (rs->stage)
			   return rs;
			else
			   return NULL;
		}

		rs = rs->next;
	}

	return NULL;
}

void RS_ReadyScript (rscript_t *rs)
{
	rs_stage_t		*stage;
	anim_stage_t	*anim;
	random_stage_t	*randStage;
	char			mode;

	if (rs->ready)
		return;

	mode = (rs->dontflush) ? it_pic : it_wall;
	stage = rs->stage;

	while (stage != NULL)
	{
		//set anim
		anim = stage->anim_stage;
		while (anim != NULL) 
		{
			anim->texture = GL_FindImage (anim->name, mode);
			if (!anim->texture)
				anim->texture = r_notexture;

			anim = anim->next;
		}

		//set tiling
		randStage = stage->rand_stage;
		while (randStage != NULL) 
		{
			randStage->texture = GL_FindImage (randStage->name, mode);
			if (!randStage->texture)
				randStage->texture = r_notexture;

			randStage = randStage->next;
		}

		//set name
		if (stage->name[0]) 
			stage->texture = GL_FindImage (stage->name, mode);
		if (!stage->texture)
			stage->texture = r_notexture;

		//check alpha
		if (stage->blendfunc.blend)
			stage->has_alpha = true;
		else
			stage->has_alpha = false;

		stage = stage->next;
	}

	rs->ready = true;
}


void RS_UpdateRegistration (void)
{
	rscript_t		*rs = rs_rootscript;
	rs_stage_t		*stage;
	anim_stage_t	*anim;
	random_stage_t	*randStage;
	int				mode;

	while (rs != NULL) 
	{
		mode = (rs->dontflush) ? it_pic : it_wall;
		stage = rs->stage;

		while (stage != NULL) 
		{
			anim = stage->anim_stage;
			while (anim != NULL) 
			{
				anim->texture = GL_FindImage(anim->name,mode);
				if (!anim->texture)
					anim->texture = r_notexture;
				anim = anim->next;
			}

			randStage = (random_stage_t*)stage->rand_stage;
			while (randStage != NULL) 
			{
				randStage->texture = GL_FindImage(randStage->name,mode);
				if (!randStage->texture)
					randStage->texture = r_notexture;
				randStage = randStage->next;
			}

			if (stage->name[0]) 
			{
				stage->texture = GL_FindImage(stage->name, mode);
			}

			if (!stage->texture)
				stage->texture = r_notexture;

			stage = stage->next;
		}

		rs = rs->next;
	}
}

int RS_BlendID (char *blend)
{
	if (!blend[0])
		return 0;
	if (!_stricmp (blend, "GL_ZERO"))
		return GL_ZERO;
	if (!_stricmp (blend, "GL_ONE"))
		return GL_ONE;
	if (!_stricmp (blend, "GL_DST_COLOR"))
		return GL_DST_COLOR;
	if (!_stricmp (blend, "GL_ONE_MINUS_DST_COLOR"))
		return GL_ONE_MINUS_DST_COLOR;
	if (!_stricmp (blend, "GL_SRC_ALPHA"))
		return GL_SRC_ALPHA;
	if (!_stricmp (blend, "GL_ONE_MINUS_SRC_ALPHA"))
		return GL_ONE_MINUS_SRC_ALPHA;
	if (!_stricmp (blend, "GL_DST_ALPHA"))
		return GL_DST_ALPHA;
	if (!_stricmp (blend, "GL_ONE_MINUS_DST_ALPHA"))
		return GL_ONE_MINUS_DST_ALPHA;
	if (!_stricmp (blend, "GL_SRC_ALPHA_SATURATE"))
		return GL_SRC_ALPHA_SATURATE;
	if (!_stricmp (blend, "GL_SRC_COLOR"))
		return GL_SRC_COLOR;
	if (!_stricmp (blend, "GL_ONE_MINUS_SRC_COLOR"))
		return GL_ONE_MINUS_SRC_COLOR;

	return 0;
}

int RS_FuncName (char *text)
{
	if (!_stricmp (text, "static"))			// static
		return 0;
	else if (!_stricmp (text, "sine"))		// sine wave
		return 1;
	else if (!_stricmp (text, "cosine"))	// cosine wave
		return 2;

	return 0;
}

/*
scriptname
{
	subdivide <size>
	vertexwarp <speed> <distance> <smoothness>
	safe
	{
		map <texturename>
		scroll <xtype> <xspeed> <ytype> <yspeed>
		blendfunc <source> <dest>
		alphashift <speed> <min> <max>
		anim <delay> <tex1> <tex2> <tex3> ... end
		envmap
		nolightmap
		alphamask
	}
}
*/

void rs_stage_map (rs_stage_t *stage, char **token)
{
	*token = strtok (NULL, TOK_DELIMINATORS);

	strncpy (stage->name, *token, sizeof(stage->name));
}

void rs_stage_model (rs_stage_t *stage, char **token)
{
	*token = strtok (NULL, TOK_DELIMINATORS);

	strncpy (stage->model, *token, sizeof(stage->model));
}

void rs_stage_colormap (rs_stage_t *stage, char **token)
{
	stage->colormap.enabled = true;

	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->colormap.red = atoi(*token);

	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->colormap.green = atoi(*token);

	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->colormap.blue = atoi(*token);
}

void rs_stage_frames (rs_stage_t *stage, char **token)
{
	stage->frames.enabled = true;

	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->frames.speed = atof(*token);

	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->frames.start = atoi(*token);

	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->frames.end = atoi(*token);
}

void rs_stage_scroll (rs_stage_t *stage, char **token)
{
	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->scroll.typeX = RS_FuncName(*token);
	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->scroll.speedX = atof(*token);
	
	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->scroll.typeY = RS_FuncName(*token);
	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->scroll.speedY = atof(*token);
}

void rs_stage_blendfunc (rs_stage_t *stage, char **token)
{
	stage->blendfunc.blend = true;

	*token = strtok (NULL, TOK_DELIMINATORS);

	if (!Q_stricmp (*token, "add")) 
	{
		stage->blendfunc.source = GL_ONE;
		stage->blendfunc.dest = GL_ONE;
	} 
	else if (!Q_stricmp (*token, "blend")) 
	{
		stage->blendfunc.source = GL_SRC_ALPHA;
		stage->blendfunc.dest = GL_ONE_MINUS_SRC_ALPHA;
	} 
	else if (!Q_stricmp (*token, "filter")) 
	{
		stage->blendfunc.source = GL_ZERO;
		stage->blendfunc.dest = GL_SRC_COLOR;
	} 
	else 
	{
		stage->blendfunc.source = RS_BlendID (*token);

		*token = strtok (NULL, TOK_DELIMINATORS);
		stage->blendfunc.dest = RS_BlendID (*token);
	}
}

void rs_stage_alphashift (rs_stage_t *stage, char **token)
{
	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->alphashift.speed = (float)atof(*token);

	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->alphashift.min = (float)atof(*token);

	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->alphashift.max = (float)atof(*token);
}

void rs_stage_random (rs_stage_t *stage, char **token)
{
	random_stage_t	*rand = (random_stage_t *)malloc(sizeof(random_stage_t));

	stage->rand_stage = rand;
	stage->rand_count = 0;

	*token = strtok(NULL, TOK_DELIMINATORS);
	
	while (_stricmp (*token, "end")) 
	{
		stage->rand_count++;

		strncpy (stage->name, *token, sizeof(stage->name));

		stage->texture = NULL;

		*token = strtok(NULL, TOK_DELIMINATORS);

		if (!_stricmp (*token, "end")) 
		{
			rand->next = NULL;
			break;
		}

		rand->next = (random_stage_t *)malloc(sizeof(random_stage_t));
		rand = rand->next;
	}
}

void rs_stage_anim (rs_stage_t *stage, char **token)
{
	anim_stage_t	*anim = (anim_stage_t *)malloc(sizeof(anim_stage_t));

	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->anim_delay = (float)atof(*token);

	stage->anim_stage = anim;
	stage->last_anim = anim;

	*token = strtok(NULL, TOK_DELIMINATORS);
	
	while (_stricmp (*token, "end")) 
	{
		stage->anim_count++;

		strncpy (anim->name, *token, sizeof(anim->name));

		anim->texture = NULL;

		*token = strtok(NULL, TOK_DELIMINATORS);

		if (!_stricmp (*token, "end")) 
		{
			anim->next = NULL;
			break;
		}

		anim->next = (anim_stage_t *)malloc(sizeof(anim_stage_t));
		anim = anim->next;
	}
}

void rs_stage_depthhack (rs_stage_t *stage, char **token)
{
	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->depthhack = (float)atof(*token);
}

void rs_stage_envmap (rs_stage_t *stage, char **token)
{
	stage->envmap = true;
}

void rs_stage_dynamic (rs_stage_t *stage, char **token)
{
	stage->dynamic = true;
}

void rs_stage_detail (rs_stage_t *stage, char **token)
{
	stage->detail = true;
}

void rs_stage_nolightmap (rs_stage_t *stage, char **token)
{
	stage->lightmap = false;
}

void rs_stage_alphamask (rs_stage_t *stage, char **token)
{
	stage->alphamask = true;
}

void rs_stage_rotate (rs_stage_t *stage, char **token)
{
	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->rot_speed = (float)atof(*token);
}

void rs_stage_origin (rs_stage_t *stage, char **token)
{
	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->origin[0] = (float)atof(*token);
	
	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->origin[1] = (float)atof(*token);
	
	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->origin[2] = (float)atof(*token);
}

void rs_stage_angle (rs_stage_t *stage, char **token)
{
	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->angle[0] = (float)atof(*token);
	
	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->angle[1] = (float)atof(*token);
	
	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->angle[2] = (float)atof(*token);
}

void rs_stage_scale (rs_stage_t *stage, char **token)
{
	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->scale.typeX = RS_FuncName(*token);
	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->scale.scaleX = atof(*token);
	
	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->scale.typeY = RS_FuncName(*token);
	*token = strtok (NULL, TOK_DELIMINATORS);
	stage->scale.scaleY = atof(*token);
}

void rs_stage_alphafunc (rs_stage_t *stage, char **token)
{
	*token = strtok (NULL, TOK_DELIMINATORS);

	if (!_stricmp (*token, "normal"))
		stage->alphafunc = ALPHAFUNC_NORMAL;
	else if (!_stricmp (*token, "-normal"))
		stage->alphafunc = -ALPHAFUNC_NORMAL;
	else if (!_stricmp (*token, "gloss"))
		stage->alphafunc = ALPHAFUNC_GLOSS;
	else if (!_stricmp (*token, "-gloss"))
		stage->alphafunc = -ALPHAFUNC_GLOSS;
	else if (!_stricmp (*token, "basic"))
		stage->alphafunc = ALPHAFUNC_BASIC;
	else if (!_stricmp (*token, "-basic"))
		stage->alphafunc = -ALPHAFUNC_BASIC;
}

static rs_stagekey_t rs_stagekeys[] = 
{
	{	"colormap",		&rs_stage_colormap		},
	{	"map",			&rs_stage_map			},
	{	"model",		&rs_stage_model			},
	{	"scroll",		&rs_stage_scroll		},
	{	"frames",		&rs_stage_frames		},
	{	"blendfunc",	&rs_stage_blendfunc		},
	{	"alphashift",	&rs_stage_alphashift	},
	{	"rand",			&rs_stage_random		},
	{	"anim",			&rs_stage_anim			},
	{	"envmap",		&rs_stage_envmap		},
	{	"depthhack",	&rs_stage_depthhack		},
	{	"nolightmap",	&rs_stage_nolightmap	},
	{	"alphamask",	&rs_stage_alphamask		},
	{	"rotate",		&rs_stage_rotate		},
	{	"origin",		&rs_stage_origin		},
	{	"angle",		&rs_stage_angle			},
	{	"scale",		&rs_stage_scale			},
	{	"dynamic",		&rs_stage_dynamic		},
	{	"detail",		&rs_stage_detail		},
	{	"alphafunc",	&rs_stage_alphafunc	},

	{	NULL,			NULL					}
};

static int num_stagekeys = sizeof (rs_stagekeys) / sizeof(rs_stagekeys[0]) - 1;

// =====================================================

void rs_script_safe (rscript_t *rs, char **token)
{
	rs->dontflush = true;
}

void rs_script_subdivide (rscript_t *rs, char **token)
{
	int divsize, p2divsize;

	*token = strtok (NULL, TOK_DELIMINATORS);
	divsize = atoi (*token);
 
	// cap max & min subdivide sizes
	if (divsize > 128)
		divsize = 128;
	else if (divsize <= 8)
		divsize = 8;

	// find the next smallest valid ^2 size, if not already one
	for (p2divsize = 2; p2divsize <= divsize ; p2divsize <<= 1 );

	p2divsize >>= 1;

	rs->subdivide = (char)p2divsize;
}

void rs_script_vertexwarp (rscript_t *rs, char **token)
{
	*token = strtok(NULL, TOK_DELIMINATORS);
	rs->warpspeed = atof (*token);
	*token = strtok(NULL, TOK_DELIMINATORS);
	rs->warpdist = atof (*token);
	*token = strtok(NULL, TOK_DELIMINATORS);
	rs->warpsmooth = atof (*token);

	if (rs->warpsmooth < 0.001f)
		rs->warpsmooth = 0.001f;
	else if (rs->warpsmooth > 1.0f)
		rs->warpsmooth = 1.0f;
}

void rs_script_mirror (rscript_t *rs, char **token)
{
	rs->mirror = true;
}

void rs_script_model (rscript_t *rs, char **token)
{
	rs->model = true;
}

void rs_script_picsize (rscript_t *rs, char **token)
{
	rs->picsize.enable = true;

	*token = strtok(NULL, TOK_DELIMINATORS);
	rs->picsize.width = atof (*token);
	
	*token = strtok(NULL, TOK_DELIMINATORS);
	rs->picsize.height = atof (*token);
}

static rs_scriptkey_t rs_scriptkeys[] = 
{
	{	"safe",			&rs_script_safe			},
	{	"subdivide",	&rs_script_subdivide	},
	{	"vertexwarp",	&rs_script_vertexwarp	},
	{	"mirror",		&rs_script_mirror		},
	{	"model",		&rs_script_model		},
	{	"picsize",		&rs_script_picsize		},
	{	NULL,			NULL					}
};

static int num_scriptkeys = sizeof (rs_scriptkeys) / sizeof(rs_scriptkeys[0]) - 1;

// =====================================================

void RS_LoadScript(char *script)
{
	qboolean		inscript = false, instage = false;
	char			ignored = 0;
	char			*token, *fbuf, *buf;
	rscript_t		*rs = NULL;
	rs_stage_t		*stage;
	unsigned char	tcmod = 0;
	unsigned int	len, i;

	len = ri.FS_LoadFile (script, (void **)&fbuf);

	if (!fbuf || len < 16) 
	{
		ri.Con_Printf (PRINT_ALL, "Could not load script %s\n", script);
		return;
	}

	buf = (char *)malloc(len+1);
	memcpy (buf, fbuf, len);
	buf[len] = 0;

	ri.FS_FreeFile (fbuf);

	token = strtok (buf, TOK_DELIMINATORS);

	while (token != NULL) 
	{
		if (!_stricmp (token, "/*") || !_stricmp (token, "["))
			ignored++;
		else if (!_stricmp (token, "*/") || !_stricmp (token, "]"))
			ignored--;

		if (!_stricmp (token, "//"))
		{
			//IGNORE
		}
		else if (!inscript && !ignored) 
		{
			if (!_stricmp (token, "{")) 
			{
				inscript = true;
			}
			else
			{
				rs = RS_FindScript(token);

				if (rs)
					RS_FreeScript(rs);

				rs = RS_NewScript(token);
			}
		} 
		else if (inscript && !ignored) 
		{
			if (!_stricmp(token, "}"))
			{
				if (instage) 
				{
					instage = false;
				} 
				else 
				{
					inscript = false;
				}
			} 
			else if (!_stricmp(token, "{"))
			{
				if (!instage) {
					instage = true;
					stage = RS_NewStage(rs);
				}
			}
			else
			{
				if (instage && !ignored) 
				{
					for (i = 0; i < num_stagekeys; i++) {
						if (!_stricmp (rs_stagekeys[i].stage, token))
						{
							rs_stagekeys[i].func (stage, &token);
							break;
						}
					}
				}
				else 
				{
					for (i = 0; i < num_scriptkeys; i++) 
					{
						if (!_stricmp (rs_scriptkeys[i].script, token)) 
						{
							rs_scriptkeys[i].func (rs, &token);
							break;
						}
					}
				}
			}			
		}

		token = strtok (NULL, TOK_DELIMINATORS);
	}

	free(buf);
}

void RS_ScanPathForScripts (void)
{
	char			script[MAX_OSPATH];
	char			dirstring[1024], *c;
	int				handle, npakscripts, i;
	struct			_finddata_t fileinfo;
	char			*dir = ri.FS_Gamedir();
	char			*basedir = va("./%s", BASEDIRNAME);
	char			**pakscripts;

	void			*delstuff;

	if (pakscripts = ri.FS_ListPak("scripts/", &npakscripts))
	{
		char *currentScript;

		for (i=0; i<npakscripts; i++)
		{	
			int len;
			char *p = NULL;

			currentScript = pakscripts[i];

			len = strlen(currentScript);

			//can never work
			if (len<8)
				continue;

			// /scripts/ directory only
			if (	toupper(currentScript[0])!='S'
				||	toupper(currentScript[1])!='C'
				||	toupper(currentScript[2])!='R'
				||	toupper(currentScript[3])!='I'
				||	toupper(currentScript[4])!='P'
				||	toupper(currentScript[5])!='T'
				||	toupper(currentScript[6])!='S'
				||	currentScript[7]!='/'
				)
				continue;

			//no subdirs, only used per specific map
			p = strstr(&currentScript[8], "/");
			if (p)
				continue;

			//load .rscript files

			if (	currentScript[len-8]!='.'
				||	toupper(currentScript[len-7])!='R'
				||	toupper(currentScript[len-6])!='S'
				||	toupper(currentScript[len-5])!='C'
				||	toupper(currentScript[len-4])!='R'
				||	toupper(currentScript[len-3])!='I'
				||	toupper(currentScript[len-2])!='P'
				||	toupper(currentScript[len-1])!='T'
				)
				continue;

			RS_LoadScript (currentScript);
		}
	}

	if (strcmp(dir,basedir))
	{
		Com_sprintf (dirstring, sizeof(dirstring), "%s/scripts/*.rscript", basedir);
		handle = _findfirst (dirstring, &fileinfo);

		if (handle != -1) 
		{
			do
			{
				if (fileinfo.name[0] == '.')
					continue;

				c = COM_SkipPath(fileinfo.name);
				Com_sprintf(script, MAX_OSPATH, "scripts/%s", c);
				RS_LoadScript (script);
			} while (_findnext( handle, &fileinfo ) != -1);

			_findclose (handle);
		}

	}

	Com_sprintf (dirstring, sizeof(dirstring), "%s/scripts/*.rscript", dir);
	handle = _findfirst (dirstring, &fileinfo);

	if (handle != -1) 
	{
		do 
		{
			if (fileinfo.name[0] == '.')
				continue;

			c = COM_SkipPath(fileinfo.name);
			Com_sprintf(script, MAX_OSPATH, "scripts/%s", c);
			RS_LoadScript (script);
		} while (_findnext( handle, &fileinfo ) != -1);

		_findclose (handle);
	}
}

_inline void RS_RotateST (float *os, float *ot, float degrees, msurface_t *fa)
{
	float cost = cos(degrees), sint = sin(degrees);
	float is = *os, it = *ot;

	*os = cost * (is - fa->c_s) + sint * (fa->c_t - it) + fa->c_s;
	*ot = cost * (it - fa->c_t) + sint * (is - fa->c_s) + fa->c_t;
}

void RS_SetEnvmap (vec3_t v, float *os, float *ot)
{
	vec3_t vert;
	
	vert[0] = v[0]*r_world_matrix[0]+v[1]*r_world_matrix[4]+v[2]*r_world_matrix[8] +r_world_matrix[12];
	vert[1] = v[0]*r_world_matrix[1]+v[1]*r_world_matrix[5]+v[2]*r_world_matrix[9] +r_world_matrix[13];
	vert[2] = v[0]*r_world_matrix[2]+v[1]*r_world_matrix[6]+v[2]*r_world_matrix[10]+r_world_matrix[14];
	
	VectorNormalize (vert);

	*os = vert[0];
	*ot = vert[1];
}

void RS_ScaleTexcoords (rs_stage_t *stage, float *os, float *ot)
{
	float	txm = 0, tym = 0;

	// scale
	if (stage->scale.scaleX) 
	{
		switch (stage->scale.typeX) 
		{
		case 0:	// static
			*os *= stage->scale.scaleX;
			break;
		case 1:	// sine
			*os *= stage->scale.scaleX*sin(rs_realtime*0.05);
			break;
		case 2:	// cosine
			*os *= stage->scale.scaleX*cos(rs_realtime*0.05);
			break;
		}
	}

	if (stage->scale.scaleY) 
	{
		switch (stage->scale.typeY) 
		{
		case 0:	// static
			*ot *= stage->scale.scaleY;
			break;
		case 1:	// sine
			*ot *= stage->scale.scaleY*sin(rs_realtime*0.05);
			break;
		case 2:	// cosine
			*ot *= stage->scale.scaleY*cos(rs_realtime*0.05);
			break;
		}
	}
}

void RS_SetTexcoords (rs_stage_t *stage, float *os, float *ot, msurface_t *fa)
{
	RS_ScaleTexcoords(stage, os, ot);

	// rotate
	if (stage->rot_speed)
		RS_RotateST (os, ot, -stage->rot_speed * rs_realtime * 0.0087266388888888888888888888888889, fa);
}

_inline void RS_RotateST2 (float *os, float *ot, float degrees)
{
	float cost = cos(degrees), sint = sin(degrees);
	float is = *os, it = *ot;

	*os = cost * (is - 0.5) + sint * (0.5 - it) + 0.5;
	*ot = cost * (it - 0.5) + sint * (is - 0.5) + 0.5;
}

void RS_SetTexcoords2D (rs_stage_t *stage, float *os, float *ot)
{
	float	txm = 0, tym = 0;

	// scale
	if (stage->scale.scaleX) 
	{
		switch (stage->scale.typeX)
		{
		case 0:	// static
			*os *= stage->scale.scaleX;
			break;
		case 1:	// sine
			*os *= stage->scale.scaleX*sin(rs_realtime*0.05);
			break;
		case 2:	// cosine
			*os *= stage->scale.scaleX*cos(rs_realtime*0.05);
			break;
		}
	}

	if (stage->scale.scaleY)
	{
		switch (stage->scale.typeY)
		{
		case 0:	// static
			*ot *= stage->scale.scaleY;
			break;
		case 1:	// sine
			*ot *= stage->scale.scaleY*sin(rs_realtime*0.05);
			break;
		case 2:	// cosine
			*ot *= stage->scale.scaleY*cos(rs_realtime*0.05);
			break;
		}

	}

	// rotate
	if (stage->rot_speed)
		RS_RotateST2 (os, ot, -stage->rot_speed * rs_realtime * 0.0087266388888888888888888888888889);

	if (stage->scroll.speedX) 
	{
		switch(stage->scroll.typeX) 
		{
			case 0:	// static
				txm=rs_realtime*stage->scroll.speedX;
				break;
			case 1:	// sine
				txm=sin(rs_realtime*stage->scroll.speedX);
				break;
			case 2:	// cosine
				txm=cos(rs_realtime*stage->scroll.speedX);
				break;
		}
	} 
	else
		txm=0;

	if (stage->scroll.speedY) 
	{
		switch(stage->scroll.typeY) 
		{
			case 0:	// static
				tym=rs_realtime*stage->scroll.speedY;
				break;
			case 1:	// sine
				tym=sin(rs_realtime*stage->scroll.speedY);
				break;
			case 2:	// cosine
				tym=cos(rs_realtime*stage->scroll.speedY);
				break;
		}
	} 
	else
		tym=0;

	*os += txm;
	*ot += tym;
}

float RS_AlphaFunc (int alphafunc, float alpha, vec3_t normal, vec3_t org)
{
	vec3_t	forward, dir;
	float temp;

	if (!abs(alphafunc))
		goto endalpha;

	if (alphafunc == ALPHAFUNC_GLOSS)
	{
		//glossmap stuff here...
	}
	else if (alphafunc == ALPHAFUNC_NORMAL)
	{
		VectorSubtract(org, r_newrefdef.vieworg, dir);
		VectorNormalize(dir);
		alpha *= fabs(cutDot(dir, normal));
	}

endalpha:

	if (alpha<0) alpha = 0;
	if (alpha>1) alpha = 1;
	if (alphafunc<0) alpha = 1-alpha;

	return alpha;
}

float RS_AlphaFuncAlias (int alphafunc, float alpha, vec3_t normal, vec3_t org)
{
	vec3_t	forward;
	float oldalpha = alpha;

	if (!abs(alphafunc))
		goto endalpha;

	if (alphafunc == ALPHAFUNC_GLOSS)
	{
		//glossmap stuff here...
	}

endalpha:

	if (alpha<0) alpha = 0;
	if (alpha>1) alpha = 1;
	if (alphafunc<0) alpha = 1-alpha;

	return alpha;
}

image_t *R_TextureAnimation (mtexinfo_t *tex);
rscript_t	*surfaceScript(msurface_t *surf)
{
	image_t *image = R_TextureAnimation( surf->texinfo );

	if (image && image->script)
	{
		rscript_t *rs = image->script;

		RS_ReadyScript(rs);

		return rs;
	}
	return NULL;
}

qboolean lightmaptoggle;
void ToggleLightmap (qboolean toggle)
{
	if (toggle==lightmaptoggle)
		return;

	lightmaptoggle = toggle;
	if (toggle)
	{
		SetVertexOverbrights(false);
		GL_EnableMultitexture( true );
		SetLightingMode ();
	}
	else
	{
		GL_EnableMultitexture( false );
		SetVertexOverbrights(true);
	}
}

void RS_DrawSurface (msurface_t *surf, qboolean lightmap)
{
	glpoly_t	*p;
	float		*v;
	int			i, nv = surf->polys->numverts;
	vec3_t		wv, vieworg, vectors[3];
	rscript_t	*rs = surfaceScript(surf);
	rs_stage_t	*stage = rs->stage;
	float		os, ot, alpha;
	float		scale, time = rs_realtime * rs->warpspeed, txm, tym;
	qboolean	depthmaskscript = stage->has_alpha;


	//for envmap by normals
	AngleVectors (r_newrefdef.viewangles, vectors[0], vectors[1], vectors[2]);

	if (depthmaskscript)
		qglDepthMask(false);

	if (!lightmap)
		SetVertexOverbrights(true);
	else
		lightmaptoggle = true;

	do
	{
		if (stage->detail && !rs_detail->value)
			continue;

		if (lightmap && stage->lightmap)
		{
			
			ToggleLightmap(true);
			GL_ShadeModel (GL_FLAT);

			if (stage->colormap.enabled)
				qglDisable (GL_TEXTURE_2D);
			else if (stage->dynamic)
				GL_MBind (GL_TEXTURE0, r_dynamicimage->texnum);
			else if (stage->rand_count)
				GL_MBind (GL_TEXTURE0, RS_Random(stage, surf));
			else if (stage->anim_count)
				GL_MBind (GL_TEXTURE0, RS_Animate(stage));
			else
				GL_MBind (GL_TEXTURE0, stage->texture->texnum);
		}
		else
		{
			if (!stage->lightmap && lightmap)
				ToggleLightmap(false);
			GL_ShadeModel (GL_SMOOTH);

			if (stage->colormap.enabled)
				qglDisable (GL_TEXTURE_2D);
			else if (stage->dynamic)
				GL_Bind (r_dynamicimage->texnum);
			else if (stage->rand_count)
				GL_Bind (RS_Random(stage, surf));
			else if (stage->anim_count)
				GL_Bind (RS_Animate(stage));
			else
				GL_Bind (stage->texture->texnum);
		}

		if (stage->blendfunc.blend) 
		{
			GL_BlendFunction(stage->blendfunc.source, stage->blendfunc.dest);
			GLSTATE_ENABLE_BLEND
		}
		else if (surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66) && !stage->alphamask)
		{
			GL_BlendFunction(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			GLSTATE_ENABLE_BLEND
		}
		else 
		{
			GLSTATE_DISABLE_BLEND
		}

		// sane defaults
		alpha = 1.0f;

		if (stage->alphashift.min || stage->alphashift.speed) 
		{
			if (!stage->alphashift.speed && stage->alphashift.min > 0) 
			{
				alpha = stage->alphashift.min;
			} 
			else if (stage->alphashift.speed) 
			{
				alpha = sin (rs_realtime * stage->alphashift.speed);
				alpha = (alpha + 1.0)*0.5f;
				if (alpha > stage->alphashift.max) 
					alpha = stage->alphashift.max;
				if (alpha < stage->alphashift.min) 
					alpha = stage->alphashift.min;
			}
		}
		
		if (surf->texinfo->flags & SURF_TRANS33 && surf->texinfo->flags & SURF_TRANS66)
			alpha *= DIV254BY255;
		else if (surf->texinfo->flags & SURF_TRANS33)
			alpha *= 0.33333;
		else if (surf->texinfo->flags & SURF_TRANS66)
			alpha *= 0.66666;

		if (stage->scroll.speedX) 
		{
			switch (stage->scroll.typeX) 
			{
			case 0:	// static
				txm = rs_realtime*stage->scroll.speedX;
				break;
			case 1:	// sine
				txm = sin (rs_realtime*stage->scroll.speedX);
				break;
			case 2:	// cosine
				txm = cos (rs_realtime*stage->scroll.speedX);
				break;
			}
		}
		else
			txm=0;
	
		if (stage->scroll.speedY) 
		{
			switch (stage->scroll.typeY) 
			{
			case 0:	// static
				tym = rs_realtime*stage->scroll.speedY;
				break;
			case 1:	// sine
				tym = sin (rs_realtime*stage->scroll.speedY);
				break;
			case 2:	// cosine
				tym = cos (rs_realtime*stage->scroll.speedY);
				break;
			}
		} else
			tym=0;

		if (surf->texinfo->flags & SURF_FLOWING)
		{
			float scroll;
		
			scroll = -64 * ( (r_newrefdef.time / 40.0) - (int)(r_newrefdef.time / 40.0) );
			if(scroll == 0.0)
				scroll = -64.0;

			txm += scroll;
		}

		qglColor4f (1, 1, 1, alpha);

		if (stage->alphamask) 
		{
			GLSTATE_ENABLE_ALPHATEST
		} 
		else 
		{
			GLSTATE_DISABLE_ALPHATEST
		}

		if (rs->subdivide || surf->flags & SURF_DRAWTURB)
		{
			glpoly_t *bp;
			int i;

			for (bp = surf->polys; bp; bp = bp->next)
			{
				p = bp;

				qglBegin(GL_TRIANGLE_FAN);
				for (i = 0, v = p->verts[0]; i < p->numverts; i++, v += VERTEXSIZE)
				{

					if (stage->envmap) 
					{
						RS_SetEnvmap (v, &os, &ot);
						//move by normal & position
						os-=DotProduct (surf->plane->normal, vectors[1] ) + (r_newrefdef.vieworg[0]-r_newrefdef.vieworg[1]+r_newrefdef.vieworg[2])*0.0025;
						ot+=DotProduct (surf->plane->normal, vectors[2] ) + (-r_newrefdef.vieworg[0]+r_newrefdef.vieworg[1]-r_newrefdef.vieworg[2])*0.0025;

						if (surf->texinfo->flags & SURF_FLOWING)
							txm = tym = 0;
					}
					else if (fabs(stage->depthhack))
					{
						vec3_t vec;

						VectorSubtract(p->center, r_newrefdef.vieworg, vec);
						VectorNormalize(vec);

						os = v[3] + cutDot(vec, surf->texinfo->vecs[0])*stage->depthhack*0.01;
						ot = v[4] + cutDot(vec, surf->texinfo->vecs[1])*stage->depthhack*0.01;
					}
					else 
					{
						os = v[3];
						ot = v[4];
					}
					RS_SetTexcoords (stage, &os, &ot, surf);

					if (stage->lightmap && lightmap && !stage->colormap.enabled ) 
					{
						qglMTexCoord2fSGIS(GL_TEXTURE0, os+txm, ot+tym);
						qglMTexCoord2fSGIS(GL_TEXTURE1, v[5], v[6]);
					} 
					else
					{
						float red=255, green=255, blue=255;

						if (stage->lightmap && p->vertexlight)
						{
							red = p->vertexlight[i*3+0]/255.0f;
							green = p->vertexlight[i*3+1]/255.0f;
							blue = p->vertexlight[i*3+2]/255.0f;
						}
						
						if (stage->colormap.enabled)
						{
							red *= stage->colormap.red/255.0f;
							green *= stage->colormap.green/255.0f;
							blue *= stage->colormap.blue/255.0f;
						}

						alpha = RS_AlphaFunc(stage->alphafunc, alpha, surf->plane->normal, p->center);
						if (red>1)red=1; if (red<0) red = 0;
						if (green>1)green=1; if (green<0) green = 0;
						if (blue>1)blue=1; if (blue<0) blue = 0;

						qglColor4f (red, green, blue, alpha);

						if (!stage->colormap.enabled)
							qglTexCoord2f (os+txm, ot+tym);
					}

					if (!rs->warpsmooth)
						qglVertex3fv (v);
					else 
					{
						scale = rs->warpdist * sin(v[0]*rs->warpsmooth+time)*sin(v[1]*rs->warpsmooth+time)*sin(v[2]*rs->warpsmooth+time);
						VectorMA (v, scale, surf->plane->normal, wv);
						qglVertex3fv (wv);
					}
				}
				qglEnd();
			}
		} 
		else 
		{
			for (p = surf->polys; p; p = p->chain)
			{
				qglBegin (GL_TRIANGLE_FAN);

				for (i = 0, v = p->verts[0]; i < nv; i++, v += VERTEXSIZE)
				{

					if (stage->envmap) 
					{
						RS_SetEnvmap (v, &os, &ot);
						//move by normal & position
						os-=DotProduct (surf->plane->normal, vectors[1] ) + (r_newrefdef.vieworg[0]-r_newrefdef.vieworg[1]+r_newrefdef.vieworg[2])*0.0025;
						ot+=DotProduct (surf->plane->normal, vectors[2] ) + (-r_newrefdef.vieworg[0]+r_newrefdef.vieworg[1]-r_newrefdef.vieworg[2])*0.0025;

						if (surf->texinfo->flags & SURF_FLOWING)
							txm = tym = 0;
					}
					else if (fabs(stage->depthhack))
					{
						vec3_t vec;

						VectorSubtract(p->center, r_newrefdef.vieworg, vec);
						VectorNormalize(vec);

						os = v[3] + cutDot(vec, surf->texinfo->vecs[0])*stage->depthhack*0.01;
						ot = v[4] + cutDot(vec, surf->texinfo->vecs[1])*stage->depthhack*0.01;
					}
					else 
					{
						os = v[3];
						ot = v[4];
					}
					RS_SetTexcoords (stage, &os, &ot, surf);

					if (stage->lightmap && lightmap && !stage->colormap.enabled ) 
					{
						qglMTexCoord2fSGIS(GL_TEXTURE0, os+txm, ot+tym);
						qglMTexCoord2fSGIS(GL_TEXTURE1, v[5], v[6]);
					} 
					else
					{
						float red=255, green=255, blue=255;

						if (stage->lightmap && p->vertexlight)
						{
							red = p->vertexlight[i*3+0]/255.0f;
							green = p->vertexlight[i*3+1]/255.0f;
							blue = p->vertexlight[i*3+2]/255.0f;
						}
						
						if (stage->colormap.enabled)
						{
							red *= stage->colormap.red/255.0f;
							green *= stage->colormap.green/255.0f;
							blue *= stage->colormap.blue/255.0f;
						}

						alpha = RS_AlphaFunc(stage->alphafunc, alpha, surf->plane->normal, v);
						if (red>1)red=1; if (red<0) red = 0;
						if (green>1)green=1; if (green<0) green = 0;
						if (blue>1)blue=1; if (blue<0) blue = 0;

						qglColor4f (red, green, blue, alpha);

						qglTexCoord2f (os+txm, ot+tym);
					}

					if (!rs->warpsmooth)
						qglVertex3fv (v);
					else 
					{
						scale = rs->warpdist * sin(v[0]*rs->warpsmooth+time)*sin(v[1]*rs->warpsmooth+time)*sin(v[2]*rs->warpsmooth+time);
						VectorMA (v, scale, surf->plane->normal, wv);
						qglVertex3fv (wv);
					}
				}

				qglEnd ();

			}
		}

		qglColor4f(1,1,1,1);
		if (stage->colormap.enabled)
			qglEnable (GL_TEXTURE_2D);

	} while (stage = stage->next);
	
	if (!lightmap)
		SetVertexOverbrights(false);
	else	
		ToggleLightmap(true);

	GLSTATE_DISABLE_BLEND
	GLSTATE_DISABLE_ALPHATEST
	GLSTATE_DISABLE_TEXGEN
	GL_ShadeModel (GL_SMOOTH);

	if (depthmaskscript)
		qglDepthMask(true);
}

//for special cases only
void RS_DrawSurfaceTexture (msurface_t *surf, rscript_t *rs)
{
	glpoly_t	*p;
	float		*v;
	int			i, nv;
	vec3_t		wv, vieworg, vectors[3];
	rs_stage_t	*stage = rs->stage;
	float		os, ot, alpha;
	float		scale, time, txm, tym;

	if (!rs)
		return;

	nv = surf->polys->numverts;
	stage = rs->stage;
	time = rs_realtime * rs->warpspeed;

	//for envmap by normals
	AngleVectors (r_newrefdef.viewangles, vectors[0], vectors[1], vectors[2]);

	SetVertexOverbrights(true);

	do
	{
		if (stage->detail && !rs_detail->value)
			continue;

		if (stage->colormap.enabled)
			qglDisable (GL_TEXTURE_2D);
		else if (stage->dynamic)
			GL_Bind (r_dynamicimage->texnum);
		else if (stage->rand_count)
			GL_Bind (RS_Random(stage, surf));
		else if (stage->anim_count)
			GL_Bind (RS_Animate(stage));
		else
			GL_Bind (stage->texture->texnum);

		if (stage->blendfunc.blend) 
		{
			GL_BlendFunction(stage->blendfunc.source, stage->blendfunc.dest);
			GLSTATE_ENABLE_BLEND
		}
		else if (surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66) && !stage->alphamask)
		{
			GL_BlendFunction(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			GLSTATE_ENABLE_BLEND
		}
		else 
		{
			GLSTATE_DISABLE_BLEND
		}

		// sane defaults
		alpha = 1.0f;

		if (stage->alphashift.min || stage->alphashift.speed) 
		{
			if (!stage->alphashift.speed && stage->alphashift.min > 0) 
			{
				alpha = stage->alphashift.min;
			} 
			else if (stage->alphashift.speed) 
			{
				alpha = sin (rs_realtime * stage->alphashift.speed);
				alpha = (alpha + 1)*0.5f;
				if (alpha > stage->alphashift.max) 
					alpha = stage->alphashift.max;
				if (alpha < stage->alphashift.min) 
					alpha = stage->alphashift.min;
			}
		}

		if (stage->scroll.speedX) 
		{
			switch (stage->scroll.typeX) 
			{
			case 0:	// static
				txm = rs_realtime*stage->scroll.speedX;
				break;
			case 1:	// sine
				txm = sin (rs_realtime*stage->scroll.speedX);
				break;
			case 2:	// cosine
				txm = cos (rs_realtime*stage->scroll.speedX);
				break;
			}
		}
		else
			txm=0;
	
		if (stage->scroll.speedY) 
		{
			switch (stage->scroll.typeY) 
			{
			case 0:	// static
				tym = rs_realtime*stage->scroll.speedY;
				break;
			case 1:	// sine
				tym = sin (rs_realtime*stage->scroll.speedY);
				break;
			case 2:	// cosine
				tym = cos (rs_realtime*stage->scroll.speedY);
				break;
			}
		} else
			tym=0;

		qglColor4f (1, 1, 1, alpha);

		if (stage->alphamask) 
		{
			GLSTATE_ENABLE_ALPHATEST
		} 
		else 
		{
			GLSTATE_DISABLE_ALPHATEST
		}

		if (rs->subdivide)
		{
			glpoly_t *bp;
			int i;

			for (bp = surf->polys; bp; bp = bp->next)
			{
				p = bp;

				qglBegin(GL_TRIANGLE_FAN);
				for (i = 0, v = p->verts[0]; i < p->numverts; i++, v += VERTEXSIZE)
				{
					if (stage->envmap) 
					{
						RS_SetEnvmap (v, &os, &ot);
						//move by normal & position
						os-=DotProduct (surf->plane->normal, vectors[1] ) + (r_newrefdef.vieworg[0]-r_newrefdef.vieworg[1]+r_newrefdef.vieworg[2])*0.0025;
						ot+=DotProduct (surf->plane->normal, vectors[2] ) + (-r_newrefdef.vieworg[0]+r_newrefdef.vieworg[1]-r_newrefdef.vieworg[2])*0.0025;

						if (surf->texinfo->flags & SURF_FLOWING)
							txm = tym = 0;
					} 
					else if (fabs(stage->depthhack))
					{
						vec3_t vec;

						VectorSubtract(p->center, r_newrefdef.vieworg, vec);
						VectorNormalize(vec);

						os = v[3] + cutDot(vec, surf->texinfo->vecs[0])*stage->depthhack*0.01;
						ot = v[4] + cutDot(vec, surf->texinfo->vecs[1])*stage->depthhack*0.01;
					}
					else 
					{
						os = v[3];
						ot = v[4];
					}
					RS_SetTexcoords (stage, &os, &ot, surf);
					{
						float red=255, green=255, blue=255;

						if (stage->lightmap && p->vertexlight)
						{
							red = p->vertexlight[i*3+0]/255.0f;
							green = p->vertexlight[i*3+1]/255.0f;
							blue = p->vertexlight[i*3+2]/255.0f;
						}
						
						if (stage->colormap.enabled)
						{
							red *= stage->colormap.red/255.0f;
							green *= stage->colormap.green/255.0f;
							blue *= stage->colormap.blue/255.0f;
						}

						alpha = RS_AlphaFunc(stage->alphafunc, alpha, surf->plane->normal, v);
						if (red>1)red=1; if (red<0) red = 0;
						if (green>1)green=1; if (green<0) green = 0;
						if (blue>1)blue=1; if (blue<0) blue = 0;

						qglColor4f (red, green, blue, alpha);

						qglTexCoord2f (os+txm, ot+tym);
					}

					if (!rs->warpsmooth)
						qglVertex3fv (v);
					else 
					{
						scale = rs->warpdist * sin(v[0]*rs->warpsmooth+time)*sin(v[1]*rs->warpsmooth+time)*sin(v[2]*rs->warpsmooth+time);
						VectorMA (v, scale, surf->plane->normal, wv);
						qglVertex3fv (wv);
					}
				}
				qglEnd();
			}
		} 
		else 
		{
			for (p = surf->polys; p; p = p->chain)
			{
				qglBegin (GL_TRIANGLE_FAN);

				for (i = 0, v = p->verts[0]; i < nv; i++, v += VERTEXSIZE)
				{

					if (stage->envmap) 
					{
						RS_SetEnvmap (v, &os, &ot);
						//move by normal & position
						os-=DotProduct (surf->plane->normal, vectors[1] ) + (r_newrefdef.vieworg[0]-r_newrefdef.vieworg[1]+r_newrefdef.vieworg[2])*0.0025;
						ot+=DotProduct (surf->plane->normal, vectors[2] ) + (-r_newrefdef.vieworg[0]+r_newrefdef.vieworg[1]-r_newrefdef.vieworg[2])*0.0025;

						if (surf->texinfo->flags & SURF_FLOWING)
							txm = tym = 0;
					}
					else if (fabs(stage->depthhack))
					{
						vec3_t vec;

						VectorSubtract(p->center, r_newrefdef.vieworg, vec);
						VectorNormalize(vec);

						os = v[3] + cutDot(vec, surf->texinfo->vecs[0])*stage->depthhack*0.01;
						ot = v[4] + cutDot(vec, surf->texinfo->vecs[1])*stage->depthhack*0.01;
					}
					else 
					{
						os = v[3];
						ot = v[4];
					}
					RS_SetTexcoords (stage, &os, &ot, surf);
					{
						float red=255, green=255, blue=255;

						if (stage->lightmap && p->vertexlight)
						{
							red = p->vertexlight[i*3+0]/255.0f;
							green = p->vertexlight[i*3+1]/255.0f;
							blue = p->vertexlight[i*3+2]/255.0f;
						}
						
						if (stage->colormap.enabled)
						{
							red *= stage->colormap.red/255.0f;
							green *= stage->colormap.green/255.0f;
							blue *= stage->colormap.blue/255.0f;
						}

						alpha = RS_AlphaFunc(stage->alphafunc, alpha, surf->plane->normal, v);
						if (red>1)red=1; if (red<0) red = 0;
						if (green>1)green=1; if (green<0) green = 0;
						if (blue>1)blue=1; if (blue<0) blue = 0;

						qglColor4f (red, green, blue, alpha);

						qglTexCoord2f (os+txm, ot+tym);
					}

					if (!rs->warpsmooth)
						qglVertex3fv (v);
					else 
					{
						scale = rs->warpdist * sin(v[0]*rs->warpsmooth+time)*sin(v[1]*rs->warpsmooth+time)*sin(v[2]*rs->warpsmooth+time);
						VectorMA (v, scale, surf->plane->normal, wv);
						qglVertex3fv (wv);
					}
				}

				qglEnd ();

			}
		}

		qglColor4f(1,1,1,1);
		if (stage->colormap.enabled)
			qglEnable (GL_TEXTURE_2D);

	} while (stage = stage->next);
	
	SetVertexOverbrights(false);
}

rscript_t *rs_caustics;
void RS_LoadSpecialScripts (void)
{
	rs_caustics = RS_FindScript("gfx/caustics");
}

//add all specia effects here
void RS_SpecialSurface (msurface_t *surf)
{
	rscript_t *rs;

	//Underwater Caustics
	if (surf->flags & SURF_UNDERWATER)
			RS_DrawSurfaceTexture(surf, rs_caustics);
}