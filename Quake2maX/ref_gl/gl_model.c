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
// models.c -- model loading and caching

#include "gl_local.h"

model_t	*loadmodel;
int		modfilelen;

void Mod_LoadSpriteModel (model_t *mod, void *buffer);
void Mod_LoadBrushModel (model_t *mod, void *buffer);
void Mod_LoadAliasModel (model_t *mod, void *buffer);
model_t *Mod_LoadModel (model_t *mod, qboolean crash);

byte	mod_novis[MAX_MAP_LEAFS/8];

#define	MAX_MOD_KNOWN	512
model_t	mod_known[MAX_MOD_KNOWN];
int		mod_numknown;

// the inline * models from the current map are kept seperate
model_t	mod_inline[MAX_MOD_KNOWN];

int		registration_sequence;

typedef struct {
	char	name[MAX_OSPATH];
	int	width;
	int	height;
} texSize_t;

static texSize_t	r_texSizes[MAX_IMAGES];
static int		r_numTexSizes;

int			numentitychars;
char		map_entitystring[MAX_MAP_ENTSTRING];

byte	*mod_base;

/*
=================
Mod_LoadEntityString
=================
*/
void Mod_LoadEntityString (lump_t *l)
{
	numentitychars = l->filelen;
	if (l->filelen > MAX_MAP_ENTSTRING)
		Sys_Error (ERR_DROP, "Map has too large entity lump");

	memcpy (map_entitystring, mod_base + l->fileofs, l->filelen);
}

char	*CM_EntityString (void)
{
	return map_entitystring;
}

/*
================
R_ParseLightEntities - BERSERK

parses light entity string
================
*/

static void R_ParseLightEntities (void)
{

	int			i;
	char		*entString;
	char		*buf, *tok;
	char		block[2048], *bl;
	vec3_t		origin, color;
	float		intensity;

	//DONT LOAD MAP ETS
//	return;

	entString = map_entitystring;

	buf = CM_EntityString();
	while (1){
		tok = Com_ParseExt(&buf, true);
		if (!tok[0])
			break;			// End of data

		if (Q_stricmp(tok, "{"))
			continue;		// Should never happen!

		// Parse the text inside brackets
		block[0] = 0;
		do {
			tok = Com_ParseExt(&buf, false);
			if (!Q_stricmp(tok, "}"))
				break;		// Done

			if (!tok[0])	// Newline
				Q_strcat(block, "\n", sizeof(block));
			else {			// Token
				Q_strcat(block, " ", sizeof(block));
				Q_strcat(block, tok, sizeof(block));
			}
		} while (buf);

		// Now look for "classname"
		tok = strstr(block, "classname");
		if (!tok)
			continue;		// Not found

		// Skip over "classname" and whitespace
		tok += strlen("classname");
		while (*tok && *tok == ' ')
			tok++;

		// Next token must be "light"
		if (Q_strnicmp(tok, "light", 5))
			continue;		// Not "light"

		// Finally parse the light entity
		VectorClear(origin);
		intensity = 0;

		bl = block;
		while (1){
			tok = Com_ParseExt(&bl, true);
			if (!tok[0])
				break;		// End of data

			if (!Q_stricmp("origin", tok)){
				for (i = 0; i < 3; i++){
					tok = Com_ParseExt(&bl, false);
					origin[i] = atof(tok);
				}
			}
			else if (!Q_stricmp("light", tok) || !Q_stricmp("_light", tok)){
				tok = Com_ParseExt(&bl, false);
				intensity = atof(tok);
			}
			else
				Com_SkipRestOfLine(&bl);
		}

		if (!intensity)
			intensity = 150;

		// Add it to the list
		if (r_numWorldLights == MAX_LIGHTS)
			break;

		VectorCopy(origin, r_worldLights[r_numWorldLights].origin);
		r_worldLights[r_numWorldLights].intensity = intensity/2;
		r_worldLights[r_numWorldLights].surf = NULL;
		r_numWorldLights++;
	}
}
void GL_LightmapColor (msurface_t *surf, vec3_t color);
//only add one light per light surface
void GL_AddSurfaceWorldLight (msurface_t *surf)
{
	int i, intensity;
	glpoly_t *poly;
	vec3_t origin = {0,0,0}, color = {0,0,0};

	if (!(surf->texinfo->flags & SURF_LIGHT))
		return;
	if (r_numWorldLights == MAX_LIGHTS)
		return;

	for (poly=surf->polys, i=0 ; poly ; poly=poly->next, i++)
		VectorAdd(origin, poly->center, origin);

	VectorScale(origin, 1.0/(float)i, origin);
	VectorCopy(origin, r_worldLights[r_numWorldLights].origin);

	intensity = surf->texinfo->value/2;
	if (intensity>200) intensity = 200;
	r_worldLights[r_numWorldLights].intensity = intensity;
	
	r_worldLights[r_numWorldLights].surf = surf;

	r_numWorldLights++;
}

/*
 =================
 R_GetTexSize
 =================
*/
static void R_GetTexSize (char *name, int *width, int *height)
{
	int			i;
	char		fullname[MAX_QPATH];
	miptex_t	*mt;
	
	// Look in the script file
	for (i = 0; i < r_numTexSizes; i++)
	{
		if (!Q_stricmp(r_texSizes[i].name, name))
		{
			*width = r_texSizes[i].width;
			*height = r_texSizes[i].height;
			return;
		}
	}

	// Load it from disk, but also cache the width and height to avoid
	// reloading it during the map loading process if it's referenced
	// again later
	Com_sprintf(fullname, sizeof(fullname), "textures/%s.wal", name);
	ri.FS_LoadFile(fullname, (void **)&mt);
	if (mt)
	{
		if (r_numTexSizes < MAX_IMAGES)
		{
			strcpy(r_texSizes[r_numTexSizes].name, name);
			r_texSizes[r_numTexSizes].width = LittleLong(mt->width);
			r_texSizes[r_numTexSizes].height = LittleLong(mt->height);
			r_numTexSizes++;
		}

		*width = LittleLong(mt->width);
		*height = LittleLong(mt->height);

		ri.FS_FreeFile(mt);
		return;
	}

	// This is not the best solution, but what could we do?
	*width = 64;
	*height = 64;
}

