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
// cl_view.c -- player rendering positioning

#include "client.h"

//=============
//
// development tools for weapons
//
int			gun_frame;
struct model_s	*gun_model;
struct model_s	*clientGun;
extern cvar_t	*skin;

//=============

cvar_t		*crosshair;
cvar_t		*crosshair_scale;
cvar_t		*gl_transrendersort;
cvar_t		*gl_particle_lighting;
cvar_t		*gl_particle_min;
cvar_t		*gl_particle_max;
cvar_t		*cl_testparticles;
cvar_t		*cl_testentities;
cvar_t		*cl_testlights;
cvar_t		*cl_testblend;

cvar_t		*cl_stats;

cvar_t		*hand;


int			r_numdlights;
dlight_t	r_dlights[MAX_DLIGHTS];

int			r_numentities;
entity_t	r_entities[MAX_ENTITIES];

int			r_numparticles;
particle_t	r_particles[MAX_PARTICLES];

lightstyle_t	r_lightstyles[MAX_LIGHTSTYLES];

char cl_weaponmodels[MAX_CLIENTWEAPONMODELS][MAX_QPATH];
int num_cl_weaponmodels;



/*
====================
CL_Trace

====================
*/

trace_t CL_Trace (vec3_t start, vec3_t end, float size,  int contentmask)
{
	vec3_t maxs, mins;

	VectorSet(maxs, size, size, size);
	VectorSet(mins, -size, -size, -size);

	return CM_BoxTrace (start, end, mins, maxs, 0, contentmask);
}

/*
====================
V_ClearScene

Specifies the model that will be used as the world
====================
*/
void V_ClearScene (void)
{
	r_numdlights = 0;
	r_numentities = 0;
	r_numparticles = 0;
}

/*
=====================
3D Cam Stuff -psychospaz

=====================
*/

#define CAM_MAXALPHADIST 0.000111

float viewermodelalpha;

void AddViewerEntAlpha (entity_t *ent)
{
	if (viewermodelalpha==1 || !cl_3dcam_alpha->value)
		return;

	ent->alpha *= viewermodelalpha;
	if (ent->alpha<.9)
		ent->flags |= RF_TRANSLUCENT;
}

#define ANGLEMAX 90.0
void CalcViewerCamTrans (float distance)
{
	float alphacalc = cl_3dcam_dist->value;

	//no div by 0
	if (alphacalc<1)
		alphacalc=1;

	viewermodelalpha = distance/alphacalc;

	if (viewermodelalpha>1)
		viewermodelalpha = 1;
}

/*
=====================
V_AddEntity

=====================
*/
void V_AddEntity (entity_t *ent)
{
	if (ent->flags&RF_VIEWERMODEL) //here is our client
	{
		int i; 
		vec3_t oldorgdiff;

		VectorSubtract(ent->oldorigin, ent->origin, oldorgdiff);

		//what was i thinking before!?
		for (i=0;i<3;i++)
			clientOrg[i] = ent->oldorigin[i] = ent->origin[i] = cl.predicted_origin[i];

		VectorAdd(ent->oldorigin, oldorgdiff, ent->oldorigin);

		if (hand->value==1) //lefthanded
		{
			ent->renderfx |= RF_MIRRORMODEL;
		}

		//saber hack
		if (ent->renderfx&RF2_CAMERAMODEL)
			ent->flags&=~RF_VIEWERMODEL;

		if (cl_3dcam->value && !(cl.attractloop && !(cl.cinematictime > 0 && cls.realtime - cl.cinematictime > 1000)))
		{
			AddViewerEntAlpha(ent);
			ent->flags&=~RF_VIEWERMODEL;
			ent->renderfx|=RF2_FORCE_SHADOW|RF2_CAMERAMODEL;
		}
		else if (ent->renderfx&RF2_CAMERAMODEL)
		{
			ent->flags &= ~RF_VIEWERMODEL;
			ent->renderfx|=RF2_FORCE_SHADOW|RF2_CAMERAMODEL;
		}
	}
	
	if (r_numentities >= MAX_ENTITIES)
		return;
	r_entities[r_numentities++] = *ent;
}


