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
// cl_newfx.c -- MORE entity effects parsing and management

#include "client.h"

extern cparticle_t	*active_particles, *free_particles;
extern cparticle_t	particles[MAX_PARTICLES];
extern int			cl_numparticles;

extern void MakeNormalVectors (vec3_t forward, vec3_t right, vec3_t up);

cparticle_t *setupParticle (
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
			void (*think)(cparticle_t *p, vec3_t org, vec3_t angle, float *alpha, float *size, int *image, float *time),
			qboolean thinknext);

/*
======
vectoangles2 - this is duplicated in the game DLL, but I need it here.
======
*/
void vectoangles2 (vec3_t value1, vec3_t angles)
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

//=============
//=============
void CL_Flashlight (int ent, vec3_t pos)
{
	cdlight_t	*dl;

	dl = CL_AllocDlight (ent);
	VectorCopy (pos,  dl->origin);
	dl->radius = 400;
	dl->minlight = 250;
	dl->die = cl.time + 100;
	dl->color[0] = dl->color[1] = dl->color[2] = 1;
}

/*
======
CL_ColorFlash - flash of light
======
*/
void CL_ColorFlash (vec3_t pos, int ent, int intensity, float r, float g, float b)
{
	cdlight_t	*dl;

	dl = CL_AllocDlight (ent);
	VectorCopy (pos,  dl->origin);
	dl->radius = intensity;
	dl->minlight = 250;
	dl->die = cl.time + 100;
	dl->color[0] = r;
	dl->color[1] = g;
	dl->color[2] = b;
}


/*
======
CL_DebugTrail
======
*/
void CL_DebugTrail (vec3_t start, vec3_t end)
{
	vec3_t		move;
	vec3_t		vec;
	float		len;
	float		dec;
	vec3_t		right, up;

	VectorCopy (start, move);
	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	MakeNormalVectors (vec, right, up);

	dec = 2;
	VectorScale (vec, dec, vec);
	VectorCopy (start, move);

	while (len > 0)
	{
		len -= dec;

		setupParticle (
			0,	0,	0,
			move[0],	move[1],	move[2],
			0,	0,	0,
			0,		0,		0,
			50,	50,	255,
			0,	0,	0,
			1,		-0.75,
			GL_SRC_ALPHA, GL_ONE,
			7.5,			0,			
			particle_generic,
			0,
			NULL,0);

		VectorAdd (move, vec, move);
	}

}

void CL_ForceWall (vec3_t start, vec3_t end, int color8)
{
	vec3_t		move;
	vec3_t		vec;
	float		len;
	vec3_t color = { color8red(color8), color8green(color8), color8blue(color8)};

	VectorCopy (start, move);
	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	VectorScale (vec, 4, vec);

	// FIXME: this is a really silly way to have a loop
	while (len > 0)
	{
		len -= 4;
		
		if (frand() > 0.3)
			setupParticle (
				0,	0,	0,
				move[0] + crand()*3,	move[1] + crand()*3,	move[2] + crand()*3,
				0,	0,	-40 - (crand()*10),
				0,		0,		0,
				color[0]+5,	color[1]+5,	color[2]+5,
				0,	0,	0,
				1,		-1.0 / (3.0+frand()*0.5),
				GL_SRC_ALPHA, GL_ONE,
				5,			0,			
				particle_generic,
				0,
				NULL,0);

		VectorAdd (move, vec, move);
	}
}



/*
===============
CL_BubbleTrail2 (lets you control the # of bubbles by setting the distance between the spawns)

===============
*/
void CL_BubbleTrail2 (vec3_t start, vec3_t end, int dist)
{
	vec3_t		move;
	vec3_t		vec;
	float		len;
	int			i;
	float		dec, size;

	VectorCopy (start, move);
	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	dec = dist;
	VectorScale (vec, dec, vec);

	for (i=0 ; i<len ; i+=dec)
	{
		size = (frand()>0.25)? 1 : (frand()>0.5) ? 2 : (frand()>0.75) ? 3 : 4;

		setupParticle (
			0,	0,	0,
			move[0]+crand()*2,	move[1]+crand()*2,	move[2]+crand()*2,
			crand()*5,	crand()*5,	crand()*5+6,
			0,		0,		0,
			255,	255,	255,
			0,	0,	0,
			0.75,		-1.0 / (1 + frand() * 0.2),
			GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
			size,			1,			
			particle_bubble,
			PART_SHADED,
			NULL,0);

		VectorAdd (move, vec, move);
	}
}