/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t *Mod_PointInLeaf (vec3_t p, model_t *model)
{
	mnode_t		*node;
	float		d;
	cplane_t	*plane;
	
	if (!model || !model->nodes)
		ri.Sys_Error (ERR_DROP, "Mod_PointInLeaf: bad model");

	node = model->nodes;
	while (1)
	{
		if (node->contents != -1)
			return (mleaf_t *)node;

		plane = node->plane;

		d = DotProduct (p,plane->normal) - plane->dist;

		if (d > 0)
			node = node->children[0];
		else
			node = node->children[1];
	}
	
	return NULL;	// never reached
}


/*
===================
Mod_DecompressVis
===================
*/
byte *Mod_DecompressVis (byte *in, model_t *model)
{
	static byte	decompressed[MAX_MAP_LEAFS/8];
	int		c;
	byte	*out;
	int		row;

	row = (model->vis->numclusters+7)>>3;	
	out = decompressed;

	if (!in)
	{	// no vis info, so make all visible
		while (row)
		{
			*out++ = 0xff;
			row--;
		}
		return decompressed;		
	}

	do
	{
		if (*in)
		{
			*out++ = *in++;
			continue;
		}
	
		c = in[1];
		in += 2;
		while (c)
		{
			*out++ = 0;
			c--;
		}
	} while (out - decompressed < row);
	
	return decompressed;
}

/*
==============
Mod_ClusterPVS
==============
*/
byte *Mod_ClusterPVS (int cluster, model_t *model)
{
	if (cluster == -1 || !model->vis)
		return mod_novis;
	return Mod_DecompressVis ( (byte *)model->vis + model->vis->bitofs[cluster][DVIS_PVS],
		model);
}


//===============================================================================

/*
================
Mod_Modellist_f
================
*/
void Mod_Modellist_f (void)
{
	int		i;
	model_t	*mod;
	int		total;

	total = 0;
	ri.Con_Printf (PRINT_ALL,"Loaded models:\n");
	for (i=0, mod=mod_known ; i < mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			continue;
		ri.Con_Printf (PRINT_ALL, "%8i : %s\n",mod->extradatasize, mod->name);
		total += mod->extradatasize;
	}
	ri.Con_Printf (PRINT_ALL, "Total resident: %i\n", total);
}

/*
===============
Mod_Init
===============
*/
void Mod_Init (void)
{
	memset (mod_novis, 0xff, sizeof(mod_novis));
}



/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/

char *Get_ModelName (struct model_s *mod)
{
	return mod->name;
}

model_t *Mod_ForName (char *name, qboolean crash)
{
	model_t	*mod;
	unsigned *buf;
	int		i;
	
	if (!name[0])
		ri.Sys_Error (ERR_DROP, "Mod_ForName: NULL name");
		
	//
	// inline models are grabbed only from worldmodel
	//
	if (name[0] == '*')
	{
		i = atoi(name+1);
		if (i < 1 || !r_worldmodel || i >= r_worldmodel->numsubmodels)
			ri.Sys_Error (ERR_DROP, "bad inline model number");
		return &mod_inline[i];
	}

	//
	// search the currently loaded models
	//
	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			continue;
		if (!strcmp (mod->name, name) ) {
			if (mod->type == mod_alias) {
				// Make sure models scripts are definately reloaded between maps - MrG
				char rs[MAX_OSPATH];
				int i = 0;
				image_t *img;

				img=mod->skins[i];
				while (img != NULL) {
					strcpy(rs,mod->skins[i]->name);
					rs[strlen(rs)-4]=0;
					(struct rscript_s *)mod->script[i] = RS_FindScript(rs);
					if (mod->script[i])
						RS_ReadyScript((rscript_t *)mod->script[i]);
					i++;
					img=mod->skins[i];
				}
			}

			return mod;
		}
	}
	
	//
	// find a free model slot spot
	//
	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			break;	// free spot
	}
	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
			ri.Sys_Error (ERR_DROP, "mod_numknown == MAX_MOD_KNOWN");
		mod_numknown++;
	}
	strcpy (mod->name, name);
	
	//
	// load the file
	//
	modfilelen = ri.FS_LoadFile (mod->name, &buf);
	if (!buf)
	{
		if (crash)
			ri.Sys_Error (ERR_DROP, "Mod_NumForName: %s not found", mod->name);
		memset (mod->name, 0, sizeof(mod->name));
		return NULL;
	}
	
	loadmodel = mod;

	//
	// fill it in
	//


	// call the apropriate loader
	
	switch (LittleLong(*(unsigned *)buf))
	{
	case IDALIASHEADER:
		loadmodel->extradata = Hunk_Begin (0x200000);
		Mod_LoadAliasModel (mod, buf);
		break;

	case IDSPRITEHEADER:
		loadmodel->extradata = Hunk_Begin (0x10000);
		Mod_LoadSpriteModel (mod, buf);
		break;
	
	case IDBSPHEADER:
		loadmodel->extradata = Hunk_Begin (0x1000000);
		Mod_LoadBrushModel (mod, buf);
		break;

	default:
		ri.Sys_Error (ERR_DROP,"Mod_NumForName: unknown fileid for %s", mod->name);
		break;
	}

	loadmodel->extradatasize = Hunk_End ();

	ri.FS_FreeFile (buf);

	return mod;
}

/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/


