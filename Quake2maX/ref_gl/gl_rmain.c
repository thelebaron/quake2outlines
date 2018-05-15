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
// r_main.c
#include "gl_local.h"
#include "vlights.h"

void R_Clear (void);

#define random()	((rand () & 0x7fff) / ((float)0x7fff))
#define crandom()	(2.0 * (random() - 0.5))

viddef_t	vid;

refimport_t	ri;

int GL_TEXTURE0, GL_TEXTURE1;

model_t		*r_worldmodel;

float		gldepthmin, gldepthmax;

glconfig_t gl_config;
glstate_t  gl_state;

image_t		*r_particletextures[PARTICLE_TYPES]; //list for particles
image_t		*r_particlebeam; //used for beam ents
image_t		*r_celtexture; //used for cel shading
image_t		*r_notexture;		// use for bad textures
//image_t     *r_blurtexture=NULL; // blur

image_t		*r_dynamicimage;

particle_t	*currentparticle;
entity_t	*currententity;
model_t		*currentmodel;

cplane_t	frustum[4];

int			r_viewport[4];

int			r_visframecount;	// bumped when going to a new PVS
int			r_framecount;		// used for dlight push checking

int			c_brush_polys, c_alias_polys;

float		v_blend[4];			// final blending color

void GL_Strings_f( void );

//
// view origin
//
vec3_t	vup;
vec3_t	vpn;
vec3_t	vright;
vec3_t	r_origin;

float	r_world_matrix[16];
float	r_base_world_matrix[16];

//
// screen size info
//
refdef_t	r_newrefdef;

int		r_viewcluster, r_viewcluster2, r_oldviewcluster, r_oldviewcluster2;

cvar_t *skydistance; // DMP - skybox size change

cvar_t *gl_transrendersort;
cvar_t *gl_particle_lighting;
cvar_t *gl_particle_min;
cvar_t *gl_particle_max;
cvar_t *gl_wire; //SEB

cvar_t  *r_outline; // old outline
cvar_t  *r_outline2; // new outline
//cvar_t  *r_motionblur;
//cvar_t  *r_motionblur_density;


#ifdef LIGHT_BLOOMS
cvar_t  *r_bloom;
#endif

cvar_t	*r_norefresh;
cvar_t	*r_drawentities;
cvar_t	*r_drawworld;
cvar_t	*r_speeds;
cvar_t	*r_fullbright;
cvar_t	*r_novis;
cvar_t	*r_nocull;
cvar_t	*r_lerpmodels;
cvar_t	*r_lefthand;

cvar_t *r_dlights_normal;

cvar_t *r_stainmap;

cvar_t *r_shaders;

cvar_t *rs_dynamic_time;
cvar_t *rs_detail;

cvar_t *r_model_lightlerp;
cvar_t *r_model_dlights;

cvar_t	*r_lightlevel;	// FIXME: This is a HACK to get the client's light level

cvar_t	*gl_nosubimage;
cvar_t	*gl_allow_software;

cvar_t	*gl_vertex_arrays;

cvar_t	*gl_ext_texture_compression; // Heffo - ARB Texture Compression
// Vic - begin
cvar_t	*gl_ext_mtexcombine;
cvar_t	*r_overbrightbits;
// Vic - end

cvar_t	*r_celshading;
cvar_t	*r_celshading_width;

cvar_t	*con_font;
cvar_t	*con_font_size;

cvar_t	*cl_3dcam;
cvar_t	*cl_3dcam_angle;
cvar_t	*cl_3dcam_chase;
cvar_t	*cl_3dcam_dist;
cvar_t	*cl_3dcam_alpha;
cvar_t	*cl_3dcam_adjust;

cvar_t	*gl_ext_swapinterval;
cvar_t	*gl_ext_multitexture;
cvar_t	*gl_ext_pointparameters;
cvar_t	*gl_ext_compiled_vertex_array;

cvar_t	*gl_stencil;

cvar_t	*gl_screenshot_quality;

cvar_t	*gl_surftrans_light;
cvar_t	*gl_log;
cvar_t	*gl_bitdepth;
cvar_t	*gl_drawbuffer;
cvar_t  *gl_driver;
cvar_t	*gl_lightmap;
cvar_t	*gl_shadows;
cvar_t	*gl_mode;
cvar_t	*gl_dynamic;
cvar_t  *gl_monolightmap;
cvar_t	*gl_modulate;
cvar_t	*gl_nobind;
cvar_t	*gl_round_down;
cvar_t	*gl_picmip;
cvar_t	*gl_skymip;
cvar_t	*gl_showtris;
cvar_t	*gl_ztrick;
cvar_t	*gl_finish;
cvar_t	*gl_clear;
cvar_t	*gl_cull;
cvar_t	*gl_polyblend;
cvar_t	*gl_flashblend;
cvar_t	*gl_playermip;
cvar_t  *gl_saturatelighting;
cvar_t	*gl_swapinterval;
cvar_t	*gl_texturemode;
cvar_t	*gl_anisotropic;
cvar_t	*gl_texturealphamode;
cvar_t	*gl_texturesolidmode;
cvar_t	*gl_lockpvs;

cvar_t	*gl_3dlabs_broken;

cvar_t	*vid_fullscreen;
cvar_t	*vid_gamma;
cvar_t	*vid_ref;

/*
=================
GL_Stencil

setting stencil buffer
=================
*/
extern qboolean have_stencil;
void GL_Stencil (qboolean enable)
{
	if (!have_stencil || !gl_stencil->value) 
		return;

	if (enable)
	{
		qglEnable(GL_STENCIL_TEST);
		qglStencilFunc(GL_EQUAL, 1, 2);
		qglStencilOp(GL_KEEP,GL_KEEP,GL_INCR);
	}
	else
	{
		qglDisable(GL_STENCIL_TEST);
	}
}
qboolean GL_HasStencil (void)
{
	return (have_stencil && gl_stencil->value);
}

/*
=================
GL_Spheremap

setting up spheremap
=================
*/

void GL_Spheremap (qboolean enable)
{

	if (enable)
	{
		qglTexGenf(GL_S, GL_TEXTURE_GEN_MODE,GL_SPHERE_MAP);
		qglTexGenf(GL_T, GL_TEXTURE_GEN_MODE,GL_SPHERE_MAP);
		GLSTATE_ENABLE_TEXGEN
	}
	else
	{
		GLSTATE_DISABLE_TEXGEN
	}
}

/*
=================
R_CullBox

Returns true if the box is completely outside the frustom
=================
*/
qboolean R_CullBox (vec3_t mins, vec3_t maxs)
{
	int		i;

	if (r_nocull->value)
		return false;

	for (i=0 ; i<4 ; i++)
		if ( BOX_ON_PLANE_SIDE(mins, maxs, &frustum[i]) == 2)
			return true;
	return false;
}

qboolean R_CullSphere (const vec3_t origin, float radius)
{ 
 	int i; 
	cplane_t *p; 
 
	if (r_nocull->value) 
		return false; 
 
	for (i = 0, p = frustum; i < 4; i++, p++) 
		if (DotProduct(origin, p->normal) - p->dist <= -radius) 
			return true; 
	 
	return false; 
}

void R_RotateForEntity (entity_t *e, qboolean full)
{
    qglTranslatef (e->origin[0],  e->origin[1],  e->origin[2]);

    qglRotatef (e->angles[1],  0, 0, 1);
	if (full==true)
	{
		qglRotatef (-e->angles[0],  0, 1, 0);
		qglRotatef (-e->angles[2],  1, 0, 0);
	}
}

/*
=============================================================

  SPRITE MODELS

=============================================================
*/


/*
=================
R_DrawSpriteModel

=================
*/
void R_DrawSpriteModel (entity_t *e)
{
	float alpha = 1.0F;
	vec3_t	point, up, right;
	dsprframe_t	*frame;
	dsprite_t		*psprite;

	// don't even bother culling, because it's just a single
	// polygon without a surface cache

	psprite = (dsprite_t *)currentmodel->extradata;

	e->frame %= psprite->numframes;

	frame = &psprite->frames[e->frame];

	if (!frame)
		return;

	// normal sprite
	VectorCopy(vup, up);
	VectorCopy(vright, right);


	if ( e->flags & RF_TRANSLUCENT )
		alpha = e->alpha;

	if (!currentmodel->skins[e->frame])
		return;

	GL_Bind(currentmodel->skins[e->frame]->texnum);

	if ((currententity->flags&RF_TRANS_ADDITIVE) && (alpha != 1.0F))
	{ 	
		GLSTATE_ENABLE_BLEND
		GL_TexEnv( GL_MODULATE );

		GLSTATE_DISABLE_ALPHATEST
		GL_BlendFunction (GL_SRC_ALPHA, GL_ONE);

		qglColor4ub(255, 255, 255, alpha*254);		
	}
	else
	{
		if ( alpha != 1.0F )
			GLSTATE_ENABLE_BLEND

		GL_TexEnv( GL_MODULATE );
		
		if ( alpha == 1.0 )
			GLSTATE_ENABLE_ALPHATEST
		else
			GLSTATE_DISABLE_ALPHATEST
		
		qglColor4f( 1, 1, 1, alpha );
	}

	qglBegin (GL_QUADS);

	qglTexCoord2f (0, 1);
	VectorMA (e->origin, -frame->origin_y, up, point);
	VectorMA (point, -frame->origin_x, right, point);
	qglVertex3fv (point);

	qglTexCoord2f (0, 0);
	VectorMA (e->origin, frame->height - frame->origin_y, up, point);
	VectorMA (point, -frame->origin_x, right, point);
	qglVertex3fv (point);

	qglTexCoord2f (1, 0);
	VectorMA (e->origin, frame->height - frame->origin_y, up, point);
	VectorMA (point, frame->width - frame->origin_x, right, point);
	qglVertex3fv (point);

	qglTexCoord2f (1, 1);
	VectorMA (e->origin, -frame->origin_y, up, point);
	VectorMA (point, frame->width - frame->origin_x, right, point);
	qglVertex3fv (point);
	
	qglEnd ();


	GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GLSTATE_DISABLE_ALPHATEST

	GL_TexEnv( GL_REPLACE );

	if ( alpha != 1.0F )
		GLSTATE_DISABLE_BLEND

	qglColor4f( 1, 1, 1, 1 );
}

//==================================================================================

/*
=============
R_DrawNullModel
=============
*/
void R_DrawNullModel (void)
{
	vec3_t	shadelight;
	int		i;

	if ( currententity->flags & RF_FULLBRIGHT )
		shadelight[0] = shadelight[1] = shadelight[2] = 1.0F;
	else
		R_LightPoint (currententity->origin, shadelight);

    qglPushMatrix ();
	R_RotateForEntity (currententity, true);

	qglDisable (GL_TEXTURE_2D);
	qglColor3fv (shadelight);

	qglBegin (GL_TRIANGLE_FAN);
	qglVertex3f (0, 0, -16);
	for (i=0 ; i<=4 ; i++)
		qglVertex3f (16*cos(i*M_PI/2), 16*sin(i*M_PI/2), 0);
	qglEnd ();

	qglBegin (GL_TRIANGLE_FAN);
	qglVertex3f (0, 0, 16);
	for (i=4 ; i>=0 ; i--)
		qglVertex3f (16*cos(i*M_PI/2), 16*sin(i*M_PI/2), 0);
	qglEnd ();

	qglColor3f (1,1,1);
	qglPopMatrix ();
	qglEnable (GL_TEXTURE_2D);
}
/*
=============
R_DrawEntitiesOnList
=============
*/

// STRUCTURE STUFF

typedef struct sortedelement_s 
{
	void *data;
	vec_t len;
	vec3_t org;
	void *left, *right, *next;
}
sortedelement_t;

int entstosort;
sortedelement_t theents[MAX_ENTITIES];
sortedelement_t *ents_viewweaps;
sortedelement_t *ents_viewweaps_trans;
sortedelement_t *ents_prerender;
sortedelement_t *ents_last;


void resetSortList (void)
{
	entstosort = 0;
	ents_last = NULL;
	ents_prerender = NULL;
	ents_viewweaps = NULL;
	ents_viewweaps_trans = NULL;
}

sortedelement_t *NewSortEnt (entity_t *ent)
{
	qboolean entinwater, is_weapmodel = false;
	vec3_t distance;
	sortedelement_t *element;
	mleaf_t *point_in;

	element = &theents[entstosort];

	VectorSubtract(ent->origin, r_origin, distance);
	VectorCopy(ent->origin, element->org);

	element->data = (entity_t *)ent;
	element->len = (vec_t)VectorLength(distance);
	element->left = NULL;
	element->right = NULL;
	element->next = NULL;

	return element;
}

// TREE BUILDING AND USAGE

void ElementAddNode ( sortedelement_t *base, sortedelement_t *thisElement )
{
	if (thisElement->len > base->len)
	{
		if (base->left)
			ElementAddNode(base->left, thisElement);
		else
			base->left = thisElement;
	}
	else
	{
		if (base->right)
			ElementAddNode(base->right, thisElement);
		else
			base->right = thisElement;
	}
}

void AddEntViewWeapTree( entity_t *ent, qboolean trans)
{
	int closer = 0;
	sortedelement_t *thisEnt;


	thisEnt = NewSortEnt(ent);

	if (!thisEnt)
		return;

	if (!trans)
	{
		if (ents_viewweaps)
			ElementAddNode(ents_viewweaps, thisEnt);
		else
			ents_viewweaps = thisEnt;
	}
	else
	{		
		if (ents_viewweaps_trans)
			ElementAddNode(ents_viewweaps_trans, thisEnt);
		else
			ents_viewweaps_trans = thisEnt;	
	}

	entstosort++;
}

void AddEntTransTree( entity_t *ent )
{
	int closer = 0;
	sortedelement_t *thisEnt;


	thisEnt = NewSortEnt(ent);

	if (!thisEnt)
		return;


	if (ents_prerender)
		ElementAddNode(ents_prerender, thisEnt);
		//ents_last->next = thisEnt;
	else
		ents_prerender = thisEnt;

	ents_last = thisEnt;

	entstosort++;
}