void CL_Heatbeam (vec3_t start, vec3_t forward)
{
	vec3_t		move;
	vec3_t		vec;
	float		len;
	vec3_t		right, up;
	int			i;
	float		c, s;
	vec3_t		dir;
	float		ltime;
	float		step = 32.0, rstep;
	float		start_pt;
	float		rot;
	float		variance;
	vec3_t		end;

	VectorMA (start, 4096, forward, end);

	VectorCopy (start, move);
	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	// FIXME - pmm - these might end up using old values?
//	MakeNormalVectors (vec, right, up);
	VectorCopy (cl.v_right, right);
	VectorCopy (cl.v_up, up);

	VectorMA (move, -0.5, right, move);
	VectorMA (move, -0.5, up, move);

	ltime = (float) cl.time/1000.0;
	start_pt = fmod(ltime*96.0,step);
	VectorMA (move, start_pt, vec, move);

	VectorScale (vec, step, vec);

//	Com_Printf ("%f\n", ltime);
	rstep = M_PI/10.0;
	for (i=start_pt ; i<len ; i+=step)
	{
		if (i>step*5) // don't bother after the 5th ring
			break;

		for (rot = 0; rot < M_PI*2; rot += rstep)
		{
			variance = 0.5;
			c = cos(rot)*variance;
			s = sin(rot)*variance;
			
			// trim it so it looks like it's starting at the origin
			if (i < 10)
			{
				VectorScale (right, c*(i/10.0), dir);
				VectorMA (dir, s*(i/10.0), up, dir);
			}
			else
			{
				VectorScale (right, c, dir);
				VectorMA (dir, s, up, dir);
			}

			setupParticle (
				0,	0,	0,
				move[0]+dir[0]*3,	move[1]+dir[1]*3,	move[2]+dir[2]*3,
				0,	0,	0,
				0,		0,		0,
				200+rand()*50,	200+rand()*25,	rand()*50,
				0,	0,	0,
				0.5,		-1000.0,
				GL_SRC_ALPHA, GL_ONE,
				3,			1,			
				particle_blaster,
				0,
				NULL,0);
		}
		VectorAdd (move, vec, move);
	}
}

/*
===============
CL_ParticleSteamEffect

Puffs with velocity along direction, with some randomness thrown in
===============
*/
void CL_ParticleSteamEffect (vec3_t org, vec3_t dir, int color8, int count, int magnitude)
{
	int			i;
	cparticle_t	*p;
	float		d;
	vec3_t		r, u;
	vec3_t color = { color8red(color8), color8green(color8), color8blue(color8)};

//	vectoangles2 (dir, angle_dir);
//	AngleVectors (angle_dir, f, r, u);

	MakeNormalVectors (dir, r, u);

	for (i=0 ; i<count ; i++)
	{
		p = setupParticle (
			0,	0,	0,
			org[0]+magnitude*0.1*crand(),	org[1]+magnitude*0.1*crand(),	org[2]+magnitude*0.1*crand(),
			0,	0,	0,
			0,		0,		0,
			color[0],	color[1],	color[2],
			0,	0,	0,
			0.5,		-1.0 / (0.5 + frand()*0.3),
			GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
			4,			-2,			
			particle_smoke,
			0,
			NULL,0);

		if (!p)
			return;

		VectorScale (dir, magnitude, p->vel);
		d = crand()*magnitude/3;
		VectorMA (p->vel, d, r, p->vel);
		d = crand()*magnitude/3;
		VectorMA (p->vel, d, u, p->vel);
	}
}