/*
=====================
V_AddParticle

=====================
*/
void V_AddParticle (vec3_t org, vec3_t angle, vec3_t color, float alpha, int alpha_src, int alpha_dst, float size, int image, int flags, decalpolys_t *decal)
{
	int i;
	particle_t	*p;

	if (r_numparticles >= MAX_PARTICLES)
		return;
	p = &r_particles[r_numparticles++];

	for (i=0;i<3;i++)
	{
		p->origin[i] = org[i];
		p->angle[i] = angle[i];
	}
	p->red = color[0];
	p->green = color[1];
	p->blue = color[2];
	p->alpha = alpha;
	p->image = image;
	p->flags = flags;
	p->size  = size;
	p->decal = decal;

	p->blendfunc_src = alpha_src;
	p->blendfunc_dst = alpha_dst;
}

/*
=====================
V_AddLight

=====================
*/
void V_AddLight (vec3_t org, float intensity, float r, float g, float b)
{
	dlight_t	*dl;

	if (r_numdlights >= MAX_DLIGHTS)
		return;
	dl = &r_dlights[r_numdlights++];
	VectorCopy (org, dl->origin);
	VectorCopy (vec3_origin, dl->direction);
	dl->intensity = intensity;
	dl->color[0] = r;
	dl->color[1] = g;
	dl->color[2] = b;

	dl->spotlight = false;
}


/*
=====================
V_AddSpotLight

=====================
*/
void V_AddSpotLight (vec3_t org, vec3_t direction, float intensity, float r, float g, float b)
{
	dlight_t	*dl;

	if (r_numdlights >= MAX_DLIGHTS)
		return;
	dl = &r_dlights[r_numdlights++];
	VectorCopy (org, dl->origin);
	VectorCopy(direction, dl->direction);
	dl->intensity = intensity;
	dl->color[0] = r;
	dl->color[1] = g;
	dl->color[2] = b;

	dl->spotlight=true;
}


/*
=====================
V_AddLightStyle

=====================
*/
void V_AddLightStyle (int style, float r, float g, float b)
{
	lightstyle_t	*ls;

	if (style < 0 || style > MAX_LIGHTSTYLES)
		Com_Error (ERR_DROP, "Bad light style %i", style);
	ls = &r_lightstyles[style];

	ls->white = r+g+b;
	ls->rgb[0] = r;
	ls->rgb[1] = g;
	ls->rgb[2] = b;
}

/*
================
V_TestParticles

If cl_testparticles is set, create 4096 particles in the view
================
*/
void V_TestParticles (void)
{
	particle_t	*p;
	int			i, j;
	float		d, r, u;

	r_numparticles = MAX_PARTICLES;
	for (i=0 ; i<r_numparticles ; i++)
	{
		d = i*0.25;
		r = 4*((i&7)-3.5);
		u = 4*(((i>>3)&7)-3.5);
		p = &r_particles[i];

		for (j=0 ; j<3 ; j++)
			p->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j]*d +
			cl.v_right[j]*r + cl.v_up[j]*u;

		p->color = 8;
		p->alpha = cl_testparticles->value;
	}
}

/*
================
V_TestEntities

If cl_testentities is set, create 32 player models
================
*/
void V_TestEntities (void)
{
	int			i, j;
	float		f, r;
	entity_t	*ent;

	r_numentities = 32;
	memset (r_entities, 0, sizeof(r_entities));

	for (i=0 ; i<r_numentities ; i++)
	{
		ent = &r_entities[i];

		r = 64 * ( (i%4) - 1.5 );
		f = 64 * (i/4) + 128;

		for (j=0 ; j<3 ; j++)
			ent->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j]*f +
			cl.v_right[j]*r;

		ent->model = cl.baseclientinfo.model;
		ent->skin = cl.baseclientinfo.skin;
	}
}

/*
================
V_TestLights

If cl_testlights is set, create 32 lights models
================
*/
void V_TestLights (void)
{
	int			i, j;
	float		f, r;
	dlight_t	*dl;

	r_numdlights = 32;
	memset (r_dlights, 0, sizeof(r_dlights));

	for (i=0 ; i<r_numdlights ; i++)
	{
		dl = &r_dlights[i];

		r = 64 * ( (i%4) - 1.5 );
		f = 64 * (i/4) + 128;

		for (j=0 ; j<3 ; j++)
			dl->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j]*f +
			cl.v_right[j]*r;
		dl->color[0] = ((i%6)+1) & 1;
		dl->color[1] = (((i%6)+1) & 2)>>1;
		dl->color[2] = (((i%6)+1) & 4)>>2;
		dl->intensity = 200;
		dl->spotlight=true;
	}
}

//===================================================================