void ParseRenderEntity (entity_t *ent)
{
	currententity = ent;

	if ( currententity->flags & RF_BEAM )
	{
		R_DrawBeam( currententity );
	}
	else
	{
		currentmodel = currententity->model;
		if (!currentmodel)
		{
			R_DrawNullModel ();
			return;
		}
		switch (currentmodel->type)
		{
		case mod_alias:
		//case MODEL_MD3:			// nutro
			R_DrawAliasModel (currententity);
			break;
		case mod_brush:
			R_DrawBrushModel (currententity);
			break;
		case mod_sprite:
			R_DrawSpriteModel (currententity);
			break;
		default:
			ri.Sys_Error (ERR_DROP, "Bad modeltype");
			break;
		}
	}
}

qboolean transBrushModel (entity_t *ent)
{
	int i;
	msurface_t *surf;

	if (ent && ent->model && ent->model->type==mod_brush)
	{
		surf = &ent->model->surfaces[ent->model->firstmodelsurface];
		for (i=0 ; i<ent->model->nummodelsurfaces ; i++, surf++)
			if (surf && surf->texinfo->flags & (SURF_TRANS33|SURF_TRANS66))
				return true;
	}

	return false;
}

void RenderEntTree (sortedelement_t *element)
{
	if (!element)
		return;

	RenderEntTree(element->left);

	if (element->data)
		ParseRenderEntity(element->data);

	RenderEntTree(element->right);
}

// ACTUAL RENDERING FUNCTIONS

void R_DrawAliasShadow (entity_t *ent);
void R_DrawAllEntityShadows (void)
{
	qboolean	alpha;
	rscript_t	*rs = NULL;
	int i;

	if(!gl_shadows->value)
		return;

	for (i=0;i<r_newrefdef.num_entities; i++)
	{
		currententity = &r_newrefdef.entities[i];

		if ( currententity->flags & RF_BEAM )
			continue;
		currentmodel = currententity->model;
		if (!currentmodel)
			continue;
		if (currentmodel->type!=mod_alias)
			continue;
		

		if ( currententity->flags & ( RF_TRANSLUCENT | RF_WEAPONMODEL | RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM) ) 
			continue;
		if	(currententity->renderfx & RF2_NOSHADOW)
			continue;
		if (currententity->flags&RF_TRANSLUCENT)
			continue;
//		if (transBrushModel(currententity)) //crashy
//			continue;

		alpha = false;
		if (currententity->model && r_shaders->value)
		{
			rs=(rscript_t *)currententity->model->script[currententity->skinnum];
			if (!rs && currententity->skin)
				rs = currententity->skin->script;
			if (rs)
			{
				RS_ReadyScript(rs);
				currententity->script = rs;
				if (rs->stage && rs->stage->has_alpha)
					continue;
			}
			else
				currententity->script = NULL;
		}

		R_DrawAliasShadow (currententity);
	}
}

void R_DrawAllEntities (qboolean addViewWeaps)
{
	qboolean alpha;
	rscript_t	*rs = NULL;
	int i;
	
	if (!r_drawentities->value)
		return;

	resetSortList();

	for (i=0;i<r_newrefdef.num_entities; i++)
	{
		currententity = &r_newrefdef.entities[i];

		alpha = false;
		if (currententity->flags&RF_TRANSLUCENT)
			alpha = true;
		if (currententity->model && r_shaders->value)
		{
			rs=(rscript_t *)currententity->model->script[currententity->skinnum];
			if (currententity->skin)
				rs = currententity->skin->script;
			if (rs)
			{
				RS_ReadyScript(rs);
				currententity->script = rs;
				if (rs->stage && rs->stage->has_alpha)
					alpha = true;
			}
			else
				currententity->script = NULL;
		}
//		if (transBrushModel(currententity))
//			alpha = true;

		if (currententity->flags & RF_WEAPONMODEL)
		{
			if (addViewWeaps)
				AddEntViewWeapTree(currententity, alpha);
			continue;
		}

		if (alpha)
			continue;

		ParseRenderEntity(currententity);
	}

	qglDepthMask (0);
	for (i=0;i<r_newrefdef.num_entities; i++)
	{
		currententity = &r_newrefdef.entities[i];

		alpha = false;
		if (currententity->flags&RF_TRANSLUCENT)
			alpha = true;
		if (currententity->model && r_shaders->value)
		{
			rs=(rscript_t *)currententity->model->script[currententity->skinnum];
			if (currententity->skin)
				rs = currententity->skin->script;

			if (rs)
			{
				RS_ReadyScript(rs);
				currententity->script = rs;
				if (rs->stage && rs->stage->has_alpha)
					alpha = true;
			}
			else
				currententity->script = NULL;
		}
//		if (transBrushModel(currententity))
//			alpha = true;

		if (currententity->flags & RF_WEAPONMODEL)
			continue;
		if (!alpha)
			continue;

		ParseRenderEntity(currententity);
	}
	qglDepthMask (1);
	
}

void R_DrawSolidEntities ()
{
	qboolean alpha;
	int		i;
	rscript_t *rs = NULL;

	if (!r_drawentities->value)
		return;

	resetSortList();

	for (i=0;i<r_newrefdef.num_entities; i++)
	{
		currententity = &r_newrefdef.entities[i];
		alpha = false;

		//find alpha and script...
		if (currententity->model && r_shaders->value)
		{
			rs=(rscript_t *)currententity->model->script[currententity->skinnum];
			if (currententity->skin)
				rs = currententity->skin->script;
			if (rs)
			{
				RS_ReadyScript(rs);
				currententity->script = rs;
				//only add to list if base layer is alpha
				if (rs->stage && rs->stage->has_alpha)
					alpha = true;
			}
			else
				currententity->script = NULL;
		}
		if (currententity->flags&RF_TRANSLUCENT)
			alpha = true;
//		if (transBrushModel(currententity))
//			alpha = true;

		if (currententity->flags & RF_WEAPONMODEL)
		{
			AddEntViewWeapTree(currententity, alpha);
			continue;
		}

		if (alpha)
		{
			AddEntTransTree(currententity);
			continue;
		}

		ParseRenderEntity(currententity);
	}
}

void R_DrawEntitiesOnList (void *list)
{
	if (!r_drawentities->value)
		return;

	RenderEntTree(list);
}

/*
** GL_DrawParticles
**
*/

int partstosort;
sortedelement_t theparts[MAX_PARTICLES];
sortedelement_t *parts_prerender;
sortedelement_t *parts_decals;
sortedelement_t *parts_last;
void renderParticle (particle_t *p);
void renderDecal (particle_t *p);

sortedelement_t *NewSortPart ( particle_t *p)
{
	qboolean partinwater;
	vec3_t distance;
	sortedelement_t *element;

	element = &theparts[partstosort];

	VectorSubtract(p->origin, r_origin, distance);
	VectorCopy(p->origin, element->org);

	element->data	= p;
	element->len	= VectorLength(distance);
	element->left	= NULL;
	element->right	= NULL;

	return element;
}

void resetPartSortList (void)
{
	partstosort = 0;
	parts_prerender = NULL;
	parts_decals = NULL;
	parts_last = NULL;
}

qboolean particleClip( float len )
{
	if (gl_particle_min->value>0)
	{
		if (len < gl_particle_min->value)
			return true;
	}
	if (gl_particle_max->value>0)
	{
		if (len > gl_particle_max->value)
			return true;
	}

	return false;
}

void AddPartTransTree( particle_t *p )
{
	sortedelement_t *thisPart;

	thisPart = NewSortPart(p);

	if (p->flags&PART_DECAL)
	{
		if (parts_decals)
			ElementAddNode(parts_decals, thisPart);		
		else
			parts_decals = thisPart;	
	}
	else
	{
		if (particleClip(thisPart->len))
			return;

		if (parts_prerender)
			ElementAddNode(parts_prerender, thisPart);	
		else
			parts_prerender = thisPart;

		parts_last = thisPart;
	}

	partstosort++;
}

void GL_BuildParticleList()
{
	int		i;
	resetPartSortList();

	for ( i=0 ; i < r_newrefdef.num_particles ; i++)
	{
		currentparticle = &r_newrefdef.particles[i];
		AddPartTransTree(currentparticle);
	}
}

void RenderParticleTree (sortedelement_t *element)
{
	if (!element)
		return;

	RenderParticleTree(element->left);

	if (element->data)
		renderParticle((particle_t *)element->data);

	RenderParticleTree(element->right);
}

void RenderDecalTree (sortedelement_t *element)
{
	if (!element)
		return;

	RenderDecalTree(element->left);

	if (element->data)
		renderDecal((particle_t *)element->data);

	RenderDecalTree(element->right);
}

/*
=====================
R_RenderBlurMotions
=====================
*/
/*
static void R_RenderBlurMotions(int howmany_times) {
      int i;
      int density = howmany_times;
      if(density > 4) density = 4;
      else if(density < 1) return;

         if(r_blurtexture == NULL)
            R_InitBlurTexture ();
         else {
            float vs, vt;
            int vwidth = 1, vheight = 1;
            while (vwidth < r_refdef.width)   { vwidth *= 2;   }
            while (vheight < r_refdef.height) { vheight *= 2; }
            qglViewport (r_refdef.x, r_refdef.y, r_refdef.width, r_refdef.height);
            GL_Bind(0, r_blurtexture);
            qglMatrixMode(GL_PROJECTION);
            qglPushMatrix();
            qglLoadIdentity ();
            qglOrtho  (0, r_refdef.width, 0, r_refdef.height, -99999, 99999);
            qglMatrixMode(GL_MODELVIEW);
            qglPushMatrix();
            qglLoadIdentity ();
            vs = (float)r_refdef.width / vwidth;
            vt = (float)r_refdef.height / vheight;
            qglDisable (GL_DEPTH_TEST);
            qglDisable (GL_CULL_FACE);
            qglDisable (GL_ALPHA_TEST);
            qglEnable(GL_BLEND);
            GL_TexEnv(GL_MODULATE);
            qglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
            qglColor4f(1.0f, 1.0f, 1.0f, 0.8f);


            for(i=0; i < density; i++) {
               qglBegin(GL_QUADS);
               qglTexCoord2f(0, 0);
               qglVertex2f(0, 0);
               qglTexCoord2f(vs, 0);
               qglVertex2f(r_refdef.width, 0);
               qglTexCoord2f(vs, vt);
               qglVertex2f(r_refdef.width, r_refdef.height);
               qglTexCoord2f(0, vt);
               qglVertex2f(0, r_refdef.height);
               qglEnd();
               if(density > 1) {
                  qglBlendFunc( GL_SRC_ALPHA, GL_ONE );
                  qglColor4f(1.0f, 1.0f, 1.0f, 1.0f /((i+1)*16.0f) );
               }
            }

            qglMatrixMode(GL_PROJECTION);
            qglPopMatrix();
            qglMatrixMode(GL_MODELVIEW);
            qglPopMatrix();
            qglCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, r_refdef.x, r_refdef.y, vwidth, vheight, 0);
            qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            qglColor4f(1.0f, 1.0f, 1.0f, 1.0f);
         }
} 
*/


/*
===============
R_DrawParticles
===============
*/

vec3_t particle_coord[4];
void R_DrawDecals (void)
{
	vec3_t		up		= {vup[0]    * 0.75f, vup[1]    * 0.75f, vup[2]    * 0.75f};
	vec3_t		right	= {vright[0] * 0.75f, vright[1] * 0.75f, vright[2] * 0.75f};

	VectorAdd      (up, right, particle_coord[0]);
	VectorSubtract (right, up, particle_coord[1]);
	VectorNegate   (particle_coord[0], particle_coord[2]);
	VectorNegate   (particle_coord[1], particle_coord[3]);
	
	qglEnable (GL_TEXTURE_2D);
	GL_TexEnv( GL_MODULATE );
	qglDepthMask   (false);
	GLSTATE_ENABLE_BLEND
	GL_ShadeModel (GL_SMOOTH);
	GLSTATE_DISABLE_ALPHATEST

	RenderDecalTree(parts_decals);

	qglDepthRange (gldepthmin, gldepthmax);
	GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_TexEnv( GL_MODULATE );
	qglDepthMask (true);
	GLSTATE_DISABLE_BLEND
	qglColor4f   (1,1,1,1);
}

void R_DrawAllDecals (void)
{	
	vec3_t		up		= {vup[0]    * 0.75f, vup[1]    * 0.75f, vup[2]    * 0.75f};
	vec3_t		right	= {vright[0] * 0.75f, vright[1] * 0.75f, vright[2] * 0.75f};
	int		i;

	VectorAdd      (up, right, particle_coord[0]);
	VectorSubtract (right, up, particle_coord[1]);
	VectorNegate   (particle_coord[0], particle_coord[2]);
	VectorNegate   (particle_coord[1], particle_coord[3]);
	
	qglEnable (GL_TEXTURE_2D);
	GL_TexEnv( GL_MODULATE );
	qglDepthMask   (false);
	GLSTATE_ENABLE_BLEND
	GL_ShadeModel (GL_SMOOTH);	
	GLSTATE_DISABLE_ALPHATEST

	for ( i=0 ; i < r_newrefdef.num_particles ; i++)
		if (r_newrefdef.particles[i].flags&PART_DECAL)
			renderDecal(&r_newrefdef.particles[i]);

	qglDepthRange (gldepthmin, gldepthmax);
	GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_TexEnv( GL_MODULATE );
	qglDepthMask (true);
	GLSTATE_DISABLE_BLEND
	qglColor4f   (1,1,1,1);
}

void SetVertexOverbrights(qboolean);

qboolean ParticleOverbright;
void SetParticleOverbright(qboolean toggle)
{
	if ( (toggle && !ParticleOverbright) || (!toggle && ParticleOverbright) )
	{
		SetVertexOverbrights(toggle);
		ParticleOverbright = toggle;
	}
}