void CL_ParticleSteamEffect2 (cl_sustain_t *self)
//vec3_t org, vec3_t dir, int color, int count, int magnitude)
{
	int			i;
	cparticle_t	*p;
	float		d;
	vec3_t		r, u;
	vec3_t		dir;
	int index;

//	vectoangles2 (dir, angle_dir);
//	AngleVectors (angle_dir, f, r, u);

	VectorCopy (self->dir, dir);
	MakeNormalVectors (dir, r, u);

	for (i=0 ; i<self->count ; i++)
	{
		index = rand()&255;
		p = setupParticle (
			0,	0,	0,
			self->org[0] + self->magnitude*0.1*crand(),	self->org[1] + self->magnitude*0.1*crand(),	self->org[2] + self->magnitude*0.1*crand(),
			0,	0,	0,
			0,		0,		0,
			index,	index,	index,
			0,	0,	0,
			1.0,		-1.0 / (0.5 + frand()*0.3),
			GL_SRC_ALPHA, GL_ONE,
			4,			0,			
			particle_smoke,
			PART_GRAVITY,
			NULL,0);

		if (!p)
			return;

		VectorScale (dir, self->magnitude, p->vel);
		d = crand()*self->magnitude/3;
		VectorMA (p->vel, d, r, p->vel);
		d = crand()*self->magnitude/3;
		VectorMA (p->vel, d, u, p->vel);
	}
	self->nextthink += self->thinkinterval;
}

/*
===============
CL_TrackerTrail
===============
*/
void CL_TrackerTrail (vec3_t start, vec3_t end)
{
	vec3_t		move;
	vec3_t		vec;
	vec3_t		forward,right,up,angle_dir;
	float		len;
	cparticle_t	*p;
	int			dec;
	float		dist;

	VectorCopy (start, move);
	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	VectorCopy(vec, forward);
	vectoangles2 (forward, angle_dir);
	AngleVectors (angle_dir, forward, right, up);

	dec = 3;
	VectorScale (vec, 3, vec);

	// FIXME: this is a really silly way to have a loop
	while (len > 0)
	{
		len -= dec;

		p = setupParticle (
			0,	0,	0,
			0,	0,	0,
			0,	0,	5,
			0,		0,		0,
			0,	0,	0,
			0,	0,	0,
			1.0,		-2.0,
			GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
			2,			0,			
			particle_generic,
			0,
			NULL,0);

		if (!p)
			return;

		dist = DotProduct(move, forward);
		VectorMA(move, 8 * cos(dist), up, p->org);

		VectorAdd (move, vec, move);
	}
}

void CL_Tracker_Shell(vec3_t origin)
{
	vec3_t			dir;
	int				i;
	cparticle_t		*p;

	for(i=0;i<300;i++)
	{
		p = setupParticle (
			0,	0,	0,
			0,	0,	0,
			0,	0,	0,
			0,		0,		0,
			0,	0,	0,
			0,	0,	0,
			1.0,		-2.0,
			GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
			2,			0,			
			particle_generic,
			0,
			NULL,0);

		if (!p)
			return;

		dir[0] = crand();
		dir[1] = crand();
		dir[2] = crand();
		VectorNormalize(dir);
		VectorMA(origin, 40, dir, p->org);
	}
}

void CL_MonsterPlasma_Shell(vec3_t origin)
{
	vec3_t			dir;
	int				i;
	cparticle_t		*p;

	for(i=0;i<40;i++)
	{
		p = setupParticle (
			0,	0,	0,
			0,	0,	0,
			0,	0,	0,
			0,	0,	0,
			0,	0,	0,
			0,	0,	0,
			1.0,	0,
			GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
			2,		0,			
			particle_generic,
			PART_INSTANT,
			NULL,0);

		if (!p)
			return;

		dir[0] = crand();
		dir[1] = crand();
		dir[2] = crand();
		VectorNormalize(dir);
	
		VectorMA(origin, 10, dir, p->org);
	}
}

void CL_Widowbeamout (cl_sustain_t *self)
{
	vec3_t			dir;
	int				i;
	cparticle_t		*p;
	float			ratio;

	ratio = 1.0 - (((float)self->endtime - (float)cl.time)/2100.0);

	for(i=0;i<300;i++)
	{
		p = setupParticle (
			0,	0,	0,
			0,	0,	0,
			0,	0,	0,
			0,	0,	0,
			0,	0,	0,
			0,	0,	0,
			1.0,	0,
			GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
			2,		0,			
			particle_generic,
			PART_INSTANT,
			NULL,0);

		if (!p)
			return;

		dir[0] = crand();
		dir[1] = crand();
		dir[2] = crand();
		VectorNormalize(dir);
	
		VectorMA(self->org, (45.0 * ratio), dir, p->org);
	}
}

