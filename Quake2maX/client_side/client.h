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

// client.h -- client side fx dll

#define	GAME_API_VERSION	3

#include "../client/client.h"

//
// functions provided by the main engine
//

typedef struct
{
	int			gameversion;

	//Entity importing
	int			serverEntsNum;
	centity_t	*serverEnts;

	//Functions to setup particles
	void		(*SetParticlePicture) (int num, char *name);
	void		*(*addParticle) (
					float angle0,		float angle1,		float angle2,
					float org0,			float org1,			float org2,
					float vel0,			float vel1,			float vel2,
					float accel0,		float accel1,		float accel2,
					float color0,		float color1,		float color2,
					float colorvel0,	float colorvel1,	float colorvel2,
					float alpha,		float alphavel,
					int	blendfunc_src,	int blendfunc_dst,
					float size,			float sizevel,			
					int	image,
					int flags,
					void (*think)(void *p, vec3_t org, vec3_t angle, float *alpha, float *size, int *image, float *time),
					qboolean thinknext);
	void		(*addParticleLight) (void *p,
					float light, float lightvel,
					float lcol0, float lcol1, float lcol2);

	//Client side clipping
	trace_t		(*CL_Trace) (vec3_t start, vec3_t end, float size, int contentmask);

	//Functions to add lights
	void		(*AddLight) (vec3_t org, float intensity, float r, float g, float b);
	void		(*AddLightStyle) (int style, float r, float g, float b);
} client_import_t;

//
// functions exported by the client subsystem
//
typedef struct
{
	int			apiversion;

	//All ents, server and client side
	int			clientEntsNum;
	centity_t	*clientEnts;

	//Set up particle images per mod...
	void		(*setParticleImages)(void);

} client_export_t;

client_import_t	ci;
client_export_t	cglobals;