void R_DrawParticles (void *list)
{	
	vec3_t		up		= {vup[0]    * 0.75f, vup[1]    * 0.75f, vup[2]    * 0.75f};
	vec3_t		right	= {vright[0] * 0.75f, vright[1] * 0.75f, vright[2] * 0.75f};

	VectorAdd      (up, right, particle_coord[0]);
	VectorSubtract (right, up, particle_coord[1]);
	VectorNegate   (particle_coord[0], particle_coord[2]);
	VectorNegate   (particle_coord[1], particle_coord[3]);
	
	qglEnable (GL_TEXTURE_2D);
	GL_TexEnv( GL_MODULATE );
	qglDepthMask   (false);
	GLSTATE_ENABLE_BLEND
	GL_ShadeModel (GL_SMOOTH);
	GLSTATE_DISABLE_ALPHATEST
	ParticleOverbright = false;

	RenderParticleTree(list);

	qglDepthRange (gldepthmin, gldepthmax);
	GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_TexEnv( GL_MODULATE );
	qglDepthMask (true);
	GLSTATE_DISABLE_BLEND
	qglColor4f   (1,1,1,1);
}

void R_DrawAllParticles (void)
{	
	vec3_t		up		= {vup[0]    * 0.75f, vup[1]    * 0.75f, vup[2]    * 0.75f};
	vec3_t		right	= {vright[0] * 0.75f, vright[1] * 0.75f, vright[2] * 0.75f};

	int		i;

	VectorAdd      (up, right, particle_coord[0]);
	VectorSubtract (right, up, particle_coord[1]);
	VectorNegate   (particle_coord[0], particle_coord[2]);
	VectorNegate   (particle_coord[1], particle_coord[3]);
	
	qglEnable (GL_TEXTURE_2D);
	GL_TexEnv( GL_MODULATE );
	qglDepthMask   (false);
	GLSTATE_ENABLE_BLEND
	GL_ShadeModel (GL_SMOOTH);
	GLSTATE_DISABLE_ALPHATEST
	ParticleOverbright = false;

	for ( i=0 ; i < r_newrefdef.num_particles ; i++)
		if (!(r_newrefdef.particles[i].flags&PART_DECAL))
			renderParticle(&r_newrefdef.particles[i]);

	qglDepthRange (gldepthmin, gldepthmax);
	GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_TexEnv( GL_MODULATE );
	qglDepthMask (true);
	GLSTATE_DISABLE_BLEND
	qglColor4f   (1,1,1,1);
}

/**********************************************************
				renderParticle
**********************************************************/

int texParticle (int type)
{
	image_t		*part_img;

	part_img = r_particletextures [type];

	return part_img->texnum;
}

void vectoanglerolled (vec3_t value1, float angleyaw, vec3_t angles)
{
	float	forward, yaw, pitch;

	yaw = (int) (atan2(value1[1], value1[0]) * 180 / M_PI);
	forward = sqrt (value1[0]*value1[0] + value1[1]*value1[1]);
	pitch = (int) (atan2(value1[2], forward) * 180 / M_PI);

	if (pitch < 0)
		pitch += 360;

	angles[PITCH] = -pitch;
	angles[YAW] =  yaw;
	angles[ROLL] = - angleyaw;
}

float AngleFind(float input)
{
	return 180.0/input;
}

void setBeamAngles (vec3_t start, vec3_t end, vec3_t up, vec3_t right)
{
	vec3_t move, delta;

	VectorSubtract(end, start, move);
	VectorNormalize(move);

	VectorCopy(move, up);
	VectorSubtract(r_newrefdef.vieworg, start, delta);
	CrossProduct(up, delta, right);
//	if(!VectorCompare(right, vec3_origin))
		VectorNormalize(right);
}

void getParticleLight (particle_t *p, vec3_t pos, float lighting, vec3_t shadelight)
{
	int j;
	float lightest = 0;

	if (!lighting)
	{
		VectorSet(shadelight, p->red, p->green, p->blue);
		return;
	}

	R_LightPoint (pos, shadelight);

	shadelight[0]= (lighting*shadelight[0]+(1-lighting)) * p->red;
	shadelight[1]= (lighting*shadelight[1]+(1-lighting)) * p->green;
	shadelight[2]= (lighting*shadelight[2]+(1-lighting)) * p->blue;

	//this cleans up the lighting
	{
		for (j=0;j<3;j++)
			if (shadelight[j]>lightest)
				lightest= shadelight[j];
		if (lightest>255)
			for (j=0;j<3;j++)
			{
				shadelight[j]*= 255/lightest;
				if (shadelight[j]>255)
					shadelight[j] = 255;
			}

		for (j=0;j<3;j++)
		{
			if (shadelight[j]<0)
				shadelight[j] = 0;
		}	
	}
}

//id move this somewhere less dirty, but im too damn lazy - psychospaz
#define DEPTHHACK_RANGE_SHORT	0.999f
#define DEPTHHACK_RANGE_MID		0.99f
#define DEPTHHACK_RANGE_LONG	0.975f

vec3_t ParticleVec[4];
vec3_t shadelight;

rscript_t *shaderParticle (int type)
{
	image_t		*part_img;
	part_img = r_particletextures[type];

	return part_img->script;
}

void renderParticleShader (particle_t *p, vec3_t origin, float size, qboolean translate)
{
	rscript_t *rs = NULL;
	float	txm,tym, alpha,s,t;
	rs_stage_t *stage;
	char scriptname [MAX_QPATH];
	vec3_t color;

	if (r_shaders->value)
		rs=shaderParticle(p->image);

	if (rs)
	{
		RS_ReadyScript(rs);

		stage=rs->stage;
		while (stage) 
		{
			if (stage->colormap.enabled)
				qglDisable (GL_TEXTURE_2D);
			else if (stage->anim_count)
				GL_Bind(RS_Animate(stage));
			else
				GL_Bind (stage->texture->texnum);

			if (stage->blendfunc.blend) 
				GL_BlendFunction(stage->blendfunc.source,stage->blendfunc.dest);
			else
				GL_BlendFunction (p->blendfunc_src, p->blendfunc_dst);

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
			alpha *= p->alpha;

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
				float red = 1, green = 1, blue = 1;

				red = stage->colormap.red/255.0;
				green = stage->colormap.green/255.0;
				blue = stage->colormap.blue/255.0;
				
				VectorSet(color, red,green,blue);
			}
			else if (stage->lightmap && p->flags&PART_SHADED)
				VectorSet(color, shadelight[0]/255.0, shadelight[1]/255.0, shadelight[2]/255.0);
			else
				VectorSet(color, p->red/255.0, p->green/255.0, p->blue/255.0);


			if (p->flags&PART_ALPHACOLOR)
				qglColor4f(color[0]*alpha,color[1]*alpha,color[2]*alpha, alpha);
			else
				qglColor4f(color[0],color[1],color[2], alpha);

			qglPushMatrix();
			{
				if (translate)
				{
					qglTranslatef( origin[0], origin[1], origin[2] );
					qglScalef(size, size, size );
				}
				if (p->decal)
				{
					qglEnable(GL_POLYGON_OFFSET_FILL); 
					qglPolygonOffset(-2, -1); 

					qglBegin (GL_TRIANGLE_FAN);
					{
						int i;
						float s, t;
						vec3_t point;
						decalpolys_t *decal = p->decal;

						for (i = 0; i < p->decal->numpolys; i++)
						{ 
							s = p->decal->coords[i][0];
							t = p->decal->coords[i][1];
							VectorCopy(decal->polys[i], point);
							
							RS_SetTexcoords2D (stage, &s, &t);
							qglTexCoord2f (s, t);
							qglVertex3fv  (point);
						}
					}
					qglEnd ();
					
					qglDisable(GL_POLYGON_OFFSET_FILL); 
				}
				else
				{
					qglBegin (GL_QUADS);
					{
						float s, t;

						s = 0;
						t = 1;
						RS_SetTexcoords2D (stage, &s, &t);
						qglTexCoord2f (s, t);
						qglVertex3fv  (ParticleVec[0]);
						s = 0;
						t = 0;
						RS_SetTexcoords2D (stage, &s, &t);
						qglTexCoord2f (s, t);
						qglVertex3fv  (ParticleVec[1]);
						s = 1;
						t = 0;
						RS_SetTexcoords2D (stage, &s, &t);
						qglTexCoord2f (s, t);
						qglVertex3fv  (ParticleVec[2]);
						s = 1;
						t = 1;
						RS_SetTexcoords2D (stage, &s, &t);
						qglTexCoord2f (s, t);
						qglVertex3fv  (ParticleVec[3]);
					}
					qglEnd ();
				}
			}
			qglPopMatrix ();

			if (stage->colormap.enabled)
				qglEnable (GL_TEXTURE_2D);

			stage=stage->next;
		}
	}
	else
	{
		qglPushMatrix();
		{
			if (translate)
			{
				qglTranslatef( origin[0], origin[1], origin[2] );
				qglScalef(size, size, size );
			}
			if (p->decal)
			{
				qglEnable(GL_POLYGON_OFFSET_FILL); 
				qglPolygonOffset(-2, -1); 
 
				qglBegin (GL_TRIANGLE_FAN);
				{
					int i;
					for (i = 0; i < p->decal->numpolys; i++)
					{ 
						qglTexCoord2f (p->decal->coords[i][0], p->decal->coords[i][1]);
						qglVertex3fv  (p->decal->polys[i]);
					}
				}
				qglEnd ();
				
				qglDisable(GL_POLYGON_OFFSET_FILL);
			}
			else
			{
				qglBegin (GL_QUADS);
				{
					qglTexCoord2f (0, 1);
					qglVertex3fv  (ParticleVec[0]);
					qglTexCoord2f (0, 0);
					qglVertex3fv  (ParticleVec[1]);
					qglTexCoord2f (1, 0); 
					qglVertex3fv  (ParticleVec[2]);
					qglTexCoord2f (1, 1);
					qglVertex3fv  (ParticleVec[3]);
				}
				qglEnd ();
			}
		}
		qglPopMatrix ();
	}
}

void renderDecal (particle_t *p)
{
	float size, alpha;
	vec3_t angl_coord[4];
	vec3_t ang_up, ang_right, ang_forward, color;

	size = (p->size>0.1) ? p->size : 0.1;
	alpha = p->alpha;

	GL_BlendFunction (p->blendfunc_src, p->blendfunc_dst);

	GL_Bind(texParticle(p->image));

	if (p->flags&PART_SHADED)
	{
		getParticleLight (p, p->origin, gl_particle_lighting->value, shadelight);
		VectorSet(color, shadelight[0]/255.0, shadelight[1]/255.0, shadelight[2]/255.0);
	}
	else
	{
		VectorSet(shadelight, p->red, p->green, p->blue);
		VectorSet(color, p->red/255.0, p->green/255.0, p->blue/255.0);
	}

	if (p->flags&PART_ALPHACOLOR)
		qglColor4f(color[0]*alpha,color[1]*alpha,color[2]*alpha, alpha);
	else
		qglColor4f(color[0],color[1],color[2], alpha);

	if (!p->decal)
	{
		AngleVectors(p->angle, ang_forward, ang_right, ang_up); 

		VectorScale (ang_right, 0.75f, ang_right);
		VectorScale (ang_up, 0.75f, ang_up);

		VectorAdd      (ang_up, ang_right, angl_coord[0]);
		VectorSubtract (ang_right, ang_up, angl_coord[1]);
		VectorNegate   (angl_coord[0], angl_coord[2]);
		VectorNegate   (angl_coord[1], angl_coord[3]);
		
		VectorMA(p->origin, size, angl_coord[0], ParticleVec[0]);
		VectorMA(p->origin, size, angl_coord[1], ParticleVec[1]);
		VectorMA(p->origin, size, angl_coord[2], ParticleVec[2]);
		VectorMA(p->origin, size, angl_coord[3], ParticleVec[3]);
	}

	renderParticleShader(p, NULL, 0, false);
}