/*
=================
CL_PrepRefresh

Call before entering a new level, or after changing dlls
=================
*/
qboolean needLoadingPlaque (void);
void CL_PrepRefresh ()
{
	char		mapname[32];
	int			i, max;
	char		name[MAX_QPATH];
	float		rotate;
	vec3_t		axis;
	qboolean	newPlaque = needLoadingPlaque();

	if (!cl.configstrings[CS_MODELS+1][0])
		return;		// no map loaded

	if (newPlaque)
		SCR_BeginLoadingPlaque();

	loadingMessage = true;
	Com_sprintf (loadingMessages[0], sizeof(loadingMessages[0]), "^2loading map...");
	Com_sprintf (loadingMessages[1], sizeof(loadingMessages[1]), "^2loading models...");
	Com_sprintf (loadingMessages[2], sizeof(loadingMessages[2]), "^2loading pics...");
	Com_sprintf (loadingMessages[3], sizeof(loadingMessages[3]), "^2loading clients...");
	loadingPercent = 0;

	SCR_AddDirtyPoint (0, 0);
	SCR_AddDirtyPoint (viddef.width-1, viddef.height-1);

	// let the render dll load the map
	strcpy (mapname, cl.configstrings[CS_MODELS+1] + 5);	// skip "maps/"
	mapname[strlen(mapname)-4] = 0;		// cut off ".bsp"

	// register models, pics, and skins
	Com_Printf ("Map: %s\r", mapname); 
	SCR_UpdateScreen ();
	re.BeginRegistration (mapname);
	Com_Printf ("                                     \r");
	Com_sprintf (loadingMessages[0], sizeof(loadingMessages[0]), "^i^0loading map...done");
	loadingPercent += 20;

	// precache status bar pics
	Com_Printf ("pics\r"); 
	SCR_UpdateScreen ();
	SCR_TouchPics ();
	Com_Printf ("                                     \r");

	CL_RegisterTEntModels ();

	num_cl_weaponmodels = 1;
	strcpy(cl_weaponmodels[0], "weapon.md2");

	for (i=1, max=0 ; i<MAX_MODELS && cl.configstrings[CS_MODELS+i][0] ; i++)
		max++;
	for (i=1 ; i<MAX_MODELS && cl.configstrings[CS_MODELS+i][0] ; i++)
	{
		strcpy (name, cl.configstrings[CS_MODELS+i]);
		name[37] = 0;	// never go beyond one line
		if (name[0] != '*')
		{
			Com_Printf ("%s\r", name);

			//only make max of 20 chars long
			Com_sprintf (loadingMessages[1], sizeof(loadingMessages[1]), "^2loading models...%s", 
				(strlen(name)>20)? &name[strlen(name)-20]: name);
		}

		SCR_UpdateScreen ();
		Sys_SendKeyEvents ();	// pump message loop
		if (name[0] == '#')
		{
			// special player weapon model
			if (num_cl_weaponmodels < MAX_CLIENTWEAPONMODELS)
			{
				strncpy(cl_weaponmodels[num_cl_weaponmodels], cl.configstrings[CS_MODELS+i]+1,
					sizeof(cl_weaponmodels[num_cl_weaponmodels]) - 1);
				num_cl_weaponmodels++;
			}
		} 
		else
		{
			cl.model_draw[i] = re.RegisterModel (cl.configstrings[CS_MODELS+i]);
			if (name[0] == '*')
				cl.model_clip[i] = CM_InlineModel (cl.configstrings[CS_MODELS+i]);
			else
				cl.model_clip[i] = NULL;
		}
		if (name[0] != '*')
			Com_Printf ("                                     \r");

		loadingPercent += 60.0f/(float)max;
	}
	Com_sprintf (loadingMessages[1], sizeof(loadingMessages[1]), "^i^0loading models...done");

	Com_Printf ("images\r", i); 
	SCR_UpdateScreen ();

	for (i=1, max=0 ; i<MAX_IMAGES && cl.configstrings[CS_IMAGES+i][0] ; i++)
		max++;
	for (i=1 ; i<MAX_IMAGES && cl.configstrings[CS_IMAGES+i][0] ; i++)
	{
		cl.image_precache[i] = re.RegisterPic (cl.configstrings[CS_IMAGES+i]);
		Sys_SendKeyEvents ();	// pump message loop		
		loadingPercent += 10.0f/(float)max;
	}
	Com_sprintf (loadingMessages[2], sizeof(loadingMessages[2]), "^i^0loading pics...done");

	Com_Printf ("                                     \r");

	//refresh the player model/skin info 
	CL_LoadClientinfo (&cl.baseclientinfo, va("unnamed\\%s\\%s", DEFAULTMODEL, DEFAULTSKIN));

	for (i=1, max=0 ; i<MAX_CLIENTS ; i++)
		if (cl.configstrings[CS_PLAYERSKINS+i][0])
			max++;
	for (i=0 ; i<MAX_CLIENTS ; i++)
	{
		if (!cl.configstrings[CS_PLAYERSKINS+i][0])
			continue;

		Com_sprintf (loadingMessages[3], sizeof(loadingMessages[3]), "^2loading clients...%i", i);
		Com_Printf ("client %i\r", i); 
		SCR_UpdateScreen ();
		Sys_SendKeyEvents ();	// pump message loop
		CL_ParseClientinfo (i);
		Com_Printf ("                                     \r");
		loadingPercent += 10.0f/(float)max;
	}
	Com_sprintf (loadingMessages[3], sizeof(loadingMessages[3]), "^i^0loading clients...done");

	//hack hack hack - psychospaz
	loadingPercent = 100;

	// set sky textures and speed
	Com_Printf ("sky\r", i); 
	SCR_UpdateScreen ();
	rotate = atof (cl.configstrings[CS_SKYROTATE]);
	sscanf (cl.configstrings[CS_SKYAXIS], "%f %f %f", 
		&axis[0], &axis[1], &axis[2]);
	re.SetSky (cl.configstrings[CS_SKY], rotate, axis);
	Com_Printf ("                                     \r");

	// the renderer can now free unneeded stuff
	re.EndRegistration ();

	// clear any lines of console text
	Con_ClearNotify ();

	SCR_UpdateScreen ();
	cl.refresh_prepped = true;
	cl.force_refdef = true;	// make sure we have a valid refdef

	// start the cd track
	CDAudio_Play (atoi(cl.configstrings[CS_CDTRACK]), true);

	loadingMessage = false;

	if (newPlaque)
		SCR_EndLoadingPlaque();
	else
		Cvar_Set ("paused", "0");
}