/*
=================
Mod_LoadLighting
=================
*/
void Mod_LoadLighting (lump_t *l)
{
	if (!l->filelen)
	{
		loadmodel->lightdata = NULL;
		return;
	}
	loadmodel->lightdata = Hunk_Alloc ( l->filelen);	
	memcpy (loadmodel->lightdata, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadVisibility
=================
*/
void Mod_LoadVisibility (lump_t *l)
{
	int		i;

	if (!l->filelen)
	{
		loadmodel->vis = NULL;
		return;
	}
	loadmodel->vis = Hunk_Alloc ( l->filelen);	
	memcpy (loadmodel->vis, mod_base + l->fileofs, l->filelen);

	loadmodel->vis->numclusters = LittleLong (loadmodel->vis->numclusters);
	for (i=0 ; i<loadmodel->vis->numclusters ; i++)
	{
		loadmodel->vis->bitofs[i][0] = LittleLong (loadmodel->vis->bitofs[i][0]);
		loadmodel->vis->bitofs[i][1] = LittleLong (loadmodel->vis->bitofs[i][1]);
	}
}


/*
=================
Mod_LoadVertexes
=================
*/
void Mod_LoadVertexes (lump_t *l)
{
	dvertex_t	*in;
	mvertex_t	*out;
	int			i, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->vertexes = out;
	loadmodel->numvertexes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->position[0] = LittleFloat (in->point[0]);
		out->position[1] = LittleFloat (in->point[1]);
		out->position[2] = LittleFloat (in->point[2]);
	}
}

/*
=================
RadiusFromBounds
=================
*/
float RadiusFromBounds (vec3_t mins, vec3_t maxs)
{
	int		i;
	vec3_t	corner;

	for (i=0 ; i<3 ; i++)
	{
		corner[i] = fabs(mins[i]) > fabs(maxs[i]) ? fabs(mins[i]) : fabs(maxs[i]);
	}

	return VectorLength (corner);
}


/*
=================
Mod_LoadSubmodels
=================
*/
void Mod_LoadSubmodels (lump_t *l)
{
	dmodel_t	*in;
	mmodel_t	*out;
	int			i, j, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->submodels = out;
	loadmodel->numsubmodels = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{	// spread the mins / maxs by a pixel
			out->mins[j] = LittleFloat (in->mins[j]) - 1;
			out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
			out->origin[j] = LittleFloat (in->origin[j]);
		}
		out->radius = RadiusFromBounds (out->mins, out->maxs);
		out->headnode = LittleLong (in->headnode);
		out->firstface = LittleLong (in->firstface);
		out->numfaces = LittleLong (in->numfaces);
	}
}

/*
=================
Mod_LoadEdges
=================
*/
void Mod_LoadEdges (lump_t *l)
{
	dedge_t *in;
	medge_t *out;
	int 	i, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( (count + 1) * sizeof(*out));	

	loadmodel->edges = out;
	loadmodel->numedges = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->v[0] = (unsigned short)LittleShort(in->v[0]);
		out->v[1] = (unsigned short)LittleShort(in->v[1]);
	}
}

/*
=================
Mod_LoadTexinfo
=================
*/
void Mod_LoadTexinfo (lump_t *l)
{
	texinfo_t *in;
	mtexinfo_t *out, *step;
	int 	i, j, count;
	char	name[MAX_QPATH];
	int		next;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->texinfo = out;
	loadmodel->numtexinfo = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<8 ; j++)
			out->vecs[0][j] = LittleFloat (in->vecs[0][j]);

		out->flags = LittleLong (in->flags);
		next = LittleLong (in->nexttexinfo);
		if (next > 0)
			out->next = loadmodel->texinfo + next;
		else
		    out->next = NULL;

		Com_sprintf (name, sizeof(name), "textures/%s", in->texture);
		(struct rscript_s *)out->script = RS_FindScript(name);
		if (out->script)
			RS_ReadyScript((rscript_t *)out->script);

		//get light level
		out->value = in->value;

		R_GetTexSize(in->texture, &out->texWidth, &out->texHeight);
		Com_sprintf (name, sizeof(name), "textures/%s.wal", in->texture);

		if (!(out->image = GL_FindImage (name, it_wall)))
		{
			ri.Con_Printf (PRINT_ALL, "Couldn't load %s\n", name);
			out->image = r_notexture;
		}
	}

	// count animation frames
	for (i=0 ; i<count ; i++)
	{
		out = &loadmodel->texinfo[i];
		out->numframes = 1;
		for (step = out->next ; step && step != out ; step=step->next)
			out->numframes++;
	}
}

/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
void CalcSurfaceExtents (msurface_t *s)
{
	float	mins[2], maxs[2], val;
	int		i,j, e;
	mvertex_t	*v;
	mtexinfo_t	*tex;
	int		bmins[2], bmaxs[2];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = s->texinfo;
	
	for (i=0 ; i<s->numedges ; i++)
	{
		e = loadmodel->surfedges[s->firstedge+i];
		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];
		
		for (j=0 ; j<2 ; j++)
		{
			val = v->position[0] * tex->vecs[j][0] + 
				v->position[1] * tex->vecs[j][1] +
				v->position[2] * tex->vecs[j][2] +
				tex->vecs[j][3];
			if (val < mins[j])
				mins[j] = val;
			if (val > maxs[j])
				maxs[j] = val;
		}
	}

	for (i=0 ; i<2 ; i++)
	{	
		bmins[i] = floor(mins[i]/16);
		bmaxs[i] = ceil(maxs[i]/16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;

//		if ( !(tex->flags & TEX_SPECIAL) && s->extents[i] > 512 /* 256 */ )
//			ri.Sys_Error (ERR_DROP, "Bad surface extents");
	}
}


void GL_AddSurfaceWorldLight (msurface_t *surf);
void GL_FindPolyCenters (msurface_t *surf);
void GL_BuildPolygonFromSurface(msurface_t *fa);
void GL_CreateVertexLightmap (msurface_t *surf);
void GL_CreateSurfaceLightmap (msurface_t *surf);
void GL_CreateSurfaceStainmap (msurface_t *surf);
void GL_EndBuildingLightmaps (void);
void GL_BeginBuildingLightmaps (model_t *m);

/*
=================
Mod_LoadFaces
=================
*/