void renderParticle (particle_t *p)
{
	float		size, lighting = gl_particle_lighting->value;
	vec3_t		up		= {vup[0]    * 0.75f, vup[1]    * 0.75f, vup[2]    * 0.75f};
	vec3_t		right	= {vright[0] * 0.75f, vright[1] * 0.75f, vright[2] * 0.75f};
	vec3_t		shadelight;
	float		alpha;
	vec3_t		coord[4], color;

	VectorCopy(particle_coord[0], coord[0]);
	VectorCopy(particle_coord[1], coord[1]);
	VectorCopy(particle_coord[2], coord[2]);
	VectorCopy(particle_coord[3], coord[3]);

	GL_BlendFunction (p->blendfunc_src, p->blendfunc_dst);

	size = (p->size>0.1) ? p->size : 0.1;
	alpha = p->alpha;

	GL_Bind(texParticle(p->image));

	if (p->flags&PART_DEPTHHACK_SHORT) //nice little poly-peeking - psychospaz
		qglDepthRange (gldepthmin, gldepthmin + DEPTHHACK_RANGE_SHORT*(gldepthmax-gldepthmin));
	if (p->flags&PART_DEPTHHACK_MID)
		qglDepthRange (gldepthmin, gldepthmin + DEPTHHACK_RANGE_MID*(gldepthmax-gldepthmin));
	if (p->flags&PART_DEPTHHACK_LONG)
		qglDepthRange (gldepthmin, gldepthmin + DEPTHHACK_RANGE_LONG*(gldepthmax-gldepthmin));

	if (p->flags&PART_OVERBRIGHT)
		SetParticleOverbright(true);

	if (p->flags&PART_SHADED)
	{
		getParticleLight (p, p->origin, lighting, shadelight);
		VectorSet(color, shadelight[0]/255.0, shadelight[1]/255.0, shadelight[2]/255.0);
	}
	else
	{
		VectorSet(shadelight, p->red, p->green, p->blue);
		VectorSet(color, p->red/255.0, p->green/255.0, p->blue/255.0);
	}

	if (p->flags&PART_ALPHACOLOR)
		qglColor4f(color[0]*alpha,color[1]*alpha,color[2]*alpha, alpha);
	else
		qglColor4f(color[0],color[1],color[2], alpha);
	
	if (p->flags&PART_BEAM) //given start and end positions, will have fun here :)
	{	//p->angle = start
		//p->origin= end
		vec3_t	angl_coord[4];
		vec3_t	ang_up, ang_right;

		setBeamAngles(p->angle, p->origin, ang_up, ang_right);
		VectorScale(ang_right, size, ang_right);

		VectorAdd(p->origin, ang_right, ParticleVec[0]);
		VectorAdd(p->angle, ang_right, ParticleVec[1]);
		VectorSubtract(p->angle, ang_right, ParticleVec[2]);
		VectorSubtract(p->origin, ang_right, ParticleVec[3]);

		renderParticleShader(p, NULL, 0, false);
	}
	else if (p->flags&PART_LIGHTNING) //given start and end positions, will have fun here :)
	{	//p->angle = start
		//p->origin= end
		int k = 0;
		float	len, dec, angdot;
		vec3_t	move, lastvec, thisvec, tempvec;
		vec3_t	angl_coord[4], old_coord[2];
		vec3_t	ang_up, ang_right, last_right, abs_up, abs_right;
		float	width, scale_up, scale_right;

		scale_up = scale_right = 0;
		dec = size*2.0;
		width = size;

		VectorSubtract(p->origin, p->angle, move);
		len = VectorNormalize(move);
	
		setBeamAngles(p->angle, p->origin, abs_up, abs_right);
		VectorScale(move, dec, move);
		VectorCopy(p->angle, thisvec);
		VectorSubtract(thisvec, move, lastvec);
		VectorCopy(thisvec, tempvec);
		len+=dec;

		setBeamAngles(lastvec, thisvec, ang_up, ang_right);
		VectorScale (ang_right, width, ang_right);

		qglDisable (GL_CULL_FACE);

		while (len>dec)
		{
			VectorCopy(ang_right, last_right);

			setBeamAngles(lastvec, thisvec, ang_up, ang_right);
			VectorScale (ang_right, width, ang_right);

			angdot = DotProduct(ang_right, last_right);
			if (angdot<0)
			{
				VectorCopy(old_coord[0], ParticleVec[2]);
				VectorCopy(old_coord[1], ParticleVec[1]);
			}
			else
			{
				VectorCopy(old_coord[0], ParticleVec[1]);
				VectorCopy(old_coord[1], ParticleVec[2]);
			}

			VectorAdd(thisvec, ang_right, ParticleVec[0]);
			VectorSubtract(thisvec, ang_right, ParticleVec[3]);

			VectorCopy(ParticleVec[0], old_coord[0]);
			VectorCopy(ParticleVec[3], old_coord[1]);

			if (k>0)
				renderParticleShader(p, NULL, 0, false);
				
			k++;

			len-=dec;

			VectorCopy(thisvec, lastvec);

			//now modify stuff

			scale_up += crandom() * size;
			scale_right -= crandom() * size;
			if (scale_right > size) scale_right = size;
			if (scale_right < -size) scale_right = -size;
			if (scale_up > size) scale_up = size;
			if (scale_up < -size) scale_up = -size;


			VectorAdd(tempvec, move, tempvec);
			thisvec[0] = tempvec[0] + abs_up[0]*scale_up + abs_right[0]*scale_right;
			thisvec[1] = tempvec[1] + abs_up[1]*scale_up + abs_right[1]*scale_right;
			thisvec[2] = tempvec[2] + abs_up[2]*scale_up + abs_right[2]*scale_right;
		}

		//one more time
		if (len>0)
		{
			VectorCopy(p->origin, thisvec);
			VectorCopy(ang_right, last_right);

			setBeamAngles(lastvec, thisvec, ang_up, ang_right);
			VectorScale (ang_right, width, ang_right);

			angdot = DotProduct(ang_right, last_right);
			if (angdot<0)
			{
				VectorCopy(old_coord[0], ParticleVec[2]);
				VectorCopy(old_coord[1], ParticleVec[1]);
			}
			else
			{
				VectorCopy(old_coord[0], ParticleVec[1]);
				VectorCopy(old_coord[1], ParticleVec[2]);
			}

			VectorAdd(thisvec, ang_right, ParticleVec[0]);
			VectorSubtract(thisvec, ang_right, ParticleVec[3]);

			renderParticleShader(p, NULL, 0, false);
		}
		qglEnable (GL_CULL_FACE);
	}
	else if (p->flags&PART_DIRECTION) //streched out in direction...tracers etc...
		//never dissapears because of angle like other engines :D
	{
		vec3_t angl_coord[4];
		vec3_t ang_up, ang_right, vdelta;

		VectorAdd(p->angle, p->origin, vdelta); 
		setBeamAngles(vdelta, p->origin, ang_up, ang_right);

		VectorScale (ang_right, 0.75f, ang_right);
		VectorScale (ang_up, 0.75f * VectorLength(p->angle), ang_up);

		VectorAdd      (ang_up, ang_right, ParticleVec[0]);
		VectorSubtract (ang_right, ang_up, ParticleVec[1]);
		VectorNegate   (ParticleVec[0], ParticleVec[2]);
		VectorNegate   (ParticleVec[1], ParticleVec[3]);

		renderParticleShader(p, p->origin, size, true);
	}
	else if (p->flags&PART_ANGLED) //facing direction... (decal wannabes)
	{
		vec3_t angl_coord[4];
		vec3_t ang_up, ang_right, ang_forward;

		AngleVectors(p->angle, ang_forward, ang_right, ang_up); 

		VectorScale (ang_right, 0.75f, ang_right);
		VectorScale (ang_up, 0.75f, ang_up);

		VectorAdd      (ang_up, ang_right, ParticleVec[0]);
		VectorSubtract (ang_right, ang_up, ParticleVec[1]);
		VectorNegate   (ParticleVec[0], ParticleVec[2]);
		VectorNegate   (ParticleVec[1], ParticleVec[3]);

		qglDisable (GL_CULL_FACE);
		
		renderParticleShader(p, p->origin, size, true);

		qglEnable (GL_CULL_FACE);

	}
	else //normal sprites
	{
		if (p->angle[2]) //if we have roll, we do calcs
		{
			vec3_t angl_coord[4];
			vec3_t ang_up, ang_right, ang_forward;

			VectorSubtract(p->origin, r_newrefdef.vieworg, angl_coord[0]);

			vectoanglerolled(angl_coord[0], p->angle[2], angl_coord[1]);
			AngleVectors(angl_coord[1], ang_forward, ang_right, ang_up); 

			VectorScale (ang_forward, 0.75f, ang_forward);
			VectorScale (ang_right, 0.75f, ang_right);
			VectorScale (ang_up, 0.75f, ang_up);

			VectorAdd      (ang_up, ang_right, ParticleVec[0]);
			VectorSubtract (ang_right, ang_up, ParticleVec[1]);
			VectorNegate   (ParticleVec[0], ParticleVec[2]);
			VectorNegate   (ParticleVec[1], ParticleVec[3]);
		}
		else
		{
			VectorCopy(coord[0], ParticleVec[0]);
			VectorCopy(coord[1], ParticleVec[1]);
			VectorCopy(coord[2], ParticleVec[2]);
			VectorCopy(coord[3], ParticleVec[3]);
		}				
		renderParticleShader(p, p->origin, size, true);
	}

	if (p->flags&PART_OVERBRIGHT)
		SetParticleOverbright(false);

	if (p->flags&PART_DEPTHHACK)
		qglDepthRange (gldepthmin, gldepthmax);
}


/*
============
R_PolyBlend
============
*/
void R_PolyBlend (void)
{
	if (!gl_polyblend->value)
		return;
	if (!v_blend[3])
		return;

	GLSTATE_DISABLE_ALPHATEST
	GLSTATE_ENABLE_BLEND
	qglDisable (GL_DEPTH_TEST);
	qglDisable (GL_TEXTURE_2D);

    qglLoadIdentity ();

	// FIXME: get rid of these
    qglRotatef (-90,  1, 0, 0);	    // put Z going up
    qglRotatef (90,  0, 0, 1);	    // put Z going up

	qglColor4fv (v_blend);

	qglBegin (GL_QUADS);

	qglVertex3f (10, 100, 100);
	qglVertex3f (10, -100, 100);
	qglVertex3f (10, -100, -100);
	qglVertex3f (10, 100, -100);
	qglEnd ();

	GLSTATE_DISABLE_BLEND
	qglEnable (GL_TEXTURE_2D);
	GLSTATE_ENABLE_ALPHATEST

	qglColor4f(1,1,1,1);
}

void R_StencilBlend (void)
{

	GLSTATE_DISABLE_ALPHATEST
	GLSTATE_ENABLE_BLEND
	qglDisable (GL_DEPTH_TEST);
	qglDisable (GL_TEXTURE_2D);

    qglLoadIdentity ();

	// FIXME: get rid of these
    qglRotatef (-90,  1, 0, 0);	    // put Z going up
    qglRotatef (90,  0, 0, 1);	    // put Z going up

	qglColor4f(0,0,0,0.3);
	qglStencilFunc( GL_NOTEQUAL, 0, 0xFFFFFFFFL );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	qglEnable(GL_STENCIL_TEST);

	qglBegin (GL_QUADS);
	qglVertex3f (10, 100, 100);
	qglVertex3f (10, -100, 100);
	qglVertex3f (10, -100, -100);
	qglVertex3f (10, 100, -100);
	qglEnd ();

	qglDisable(GL_STENCIL_TEST);


	GLSTATE_DISABLE_BLEND
	qglEnable (GL_TEXTURE_2D);
	GLSTATE_ENABLE_ALPHATEST

	qglColor4f(1,1,1,1);
}

//=======================================================================

int SignbitsForPlane (cplane_t *out)
{
	int	bits, j;

	// for fast box on planeside test

	bits = 0;
	for (j=0 ; j<3 ; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1<<j;
	}
	return bits;
}


void R_SetFrustum (void)
{
	int		i;

#if 0
	/*
	** this code is wrong, since it presume a 90 degree FOV both in the
	** horizontal and vertical plane
	*/
	// front side is visible
	VectorAdd (vpn, vright, frustum[0].normal);
	VectorSubtract (vpn, vright, frustum[1].normal);
	VectorAdd (vpn, vup, frustum[2].normal);
	VectorSubtract (vpn, vup, frustum[3].normal);

	// we theoretically don't need to normalize these vectors, but I do it
	// anyway so that debugging is a little easier
	VectorNormalize( frustum[0].normal );
	VectorNormalize( frustum[1].normal );
	VectorNormalize( frustum[2].normal );
	VectorNormalize( frustum[3].normal );
#else
	// rotate VPN right by FOV_X/2 degrees
	RotatePointAroundVector( frustum[0].normal, vup, vpn, -(90-r_newrefdef.fov_x / 2 ) );
	// rotate VPN left by FOV_X/2 degrees
	RotatePointAroundVector( frustum[1].normal, vup, vpn, 90-r_newrefdef.fov_x / 2 );
	// rotate VPN up by FOV_X/2 degrees
	RotatePointAroundVector( frustum[2].normal, vright, vpn, 90-r_newrefdef.fov_y / 2 );
	// rotate VPN down by FOV_X/2 degrees
	RotatePointAroundVector( frustum[3].normal, vright, vpn, -( 90 - r_newrefdef.fov_y / 2 ) );
#endif

	for (i=0 ; i<4 ; i++)
	{
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_origin, frustum[i].normal);
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}

//=======================================================================

/*
===============
R_SetupFrame
===============
*/
void R_SetupFrame (void)
{
	int i;
	mleaf_t	*leaf;

	r_framecount++;

// build the transformation matrix for the given view angles
	VectorCopy (r_newrefdef.vieworg, r_origin);

	AngleVectors (r_newrefdef.viewangles, vpn, vright, vup);

// current viewcluster
	if ( !( r_newrefdef.rdflags & RDF_NOWORLDMODEL ) )
	{
		r_oldviewcluster = r_viewcluster;
		r_oldviewcluster2 = r_viewcluster2;
		leaf = Mod_PointInLeaf (r_origin, r_worldmodel);
		r_viewcluster = r_viewcluster2 = leaf->cluster;

		// check above and below so crossing solid water doesn't draw wrong
		if (!leaf->contents)
		{	// look down a bit
			vec3_t	temp;

			VectorCopy (r_origin, temp);
			temp[2] -= 16;
			leaf = Mod_PointInLeaf (temp, r_worldmodel);
			if ( !(leaf->contents & CONTENTS_SOLID) &&
				(leaf->cluster != r_viewcluster2) )
				r_viewcluster2 = leaf->cluster;
		}
		else
		{	// look up a bit
			vec3_t	temp;

			VectorCopy (r_origin, temp);
			temp[2] += 16;
			leaf = Mod_PointInLeaf (temp, r_worldmodel);
			if ( !(leaf->contents & CONTENTS_SOLID) &&
				(leaf->cluster != r_viewcluster2) )
				r_viewcluster2 = leaf->cluster;
		}
	}

	for (i=0 ; i<4 ; i++)
		v_blend[i] = r_newrefdef.blend[i];

	c_brush_polys = 0;
	c_alias_polys = 0;

	// clear out the portion of the screen that the NOWORLDMODEL defines
/*	if ( r_newrefdef.rdflags & RDF_NOWORLDMODEL )
	{
		qglEnable( GL_SCISSOR_TEST );
		qglClearColor( 0.3, 0.3, 0.3, 1 );
		qglScissor( r_newrefdef.x, vid.height - r_newrefdef.height - r_newrefdef.y, r_newrefdef.width, r_newrefdef.height );
		qglClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		qglClearColor( 1, 0, 0.5, 0.5 );
		qglDisable( GL_SCISSOR_TEST );
	}*/
}