void CL_Nukeblast (cl_sustain_t *self)
{
	vec3_t			dir;
	int				i;
	cparticle_t		*p;
	static int colortable0[4] = {255,	215,	185,	150};
	static int colortable1[4] = {255,	215,	185,	150};
	static int colortable2[4] = {255,	255,	255,	255};
	float			ratio, size;
	int				index;
	int colors[][3] =
	{
		250, 250, 255,
		200, 225, 255,
		150, 175, 255,
		100, 100, 255,
		0
	};

	ratio = 1.0 - (((float)self->endtime - (float)cl.time)/1000.0);
	size = ratio*ratio;

	for(i=0;i<256;i++)
	{
		index = rand()&3;
		p = setupParticle (
			0,	0,	0,
			0,	0,	0,
			0,	0,	0,
			0,	0,	0,
			colors[index][0],	colors[index][1],	colors[index][2],
			0,	0,	0,
			1-size,	0,
			GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
			3*(0.5+ratio*0.5),	-1,			
			particle_generic,
			PART_DIRECTION|PART_INSTANT,
			NULL,0);

		if (!p)
			return;

		VectorSet(dir, crandom(), crandom(), crandom());
		VectorNormalize(dir);

		VectorScale(dir, 50.0*size, p->angle);
		VectorCopy(self->org, p->org);
	}
}

void CL_WidowSplash (vec3_t org)
{
	static int colortable[4] = {2*8,13*8,21*8,18*8};
	int			i;
	cparticle_t	*p;
	vec3_t		dir;

	for (i=0 ; i<256 ; i++)
	{
		p = setupParticle (
			0,	0,	0,
			0,	0,	0,
			0,	0,	0,
			0,	0,	0,
			rand()&255,	rand()&255,	rand()&255,
			0,	0,	0,
			1.0,		-0.8 / (0.5 + frand()*0.3),
			GL_SRC_ALPHA, GL_ONE,
			3,			0,			
			particle_generic,
			0,
			NULL,0);

		if (!p)
			return;

		dir[0] = crand();
		dir[1] = crand();
		dir[2] = crand();
		VectorNormalize(dir);
		VectorMA(org, 45.0, dir, p->org);
		VectorMA(vec3_origin, 40.0, dir, p->vel);
	}

}

void CL_Tracker_Explode(vec3_t	origin)
{
	vec3_t			dir, backdir;
	int				i;
	cparticle_t		*p;

	for(i=0;i<300;i++)
	{
		p = setupParticle (
			0,	0,	0,
			0,	0,	0,
			0,	0,	0,
			0,		0,		20,
			0,	0,	0,
			0,	0,	0,
			1.0,		-1.0,
			GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
			1.5,			0,			
			particle_generic,
			0,
			NULL,0);

		if (!p)
			return;

		dir[0] = crand();
		dir[1] = crand();
		dir[2] = crand();
		VectorNormalize(dir);
		VectorScale(dir, -1, backdir);
	
		VectorMA(origin, 64, dir, p->org);
		VectorScale(backdir, 64, p->vel);
	}
	
}

/*
===============
CL_TagTrail

===============
*/
void CL_TagTrail (vec3_t start, vec3_t end, int color8)
{
	vec3_t		move;
	vec3_t		vec;
	float		len;
	int			dec;
	vec3_t color = { color8red(color8), color8green(color8), color8blue(color8)};

	VectorCopy (start, move);
	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	dec = 5;
	VectorScale (vec, 5, vec);

	while (len >= 0)
	{
		len -= dec;

		setupParticle (
			0,	0,	0,
			move[0] + crand()*16,	move[1] + crand()*16,	move[2] + crand()*16,
			crand()*5,	crand()*5,	crand()*5,
			0,		0,		20,
			color[0],	color[1],	color[2],
			0,	0,	0,
			1.0,		-1.0 / (0.8+frand()*0.2),
			GL_SRC_ALPHA, GL_ONE,
			1.5,			0,			
			particle_generic,
			0,
			NULL,0);

		VectorAdd (move, vec, move);
	}
}