/*
====================
CalcFov
====================
*/
float CalcFov (float fov_x, float width, float height)
{
	float	a;
	float	x;

	if (fov_x < 1 || fov_x > 179)
		Com_Error (ERR_DROP, "Bad fov: %f", fov_x);

	x = width/tan(fov_x/360*M_PI);

	a = atan (height/x);

	a = a*360/M_PI;

	return a;
}

//============================================================================

// gun frame debugging functions
void V_Gun_Next_f (void)
{
	gun_frame++;
	Com_Printf ("frame %i\n", gun_frame);
}

void V_Gun_Prev_f (void)
{
	gun_frame--;
	if (gun_frame < 0)
		gun_frame = 0;
	Com_Printf ("frame %i\n", gun_frame);
}

void V_Gun_Model_f (void)
{
	char	name[MAX_QPATH];

	if (Cmd_Argc() != 2)
	{
		gun_model = NULL;
		return;
	}
	Com_sprintf (name, sizeof(name), "models/%s/tris.md2", Cmd_Argv(1));
	gun_model = re.RegisterModel (name);
}

//============================================================================


/*
=================
SCR_DrawCrosshair
=================
*/
#define DIV640 0.0015625

void SCR_DrawCrosshair (void)
{	
	float scale;
	int x, y, w, h;

	if (!crosshair->value)
		return;

	if (crosshair->modified)
	{
		crosshair->modified = false;
		SCR_TouchPics ();
	}

	if (crosshair_scale->modified)
	{
		crosshair_scale->modified=false;
		if (crosshair_scale->value>10)
			Cvar_SetValue("crosshair_scale", 10);
		else if (crosshair_scale->value<0.25)
			Cvar_SetValue("crosshair_scale", 0.25);
	}

	if (!crosshair_pic[0])
		return;

	scale = crosshair_scale->value * (viddef.width*DIV640);

	w = crosshair_width * scale;
	h = crosshair_height * scale;

	x = (viddef.width)*0.5;
	y = (viddef.height)*0.5;

	re.DrawStretchPic (
		x-w*0.5, y-h*0.5, 
		w, h, 
		crosshair_pic);
/*
	re.DrawScaledPic (scr_vrect.x + ((scr_vrect.width - crosshair_width)>>1) //width
	, scr_vrect.y + ((scr_vrect.height - crosshair_height)>>1)	//height
	, scale	//scale
	, crosshair_pic); //pic*/
}