void MYgluPerspective( GLdouble fovy, GLdouble aspect,
		     GLdouble zNear, GLdouble zFar )
{
   GLdouble xmin, xmax, ymin, ymax;

   ymax = zNear * tan( fovy * M_PI / 360.0 );
   ymin = -ymax;

   xmin = ymin * aspect;
   xmax = ymax * aspect;

   xmin += -( 2 * gl_state.camera_separation ) / zNear;
   xmax += -( 2 * gl_state.camera_separation ) / zNear;

   qglFrustum( xmin, xmax, ymin, ymax, zNear, zFar );
}


/*
=============
R_SetupGL
=============
*/
void R_SetupGL (void)
{
	static GLdouble farz; // DMP skybox size change
	GLdouble boxsize;  // DMP skybox size change

	float	screenaspect;
//	float	yfov;
	int		x, x2, y2, y, w, h;

	//
	// set up viewport
	//
	x = floor(r_newrefdef.x * vid.width / vid.width);
	x2 = ceil((r_newrefdef.x + r_newrefdef.width) * vid.width / vid.width);
	y = floor(vid.height - r_newrefdef.y * vid.height / vid.height);
	y2 = ceil(vid.height - (r_newrefdef.y + r_newrefdef.height) * vid.height / vid.height);

	w = x2 - x;
	h = y - y2;

	qglViewport (x, y2, w, h);

	if (skydistance->modified)
	{
		skydistance->modified = false;
		boxsize = skydistance->value;
		boxsize -= 252 * ceil(boxsize / 2300);
		farz = 1.0;
		while (farz < boxsize)  // DMP: make this value a power-of-2
		{
			farz *= 2.0;
			if (farz >= 65536.0)  // DMP: don't make it larger than this
				break;
  		}
		farz *= 2.0;	// DMP: double since boxsize is distance from camera to
				// edge of skybox - not total size of skybox
		ri.Con_Printf(PRINT_DEVELOPER, "farz now set to %g\n", farz);
	}


	//
	// set up projection matrix
	//
    screenaspect = (float)r_newrefdef.width/r_newrefdef.height;
//	yfov = 2*atan((float)r_newrefdef.height/r_newrefdef.width)*180/M_PI;
	qglMatrixMode(GL_PROJECTION);
    qglLoadIdentity ();

//    MYgluPerspective (r_newrefdef.fov_y,  screenaspect,  4,  4096);
	MYgluPerspective (r_newrefdef.fov_y,  screenaspect, 4, farz); // DMP skybox


	qglCullFace(GL_FRONT);

	qglMatrixMode(GL_MODELVIEW);
    qglLoadIdentity ();

    qglRotatef (-90,  1, 0, 0);	    // put Z going up
    qglRotatef (90,  0, 0, 1);	    // put Z going up
    qglRotatef (-r_newrefdef.viewangles[2],  1, 0, 0);
    qglRotatef (-r_newrefdef.viewangles[0],  0, 1, 0);
    qglRotatef (-r_newrefdef.viewangles[1],  0, 0, 1);
    qglTranslatef (-r_newrefdef.vieworg[0],  -r_newrefdef.vieworg[1],  -r_newrefdef.vieworg[2]);

//	if ( gl_state.camera_separation != 0 && gl_state.stereo_enabled )
//		qglTranslatef ( gl_state.camera_separation, 0, 0 );

	qglGetFloatv (GL_MODELVIEW_MATRIX, r_world_matrix);

	qglGetIntegerv (GL_VIEWPORT, r_viewport);

	//
	// set drawing parms
	//
	if (gl_cull->value)
		qglEnable(GL_CULL_FACE);
	else
		qglDisable(GL_CULL_FACE);

	GLSTATE_DISABLE_BLEND
	GLSTATE_DISABLE_ALPHATEST
	qglEnable(GL_DEPTH_TEST);
}

/*
=============
R_Clear
=============
*/
void R_Clear (void)
{
	if (gl_ztrick->value)
	{
		static int trickframe;

		if (gl_clear->value)
			qglClear (GL_COLOR_BUFFER_BIT);

		trickframe++;
		if (trickframe & 1)
		{
			gldepthmin = 0;
			gldepthmax = 0.49999;
			qglDepthFunc (GL_LEQUAL);
		}
		else
		{
			gldepthmin = 1;
			gldepthmax = 0.5;
			qglDepthFunc (GL_GEQUAL);
		}
	}
	else
	{
		if (gl_clear->value)
			qglClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		else
			qglClear (GL_DEPTH_BUFFER_BIT);
		gldepthmin = 0;
		gldepthmax = 1;
		qglDepthFunc (GL_LEQUAL);
	}

	if (have_stencil)
	{
		qglClearStencil(0);
		qglClear(GL_STENCIL_BUFFER_BIT);
	}

	//this is for spheremapping...
	qglTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
	qglTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);

	qglDepthRange (gldepthmin, gldepthmax);

}

void R_ShadowBlend()
{
	if (!gl_shadows->value)
		return;

    qglLoadIdentity ();

	// FIXME: get rid of these
    qglRotatef (-90,  1, 0, 0);	    // put Z going up
    qglRotatef (90,  0, 0, 1);	    // put Z going up

	qglColor4f (0,0,0,0.4f);

	GLSTATE_DISABLE_ALPHATEST
	GLSTATE_ENABLE_BLEND
	qglDisable (GL_DEPTH_TEST);
	qglDisable (GL_TEXTURE_2D);

	qglEnable(GL_STENCIL_TEST);
	qglStencilFunc( GL_NOTEQUAL, 0, 0xFF);
	qglStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

	qglBegin (GL_QUADS);

	qglVertex3f (10, 100, 100);
	qglVertex3f (10, -100, 100);
	qglVertex3f (10, -100, -100);
	qglVertex3f (10, 100, -100);
	qglEnd ();

	GLSTATE_DISABLE_BLEND
	qglEnable (GL_TEXTURE_2D);
	GLSTATE_ENABLE_ALPHATEST
	qglDisable(GL_STENCIL_TEST);

	qglColor4f(1,1,1,1);
}

void R_Flash( void )
{
	R_PolyBlend ();
	R_ShadowBlend ();
}
/*
================
R_RenderView

r_newrefdef must be set before the first call
================
*/

void R_DrawSpecialSurfaces(void);
void R_RenderMirrorView (refdef_t *fd)
{
	if (r_norefresh->value)
		return;
	r_newrefdef = *fd;
	if (!r_worldmodel && !( r_newrefdef.rdflags & RDF_NOWORLDMODEL ) )
		ri.Sys_Error (ERR_DROP, "R_RenderView: NULL worldmodel");

	if (r_speeds->value)
	{
		c_brush_polys = 0;
		c_alias_polys = 0;
	}

	R_PushDlights ();
	if (gl_finish->value)
		qglFinish ();

	R_SetupFrame ();
	R_SetFrustum ();
	R_SetupGL ();

    R_MarkLeaves (); // done here so we know if we're in water
    if (gl_wire->value) qglPolygonMode (GL_FRONT, GL_LINE, GL_FILL); 
		glLineWidth (5.5f); // used to be 5.5f
	    glEnable (GL_LINE_SMOOTH);
    R_DrawWorld ();
	
	GLSTATE_DISABLE_ALPHATEST

	R_RenderDlights ();

	//never sort - could get too messy!

	R_DrawSpecialSurfaces();
	R_DrawAllDecals();
	R_DrawAllParticles();
	R_DrawAllEntities(false);
	R_DrawAlphaSurfaces (false);

	R_Flash();
}

#define timeoffset 0.1

int lastcapture = -99999;
void resetDynamicImage ()
{
	lastcapture = -99999;
}
void CaptureScreenToImage ()
{
	if (!r_dynamicimage && !r_shaders->value)
		return;

	if (r_newrefdef.time < lastcapture+rs_dynamic_time->value)
		return;

	lastcapture = r_newrefdef.time;

	GL_Bind(r_dynamicimage->texnum);

	qglCopyTexImage2D
	(
		GL_TEXTURE_2D, 
		0, 
		GL_RGB, 
		vid.width*0.5-r_dynamicimage->upload_width*0.5, 
		vid.height*0.5-r_dynamicimage->upload_height*0.5, 
		r_dynamicimage->upload_width, r_dynamicimage->upload_height, 
		0
	);

	GL_Bind(r_dynamicimage->texnum);
}


qboolean checkSurfaceTrace (msurface_t *surf, vec3_t end, vec3_t start)
{
	float front, back, side, dist;
	vec3_t normal;
	cplane_t *plane;

	plane = surf->plane;

	if (!plane)
		return false;

	dist = plane->dist;
	VectorCopy(plane->normal, normal);

	front = DotProduct (start, normal) - dist;
	back = DotProduct (end, normal) - dist;
	side = front < 0;

	if ( (back < 0) == side)
		return false;

	return true;
}

qboolean TracePoints (vec3_t start, vec3_t end, void *surface)
{
	int i;
	msurface_t	*surf = r_worldmodel->surfaces;
	for (i=0; i<r_worldmodel->numsurfaces; i++, surf++)
	{
		if (surf == surface)
			continue;
		if (surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66)) 
			continue;
		if (checkSurfaceTrace(surf, start, end))
			return true;
	}
	return false;
}


qboolean checkElementSurfaceScan (msurface_t *surf, vec3_t start)
{
	float front, back, side, dist;
	vec3_t end, normal;
	cplane_t *plane;

	plane = surf->plane;

	if (!plane)
		return false;

	dist = plane->dist;
	VectorCopy(plane->normal, normal);
	VectorCopy(r_newrefdef.vieworg, end);

	front = DotProduct (start, normal) - dist;
	back = DotProduct (end, normal) - dist;
	side = front < 0;

	if ( (back < 0) == side)
		return false;

	return true;
}

sortedelement_t *listswap[2];
void ElementChecker (sortedelement_t *element, msurface_t *surf)
{
	if (!element)
		return;

	ElementChecker(element->right, surf);
	ElementChecker(element->left, surf);
	element->left = element->right = NULL;

	if (checkElementSurfaceScan(surf, element->org))
	{
		if (!listswap[0])
			listswap[0] = element;
		else
			ElementAddNode(listswap[0], element);
	}
	else
	{
		if (!listswap[1])
			listswap[1] = element;
		else
			ElementAddNode(listswap[1], element);
	}
}

void surf_ElementList(msurface_t *surf, qboolean ents)
{
	sortedelement_t *temp, *element, *last = NULL, *found = NULL;

	listswap[0] = listswap[1] = NULL;

	if (ents)
		ElementChecker(ents_prerender, surf);
	else
		ElementChecker(parts_prerender, surf);

	if (ents)
	{
		ents_prerender = listswap[1];
		RenderEntTree(listswap[0]);
	}
	else
	{
		parts_prerender = listswap[1];
		R_DrawParticles(listswap[0]);
	}
}

void R_DrawLastElements (void)
{
	if (parts_prerender)
		R_DrawParticles(parts_prerender);
	if (ents_prerender)
		RenderEntTree(ents_prerender);
}

void R_RenderView (refdef_t *fd) // hello qmb
{

	if (r_norefresh->value)
		return;

	r_newrefdef = *fd;

	if (!r_worldmodel && !( r_newrefdef.rdflags & RDF_NOWORLDMODEL ) )
		ri.Sys_Error (ERR_DROP, "R_RenderView: NULL worldmodel");

	if (r_speeds->value)
	{
		c_brush_polys = 0;
		c_alias_polys = 0;
	}

	R_PushDlights ();

	if (gl_finish->value)
		qglFinish ();

	R_SetupFrame ();

	R_SetFrustum ();

	R_SetupGL ();

	R_MarkLeaves ();	// done here so we know if we're in water

	R_DrawWorld ();


	if (r_newrefdef.rdflags & RDF_NOWORLDMODEL) //options menu
	{
			R_DrawAllDecals();
		R_DrawAllEntities(false);
		R_DrawAllParticles();
	}
	else
	{
		R_DrawSpecialSurfaces();

		GLSTATE_DISABLE_ALPHATEST

		R_RenderDlights ();

		//spaz -> my nice trees
		if (gl_transrendersort->value)
		{
			GL_BuildParticleList();
			R_DrawSolidEntities();
	//		R_DrawAllEntityShadows();
			R_DrawDecals ();

			if (gl_transrendersort->value==1)
			{
				R_DrawLastElements();
				R_DrawAlphaSurfaces (false);
			}
			else
			{
				R_DrawAlphaSurfaces (true);
				R_DrawLastElements();
			}
		}
		//nonsorted routines
		else
		{

			R_DrawAllDecals();

			R_DrawAllEntities(true);
	//		R_DrawAllEntityShadows();

			R_DrawAllParticles();
			R_DrawAlphaSurfaces (false);
		}

		CaptureScreenToImage();

		//always draw vwep last...
		R_DrawEntitiesOnList(ents_viewweaps);
		R_DrawEntitiesOnList(ents_viewweaps_trans);

		R_DrawAllEntityShadows();
	#ifdef LIGHT_BLOOMS
		R_BloomBlend (fd);//BLOOMS not sure about this one arg
	#endif
		R_Flash();

		if (r_speeds->value)
		{
			ri.Con_Printf (PRINT_ALL, "%4i wpoly %4i epoly %i tex %i lmaps\n",
				c_brush_polys, 
				c_alias_polys, 
				c_visible_textures, 
				c_visible_lightmaps); 
		}
	}	
}