void Mod_LoadFaces (lump_t *l)
{
	dface_t		*in;
	msurface_t 	*out;
	int			i, j, count, surfnum;
	int			planenum, side;
	int			ti;
	qboolean	lit;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	currentmodel = loadmodel;

	GL_BeginBuildingLightmaps (loadmodel);

	for ( surfnum=0 ; surfnum<count ; surfnum++, in++, out++)
	{
		//refresh ent link...
		out->firstedge = LittleLong(in->firstedge);
		out->numedges = LittleShort(in->numedges);		
		out->flags = 0;
		out->polys = NULL;

		planenum = LittleShort(in->planenum);
		side = LittleShort(in->side);
		if (side)
			out->flags |= SURF_PLANEBACK;			

		out->plane = loadmodel->planes + planenum;

		ti = LittleShort (in->texinfo);
		if (ti < 0 || ti >= loadmodel->numtexinfo)
			ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: bad texinfo number");
		out->texinfo = loadmodel->texinfo + ti;

		CalcSurfaceExtents (out);
				
	// lighting info

		for (i=0 ; i<MAXLIGHTMAPS ; i++)
			out->styles[i] = in->styles[i];
		i = LittleLong(in->lightofs);
		if (i == -1)
		{
			out->samples = NULL;
			out->stains = NULL;
		}
		else
		{
			out->samples = loadmodel->lightdata + i;
		}
		
	// set the drawing flags
		
		if (out->texinfo->flags & SURF_WARP)
		{

			out->flags |= SURF_DRAWTURB;
			for (i=0 ; i<2 ; i++)
			{
				out->extents[i] = 16384;
				out->texturemins[i] = -8192;
			}
			GL_SubdivideSurface (out);	// cut up polygon for warps
		}

		// create lightmaps and polygons
		if ( !(out->texinfo->flags & (SURF_SKY|SURF_TRANS33|SURF_TRANS66|SURF_WARP) ) )
		{
			GL_CreateSurfaceLightmap (out);
			GL_CreateSurfaceStainmap (out);

		}

		if (! (out->texinfo->flags & SURF_WARP) )
		{
			if(!out->texinfo->script)
			{
				GL_BuildPolygonFromSurface(out);
			}
			else
			{
				rscript_t *rs = (rscript_t *)out->texinfo->script;

				if(rs->subdivide)
					GL_SubdivideLightmappedSurface(out, rs->subdivide);
				else
				{
					rscript_t *rs = (rscript_t *)out->texinfo->script;

					if (rs->stage->texture != NULL)
						out->texinfo->image = rs->stage->texture;
					else if (rs->stage->anim_stage != NULL)
						out->texinfo->image = rs->stage->anim_stage->texture;

					GL_BuildPolygonFromSurface(out);
				}
			}
		}

		if (out->polys)
		{
			GL_CreateVertexLightmap(out);
			GL_FindPolyCenters(out);
		}
	
		GL_AddSurfaceWorldLight(out);
	}

	GL_EndBuildingLightmaps ();
}


/*
=================
Mod_SetParent
=================
*/
void Mod_SetParent (mnode_t *node, mnode_t *parent)
{
	node->parent = parent;
	if (node->contents != -1)
		return;
	Mod_SetParent (node->children[0], node);
	Mod_SetParent (node->children[1], node);
}

/*
=================
Mod_LoadNodes
=================
*/
void Mod_LoadNodes (lump_t *l)
{
	int			i, j, count, p;
	dnode_t		*in;
	mnode_t 	*out;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}
	
		p = LittleLong(in->planenum);
		out->plane = loadmodel->planes + p;

		out->firstsurface = LittleShort (in->firstface);
		out->numsurfaces = LittleShort (in->numfaces);
		out->contents = -1;	// differentiate from leafs

		for (j=0 ; j<2 ; j++)
		{
			p = LittleLong (in->children[j]);
			if (p >= 0)
				out->children[j] = loadmodel->nodes + p;
			else
				out->children[j] = (mnode_t *)(loadmodel->leafs + (-1 - p));
		}
	}
	
	Mod_SetParent (loadmodel->nodes, NULL);	// sets nodes and leafs
}

/*
=================
Mod_LoadLeafs
=================
*/
void Mod_LoadLeafs (lump_t *l)
{
	dleaf_t 	*in;
	mleaf_t 	*out;
	int			i, j, count, p;
	glpoly_t	*poly;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}

		p = LittleLong(in->contents);
		out->contents = p;

		out->cluster = LittleShort(in->cluster);
		out->area = LittleShort(in->area);

		out->firstmarksurface = loadmodel->marksurfaces +
			LittleShort(in->firstleafface);
		out->nummarksurfaces = LittleShort(in->numleaffaces);
		
		// gl underwater warp
		if (out->contents & MASK_WATER )
		{
			for (j=0 ; j<out->nummarksurfaces ; j++)
			{
				out->firstmarksurface[j]->flags |= SURF_UNDERWATER;
				for (poly = out->firstmarksurface[j]->polys ; poly ; poly=poly->next)
					poly->flags |= SURF_UNDERWATER;
			}
		}
	}	
}

/*
=================
Mod_LoadMarksurfaces
=================
*/
void Mod_LoadMarksurfaces (lump_t *l)
{	
	int		i, j, count;
	short		*in;
	msurface_t **out;
	
	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->marksurfaces = out;
	loadmodel->nummarksurfaces = count;

	for ( i=0 ; i<count ; i++)
	{
		j = LittleShort(in[i]);
		if (j < 0 ||  j >= loadmodel->numsurfaces)
			ri.Sys_Error (ERR_DROP, "Mod_ParseMarksurfaces: bad surface number");
		out[i] = loadmodel->surfaces + j;
	}
}

/*
=================
Mod_LoadSurfedges
=================
*/
void Mod_LoadSurfedges (lump_t *l)
{	
	int		i, count;
	int		*in, *out;
	
	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	if (count < 1 || count >= MAX_MAP_SURFEDGES)
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: bad surfedges count in %s: %i",
		loadmodel->name, count);

	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->surfedges = out;
	loadmodel->numsurfedges = count;

	for ( i=0 ; i<count ; i++)
		out[i] = LittleLong (in[i]);
}


/*
=================
Mod_LoadPlanes
=================
*/
void Mod_LoadPlanes (lump_t *l)
{
	int			i, j;
	cplane_t	*out;
	dplane_t 	*in;
	int			count;
	int			bits;
	
	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);

	count = l->filelen / sizeof(*in);