/*
===============
CL_ColorExplosionParticles
===============
*/
void CL_ColorExplosionParticles (vec3_t org, int color8, int run)
{
	int			i;
	vec3_t color = { color8red(color8), color8green(color8), color8blue(color8)};

	for (i=0 ; i<128 ; i++)
	{
		setupParticle (
			0,	0,	0,
			org[0] + ((rand()%32)-16),	org[1] + ((rand()%32)-16),	org[2] + ((rand()%32)-16),
			(rand()%256)-128,	(rand()%256)-128,	(rand()%256)-128,
			0,		0,		20,
			color[0] + (rand() % run),	color[1] + (rand() % run),	color[2] + (rand() % run),
			0,	0,	0,
			1.0,		-0.4 / (0.6 + frand()*0.2),
			GL_SRC_ALPHA, GL_ONE,
			2,			1,			
			particle_generic,
			0,
			NULL,0);
	}
}

/*
===============
CL_ParticleSmokeEffect - like the steam effect, but unaffected by gravity
===============
*/

void pRotateThink (cparticle_t *p, vec3_t org, vec3_t angle, float *alpha, float *size, int *image, float *time)
{
	angle[2] =	angle[0] + *time*angle[1] + *time**time*angle[2];
	p->thinknext=true;
}

void CL_ParticleSmokeEffect (vec3_t org, vec3_t dir, float size)
{
	float alpha = fabs(crand())*0.25 + 0.750;

	setupParticle (
		crand()*180, crand()*100, 0,
		org[0],	org[1],	org[2],
		dir[0],	dir[1],	dir[2],
		0,		0,		10,
		255,	255,	255,
		0,	0,	0,
		alpha,		-1.0,
		GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
		size,			5,			
		particle_smoke,
		PART_SHADED|PART_OVERBRIGHT,
		pRotateThink,true);
}

/*
===============
CL_BlasterParticles2

Wall impact puffs (Green)
===============
*/
#define pBlasterMaxSize 5
void pBlaster2Think (cparticle_t *p, vec3_t org, vec3_t angle, float *alpha, float *size, int *image, float *time)
{
	vec3_t len;
	VectorSubtract(p->angle, org, len);
	
	*size *= (float)(pBlasterMaxSize/VectorLength(len)) * 1.0/((4-*size));
	if (*size > pBlasterMaxSize)
		*size = pBlasterMaxSize;

	p->thinknext = true;
}
void CL_BlasterParticles2 (vec3_t org, vec3_t dir, unsigned int color)
{
	int			i;
	float		d;
	int			count;
	float speed = .75;

	count = 40;
	for (i=0 ; i<count ; i++)
	{
		d = rand()&5;
		setupParticle (
			org[0],	org[1],	org[2],
			org[0]+((rand()&5)-2)+d*dir[0],	org[1]+((rand()&5)-2)+d*dir[1],	org[2]+((rand()&5)-2)+d*dir[2],
			(dir[0]*50 + crand()*20)*speed,	(dir[1]*50 + crand()*20)*speed,	(dir[2]*50 + crand()*20)*speed,
			0,		0,		0,
			50,		235,		50,
			-10,	0,	-10,
			1,		-1.0 / (0.5 + frand()*0.3),
			GL_SRC_ALPHA, GL_ONE,
			4,			-6,			
			particle_generic,
			PART_GRAVITY,
			pBlaster2Think,true);
	}
}

/*
===============
CL_BlasterTrail2

Green!
===============
*/
void CL_BlasterTrail2 (vec3_t start, vec3_t end)
{
	vec3_t		move;
	vec3_t		vec;
	float		len;
	int			dec;

	VectorCopy (start, move);
	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	dec = 5;
	VectorScale (vec, 5, vec);

	// FIXME: this is a really silly way to have a loop
	while (len > 0)
	{
		len -= dec;

		setupParticle (
			0,	0,	0,
			move[0] + crand(),	move[1] + crand(),	move[2] + crand(),
			crand()*5,	crand()*5,	crand()*5,
			0,		0,		0,
			50,		235,		50,
			-10,	0,	-10,
			1,		-1.0 / (0.5 + frand()*0.3),
			GL_SRC_ALPHA, GL_ONE,
			4,			-6,			
			particle_generic,
			0,
			NULL,0);

		VectorAdd (move, vec, move);
	}
}