void	R_SetGL2D (void)
{
	// set 2D virtual screen size
	qglViewport (0,0, vid.width, vid.height);
	qglMatrixMode(GL_PROJECTION);
    qglLoadIdentity ();
	qglOrtho  (0, vid.width, vid.height, 0, -99999, 99999);
	qglMatrixMode(GL_MODELVIEW);
    qglLoadIdentity ();
	qglDisable (GL_DEPTH_TEST);
	qglDisable (GL_CULL_FACE);
	GLSTATE_DISABLE_BLEND
	GLSTATE_ENABLE_ALPHATEST
	GLSTATE_DISABLE_TEXGEN
	qglColor4f (1,1,1,1);
	GL_BlendFunction(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void GL_DrawColoredStereoLinePair( float r, float g, float b, float y )
{
	qglColor3f( r, g, b );
	qglVertex2f( 0, y );
	qglVertex2f( vid.width, y );
	qglColor3f( 0, 0, 0 );
	qglVertex2f( 0, y + 1 );
	qglVertex2f( vid.width, y + 1 );
}

static void GL_DrawStereoPattern( void )
{
	int i;

	if ( !( gl_config.renderer & GL_RENDERER_INTERGRAPH ) )
		return;

	if ( !gl_state.stereo_enabled )
		return;

	R_SetGL2D();

	qglDrawBuffer( GL_BACK_LEFT );

	for ( i = 0; i < 20; i++ )
	{
		qglBegin( GL_LINES );
			GL_DrawColoredStereoLinePair( 1, 0, 0, 0 );
			GL_DrawColoredStereoLinePair( 1, 0, 0, 2 );
			GL_DrawColoredStereoLinePair( 1, 0, 0, 4 );
			GL_DrawColoredStereoLinePair( 1, 0, 0, 6 );
			GL_DrawColoredStereoLinePair( 0, 1, 0, 8 );
			GL_DrawColoredStereoLinePair( 1, 1, 0, 10);
			GL_DrawColoredStereoLinePair( 1, 1, 0, 12);
			GL_DrawColoredStereoLinePair( 0, 1, 0, 14);
		qglEnd();
		
		GLimp_EndFrame();
	}
}


/*
====================
R_SetLightLevel

====================
*/
void R_SetLightLevel (void)
{
	vec3_t		shadelight;

	if (r_newrefdef.rdflags & RDF_NOWORLDMODEL)
		return;

	// save off light value for server to look at (BIG HACK!)

	R_LightPoint (r_newrefdef.vieworg, shadelight);

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

/*
@@@@@@@@@@@@@@@@@@@@@
blackscreen

@@@@@@@@@@@@@@@@@@@@@
*/
void Draw_BlackScreen (void)
{
GLSTATE_DISABLE_ALPHATEST

GLSTATE_ENABLE_BLEND

qglDisable (GL_TEXTURE_2D);

qglColor4f (0, 0, 0, 1); // here you can set the color 0,0,0 is just black , white looks nice too.

 

VA_SetElem2(vert_array[0],0,0);

VA_SetElem2(vert_array[1],vid.width, 0);

VA_SetElem2(vert_array[2],vid.width, vid.height);

VA_SetElem2(vert_array[3],0, vid.height);

qglDrawArrays (GL_QUADS, 0, 4);

 

qglColor4f (1,1,1,1);

qglEnable (GL_TEXTURE_2D);

GLSTATE_DISABLE_BLEND

GLSTATE_ENABLE_ALPHATEST 
} 
/*
@@@@@@@@@@@@@@@@@@@@@
R_RenderFrame

@@@@@@@@@@@@@@@@@@@@@
*/
void R_RenderFrame (refdef_t *fd)
{
	if (gl_wire->value) Draw_BlackScreen ();//SEB
	R_RenderView( fd );
	R_SetLightLevel ();
	R_SetGL2D ();
}


void R_Register( void )
{
	r_stainmap = ri.Cvar_Get ("r_stainmap", "1", CVAR_ARCHIVE);

	con_font = ri.Cvar_Get ("con_font", "default", CVAR_ARCHIVE);
	con_font_size = ri.Cvar_Get ("con_font_size", "8", CVAR_ARCHIVE);

	r_dlights_normal = ri.Cvar_Get("r_dlights_normal", "1", CVAR_ARCHIVE);

	skydistance = ri.Cvar_Get("r_skydistance", "10000", CVAR_ARCHIVE); // DMP - skybox size change
	gl_wire = ri.Cvar_Get ("gl_wire", "0", 0); //SEB
	gl_transrendersort = ri.Cvar_Get ("gl_transrendersort", "1", CVAR_ARCHIVE );
	gl_particle_lighting = ri.Cvar_Get ("gl_particle_lighting", "0.75", CVAR_ARCHIVE );
	gl_particle_min = ri.Cvar_Get ("gl_particle_min", "0", CVAR_ARCHIVE );
	gl_particle_max = ri.Cvar_Get ("gl_particle_max", "0", CVAR_ARCHIVE );
	gl_surftrans_light = ri.Cvar_Get ("gl_surftrans_light", "1", CVAR_ARCHIVE );

	gl_stencil = ri.Cvar_Get ("gl_stencil", "1", CVAR_ARCHIVE );

	gl_screenshot_quality = ri.Cvar_Get ("gl_screenshot_quality", "85", CVAR_ARCHIVE );

	r_lefthand = ri.Cvar_Get( "hand", "0", CVAR_USERINFO | CVAR_ARCHIVE );
	r_norefresh = ri.Cvar_Get ("r_norefresh", "0", 0);
	r_fullbright = ri.Cvar_Get ("r_fullbright", "0", 0);
	r_drawentities = ri.Cvar_Get ("r_drawentities", "1", 0);
	r_drawworld = ri.Cvar_Get ("r_drawworld", "1", 0);
	r_novis = ri.Cvar_Get ("r_novis", "0", 0);
	r_nocull = ri.Cvar_Get ("r_nocull", "0", 0);
	r_lerpmodels = ri.Cvar_Get ("r_lerpmodels", "1", 0);
	r_speeds = ri.Cvar_Get ("r_speeds", "0", 0);

	r_lightlevel = ri.Cvar_Get ("r_lightlevel", "0", 0);

	r_shaders = ri.Cvar_Get ("r_shaders", "1", CVAR_ARCHIVE);
	
	rs_dynamic_time = ri.Cvar_Get("rs_dynamic_time", "0.1", CVAR_ARCHIVE);
	rs_detail = ri.Cvar_Get("rs_detail", "1", CVAR_ARCHIVE);

	r_model_lightlerp = ri.Cvar_Get( "r_model_lightlerp", "1", CVAR_ARCHIVE );
	r_model_dlights = ri.Cvar_Get( "r_model_dlights", "3", CVAR_ARCHIVE );

	gl_nosubimage = ri.Cvar_Get( "gl_nosubimage", "0", 0 );
	gl_allow_software = ri.Cvar_Get( "gl_allow_software", "0", 0 );

// Vic - begin
	r_overbrightbits = ri.Cvar_Get( "r_overbrightbits", "2", CVAR_ARCHIVE );
	gl_ext_mtexcombine = ri.Cvar_Get( "gl_ext_mtexcombine", "1", CVAR_ARCHIVE );
// Vic - end
	gl_ext_texture_compression = ri.Cvar_Get( "gl_ext_texture_compression", "0", CVAR_ARCHIVE ); // Heffo - ARB Texture Compression

	gl_modulate = ri.Cvar_Get ("gl_modulate", "1", CVAR_ARCHIVE );
	gl_log = ri.Cvar_Get( "gl_log", "0", 0 );
	gl_bitdepth = ri.Cvar_Get( "gl_bitdepth", "0", 0 );
	gl_mode = ri.Cvar_Get( "gl_mode", "3", CVAR_ARCHIVE );
	gl_lightmap = ri.Cvar_Get ("gl_lightmap", "0", 0);
	gl_shadows = ri.Cvar_Get ("gl_shadows", "0", CVAR_ARCHIVE );
	gl_dynamic = ri.Cvar_Get ("gl_dynamic", "1", 0);
	gl_nobind = ri.Cvar_Get ("gl_nobind", "0", 0);
	gl_round_down = ri.Cvar_Get ("gl_round_down", "1", 0);
	gl_picmip = ri.Cvar_Get ("gl_picmip", "0", 0);
	gl_skymip = ri.Cvar_Get ("gl_skymip", "0", 0);
	gl_showtris = ri.Cvar_Get ("gl_showtris", "0", 0);
	gl_ztrick = ri.Cvar_Get ("gl_ztrick", "0", 0);
	gl_finish = ri.Cvar_Get ("gl_finish", "0", CVAR_ARCHIVE);
	gl_clear = ri.Cvar_Get ("gl_clear", "0", 0);
	gl_cull = ri.Cvar_Get ("gl_cull", "1", 0);
	gl_polyblend = ri.Cvar_Get ("gl_polyblend", "1", 0);
	gl_flashblend = ri.Cvar_Get ("gl_flashblend", "0", 0);
	gl_playermip = ri.Cvar_Get ("gl_playermip", "0", 0);
	gl_monolightmap = ri.Cvar_Get( "gl_monolightmap", "0", 0 );
	gl_driver = ri.Cvar_Get( "gl_driver", "opengl32", CVAR_ARCHIVE );
	gl_texturemode = ri.Cvar_Get( "gl_texturemode", "GL_LINEAR_MIPMAP_NEAREST", CVAR_ARCHIVE );
	gl_anisotropic = ri.Cvar_Get( "gl_anisotropic", "0", CVAR_ARCHIVE );
	gl_texturealphamode = ri.Cvar_Get( "gl_texturealphamode", "default", CVAR_ARCHIVE );
	gl_texturesolidmode = ri.Cvar_Get( "gl_texturesolidmode", "default", CVAR_ARCHIVE );
	gl_lockpvs = ri.Cvar_Get( "gl_lockpvs", "0", 0 );


	gl_vertex_arrays = ri.Cvar_Get( "gl_vertex_arrays", "0", CVAR_ARCHIVE );

	r_celshading = ri.Cvar_Get( "r_celshading", "1", CVAR_ARCHIVE );
	r_celshading_width = ri.Cvar_Get( "r_celshading_width", "4", CVAR_ARCHIVE );
	r_outline = ri.Cvar_Get( "r_outline", "1", CVAR_ARCHIVE );
	r_outline2 = ri.Cvar_Get( "r_outline", "0", CVAR_ARCHIVE );
//	r_motionblur = Cvar_Get ("r_motionblur", "1", CVAR_ARCHIVE);
//	r_motionblur_density = Cvar_Get ("r_motionblur_density", "1", CVAR_ARCHIVE);


	#ifdef LIGHT_BLOOMS
	r_bloom = ri.Cvar_Get( "r_bloom", "1", CVAR_ARCHIVE );
	#endif

	cl_3dcam = ri.Cvar_Get ("cl_3dcam", "1", CVAR_ARCHIVE);
	cl_3dcam_angle = ri.Cvar_Get ("cl_3dcam_angle", "0", CVAR_ARCHIVE);
	cl_3dcam_dist = ri.Cvar_Get ("cl_3dcam_dist", "50", CVAR_ARCHIVE);
	cl_3dcam_alpha = ri.Cvar_Get ("cl_3dcam_alpha", "1", CVAR_ARCHIVE);
	cl_3dcam_chase = ri.Cvar_Get ("cl_3dcam_chase", "1", CVAR_ARCHIVE);
	cl_3dcam_adjust = ri.Cvar_Get ("cl_3dcam_adjust", "1", CVAR_ARCHIVE);

	gl_ext_swapinterval = ri.Cvar_Get( "gl_ext_swapinterval", "1", CVAR_ARCHIVE );
	gl_ext_multitexture = ri.Cvar_Get( "gl_ext_multitexture", "1", CVAR_ARCHIVE );
	gl_ext_pointparameters = ri.Cvar_Get( "gl_ext_pointparameters", "1", CVAR_ARCHIVE );
	gl_ext_compiled_vertex_array = ri.Cvar_Get( "gl_ext_compiled_vertex_array", "1", CVAR_ARCHIVE );

	gl_drawbuffer = ri.Cvar_Get( "gl_drawbuffer", "GL_BACK", 0 );
	gl_swapinterval = ri.Cvar_Get( "gl_swapinterval", "0", CVAR_ARCHIVE );

	gl_saturatelighting = ri.Cvar_Get( "gl_saturatelighting", "0", 0 );

	gl_3dlabs_broken = ri.Cvar_Get( "gl_3dlabs_broken", "1", CVAR_ARCHIVE );

	vid_fullscreen = ri.Cvar_Get( "vid_fullscreen", "0", CVAR_ARCHIVE );
	vid_gamma = ri.Cvar_Get( "vid_gamma", "1.0", CVAR_ARCHIVE );
	vid_ref = ri.Cvar_Get( "vid_ref", "gl", CVAR_ARCHIVE );

	ri.Cmd_AddCommand( "imagelist", GL_ImageList_f );
	ri.Cmd_AddCommand( "screenshot", GL_ScreenShot_f );
	ri.Cmd_AddCommand( "modellist", Mod_Modellist_f );
	ri.Cmd_AddCommand( "gl_strings", GL_Strings_f );
}

/*
==================
R_SetMode
==================
*/
qboolean R_SetMode (void)
{
	rserr_t err;
	qboolean fullscreen;

	if ( vid_fullscreen->modified && !gl_config.allow_cds )
	{
		ri.Con_Printf( PRINT_ALL, "R_SetMode() - CDS not allowed with this driver\n" );
		ri.Cvar_SetValue( "vid_fullscreen", !vid_fullscreen->value );
		vid_fullscreen->modified = false;
	}

	fullscreen = vid_fullscreen->value;

	skydistance->modified = true; // DMP skybox size change
	vid_fullscreen->modified = false;
	gl_mode->modified = false;

	if ( ( err = GLimp_SetMode( &vid.width, &vid.height, gl_mode->value, fullscreen ) ) == rserr_ok )
	{
		gl_state.prev_mode = gl_mode->value;
	}
	else
	{
		if ( err == rserr_invalid_fullscreen )
		{
			ri.Cvar_SetValue( "vid_fullscreen", 0);
			vid_fullscreen->modified = false;
			ri.Con_Printf( PRINT_ALL, "rfx_gl::R_SetMode() - fullscreen unavailable in this mode\n" );
			if ( ( err = GLimp_SetMode( &vid.width, &vid.height, gl_mode->value, false ) ) == rserr_ok )
				return true;
		}
		else if ( err == rserr_invalid_mode )
		{
			ri.Cvar_SetValue( "gl_mode", gl_state.prev_mode );
			gl_mode->modified = false;
			ri.Con_Printf( PRINT_ALL, "rfx_gl::R_SetMode() - invalid mode\n" );
		}

		// try setting it back to something safe
		if ( ( err = GLimp_SetMode( &vid.width, &vid.height, gl_state.prev_mode, false ) ) != rserr_ok )
		{
			ri.Con_Printf( PRINT_ALL, "rfx_gl::R_SetMode() - could not revert to safe mode\n" );
			return false;
		}
	}
	return true;
}

/*
===============
R_Init
===============
*/
int R_Init( void *hinstance, void *hWnd )
{	
	char renderer_buffer[1000];
	char vendor_buffer[1000];
	int		err;
	int		j;
	extern float r_turbsin[256];

	for ( j = 0; j < 256; j++ )
	{
		r_turbsin[j] *= 0.5;
	}

	ri.Con_Printf (PRINT_ALL, "rfx_gl version: "REF_VERSION"\n");

	Draw_GetPalette ();

	R_Register();

	VLight_Init();

	// initialize our QGL dynamic bindings
	if ( !QGL_Init( gl_driver->string ) )
	{
		QGL_Shutdown();
        ri.Con_Printf (PRINT_ALL, "rfx_gl::R_Init() - could not load \"%s\"\n", gl_driver->string );
		return -1;
	}

	// initialize OS-specific parts of OpenGL
	if ( !GLimp_Init( hinstance, hWnd ) )
	{
		QGL_Shutdown();
		return -1;
	}

	// set our "safe" modes
	gl_state.prev_mode = 3;

	// create the window and set up the context
	if ( !R_SetMode () )
	{
		QGL_Shutdown();
        ri.Con_Printf (PRINT_ALL, "rfx_gl::R_Init() - could not R_SetMode()\n" );
		return -1;
	}

	ri.Vid_MenuInit();

	/*
	** get our various GL strings
	*/
	gl_config.vendor_string = qglGetString (GL_VENDOR);
	ri.Con_Printf (PRINT_ALL, "GL_VENDOR: %s\n", gl_config.vendor_string );
	gl_config.renderer_string = qglGetString (GL_RENDERER);
	ri.Con_Printf (PRINT_ALL, "GL_RENDERER: %s\n", gl_config.renderer_string );
	gl_config.version_string = qglGetString (GL_VERSION);
	ri.Con_Printf (PRINT_ALL, "GL_VERSION: %s\n", gl_config.version_string );
	gl_config.extensions_string = qglGetString (GL_EXTENSIONS);
	ri.Con_Printf (PRINT_ALL, "GL_EXTENSIONS: %s\n", gl_config.extensions_string );

	strcpy( renderer_buffer, gl_config.renderer_string );
	strlwr( renderer_buffer );

	strcpy( vendor_buffer, gl_config.vendor_string );
	strlwr( vendor_buffer );

	if ( strstr( renderer_buffer, "voodoo" ) )
	{
		if ( !strstr( renderer_buffer, "rush" ) )
			gl_config.renderer = GL_RENDERER_VOODOO;
		else
			gl_config.renderer = GL_RENDERER_VOODOO_RUSH;
	}
	else if ( strstr( vendor_buffer, "sgi" ) )
		gl_config.renderer = GL_RENDERER_SGI;
	else if ( strstr( renderer_buffer, "permedia" ) )
		gl_config.renderer = GL_RENDERER_PERMEDIA2;
	else if ( strstr( renderer_buffer, "glint" ) )
		gl_config.renderer = GL_RENDERER_GLINT_MX;
	else if ( strstr( renderer_buffer, "glzicd" ) )
		gl_config.renderer = GL_RENDERER_REALIZM;
	else if ( strstr( renderer_buffer, "gdi" ) )
		gl_config.renderer = GL_RENDERER_MCD;
	else if ( strstr( renderer_buffer, "pcx2" ) )
		gl_config.renderer = GL_RENDERER_PCX2;
	else if ( strstr( renderer_buffer, "verite" ) )
		gl_config.renderer = GL_RENDERER_RENDITION;
	else
		gl_config.renderer = GL_RENDERER_OTHER;

	if ( toupper( gl_monolightmap->string[1] ) != 'F' )
	{
		if ( gl_config.renderer == GL_RENDERER_PERMEDIA2 )
		{
			ri.Cvar_Set( "gl_monolightmap", "A" );
			ri.Con_Printf( PRINT_ALL, "...using gl_monolightmap 'a'\n" );
		}
		else if ( gl_config.renderer & GL_RENDERER_POWERVR ) 
		{
			ri.Cvar_Set( "gl_monolightmap", "0" );
		}
		else
		{
			ri.Cvar_Set( "gl_monolightmap", "0" );
		}
	}

	// power vr can't have anything stay in the framebuffer, so
	// the screen needs to redraw the tiled background every frame
	if ( gl_config.renderer & GL_RENDERER_POWERVR ) 
	{
		ri.Cvar_Set( "scr_drawall", "1" );
	}
	else
	{
		ri.Cvar_Set( "scr_drawall", "0" );
	}

#ifdef __linux__
	ri.Cvar_SetValue( "gl_finish", 1 );
#endif

	// MCD has buffering issues
	if ( gl_config.renderer == GL_RENDERER_MCD )
	{
		ri.Cvar_SetValue( "gl_finish", 1 );
	}

	if ( gl_config.renderer & GL_RENDERER_3DLABS )
	{
		if ( gl_3dlabs_broken->value )
			gl_config.allow_cds = false;
		else
			gl_config.allow_cds = true;
	}
	else
	{
		gl_config.allow_cds = true;
	}

	if ( gl_config.allow_cds )
		ri.Con_Printf( PRINT_ALL, "...allowing CDS\n" );
	else
		ri.Con_Printf( PRINT_ALL, "...disabling CDS\n" );

	/*
	** grab extensions
	*/
	if ( strstr( gl_config.extensions_string, "GL_EXT_compiled_vertex_array" ) || 
		 strstr( gl_config.extensions_string, "GL_SGI_compiled_vertex_array" ) )
	{
		ri.Con_Printf( PRINT_ALL, "...enabling GL_EXT_compiled_vertex_array\n" );
		qglLockArraysEXT = ( void * ) qwglGetProcAddress( "glLockArraysEXT" );
		qglUnlockArraysEXT = ( void * ) qwglGetProcAddress( "glUnlockArraysEXT" );
	}
	else
	{
		ri.Con_Printf( PRINT_ALL, "...GL_EXT_compiled_vertex_array not found\n" );
	}

#ifdef _WIN32
	if ( strstr( gl_config.extensions_string, "WGL_EXT_swap_control" ) )
	{
		qwglSwapIntervalEXT = ( BOOL (WINAPI *)(int)) qwglGetProcAddress( "wglSwapIntervalEXT" );
		ri.Con_Printf( PRINT_ALL, "...enabling WGL_EXT_swap_control\n" );
	}
	else
	{
		ri.Con_Printf( PRINT_ALL, "...WGL_EXT_swap_control not found\n" );
	}
#endif

	if ( strstr( gl_config.extensions_string, "GL_EXT_point_parameters" ) )
	{
		if ( gl_ext_pointparameters->value )
		{
			qglPointParameterfEXT = ( void (APIENTRY *)( GLenum, GLfloat ) ) qwglGetProcAddress( "glPointParameterfEXT" );
			qglPointParameterfvEXT = ( void (APIENTRY *)( GLenum, const GLfloat * ) ) qwglGetProcAddress( "glPointParameterfvEXT" );
			ri.Con_Printf( PRINT_ALL, "...using GL_EXT_point_parameters\n" );
		}
		else
		{
			ri.Con_Printf( PRINT_ALL, "...ignoring GL_EXT_point_parameters\n" );
		}
	}
	else
	{
		ri.Con_Printf( PRINT_ALL, "...GL_EXT_point_parameters not found\n" );
	}

	if ( strstr( gl_config.extensions_string, "GL_ARB_multitexture" ) )
	{
		if ( gl_ext_multitexture->value )
		{
			ri.Con_Printf( PRINT_ALL, "...using GL_ARB_multitexture\n" );
			qglMTexCoord2fSGIS = ( void * ) qwglGetProcAddress( "glMultiTexCoord2fARB" );
			qglActiveTextureARB = ( void * ) qwglGetProcAddress( "glActiveTextureARB" );
			qglClientActiveTextureARB = ( void * ) qwglGetProcAddress( "glClientActiveTextureARB" );
			GL_TEXTURE0 = GL_TEXTURE0_ARB;
			GL_TEXTURE1 = GL_TEXTURE1_ARB;
		}
		else
		{
			ri.Con_Printf( PRINT_ALL, "...ignoring GL_ARB_multitexture\n" );
		}
	}
	else
	{
		ri.Con_Printf( PRINT_ALL, "...GL_ARB_multitexture not found\n" );
	}

	if ( strstr( gl_config.extensions_string, "GL_SGIS_multitexture" ) )
	{
		if ( qglActiveTextureARB )
		{
			ri.Con_Printf( PRINT_ALL, "...GL_SGIS_multitexture deprecated in favor of ARB_multitexture\n" );
		}
		else if ( gl_ext_multitexture->value )
		{
			ri.Con_Printf( PRINT_ALL, "...using GL_SGIS_multitexture\n" );
			qglMTexCoord2fSGIS = ( void * ) qwglGetProcAddress( "glMTexCoord2fSGIS" );
			qglSelectTextureSGIS = ( void * ) qwglGetProcAddress( "glSelectTextureSGIS" );
			GL_TEXTURE0 = GL_TEXTURE0_SGIS;
			GL_TEXTURE1 = GL_TEXTURE1_SGIS;
		}
		else
		{
			ri.Con_Printf( PRINT_ALL, "...ignoring GL_SGIS_multitexture\n" );
		}
	}
	else
	{
		ri.Con_Printf( PRINT_ALL, "...GL_SGIS_multitexture not found\n" );
	}

// Vic - begin
	gl_config.mtexcombine = false;

	if ( strstr( gl_config.extensions_string, "GL_ARB_texture_env_combine" ) )
	{
		if ( gl_ext_mtexcombine->value )
		{
			Com_Printf( "...using GL_ARB_texture_env_combine\n" );
			gl_config.mtexcombine = true;
		}
		else
		{
			Com_Printf( "...ignoring GL_ARB_texture_env_combine\n" );
		}
	}
	else
	{
		Com_Printf( "...GL_ARB_texture_env_combine not found\n" );
	}

	if ( !gl_config.mtexcombine )
	{
		if ( strstr( gl_config.extensions_string, "GL_EXT_texture_env_combine" ) )
		{
			if ( gl_ext_mtexcombine->value )
			{
				Com_Printf( "...using GL_EXT_texture_env_combine\n" );
				gl_config.mtexcombine = true;
			}
			else
			{
				Com_Printf( "...ignoring GL_EXT_texture_env_combine\n" );
			}
		}
		else
		{
			Com_Printf( "...GL_EXT_texture_env_combine not found\n" );
		}
	}
// Vic - end

	// Texture Shader support - MrG
	if ( strstr( gl_config.extensions_string, "GL_NV_texture_shader" ) )
	{
		ri.Con_Printf(PRINT_ALL, "...using GL_NV_texture_shader\n");
		gl_state.texshaders=true;
	} 
	else 
	{
		ri.Con_Printf(PRINT_ALL, "...GL_NV_texture_shader not found\n");
		gl_state.texshaders=false;
	}

	if ( strstr( gl_config.extensions_string, "GL_SGIS_generate_mipmap" ) )
	{
		ri.Con_Printf(PRINT_ALL, "...using GL_SGIS_generate_mipmap\n");
		gl_state.sgis_mipmap=true;
	} else {
		ri.Con_Printf(PRINT_ALL, "...GL_SGIS_generate_mipmap not found\n");
		gl_state.sgis_mipmap=false;
	}

	// Heffo - ARB Texture Compression
	if ( strstr( gl_config.extensions_string, "GL_ARB_texture_compression" ) )
	{
		if(!gl_ext_texture_compression->value)
		{
			ri.Con_Printf(PRINT_ALL, "...ignoring GL_ARB_texture_compression\n");
			gl_state.texture_compression = false;
		}
		else
		{
			ri.Con_Printf(PRINT_ALL, "...using GL_ARB_texture_compression\n");
			gl_state.texture_compression = true;
		}
	}
	else
	{
		ri.Con_Printf(PRINT_ALL, "...GL_ARB_texture_compression not found\n");
		gl_state.texture_compression = false;
		ri.Cvar_Set("gl_ext_texture_compression", "0");
	}

	GL_SetDefaultState();

	/*
	** draw our stereo patterns
	*/
#if 0 // commented out until H3D pays us the money they owe us
	GL_DrawStereoPattern();
#endif

	gl_swapinterval->modified = true;

	GL_InitImages ();
	Mod_Init ();
	R_InitParticleTexture ();
	Draw_InitLocal ();

	resetDynamicImage ();

	if(gl_texturemode)
		GL_TextureMode( gl_texturemode->string );

	err = qglGetError();
	if ( err != GL_NO_ERROR )
		ri.Con_Printf (PRINT_ALL, "glGetError() = 0x%x\n", err);
}

/*
===============
R_Shutdown
===============
*/
void R_Shutdown (void)
{	
	ri.Cmd_RemoveCommand ("modellist");
	ri.Cmd_RemoveCommand ("screenshot");
	ri.Cmd_RemoveCommand ("imagelist");
	ri.Cmd_RemoveCommand ("gl_strings");

	Mod_FreeAll ();

	GL_ShutdownImages ();

	/*
	** shut down OS specific OpenGL stuff like contexts, etc.
	*/
	GLimp_Shutdown();

	/*
	** shutdown our QGL subsystem
	*/
	QGL_Shutdown();
}



/*
@@@@@@@@@@@@@@@@@@@@@
R_BeginFrame
@@@@@@@@@@@@@@@@@@@@@
*/

void UpdateGammaRamp();
void RefreshFont (void);
void R_BeginFrame( float camera_separation )
{
	gl_state.camera_separation = camera_separation;

	if (con_font->modified)
		RefreshFont ();

	if (con_font_size->modified)
	{
		if (con_font_size->value<4)
			ri.Cvar_Set( "con_font_size", "4" );
		else if (con_font_size->value>32)
			ri.Cvar_Set( "con_font_size", "32" );

		con_font_size->modified = false;
	}

	if (r_overbrightbits->modified)
	{
		if (r_overbrightbits->value<1)
			ri.Cvar_Set( "r_overbrightbits", "1" );
		else if (r_overbrightbits->value>2 && r_overbrightbits->value!=4)
			ri.Cvar_Set( "r_overbrightbits", "4" );
		
		r_overbrightbits->modified = false;
	}
	if (gl_modulate->modified)
	{
		if (gl_modulate->value<0.5)
			ri.Cvar_Set( "gl_modulate", "0.5");
		else if (gl_modulate->value>3)
			ri.Cvar_Set( "gl_modulate", "3" );

		gl_modulate->modified = false;
	}

	/*
	** change modes if necessary
	*/
	if ( gl_mode->modified || vid_fullscreen->modified )
	{	// FIXME: only restart if CDS is required
		cvar_t	*ref;

		ref = ri.Cvar_Get ("vid_ref", "gl", 0);
		ref->modified = true;
	}

	if ( gl_log->modified )
	{
		GLimp_EnableLogging( gl_log->value );
		gl_log->modified = false;
	}

	if ( gl_log->value )
	{
		GLimp_LogNewFrame();
	}

	/*
	** update 3Dfx gamma -- it is expected that a user will do a vid_restart
	** after tweaking this value
	*/
	if ( vid_gamma->modified )
	{
		vid_gamma->modified = false;

		if (gl_state.gammaramp)
			UpdateGammaRamp();
		else if ( gl_config.renderer & ( GL_RENDERER_VOODOO ) )
		{
			char envbuffer[1024];
			float g;

			g = 2.00 * ( 0.8 - ( vid_gamma->value - 0.5 ) ) + 1.0F;
			Com_sprintf( envbuffer, sizeof(envbuffer), "SSTV2_GAMMA=%f", g );
			putenv( envbuffer );
			Com_sprintf( envbuffer, sizeof(envbuffer), "SST_GAMMA=%f", g );
			putenv( envbuffer );
		}
	}

	if (gl_particle_lighting->modified)
	{	
		gl_particle_lighting->modified = false;
		if (gl_particle_lighting->value<0)
			gl_particle_lighting->value=0;
		if (gl_particle_lighting->value>1)
			gl_particle_lighting->value=1;
	}

	GLimp_BeginFrame( camera_separation );

	/*
	** go into 2D mode
	*/
	qglViewport (0,0, vid.width, vid.height);
	qglMatrixMode(GL_PROJECTION);
    qglLoadIdentity ();
	qglOrtho  (0, vid.width, vid.height, 0, -99999, 99999);
	qglMatrixMode(GL_MODELVIEW);
    qglLoadIdentity ();
	qglDisable (GL_DEPTH_TEST);
	qglDisable (GL_CULL_FACE);
	GLSTATE_DISABLE_BLEND
	GLSTATE_ENABLE_ALPHATEST
	qglColor4f (1,1,1,1);

	/*
	** draw buffer stuff
	*/
	if ( gl_drawbuffer->modified )
	{
		gl_drawbuffer->modified = false;

		if ( gl_state.camera_separation == 0 || !gl_state.stereo_enabled )
		{
			if ( Q_stricmp( gl_drawbuffer->string, "GL_FRONT" ) == 0 )
				qglDrawBuffer( GL_FRONT );
			else
				qglDrawBuffer( GL_BACK );
		}
	}

	/*
	** texturemode stuff
	*/
	if ( gl_texturemode->modified )
	{
		GL_TextureMode( gl_texturemode->string );
		gl_texturemode->modified = false;
	}

	if ( gl_texturealphamode->modified )
	{
		GL_TextureAlphaMode( gl_texturealphamode->string );
		gl_texturealphamode->modified = false;
	}

	if ( gl_texturesolidmode->modified )
	{
		GL_TextureSolidMode( gl_texturesolidmode->string );
		gl_texturesolidmode->modified = false;
	}

	/*
	** swapinterval stuff
	*/
	GL_UpdateSwapInterval();

	//
	// clear screen if desired
	//
	R_Clear ();
}

/*
=============
R_SetPalette
=============
*/
unsigned r_rawpalette[256];

void R_SetPalette ( const unsigned char *palette)
{
	int		i;

	byte *rp = ( byte * ) r_rawpalette;

	if ( palette )
	{
		for ( i = 0; i < 256; i++ )
		{
			rp[i*4+0] = palette[i*3+0];
			rp[i*4+1] = palette[i*3+1];
			rp[i*4+2] = palette[i*3+2];
			rp[i*4+3] = 0xff;
		}
	}
	else
	{
		for ( i = 0; i < 256; i++ )
		{
			rp[i*4+0] = d_8to24table[i] & 0xff;
			rp[i*4+1] = ( d_8to24table[i] >> 8 ) & 0xff;
			rp[i*4+2] = ( d_8to24table[i] >> 16 ) & 0xff;
			rp[i*4+3] = 0xff;
		}
	}
//	GL_SetTexturePalette( r_rawpalette );

	qglClearColor (0,0,0,0);
	qglClear (GL_COLOR_BUFFER_BIT);
	qglClearColor (1,0, 0.5 , 0.5);
	
}

/*
** R_DrawBeam
*/
void R_RenderBeam( vec3_t start, vec3_t end, float size, float red, float green, float blue, float alpha );

void R_DrawBeam( entity_t *e )
{

	R_RenderBeam( e->origin, e->oldorigin, e->frame, 
		( d_8to24table[e->skinnum & 0xFF] ) & 0xFF,
		( d_8to24table[e->skinnum & 0xFF] >> 8 ) & 0xFF,
		( d_8to24table[e->skinnum & 0xFF] >> 16 ) & 0xFF,
		e->alpha*254 );

	//OLD BEAM RENDERING CODE ...
	/*
#define NUM_BEAM_SEGS 12

	int	i;
	float r, g, b;

	vec3_t perpvec;
	vec3_t direction, normalized_direction;
	vec3_t	start_points[NUM_BEAM_SEGS], end_points[NUM_BEAM_SEGS];
	vec3_t oldorigin, origin;

	oldorigin[0] = e->oldorigin[0];
	oldorigin[1] = e->oldorigin[1];
	oldorigin[2] = e->oldorigin[2];

	origin[0] = e->origin[0];
	origin[1] = e->origin[1];
	origin[2] = e->origin[2];

	normalized_direction[0] = direction[0] = oldorigin[0] - origin[0];
	normalized_direction[1] = direction[1] = oldorigin[1] - origin[1];
	normalized_direction[2] = direction[2] = oldorigin[2] - origin[2];

	if ( VectorNormalize( normalized_direction ) == 0 )
		return;

	PerpendicularVector( perpvec, normalized_direction );
	VectorScale( perpvec, e->frame / 2, perpvec );

	for ( i = 0; i < NUM_BEAM_SEGS; i++ )
	{
		RotatePointAroundVector( start_points[i], normalized_direction, perpvec, (360.0/NUM_BEAM_SEGS)*i );
		VectorAdd( start_points[i], origin, start_points[i] );
		VectorAdd( start_points[i], direction, end_points[i] );
	}

	qglDisable( GL_TEXTURE_2D );
	GLSTATE_ENABLE_BLEND
	qglDepthMask( GL_FALSE );

	r = ( d_8to24table[e->skinnum & 0xFF] ) & 0xFF;
	g = ( d_8to24table[e->skinnum & 0xFF] >> 8 ) & 0xFF;
	b = ( d_8to24table[e->skinnum & 0xFF] >> 16 ) & 0xFF;

	r *= DIV255;
	g *= DIV255;
	b *= DIV255;

	qglColor4f( r, g, b, e->alpha );

	qglBegin( GL_TRIANGLE_STRIP );
	for ( i = 0; i < NUM_BEAM_SEGS; i++ )
	{
		qglVertex3fv( start_points[i] );
		qglVertex3fv( end_points[i] );
		qglVertex3fv( start_points[(i+1)%NUM_BEAM_SEGS] );
		qglVertex3fv( end_points[(i+1)%NUM_BEAM_SEGS] );
	}
	qglEnd();

	qglEnable( GL_TEXTURE_2D );
	GLSTATE_DISABLE_BLEND
	qglDepthMask( GL_TRUE );
	*/
}

void R_RenderBeam( vec3_t start, vec3_t end, float size, float red, float green, float blue, float alpha )
{
	float	len;	
	vec3_t coord[4], ang_up, ang_right, vdelta;

	GL_TexEnv( GL_MODULATE );
	qglDepthMask   (false);
	GL_BlendFunction   (GL_SRC_ALPHA, GL_ONE);
	GLSTATE_ENABLE_BLEND
	GL_ShadeModel (GL_SMOOTH);
	GL_Bind(r_particlebeam->texnum);
	qglColor4ub(red, green, blue, alpha);

	VectorSubtract(start, end, ang_up);
	len = VectorNormalize(ang_up);

	VectorSubtract(r_newrefdef.vieworg, start, vdelta);
	CrossProduct(ang_up, vdelta, ang_right);
	if(!VectorCompare(ang_right, vec3_origin))
		VectorNormalize(ang_right);

	VectorScale (ang_right, size*3.0, ang_right);

	VectorAdd(start, ang_right, coord[0]);
	VectorAdd(end, ang_right, coord[1]);
	VectorSubtract(end, ang_right, coord[2]);
	VectorSubtract(start, ang_right, coord[3]);

	qglPushMatrix();
	{
		qglBegin (GL_QUADS);
		{
			qglTexCoord2f (0, 1);
			qglVertex3fv  (coord[0]);
			qglTexCoord2f (0, 0);
			qglVertex3fv  (coord[1]);
			qglTexCoord2f (1, 0); 
			qglVertex3fv  (coord[2]);
			qglTexCoord2f (1, 1);
			qglVertex3fv  (coord[3]);
		}
		qglEnd ();
	}
	qglPopMatrix ();
	
	GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_TexEnv( GL_MODULATE );
	qglDepthMask (true);
	GLSTATE_DISABLE_BLEND
	qglColor4f   (1,1,1,1);
}

//===================================================================


void	R_BeginRegistration (char *map);
struct model_s	*R_RegisterModel (char *name);
struct image_s	*R_RegisterSkin (char *name);
void R_SetSky (char *name, float rotate, vec3_t axis);
void	R_EndRegistration (void);
void	R_AddStain (vec3_t org, float intensity, float r, float g, float b, float a, staintype_t type);

void	R_RenderFrame (refdef_t *fd);

struct image_s	*Draw_FindPic (char *name);

void	Draw_ScaledPic (int x, int y, float scale, float alpha, char *pic);
void	Draw_Pic (int x, int y, char *name);
void	Draw_Char (int x, int y, int c, int alpha);
void	Draw_ScaledChar (int x, int y, int c, float scale,  
	 int red, int green, int blue, int alpha, qboolean italic);
void	Draw_TileClear (int x, int y, int w, int h, char *name);
void	Draw_Fill (int x, int y, int w, int h, int c);
void	Draw_FadeBox (int x, int y, int w, int h, float r, float g, float b, float alpha);

float	CharMapScale (void);
void	SetParticlePicture (int num, char *name);
int Mod_MarkFragments (const vec3_t origin, const vec3_t axis[3], float radius, int maxPoints, vec3_t *points, int maxFragments, markFragment_t *fragments);

/*
@@@@@@@@@@@@@@@@@@@@@
GetRefAPI

@@@@@@@@@@@@@@@@@@@@@
*/
refexport_t GetRefAPI (refimport_t rimp )
{
	refexport_t	re;

	ri = rimp;

	re.api_version = API_VERSION;

	re.BeginRegistration = R_BeginRegistration;
	re.RegisterModel = R_RegisterModel;
	re.RegisterSkin = R_RegisterSkin;
	re.RegisterPic = Draw_FindPic;
	re.SetSky = R_SetSky;
	re.EndRegistration = R_EndRegistration;

	re.AddStain = R_AddStain;

	re.RenderFrame = R_RenderFrame;

	re.SetParticlePicture = SetParticlePicture;

	re.DrawGetPicSize = Draw_GetPicSize;
	re.DrawScaledPic = Draw_ScaledPic;
	re.DrawPic = Draw_Pic;
	re.DrawStretchPic = Draw_StretchPic;
	re.DrawScaledChar = Draw_ScaledChar;
	re.DrawChar = Draw_Char;
	re.DrawTileClear = Draw_TileClear;
	re.DrawFill = Draw_Fill;
	re.DrawFadeBox= Draw_FadeBox;
	re.CharMap_Scale = CharMapScale;

	re.DrawStretchRaw = Draw_StretchRaw;

	re.MarkFragments = Mod_MarkFragments;

	re.Init = R_Init;
	re.Shutdown = R_Shutdown;

	re.CinematicSetPalette = R_SetPalette;
	re.BeginFrame = R_BeginFrame;
	re.EndFrame = GLimp_EndFrame;

	re.AppActivate = GLimp_AppActivate;

	Swap_Init ();

	return re;
}


#ifndef REF_HARD_LINKED
// this is only here so the functions in q_shared.c and q_shwin.c can link
void Sys_Error (char *error, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr, error);
	vsprintf (text, error, argptr);
	va_end (argptr);

	ri.Sys_Error (ERR_FATAL, "%s", text);
}

void Com_Printf (char *fmt, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr, fmt);
	vsprintf (text, fmt, argptr);
	va_end (argptr);

	ri.Con_Printf (PRINT_ALL, "%s", text);
}

#endif