//original bug
//	out = Hunk_Alloc ( count*2*sizeof(*out));
	out = Hunk_Alloc ( count*sizeof(*out));	
	
	loadmodel->planes = out;
	loadmodel->numplanes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		bits = 0;
		for (j=0 ; j<3 ; j++)
		{
			out->normal[j] = LittleFloat (in->normal[j]);
			if (out->normal[j] < 0)
				bits |= 1<<j;
		}

		out->dist = LittleFloat (in->dist);
		//out->type = LittleLong (in->type);
		out->type = PlaneTypeForNormal(out->normal);
		out->signbits = bits;
	}
}

/*
=================
Mod_LoadBrushModel
=================
*/
void Mod_LoadBrushModel (model_t *mod, void *buffer)
{
	int			i;
	dheader_t	*header;
	mmodel_t 	*bm;
	char		rs_name[MAX_OSPATH], tmp[16];		// rscript - MrG
	
	// rscript - MrG
	RS_FreeUnmarked();
	strcpy(tmp,loadmodel->name+5);
	tmp[strlen(tmp)-4]=0;
	Com_sprintf(rs_name,MAX_OSPATH,"scripts/maps/%s.rscript",tmp);
	RS_ScanPathForScripts();		// load all found scripts
	RS_LoadScript(rs_name);
	RS_ReloadImageScriptLinks();
	RS_LoadSpecialScripts();
	// end
	r_numWorldLights = 0;

	loadmodel->type = mod_brush;
	if (loadmodel != mod_known)
		ri.Sys_Error (ERR_DROP, "Loaded a brush model after the world");

	header = (dheader_t *)buffer;

	i = LittleLong (header->version);
	if (i != BSPVERSION)
		ri.Sys_Error (ERR_DROP, "Mod_LoadBrushModel: %s has wrong version number (%i should be %i)", mod->name, i, BSPVERSION);

// swap all the lumps
	mod_base = (byte *)header;

	for (i=0 ; i<sizeof(dheader_t)/4 ; i++)
		((int *)header)[i] = LittleLong ( ((int *)header)[i]);

// load into heap
	
	Mod_LoadEntityString (&header->lumps[LUMP_ENTITIES]);
	Mod_LoadVertexes (&header->lumps[LUMP_VERTEXES]);
	Mod_LoadEdges (&header->lumps[LUMP_EDGES]);
	Mod_LoadSurfedges (&header->lumps[LUMP_SURFEDGES]);
	Mod_LoadLighting (&header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes (&header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo (&header->lumps[LUMP_TEXINFO]);
	Mod_LoadFaces (&header->lumps[LUMP_FACES]);
	Mod_LoadMarksurfaces (&header->lumps[LUMP_LEAFFACES]);
	Mod_LoadVisibility (&header->lumps[LUMP_VISIBILITY]);
	Mod_LoadLeafs (&header->lumps[LUMP_LEAFS]);
	Mod_LoadNodes (&header->lumps[LUMP_NODES]);
	Mod_LoadSubmodels (&header->lumps[LUMP_MODELS]);
	mod->numframes = 2;		// regular and alternate animation
	
//
// set up the submodels
//
	for (i=0 ; i<mod->numsubmodels ; i++)
	{
		model_t	*starmod;

		bm = &mod->submodels[i];
		starmod = &mod_inline[i];

		*starmod = *loadmodel;
		
		starmod->firstmodelsurface = bm->firstface;
		starmod->nummodelsurfaces = bm->numfaces;
		starmod->firstnode = bm->headnode;
		if (starmod->firstnode >= loadmodel->numnodes)
			ri.Sys_Error (ERR_DROP, "Inline model %i has bad firstnode", i);

		VectorCopy (bm->maxs, starmod->maxs);
		VectorCopy (bm->mins, starmod->mins);
		starmod->radius = bm->radius;
	
		if (i == 0)
			*loadmodel = *starmod;

		starmod->numleafs = bm->visleafs;
	}
	
	R_ParseLightEntities();
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

/*
=================
Mod_LoadAliasModel
=================
*/
void Mod_LoadAliasModel (model_t *mod, void *buffer)
{
	int					i, j;
	dmdl_t				*pinmodel, *pheader;
	dstvert_t			*pinst, *poutst;
	dtriangle_t			*pintri, *pouttri;
	daliasframe_t		*pinframe, *poutframe;
	int					*pincmd, *poutcmd;
	int					version;

	pinmodel = (dmdl_t *)buffer;

	version = LittleLong (pinmodel->version);
	if (version != ALIAS_VERSION)
		ri.Sys_Error (ERR_DROP, "%s has wrong version number (%i should be %i)",
				 mod->name, version, ALIAS_VERSION);

	pheader = Hunk_Alloc (LittleLong(pinmodel->ofs_end));
	
	// byte swap the header fields and sanity check
	for (i=0 ; i<sizeof(dmdl_t)/4 ; i++)
		((int *)pheader)[i] = LittleLong (((int *)buffer)[i]);

	if (pheader->skinheight > MAX_LBM_HEIGHT)
		ri.Sys_Error (ERR_DROP, "model %s has a skin taller than %d", mod->name,
				   MAX_LBM_HEIGHT);

	if (pheader->num_xyz <= 0)
		ri.Sys_Error (ERR_DROP, "model %s has no vertices", mod->name);

	if (pheader->num_xyz > MAX_VERTS)
		ri.Sys_Error (ERR_DROP, "model %s has too many vertices", mod->name);

	if (pheader->num_st <= 0)
		ri.Sys_Error (ERR_DROP, "model %s has no st vertices", mod->name);

	if (pheader->num_tris <= 0)
		ri.Sys_Error (ERR_DROP, "model %s has no triangles", mod->name);

	if (pheader->num_frames <= 0)
		ri.Sys_Error (ERR_DROP, "model %s has no frames", mod->name);

//
// load base s and t vertices (not used in gl version)
//
	pinst = (dstvert_t *) ((byte *)pinmodel + pheader->ofs_st);
	poutst = (dstvert_t *) ((byte *)pheader + pheader->ofs_st);

	for (i=0 ; i<pheader->num_st ; i++)
	{
		poutst[i].s = LittleShort (pinst[i].s);
		poutst[i].t = LittleShort (pinst[i].t);
	}

//
// load triangle lists
//
	pintri = (dtriangle_t *) ((byte *)pinmodel + pheader->ofs_tris);
	pouttri = (dtriangle_t *) ((byte *)pheader + pheader->ofs_tris);

	for (i=0 ; i<pheader->num_tris ; i++)
	{
		for (j=0 ; j<3 ; j++)
		{
			pouttri[i].index_xyz[j] = LittleShort (pintri[i].index_xyz[j]);
			pouttri[i].index_st[j] = LittleShort (pintri[i].index_st[j]);
		}
	}

//
// load the frames
//
	for (i=0 ; i<pheader->num_frames ; i++)
	{
		pinframe = (daliasframe_t *) ((byte *)pinmodel 
			+ pheader->ofs_frames + i * pheader->framesize);
		poutframe = (daliasframe_t *) ((byte *)pheader 
			+ pheader->ofs_frames + i * pheader->framesize);

		memcpy (poutframe->name, pinframe->name, sizeof(poutframe->name));
		for (j=0 ; j<3 ; j++)
		{
			poutframe->scale[j] = LittleFloat (pinframe->scale[j]);
			poutframe->translate[j] = LittleFloat (pinframe->translate[j]);
		}
		// verts are all 8 bit, so no swapping needed
		memcpy (poutframe->verts, pinframe->verts, 
			pheader->num_xyz*sizeof(dtrivertx_t));

	}

	mod->type = mod_alias;

	//
	// load the glcmds
	//
	pincmd = (int *) ((byte *)pinmodel + pheader->ofs_glcmds);
	poutcmd = (int *) ((byte *)pheader + pheader->ofs_glcmds);
	for (i=0 ; i<pheader->num_glcmds ; i++)
		poutcmd[i] = LittleLong (pincmd[i]);


	// register all skins
	memcpy ((char *)pheader + pheader->ofs_skins, (char *)pinmodel + pheader->ofs_skins,
		pheader->num_skins*MAX_SKINNAME);

	for (i=0 ; i<pheader->num_skins ; i++)
	{
		char rs[MAX_OSPATH];
		mod->skins[i] = GL_FindImage ((char *)pheader + pheader->ofs_skins + i*MAX_SKINNAME
			, it_skin);

		strcpy(rs,(char *)pheader + pheader->ofs_skins + i*MAX_SKINNAME);

		rs[strlen(rs)-4]=0;
		(struct rscript_s *)mod->script[i] = RS_FindScript(rs);
		if (mod->script[i])
			RS_ReadyScript((rscript_t *)mod->script[i]);
	}

	mod->mins[0] = -32;
	mod->mins[1] = -32;
	mod->mins[2] = -32;
	mod->maxs[0] = 32;
	mod->maxs[1] = 32;
	mod->maxs[2] = 32;
}

/*
==============================================================================

SPRITE MODELS

==============================================================================
*/

/*
=================
Mod_LoadSpriteModel
=================
*/
void Mod_LoadSpriteModel (model_t *mod, void *buffer)
{
	dsprite_t	*sprin, *sprout;
	int			i;

	sprin = (dsprite_t *)buffer;
	sprout = Hunk_Alloc (modfilelen);

	sprout->ident = LittleLong (sprin->ident);
	sprout->version = LittleLong (sprin->version);
	sprout->numframes = LittleLong (sprin->numframes);

	if (sprout->version != SPRITE_VERSION)
		ri.Sys_Error (ERR_DROP, "%s has wrong version number (%i should be %i)",
				 mod->name, sprout->version, SPRITE_VERSION);

	if (sprout->numframes > MAX_MD2SKINS)
		ri.Sys_Error (ERR_DROP, "%s has too many frames (%i > %i)",
				 mod->name, sprout->numframes, MAX_MD2SKINS);

	// byte swap everything
	for (i=0 ; i<sprout->numframes ; i++)
	{
		sprout->frames[i].width = LittleLong (sprin->frames[i].width);
		sprout->frames[i].height = LittleLong (sprin->frames[i].height);
		sprout->frames[i].origin_x = LittleLong (sprin->frames[i].origin_x);
		sprout->frames[i].origin_y = LittleLong (sprin->frames[i].origin_y);
		memcpy (sprout->frames[i].name, sprin->frames[i].name, MAX_SKINNAME);
		mod->skins[i] = GL_FindImage (sprout->frames[i].name,
			it_sprite);
	}

	mod->type = mod_sprite;
}

//=============================================================================

/*
@@@@@@@@@@@@@@@@@@@@@
R_BeginRegistration

Specifies the model that will be used as the world
@@@@@@@@@@@@@@@@@@@@@
*/
void resetDynamicImage ();
void R_BeginRegistration (char *model)
{
	char	fullname[MAX_QPATH];
	cvar_t	*flushmap;

	registration_sequence++;
	r_oldviewcluster = -1;		// force markleafs

	Com_sprintf (fullname, sizeof(fullname), "maps/%s.bsp", model);

	// explicitly free the old map if different
	// this guarantees that mod_known[0] is the world map
	flushmap = ri.Cvar_Get ("flushmap", "0", 0);
	if ( strcmp(mod_known[0].name, fullname) || flushmap->value)
		Mod_Free (&mod_known[0]);
	r_worldmodel = Mod_ForName(fullname, true);

	r_viewcluster = -1;

	//new map wont destroy dynamic image
	resetDynamicImage();
}


signed int *Mod_GetTris(short p1, short p2, dtriangle_t *side1, dmdl_t *hdr)
{
	dtriangle_t *tris = (dtriangle_t *)((unsigned char*)hdr + hdr->ofs_tris);
	int i;

	for (i=0; i<hdr->num_tris; i++, tris++) 
	{
		if (tris == side1)
			continue;

		if (tris->index_xyz[0] == p2 && tris->index_xyz[1] == p1)
			return i;
		if (tris->index_xyz[1] == p2 && tris->index_xyz[2] == p1)
			return i;
		if (tris->index_xyz[2] == p2 && tris->index_xyz[0] == p1)
			return i;
	}
	tris--;
	return -1;
}

void Mod_FindSharedEdges(model_t *mod)
{
	dmdl_t		*hdr = (dmdl_t *)mod->extradata;
	dtriangle_t *tris = (dtriangle_t *)((unsigned char*)hdr + hdr->ofs_tris);
	int i,o;

	mod->noshadow = false;

	for (i=0; i<hdr->num_tris; i++) 
	{
		mod->edge_tri[i][0] = Mod_GetTris(tris->index_xyz[0], tris->index_xyz[1], tris, hdr);
		mod->edge_tri[i][1] = Mod_GetTris(tris->index_xyz[1], tris->index_xyz[2], tris, hdr);
		mod->edge_tri[i][2] = Mod_GetTris(tris->index_xyz[2], tris->index_xyz[0], tris, hdr);

		for (o=0; o<3; o++)
			if (mod->edge_tri[i][o] == -1)
				mod->noshadow = true;

		tris++;
	}
}

/*
@@@@@@@@@@@@@@@@@@@@@
R_RegisterModel

@@@@@@@@@@@@@@@@@@@@@
*/
struct model_s *R_RegisterModel (char *name)
{
	model_t	*mod;
	int		i;
	dsprite_t	*sprout;
	dmdl_t		*pheader;

	mod = Mod_ForName (name, false);
	if (mod)
	{
		mod->registration_sequence = registration_sequence;

		// register any images used by the models
		if (mod->type == mod_sprite)
		{
			sprout = (dsprite_t *)mod->extradata;
			for (i=0 ; i<sprout->numframes ; i++)
				mod->skins[i] = GL_FindImage (sprout->frames[i].name, it_sprite);
		}
		else if (mod->type == mod_alias)
		{
			pheader = (dmdl_t *)mod->extradata;
			for (i=0 ; i<pheader->num_skins ; i++)
				mod->skins[i] = GL_FindImage ((char *)pheader + pheader->ofs_skins + i*MAX_SKINNAME, it_skin);
//PGM
			mod->numframes = pheader->num_frames;
//PGM
			Mod_FindSharedEdges(mod);
		}
		else if (mod->type == mod_brush)
		{
			for (i=0 ; i<mod->numtexinfo ; i++)
				mod->texinfo[i].image->registration_sequence = registration_sequence;
		}
	}
	return mod;
}


/*
@@@@@@@@@@@@@@@@@@@@@
R_EndRegistration

@@@@@@@@@@@@@@@@@@@@@
*/
void R_EndRegistration (void)
{
	int		i;
	model_t	*mod;

	for (i=0, mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			continue;
		if (mod->registration_sequence != registration_sequence)
		{	// don't need this model
			Mod_Free (mod);
		}
	}

	RS_UpdateRegistration();
	GL_FreeUnusedImages ();
}


//=============================================================================


/*
================
Mod_Free
================
*/
void Mod_Free (model_t *mod)
{
	Hunk_Free (mod->extradata);
	memset (mod, 0, sizeof(*mod));
}

/*
================
Mod_FreeAll
================
*/
void Mod_FreeAll (void)
{
	int		i;

	for (i=0 ; i<mod_numknown ; i++)
	{
		if (mod_known[i].extradatasize)
			Mod_Free (&mod_known[i]);
	}
}


/*
 =======================================================================

 FRAGMENT CLIPPING

 =======================================================================
*/

#define	SIDE_FRONT				0
#define	SIDE_BACK				1
#define	SIDE_ON					2

#define ON_EPSILON				0.1

#define MAX_FRAGMENT_POINTS		128
#define MAX_FRAGMENT_PLANES		6

static int				cm_numMarkPoints;
static int				cm_maxMarkPoints;
static vec3_t			*cm_markPoints;

static int				cm_numMarkFragments;
static int				cm_maxMarkFragments;
static markFragment_t	*cm_markFragments;

static cplane_t			cm_markPlanes[MAX_FRAGMENT_PLANES];

static int				cm_markCheckCount;

int PlaneTypeForNormal (const vec3_t normal)
{ 
	if (normal[0] == 1.0)  
		return 0;//PLANE_X;  
	if (normal[1] == 1.0)  
		return 1;//PLANE_Y;  
	if (normal[2] == 1.0)  
		return 2;//PLANE_Z; 
 	return 3;//PLANE_NON_AXIAL; 
} 

/*
 =================
 Mod_ClipFragment
 =================
*/

float *worldVert (int i, msurface_t *surf)
{
	int e = r_worldmodel->surfedges[surf->firstedge + i];
	if (e >= 0)
		return &r_worldmodel->vertexes[r_worldmodel->edges[e].v[0]].position[0];
	else
		return &r_worldmodel->vertexes[r_worldmodel->edges[-e].v[1]].position[0];

}

static void Mod_ClipFragment (int numPoints, vec3_t points, int stage, markFragment_t *mf)
{
	int			i;
	float		*p;
	qboolean	frontSide;
	vec3_t		front[MAX_FRAGMENT_POINTS];
	int			f;
	float		dist;
	float		dists[MAX_FRAGMENT_POINTS];
	int			sides[MAX_FRAGMENT_POINTS];
	cplane_t	*plane;

	if (numPoints > MAX_FRAGMENT_POINTS-2)
		ri.Sys_Error(ERR_DROP, "Mod_ClipFragment: MAX_FRAGMENT_POINTS hit");

	if (stage == MAX_FRAGMENT_PLANES)
	{
		// Fully clipped
		if (numPoints > 2)
		{
			mf->numPoints = numPoints;
			mf->firstPoint = cm_numMarkPoints;
			
			if (cm_numMarkPoints + numPoints > cm_maxMarkPoints)
				numPoints = cm_maxMarkPoints - cm_numMarkPoints;

			for (i = 0, p = points; i < numPoints; i++, p += 3)
				VectorCopy(p, cm_markPoints[cm_numMarkPoints+i]);

			cm_numMarkPoints += numPoints;
		}

		return;
	}

	frontSide = false;

	plane = &cm_markPlanes[stage];
	for (i = 0, p = points; i < numPoints; i++, p += 3)
	{
		if (plane->type < 3)
			dists[i] = dist = p[plane->type] - plane->dist;
		else
			dists[i] = dist = DotProduct(p, plane->normal) - plane->dist;

		if (dist > ON_EPSILON){
			frontSide = true;
			sides[i] = SIDE_FRONT;
		}
		else if (dist < -ON_EPSILON)
			sides[i] = SIDE_BACK;
		else
			sides[i] = SIDE_ON;
	}

	if (!frontSide)
		return;		// Not clipped

	// Clip it
	dists[i] = dists[0];
	sides[i] = sides[0];
	VectorCopy(points, (points + (i*3)));

	f = 0;

	for (i = 0, p = points; i < numPoints; i++, p += 3){
		switch (sides[i]){
		case SIDE_FRONT:
			VectorCopy(p, front[f]);
			f++;
			break;
		case SIDE_BACK:
			break;
		case SIDE_ON:
			VectorCopy(p, front[f]);
			f++;
			break;
		}

		if (sides[i] == SIDE_ON || sides[i+1] == SIDE_ON || sides[i+1] == sides[i])
			continue;

		dist = dists[i] / (dists[i] - dists[i+1]);

		front[f][0] = p[0] + (p[3] - p[0]) * dist;
		front[f][1] = p[1] + (p[4] - p[1]) * dist;
		front[f][2] = p[2] + (p[5] - p[2]) * dist;

		f++;
	}

	// Continue
	Mod_ClipFragment(f, front[0], stage+1, mf);
}

/*
 =================
 Mod_ClipFragmentToSurface
 =================
*/
static void Mod_ClipFragmentToSurface (msurface_t *surf, const vec3_t normal)
{
	qboolean planeback = surf->flags & SURF_PLANEBACK;
	int				i;
	float			d;
	vec3_t			points[MAX_FRAGMENT_POINTS];
	markFragment_t	*mf;
	glpoly_t *bp, *p;

	if (cm_numMarkPoints >= cm_maxMarkPoints || cm_numMarkFragments >= cm_maxMarkFragments)
		return;		// Already reached the limit somewhere else

	d = DotProduct(normal, surf->plane->normal);
	if ((planeback && d > -0.75) || (!planeback && d < 0.75))
		return;		// Greater than X degrees

	for (i = 2; i < surf->numedges; i++)
	{
		mf = &cm_markFragments[cm_numMarkFragments];
		mf->firstPoint = mf->numPoints = 0;
		
		VectorCopy(worldVert(0, surf), points[0]);
		VectorCopy(worldVert(i-1, surf), points[1]);
		VectorCopy(worldVert(i, surf), points[2]);

		Mod_ClipFragment(3, points[0], 0, mf);

		if (mf->numPoints)
		{
			cm_numMarkFragments++;

			if (cm_numMarkPoints >= cm_maxMarkPoints || cm_numMarkFragments >= cm_maxMarkFragments)
				return;
		}
	}
}

/*
 =================
 Mod_RecursiveMarkFragments
 =================
*/
static void Mod_RecursiveMarkFragments (const vec3_t origin, const vec3_t normal, float radius, mnode_t *node)
{

	int			i;
	float		dist;
	cplane_t	*plane;
	msurface_t	*surf;

	if (cm_numMarkPoints >= cm_maxMarkPoints || cm_numMarkFragments >= cm_maxMarkFragments)
		return;		// Already reached the limit somewhere else

	if (node->contents != -1)
		return;

	// Find which side of the node we are on
	plane = node->plane;
	if (plane->type < 3)
		dist = origin[plane->type] - plane->dist;
	else
		dist = DotProduct(origin, plane->normal) - plane->dist;
	
	// Go down the appropriate sides
	if (dist > radius)
	{
		Mod_RecursiveMarkFragments(origin, normal, radius, node->children[0]);
		return;
	}
	if (dist < -radius)
	{
		Mod_RecursiveMarkFragments(origin, normal, radius, node->children[1]);
		return;
	}
	// Clip against each surface


	surf = r_worldmodel->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		if (surf->checkCount == cm_markCheckCount)
			continue;	// Already checked this surface in another node

		if (surf->texinfo->flags & (SURF_SKY|SURF_WARP))
			continue;

		surf->checkCount = cm_markCheckCount;

		Mod_ClipFragmentToSurface(surf, normal);
	}
	// Recurse down the children
	Mod_RecursiveMarkFragments(origin, normal, radius, node->children[0]);
	Mod_RecursiveMarkFragments(origin, normal, radius, node->children[1]);
}

/*
 =================
 Mod_MarkFragments
 =================
*/
int Mod_MarkFragments (const vec3_t origin, const vec3_t axis[3], float radius, int maxPoints, vec3_t *points, int maxFragments, markFragment_t *fragments)
{

	int		i;
	float	dot;

	if (!r_worldmodel || !r_worldmodel->nodes)
		return 0;			// Map not loaded

	cm_markCheckCount++;	// For multi-check avoidance

	// Initialize fragments
	cm_numMarkPoints = 0;
	cm_maxMarkPoints = maxPoints;
	cm_markPoints = points;

	cm_numMarkFragments = 0;
	cm_maxMarkFragments = maxFragments;
	cm_markFragments = fragments;

	// Calculate clipping planes
	for (i = 0; i < 3; i++)
	{
		dot = DotProduct(origin, axis[i]);

		VectorCopy(axis[i], cm_markPlanes[i*2+0].normal);
		cm_markPlanes[i*2+0].dist = dot - radius;
		cm_markPlanes[i*2+0].type = PlaneTypeForNormal(cm_markPlanes[i*2+0].normal);

		VectorNegate(axis[i], cm_markPlanes[i*2+1].normal);
		cm_markPlanes[i*2+1].dist = -dot - radius;
		cm_markPlanes[i*2+1].type = PlaneTypeForNormal(cm_markPlanes[i*2+1].normal);
	}

	// Clip against world geometry
	Mod_RecursiveMarkFragments(origin, axis[0], radius, r_worldmodel->nodes);

	return cm_numMarkFragments;
}