void SCR_DrawIRGoggles (void)
{
	re.DrawStretchPic (0, 0, viddef.width, viddef.height, "ir_crosshair");
}

void SCR_DrawSniperCrosshair (void)
{
	if (modType("dday"))
	{	
		if (!clientGun)
			return;	
		if (cl.refdef.fov_x>=85)
			return;

		//continue if model is sniper
		if ((int)(clientGun)!=(int)(re.RegisterModel("models/weapons/usa/v_m1903/tris.md2")) &&
			(int)(clientGun)!=(int)(re.RegisterModel("models/weapons/grm/v_m98ks/tris.md2")) &&
			(int)(clientGun)!=(int)(re.RegisterModel("models/weapons/gbr/v_303s/tris.md2")) &&
			(int)(clientGun)!=(int)(re.RegisterModel("models/weapons/rus/v_m9130s/tris.md2")))
			return;
	}
	else if (modType("swq"))
	{
		if (clientGun)
			return;	
		if (cl.refdef.fov_x>=90)
			return;
	}
	else if (modType("action"))
	{
		if (clientGun)
			return;	
		if (cl.refdef.fov_x>=85)
			return;
	}
	else return;


	re.DrawStretchPic (0, 0, viddef.width, viddef.height, "sniper");
}

/*
==================
V_RenderView

==================
*/

//dirty hack for gloom!!!
cvar_t *r_overbrightbits;
cvar_t *r_celshading;
cvar_t *gl_modulate;
void GloomRenderCheck(void)
{
	if (!r_celshading)
		r_celshading = Cvar_Get( "r_celshading", "0", CVAR_ARCHIVE );
	if (!gl_modulate)
		gl_modulate = Cvar_Get( "gl_modulate", "1", CVAR_ARCHIVE );

	if (gl_modulate && gl_modulate->value>1)
		Cvar_SetValue( "gl_modulate", 1 );

	if (r_celshading && r_celshading->value)
		Cvar_SetValue( "r_celshading", 0 );

	if (cl_3dcam && cl_3dcam->value)
		Cvar_SetValue( "cl_3dcam", 0 );
}

void V_RenderView( float stereo_separation )
{
	extern int entitycmpfnc( const entity_t *, const entity_t * );

	if (cls.state != ca_active)
		return;

	if (!cl.refresh_prepped)
		return;			// still loading

	if (cl_timedemo->value)
	{
		if (!cl.timedemo_start)
			cl.timedemo_start = Sys_Milliseconds ();
		cl.timedemo_frames++;
	}

	// an invalid frame will just use the exact previous refdef
	// we can't use the old frame if the video mode has changed, though...
	if ( cl.frame.valid && (cl.force_refdef || !cl_paused->value) )
	{
		cl.force_refdef = false;
		V_ClearScene ();

		if (cl_testparticles->value)
			V_TestParticles ();
		if (cl_testentities->value)
			V_TestEntities ();
		if (cl_testlights->value)
			V_TestLights ();
		if (cl_testblend->value)
		{
			cl.refdef.blend[0] = 1;
			cl.refdef.blend[1] = 0.5;
			cl.refdef.blend[2] = 0.25;
			cl.refdef.blend[3] = 0.5;
		}

		// offset vieworg appropriately if we're doing stereo separation
		if ( stereo_separation != 0 )
		{
			vec3_t tmp;

			VectorScale( cl.v_right, stereo_separation, tmp );
			VectorAdd( cl.refdef.vieworg, tmp, cl.refdef.vieworg );
		}

		// never let it sit exactly on a node line, because a water plane can
		// dissapear when viewed with the eye exactly on it.
		// the server protocol only specifies to 1/8 pixel, so add 1/16 in each axis
		cl.refdef.vieworg[0] += 1.0/16;
		cl.refdef.vieworg[1] += 1.0/16;
		cl.refdef.vieworg[2] += 1.0/16;

		// build a refresh entity list and calc cl.sim*
		// this also calls CL_CalcViewValues which loads
		// v_forward, etc.
		CL_AddEntities ();
	
		cl.refdef.x = scr_vrect.x;
		cl.refdef.y = scr_vrect.y;
		cl.refdef.width = scr_vrect.width;
		cl.refdef.height = scr_vrect.height;
		cl.refdef.fov_y = CalcFov (cl.refdef.fov_x, cl.refdef.width, cl.refdef.height);
		cl.refdef.time = cl.time*0.001;

		cl.refdef.areabits = cl.frame.areabits;

		if (!cl_add_entities->value)
			r_numentities = 0;
		if (!cl_add_particles->value)
			r_numparticles = 0;
		if (!cl_add_lights->value)
			r_numdlights = 0;
		if (!cl_add_blend->value)
		{
			VectorClear (cl.refdef.blend);
		}

		cl.refdef.num_entities = r_numentities;
		cl.refdef.entities = r_entities;

		cl.refdef.num_particles = r_numparticles;
		cl.refdef.particles = r_particles;

		cl.refdef.num_dlights = r_numdlights;
		cl.refdef.dlights = r_dlights;
		cl.refdef.lightstyles = r_lightstyles;

		cl.refdef.rdflags = cl.frame.playerstate.rdflags;

		// sort entities for better cache locality
		if (!gl_transrendersort->value)
			qsort( cl.refdef.entities, cl.refdef.num_entities, sizeof( cl.refdef.entities[0] ), (int (*)(const void *, const void *))entitycmpfnc );
	}

	#ifdef LIGHT_BLOOMS
	cl.refdef.rdflags |= RDF_BLOOM;   //BLOOMS
	#endif

	re.RenderFrame (&cl.refdef);
	if (cl_stats->value)
		Com_Printf ("ent:%i  lt:%i  part:%i\n", r_numentities, r_numdlights, r_numparticles);
	if ( log_stats->value && ( log_stats_file != 0 ) )
		fprintf( log_stats_file, "%i,%i,%i,",r_numentities, r_numdlights, r_numparticles);


	SCR_AddDirtyPoint (scr_vrect.x, scr_vrect.y);
	SCR_AddDirtyPoint (scr_vrect.x+scr_vrect.width-1,
		scr_vrect.y+scr_vrect.height-1);

/*	if (cl.refdef.rdflags & RDF_IRGOGGLES)
		SCR_DrawIRGoggles();
	else*/
	{
		if (!modType("dday")) //dday has no crosshair (FORCED)
			SCR_DrawCrosshair ();

		SCR_DrawSniperCrosshair();
	}

	//hack for gloom
	if (modType("gloom"))
		GloomRenderCheck();
}


/*
=============
V_Viewpos_f
=============
*/
void V_Viewpos_f (void)
{
	Com_Printf ("(%i %i %i) : %i\n", (int)cl.refdef.vieworg[0],
		(int)cl.refdef.vieworg[1], (int)cl.refdef.vieworg[2], 
		(int)cl.refdef.viewangles[YAW]);
}

/*
=============
V_Init
=============
*/
void V_Init (void)
{
	Cmd_AddCommand ("gun_next", V_Gun_Next_f);
	Cmd_AddCommand ("gun_prev", V_Gun_Prev_f);
	Cmd_AddCommand ("gun_model", V_Gun_Model_f);

	Cmd_AddCommand ("viewpos", V_Viewpos_f);

	gl_transrendersort = Cvar_Get ("gl_transrendersort", "1", CVAR_ARCHIVE );
	gl_particle_lighting = Cvar_Get ("gl_particle_lighting", "0.75", CVAR_ARCHIVE );
	gl_particle_min = Cvar_Get ("gl_particle_min", "0", CVAR_ARCHIVE );
	gl_particle_max = Cvar_Get ("gl_particle_max", "0", CVAR_ARCHIVE );

	hand = Cvar_Get ("hand", "0", CVAR_ARCHIVE);
	if (!strcmp("swq",Cvar_Get ("game", "0", CVAR_ARCHIVE)->string)||!strcmp("cotf",Cvar_Get ("game", "0", CVAR_ARCHIVE)->string))
		Cvar_SetValue("hand", 0);

	crosshair = Cvar_Get ("crosshair", "0", CVAR_ARCHIVE);
	crosshair_scale = Cvar_Get ("crosshair_scale", "1", CVAR_ARCHIVE);

	cl_testblend = Cvar_Get ("cl_testblend", "0", 0);
	cl_testparticles = Cvar_Get ("cl_testparticles", "0", 0);
	cl_testentities = Cvar_Get ("cl_testentities", "0", 0);
	cl_testlights = Cvar_Get ("cl_testlights", "0", 0);

	cl_stats = Cvar_Get ("cl_stats", "0", 0);
}
