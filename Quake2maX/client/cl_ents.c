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
// cl_ents.c -- entity parsing and management

#include "client.h"

trace_t CL_Trace (vec3_t start, vec3_t end, float size, int contentmask);
void CL_AllocClientSideEnt (entity_t *ent, float alpha, float alphavel);

void CL_Shield (vec3_t origin, float angle, int red, int green, int blue, float size, int times);
void CL_Shield_Inside (vec3_t origin, float angle, int red, int green, int blue, float size, int times);
void CL_ForceTrail (vec3_t start, vec3_t end, qboolean light, float size);
void CL_InfernoTrail (vec3_t start, vec3_t end, float size);
void CL_FlameTrail (vec3_t start, vec3_t end, float size, float grow, qboolean light);
void CL_BlueFlameTrail (vec3_t start, vec3_t end);
void CL_Flame (vec3_t start, qboolean light);
void CL_Tracer (vec3_t origin, vec3_t angle, int r, int g, int b, float len, float size);
void CL_BlasterSplash (vec3_t origin, int red, int green, int blue, float size);
void CL_BlasterTracer (vec3_t origin, vec3_t angle, int r, int g, int b, float len, float size);
void CL_SmokeTrail (vec3_t start, vec3_t end);
void CL_SpeedTrail (vec3_t start, vec3_t end);

extern	struct model_s	*cl_mod_powerscreen;
extern	struct model_s *clientGun;

//PGM
int	vidref_val;
//PGM

/*
=========================================================================

FRAME PARSING

=========================================================================
*/

#if 0

typedef struct
{
	int		modelindex;
	int		num; // entity number
	int		effects;
	vec3_t	origin;
	vec3_t	oldorigin;
	vec3_t	angles;
	qboolean present;
} projectile_t;

#define	MAX_PROJECTILES	64
projectile_t	cl_projectiles[MAX_PROJECTILES];

void CL_ClearProjectiles (void)
{
	int i;

	for (i = 0; i < MAX_PROJECTILES; i++) {
//		if (cl_projectiles[i].present)
//			Com_DPrintf("PROJ: %d CLEARED\n", cl_projectiles[i].num);
		cl_projectiles[i].present = false;
	}
}

/*
=====================
CL_ParseProjectiles

Flechettes are passed as efficient temporary entities
=====================
*/
void CL_ParseProjectiles (void)
{
	int		i, c, j;
	byte	bits[8];
	byte	b;
	projectile_t	pr;
	int lastempty = -1;
	qboolean old = false;

	c = MSG_ReadByte (&net_message);
	for (i=0 ; i<c ; i++)
	{
		bits[0] = MSG_ReadByte (&net_message);
		bits[1] = MSG_ReadByte (&net_message);
		bits[2] = MSG_ReadByte (&net_message);
		bits[3] = MSG_ReadByte (&net_message);
		bits[4] = MSG_ReadByte (&net_message);
		pr.origin[0] = ( ( bits[0] + ((bits[1]&15)<<8) ) <<1) - 4096;
		pr.origin[1] = ( ( (bits[1]>>4) + (bits[2]<<4) ) <<1) - 4096;
		pr.origin[2] = ( ( bits[3] + ((bits[4]&15)<<8) ) <<1) - 4096;
		VectorCopy(pr.origin, pr.oldorigin);

		if (bits[4] & 64)
			pr.effects = EF_BLASTER;
		else
			pr.effects = 0;

		if (bits[4] & 128) {
			old = true;
			bits[0] = MSG_ReadByte (&net_message);
			bits[1] = MSG_ReadByte (&net_message);
			bits[2] = MSG_ReadByte (&net_message);
			bits[3] = MSG_ReadByte (&net_message);
			bits[4] = MSG_ReadByte (&net_message);
			pr.oldorigin[0] = ( ( bits[0] + ((bits[1]&15)<<8) ) <<1) - 4096;
			pr.oldorigin[1] = ( ( (bits[1]>>4) + (bits[2]<<4) ) <<1) - 4096;
			pr.oldorigin[2] = ( ( bits[3] + ((bits[4]&15)<<8) ) <<1) - 4096;
		}

		bits[0] = MSG_ReadByte (&net_message);
		bits[1] = MSG_ReadByte (&net_message);
		bits[2] = MSG_ReadByte (&net_message);

		pr.angles[0] = 360*bits[0]*DIV256;
		pr.angles[1] = 360*bits[1]*DIV256;
		pr.modelindex = bits[2];

		b = MSG_ReadByte (&net_message);
		pr.num = (b & 0x7f);
		if (b & 128) // extra entity number byte
			pr.num |= (MSG_ReadByte (&net_message) << 7);

		pr.present = true;

		// find if this projectile already exists from previous frame 
		for (j = 0; j < MAX_PROJECTILES; j++) {
			if (cl_projectiles[j].modelindex) {
				if (cl_projectiles[j].num == pr.num) {
					// already present, set up oldorigin for interpolation
					if (!old)
						VectorCopy(cl_projectiles[j].origin, pr.oldorigin);
					cl_projectiles[j] = pr;
					break;
				}
			} else
				lastempty = j;
		}

		// not present previous frame, add it
		if (j == MAX_PROJECTILES) {
			if (lastempty != -1) {
				cl_projectiles[lastempty] = pr;
			}
		}
	}
}

/*
=============
CL_LinkProjectiles

=============
*/
void CL_AddProjectiles (void)
{
	int		i, j;
	projectile_t	*pr;
	entity_t		ent;

	memset (&ent, 0, sizeof(ent));

	for (i=0, pr=cl_projectiles ; i < MAX_PROJECTILES ; i++, pr++)
	{
		// grab an entity to fill in
		if (pr->modelindex < 1)
			continue;
		if (!pr->present) {
			pr->modelindex = 0;
			continue; // not present this frame (it was in the previous frame)
		}

		ent.model = cl.model_draw[pr->modelindex];

		// interpolate origin
		for (j=0 ; j<3 ; j++)
		{
			ent.origin[j] = ent.oldorigin[j] = pr->oldorigin[j] + cl.lerpfrac * 
				(pr->origin[j] - pr->oldorigin[j]);

		}

		if (pr->effects & EF_BLASTER)
			CL_BlasterTrail (pr->oldorigin, ent.origin);
		V_AddLight (pr->origin, 200, 1, 1, 0);

		VectorCopy (pr->angles, ent.angles);
		V_AddEntity (&ent);
	}
}
#endif

/*
=================
CL_ParseEntityBits

Returns the entity number and the header bits
=================
*/
int	bitcounts[32];	/// just for protocol profiling
int CL_ParseEntityBits (unsigned *bits)
{
	unsigned	b, total;
	int			i;
	int			number;

	total = MSG_ReadByte (&net_message);
	if (total & U_MOREBITS1)
	{
		b = MSG_ReadByte (&net_message);
		total |= b<<8;
	}
	if (total & U_MOREBITS2)
	{
		b = MSG_ReadByte (&net_message);
		total |= b<<16;
	}
	if (total & U_MOREBITS3)
	{
		b = MSG_ReadByte (&net_message);
		total |= b<<24;
	}

	// count the bits for net profiling
	for (i=0 ; i<32 ; i++)
		if (total&(1<<i))
			bitcounts[i]++;

	if (total & U_NUMBER16)
		number = MSG_ReadShort (&net_message);
	else
		number = MSG_ReadByte (&net_message);

	*bits = total;

	return number;
}

/*
==================
CL_ParseDelta

Can go from either a baseline or a previous packet_entity
==================
*/
void CL_ParseDelta (entity_state_t *from, entity_state_t *to, int number, int bits)
{
	// set everything to the state we are delta'ing from
	*to = *from;

	VectorCopy (from->origin, to->old_origin);
	to->number = number;

	if (bits & U_MODEL)
		to->modelindex = MSG_ReadByte (&net_message);
	if (bits & U_MODEL2)
		to->modelindex2 = MSG_ReadByte (&net_message);
	if (bits & U_MODEL3)
		to->modelindex3 = MSG_ReadByte (&net_message);
	if (bits & U_MODEL4)
		to->modelindex4 = MSG_ReadByte (&net_message);
		
	if (bits & U_FRAME8)
		to->frame = MSG_ReadByte (&net_message);
	if (bits & U_FRAME16)
		to->frame = MSG_ReadShort (&net_message);

	if ((bits & U_SKIN8) && (bits & U_SKIN16))		//used for laser colors
		to->skinnum = MSG_ReadLong(&net_message);
	else if (bits & U_SKIN8)
		to->skinnum = MSG_ReadByte(&net_message);
	else if (bits & U_SKIN16)
		to->skinnum = MSG_ReadShort(&net_message);

	if ( (bits & (U_EFFECTS8|U_EFFECTS16)) == (U_EFFECTS8|U_EFFECTS16) )
		to->effects = MSG_ReadLong(&net_message);
	else if (bits & U_EFFECTS8)
		to->effects = MSG_ReadByte(&net_message);
	else if (bits & U_EFFECTS16)
		to->effects = MSG_ReadShort(&net_message);

	if ( (bits & (U_RENDERFX8|U_RENDERFX16)) == (U_RENDERFX8|U_RENDERFX16) )
		to->renderfx = MSG_ReadLong(&net_message);
	else if (bits & U_RENDERFX8)
		to->renderfx = MSG_ReadByte(&net_message);
	else if (bits & U_RENDERFX16)
		to->renderfx = MSG_ReadShort(&net_message);

	if (bits & U_ORIGIN1)
		to->origin[0] = MSG_ReadCoord (&net_message);
	if (bits & U_ORIGIN2)
		to->origin[1] = MSG_ReadCoord (&net_message);
	if (bits & U_ORIGIN3)
		to->origin[2] = MSG_ReadCoord (&net_message);
		
	if (bits & U_ANGLE1)
		to->angles[0] = MSG_ReadAngle(&net_message);
	if (bits & U_ANGLE2)
		to->angles[1] = MSG_ReadAngle(&net_message);
	if (bits & U_ANGLE3)
		to->angles[2] = MSG_ReadAngle(&net_message);

	if (bits & U_OLDORIGIN)
		MSG_ReadPos (&net_message, to->old_origin);

	if (bits & U_SOUND)
		to->sound = MSG_ReadByte (&net_message);

	if (bits & U_EVENT)
		to->event = MSG_ReadByte (&net_message);
	else
		to->event = 0;

	if (bits & U_SOLID)
		to->solid = MSG_ReadShort (&net_message);
}

/*
==================
CL_DeltaEntity

Parses deltas from the given base and adds the resulting entity
to the current frame
==================
*/
void CL_DeltaEntity (frame_t *frame, int newnum, entity_state_t *old, int bits)
{
	centity_t	*ent;
	entity_state_t	*state;

	ent = &cl_entities[newnum];

	state = &cl_parse_entities[cl.parse_entities & (MAX_PARSE_ENTITIES-1)];
	cl.parse_entities++;
	frame->num_entities++;

	CL_ParseDelta (old, state, newnum, bits);

	// some data changes will force no lerping
	if (state->modelindex != ent->current.modelindex
		|| state->modelindex2 != ent->current.modelindex2
		|| state->modelindex3 != ent->current.modelindex3
		|| state->modelindex4 != ent->current.modelindex4
		|| abs(state->origin[0] - ent->current.origin[0]) > 512
		|| abs(state->origin[1] - ent->current.origin[1]) > 512
		|| abs(state->origin[2] - ent->current.origin[2]) > 512
		|| state->event == EV_PLAYER_TELEPORT
		|| state->event == EV_OTHER_TELEPORT
		)
	{
		ent->serverframe = -99;
	}

	if (ent->serverframe != cl.frame.serverframe - 1)
	{	// wasn't in last update, so initialize some things
		ent->trailcount = 1024;		// for diminishing rocket / grenade trails
		// duplicate the current state so lerping doesn't hurt anything
		ent->prev = *state;
		if (state->event == EV_OTHER_TELEPORT)
		{
			VectorCopy (state->origin, ent->prev.origin);
			VectorCopy (state->origin, ent->lerp_origin);
		}
		else
		{
			VectorCopy (state->old_origin, ent->prev.origin);
			VectorCopy (state->old_origin, ent->lerp_origin);
		}
	}
	else
	{	// shuffle the last state to previous
		ent->prev = ent->current;
	}

	ent->serverframe = cl.frame.serverframe;
	ent->current = *state;
}

/*
==================
CL_ParsePacketEntities

An svc_packetentities has just been parsed, deal with the
rest of the data stream.
==================
*/
void CL_ParsePacketEntities (frame_t *oldframe, frame_t *newframe)
{
	int			newnum;
	int			bits;
	entity_state_t	*oldstate;
	int			oldindex, oldnum;

	newframe->parse_entities = cl.parse_entities;
	newframe->num_entities = 0;

	// delta from the entities present in oldframe
	oldindex = 0;
	if (!oldframe)
		oldnum = 99999;
	else
	{
		if (oldindex >= oldframe->num_entities)
			oldnum = 99999;
		else
		{
			oldstate = &cl_parse_entities[(oldframe->parse_entities+oldindex) & (MAX_PARSE_ENTITIES-1)];
			oldnum = oldstate->number;
		}
	}

	while (1)
	{
		newnum = CL_ParseEntityBits (&bits);
		if (newnum >= MAX_EDICTS)
			Com_Error (ERR_DROP,"CL_ParsePacketEntities: bad number:%i", newnum);

		if (net_message.readcount > net_message.cursize)
			Com_Error (ERR_DROP,"CL_ParsePacketEntities: end of message");

		if (!newnum)
			break;

		while (oldnum < newnum)
		{	// one or more entities from the old packet are unchanged
			if (cl_shownet->value == 3)
				Com_Printf ("   unchanged: %i\n", oldnum);
			CL_DeltaEntity (newframe, oldnum, oldstate, 0);
			
			oldindex++;

			if (oldindex >= oldframe->num_entities)
				oldnum = 99999;
			else
			{
				oldstate = &cl_parse_entities[(oldframe->parse_entities+oldindex) & (MAX_PARSE_ENTITIES-1)];
				oldnum = oldstate->number;
			}
		}

		if (bits & U_REMOVE)
		{	// the entity present in oldframe is not in the current frame
			if (cl_shownet->value == 3)
				Com_Printf ("   remove: %i\n", newnum);
			if (oldnum != newnum)
				Com_Printf ("U_REMOVE: oldnum != newnum\n");

			oldindex++;

			if (oldindex >= oldframe->num_entities)
				oldnum = 99999;
			else
			{
				oldstate = &cl_parse_entities[(oldframe->parse_entities+oldindex) & (MAX_PARSE_ENTITIES-1)];
				oldnum = oldstate->number;
			}
			continue;
		}

		if (oldnum == newnum)
		{	// delta from previous state
			if (cl_shownet->value == 3)
				Com_Printf ("   delta: %i\n", newnum);
			CL_DeltaEntity (newframe, newnum, oldstate, bits);

			oldindex++;

			if (oldindex >= oldframe->num_entities)
				oldnum = 99999;
			else
			{
				oldstate = &cl_parse_entities[(oldframe->parse_entities+oldindex) & (MAX_PARSE_ENTITIES-1)];
				oldnum = oldstate->number;
			}
			continue;
		}

		if (oldnum > newnum)
		{	// delta from baseline
			if (cl_shownet->value == 3)
				Com_Printf ("   baseline: %i\n", newnum);
			CL_DeltaEntity (newframe, newnum, &cl_entities[newnum].baseline, bits);
			continue;
		}

	}

	// any remaining entities in the old frame are copied over
	while (oldnum != 99999)
	{	// one or more entities from the old packet are unchanged
		if (cl_shownet->value == 3)
			Com_Printf ("   unchanged: %i\n", oldnum);
		CL_DeltaEntity (newframe, oldnum, oldstate, 0);
		
		oldindex++;

		if (oldindex >= oldframe->num_entities)
			oldnum = 99999;
		else
		{
			oldstate = &cl_parse_entities[(oldframe->parse_entities+oldindex) & (MAX_PARSE_ENTITIES-1)];
			oldnum = oldstate->number;
		}
	}
}



/*
===================
CL_ParsePlayerstate
===================
*/
void CL_ParsePlayerstate (frame_t *oldframe, frame_t *newframe)
{
	int			flags;
	player_state_t	*state;
	int			i;
	int			statbits;

	state = &newframe->playerstate;

	// clear to old value before delta parsing
	if (oldframe)
		*state = oldframe->playerstate;
	else
		memset (state, 0, sizeof(*state));

	flags = MSG_ReadShort (&net_message);

	//
	// parse the pmove_state_t
	//
	if (flags & PS_M_TYPE)
		state->pmove.pm_type = MSG_ReadByte (&net_message);

	if (flags & PS_M_ORIGIN)
	{
		state->pmove.origin[0] = MSG_ReadShort (&net_message);
		state->pmove.origin[1] = MSG_ReadShort (&net_message);
		state->pmove.origin[2] = MSG_ReadShort (&net_message);
	}

	if (flags & PS_M_VELOCITY)
	{
		state->pmove.velocity[0] = MSG_ReadShort (&net_message);
		state->pmove.velocity[1] = MSG_ReadShort (&net_message);
		state->pmove.velocity[2] = MSG_ReadShort (&net_message);
	}

	if (flags & PS_M_TIME)
		state->pmove.pm_time = MSG_ReadByte (&net_message);

	if (flags & PS_M_FLAGS)
		state->pmove.pm_flags = MSG_ReadByte (&net_message);

	if (flags & PS_M_GRAVITY)
		state->pmove.gravity = MSG_ReadShort (&net_message);

	if (flags & PS_M_DELTA_ANGLES)
	{
		state->pmove.delta_angles[0] = MSG_ReadShort (&net_message);
		state->pmove.delta_angles[1] = MSG_ReadShort (&net_message);
		state->pmove.delta_angles[2] = MSG_ReadShort (&net_message);
	}

	if (cl.attractloop)
		state->pmove.pm_type = PM_FREEZE;		// demo playback

	//
	// parse the rest of the player_state_t
	//
	if (flags & PS_VIEWOFFSET)
	{
		state->viewoffset[0] = MSG_ReadChar (&net_message) * 0.25;
		state->viewoffset[1] = MSG_ReadChar (&net_message) * 0.25;
		state->viewoffset[2] = MSG_ReadChar (&net_message) * 0.25;
	}

	if (flags & PS_VIEWANGLES)
	{
		state->viewangles[0] = MSG_ReadAngle16 (&net_message);
		state->viewangles[1] = MSG_ReadAngle16 (&net_message);
		state->viewangles[2] = MSG_ReadAngle16 (&net_message);
	}

	if (flags & PS_KICKANGLES)
	{
		state->kick_angles[0] = MSG_ReadChar (&net_message) * 0.25;
		state->kick_angles[1] = MSG_ReadChar (&net_message) * 0.25;
		state->kick_angles[2] = MSG_ReadChar (&net_message) * 0.25;
	}

	if (flags & PS_WEAPONINDEX)
	{
		state->gunindex = MSG_ReadByte (&net_message);
	}

	if (flags & PS_WEAPONFRAME)
	{
		state->gunframe = MSG_ReadByte (&net_message);
		state->gunoffset[0] = MSG_ReadChar (&net_message)*0.25;
		state->gunoffset[1] = MSG_ReadChar (&net_message)*0.25;
		state->gunoffset[2] = MSG_ReadChar (&net_message)*0.25;
		state->gunangles[0] = MSG_ReadChar (&net_message)*0.25;
		state->gunangles[1] = MSG_ReadChar (&net_message)*0.25;
		state->gunangles[2] = MSG_ReadChar (&net_message)*0.25;
	}

	if (flags & PS_BLEND)
	{
		state->blend[0] = MSG_ReadByte (&net_message)*DIV255;
		state->blend[1] = MSG_ReadByte (&net_message)*DIV255;
		state->blend[2] = MSG_ReadByte (&net_message)*DIV255;
		state->blend[3] = MSG_ReadByte (&net_message)*DIV255;
	}

	if (flags & PS_FOV)
		state->fov = MSG_ReadByte (&net_message);

	if (flags & PS_RDFLAGS)
		state->rdflags = MSG_ReadByte (&net_message);

	// parse stats
	statbits = MSG_ReadLong (&net_message);
	for (i=0 ; i<MAX_STATS ; i++)
		if (statbits & (1<<i) )
			state->stats[i] = MSG_ReadShort(&net_message);
}


/*
==================
CL_FireEntityEvents

==================
*/

void CL_FireEntityEvents (frame_t *frame)
{
	entity_state_t		*s1;
	int					pnum, num;

	for (pnum = 0 ; pnum<frame->num_entities ; pnum++)
	{
		num = (frame->parse_entities + pnum)&(MAX_PARSE_ENTITIES-1);
		s1 = &cl_parse_entities[num];
		if (s1->event)
			CL_EntityEvent (s1);

		//add stains if moving...
		if (s1->origin[0]!=s1->old_origin[0]||
			s1->origin[1]!=s1->old_origin[1]||
			s1->origin[2]!=s1->old_origin[2])
		{
			if (s1->effects & EF_GIB && !r_decals->value)
				re.AddStain(s1->origin, 25, 45, 0 ,0, 150, stain_modulate);
			if (s1->effects & EF_GREENGIB)
				re.AddStain(s1->origin, 25, 0, 60, 0, 150, stain_modulate);
		}

		// EF_TELEPORTER acts like an event, but is not cleared each frame
		if (s1->effects & EF_TELEPORTER)
			CL_TeleporterParticles (s1);
	}
}


/*
================
CL_ParseFrame
================
*/
void CL_ParseFrame (void)
{
	int			cmd;
	int			len;
	frame_t		*old;

	memset (&cl.frame, 0, sizeof(cl.frame));

#if 0
	CL_ClearProjectiles(); // clear projectiles for new frame
#endif

	cl.frame.serverframe = MSG_ReadLong (&net_message);
	cl.frame.deltaframe = MSG_ReadLong (&net_message);
	cl.frame.servertime = cl.frame.serverframe*100;

	// BIG HACK to let old demos continue to work
	if (cls.serverProtocol != 26)
		cl.surpressCount = MSG_ReadByte (&net_message);

	if (cl_shownet->value == 3)
		Com_Printf ("   frame:%i  delta:%i\n", cl.frame.serverframe,
		cl.frame.deltaframe);

	// If the frame is delta compressed from data that we
	// no longer have available, we must suck up the rest of
	// the frame, but not use it, then ask for a non-compressed
	// message 
	if (cl.frame.deltaframe <= 0)
	{
		cl.frame.valid = true;		// uncompressed frame
		old = NULL;
		cls.demowaiting = false;	// we can start recording now
	}
	else
	{
		old = &cl.frames[cl.frame.deltaframe & UPDATE_MASK];
		if (!old->valid)
		{	// should never happen
			Com_Printf ("Delta from invalid frame (not supposed to happen!).\n");
		}
		if (old->serverframe != cl.frame.deltaframe)
		{	// The frame that the server did the delta from
			// is too old, so we can't reconstruct it properly.
			Com_Printf ("Delta frame too old.\n");
		}
		else if (cl.parse_entities - old->parse_entities > MAX_PARSE_ENTITIES-128)
		{
			Com_Printf ("Delta parse_entities too old.\n");
		}
		else
			cl.frame.valid = true;	// valid delta parse
	}

	// clamp time 
	if (cl.time > cl.frame.servertime)
		cl.time = cl.frame.servertime;
	else if (cl.time < cl.frame.servertime - 100)
		cl.time = cl.frame.servertime - 100;

	// read areabits
	len = MSG_ReadByte (&net_message);
	MSG_ReadData (&net_message, &cl.frame.areabits, len);

	// read playerinfo
	cmd = MSG_ReadByte (&net_message);
	SHOWNET(svc_strings[cmd]);
	if (cmd != svc_playerinfo)
		Com_Error (ERR_DROP, "CL_ParseFrame: not playerinfo");
	CL_ParsePlayerstate (old, &cl.frame);

	// read packet entities
	cmd = MSG_ReadByte (&net_message);
	SHOWNET(svc_strings[cmd]);
	if (cmd != svc_packetentities)
		Com_Error (ERR_DROP, "CL_ParseFrame: not packetentities");
	CL_ParsePacketEntities (old, &cl.frame);

#if 0
	if (cmd == svc_packetentities2)
		CL_ParseProjectiles();
#endif

	// save the frame off in the backup array for later delta comparisons
	cl.frames[cl.frame.serverframe & UPDATE_MASK] = cl.frame;

	if (cl.frame.valid)
	{
		// getting a valid frame message ends the connection process
		if (cls.state != ca_active)
		{
			cls.state = ca_active;
			cl.force_refdef = true;
			cl.predicted_origin[0] = cl.frame.playerstate.pmove.origin[0]*0.125;
			cl.predicted_origin[1] = cl.frame.playerstate.pmove.origin[1]*0.125;
			cl.predicted_origin[2] = cl.frame.playerstate.pmove.origin[2]*0.125;
			VectorCopy (cl.frame.playerstate.viewangles, cl.predicted_angles);
			if (cls.disable_servercount != cl.servercount
				&& cl.refresh_prepped)
				SCR_EndLoadingPlaque ();	// get rid of loading plaque
		}
		cl.sound_prepped = true;	// can start mixing ambient sounds
	
		// fire entity events
		CL_FireEntityEvents (&cl.frame);
		CL_CheckPredictionError ();
	}
}

/*
==========================================================================

INTERPOLATE BETWEEN FRAMES TO GET RENDERING PARMS

==========================================================================
*/

struct model_s *S_RegisterSexedModel (entity_state_t *ent, char *base)
{
	int				n;
	char			*p;
	struct model_s	*mdl;
	char			model[MAX_QPATH];
	char			buffer[MAX_QPATH];

	// determine what model the client is using
	model[0] = 0;
	n = CS_PLAYERSKINS + ent->number - 1;
	if (cl.configstrings[n][0])
	{
		p = strchr(cl.configstrings[n], '\\');
		if (p)
		{
			p += 1;
			strcpy(model, p);
			p = strchr(model, '/');
			if (p)
				*p = 0;
		}
	}
	// if we can't figure it out, they're male
	if (!model[0])
		strcpy(model, DEFAULTMODEL);

	Com_sprintf (buffer, sizeof(buffer), "players/%s/%s", model, base+1);
	mdl = re.RegisterModel(buffer);
	if (!mdl) {
		// not found, try default weapon model
		Com_sprintf (buffer, sizeof(buffer), "players/%s/weapon.md2", model);
		mdl = re.RegisterModel(buffer);
		if (!mdl) {
			// no, revert to the male model
			Com_sprintf (buffer, sizeof(buffer), "players/%s/%s", DEFAULTMODEL, base+1);
			mdl = re.RegisterModel(buffer);
			if (!mdl) {
				// last try, default male weapon.md2
				Com_sprintf (buffer, sizeof(buffer), "players/%s/weapon.md2", DEFAULTMODEL);
				mdl = re.RegisterModel(buffer);
			}
		} 
	}

	return mdl;
}

// PMM - used in shell code 
extern int Developer_searchpath (int who);

/*
===============
CL_AddPacketEntities

===============
*/

void AddEntityShells (entity_t *ent, int effects, int renderfx, qboolean isclientviewer, qboolean drawEnt)
{
	if (!drawEnt)
		return;

	if (effects & (EF_COLOR_SHELL|EF_PENT|EF_QUAD)
		&& (!isclientviewer||(cl_3dcam->value && ((cl_predict->value) && !(cl.frame.playerstate.pmove.pm_flags & PMF_NO_PREDICTION)) && !(cl.attractloop && !(cl.cinematictime > 0 && cls.realtime - cl.cinematictime > 1000)))))
	{
		int oldeffects = renderfx;

		if (renderfx & RF_SHELL_HALF_DAM)
		{
			if(Developer_searchpath(2) == 2)
			{
				// ditch the half damage shell if any of red, blue, or double are on
				if (renderfx & (RF_SHELL_RED|RF_SHELL_BLUE|RF_SHELL_DOUBLE))
					renderfx &= ~RF_SHELL_HALF_DAM;
			}
		}

		if (renderfx & RF_SHELL_DOUBLE)
		{
			if(Developer_searchpath(2) == 2)
			{
				// lose the yellow shell if we have a red, blue, or green shell
				if (renderfx & (RF_SHELL_RED|RF_SHELL_BLUE|RF_SHELL_GREEN))
					renderfx &= ~RF_SHELL_DOUBLE;
				// if we have a red shell, turn it to purple by adding blue
				if (renderfx & RF_SHELL_RED)
					renderfx |= RF_SHELL_BLUE;
				// if we have a blue shell (and not a red shell), turn it to cyan by adding green
				else if (renderfx & RF_SHELL_BLUE)
					// go to green if it's on already, otherwise do cyan (flash green)
					if (renderfx & RF_SHELL_GREEN)
						renderfx &= ~RF_SHELL_BLUE;
					else
						renderfx |= RF_SHELL_GREEN;
			}
		}

		ent->flags = renderfx | RF_TRANSLUCENT;
		ent->alpha = 0.30;

		//shell textures
		if (effects & EF_PENT)
		{
			ent->skin = re.RegisterSkin ("gfx/shell_pent.pcx");
			ent->flags = oldeffects | RF_TRANSLUCENT | RF_SHELL_RED;
			V_AddLight (ent->origin, 130, 1, 0.25, 0.25);			
			V_AddLight (ent->origin, 100, 1, 0, 0);

			if (drawEnt)
				V_AddEntity (ent);
		}
		if (effects & EF_QUAD)
		{
			ent->skin = re.RegisterSkin ("gfx/shell_quad.pcx");
			ent->flags = oldeffects | RF_TRANSLUCENT | RF_SHELL_BLUE;				
			V_AddLight (ent->origin, 130, 0.25, 0.5, 1);			
			V_AddLight (ent->origin, 100, 0, 0.25, 1);

			if (drawEnt)
				V_AddEntity (ent);
		}
		if (effects & EF_COLOR_SHELL)
		{
			ent->skin = re.RegisterSkin ("gfx/shell.pcx");

			if (drawEnt)
				V_AddEntity (ent);
		}

		ent->skin = 0;
		ent->flags = 0;
	}

}

void CL_AddPacketEntities (frame_t *frame)
{
	entity_t			ent;
	entity_state_t		*s1;
	float				autorotate;
	int					i;
	int					pnum;
	centity_t			*cent;
	int					autoanim;
	clientinfo_t		*ci;
	unsigned int		effects, renderfx;

	// bonus items rotate at a fixed rate
	autorotate = anglemod(cl.time/10);

	// brush models can auto animate their frames
	autoanim = 2*cl.time/1000;

	memset (&ent, 0, sizeof(ent));

	VectorCopy( vec3_origin, clientOrg);

	for (pnum = 0 ; pnum<frame->num_entities ; pnum++)
	{
		qboolean isclientviewer = false;
		qboolean drawEnt = true;

		s1 = &cl_parse_entities[(frame->parse_entities+pnum)&(MAX_PARSE_ENTITIES-1)];

		cent = &cl_entities[s1->number];

		effects = s1->effects;
		renderfx = s1->renderfx;

		//if i want to multiply alpha, then id better do this...
		ent.alpha = 1;
		//reset this
		ent.renderfx = 0;

		ent.scale = 1;

			// set frame
		if (effects & EF_ANIM01)
			ent.frame = autoanim & 1;
		else if (effects & EF_ANIM23)
			ent.frame = 2 + (autoanim & 1);
		else if (effects & EF_ANIM_ALL)
			ent.frame = autoanim;
		else if (effects & EF_ANIM_ALLFAST)
			ent.frame = cl.time / 100;
		else
			ent.frame = s1->frame;

		if (effects&(EF_PLASMA|EF_HYPERBLASTER|EF_ROCKET|EF_BLASTER))
			ent.renderfx |= RF2_NOSHADOW;


		// quad and pent can do different things on client
		if (!modType("swq"))
		{

			if (effects & EF_DOUBLE)
			{
				effects &= ~EF_DOUBLE;
				effects |= EF_COLOR_SHELL;
				renderfx |= RF_SHELL_DOUBLE;
			}

			if (effects & EF_HALF_DAMAGE)
			{
				effects &= ~EF_HALF_DAMAGE;
				effects |= EF_COLOR_SHELL;
				renderfx |= RF_SHELL_HALF_DAM;
			}
		}


		ent.oldframe = cent->prev.frame;
		ent.backlerp = 1.0 - cl.lerpfrac;

		if (renderfx & (RF_FRAMELERP|RF_BEAM))
		{	// step origin discretely, because the frames
			// do the animation properly
			VectorCopy (cent->current.origin, ent.origin);
			VectorCopy (cent->current.old_origin, ent.oldorigin);
		}
		else
		{	// interpolate origin
			for (i=0 ; i<3 ; i++)
			{
				ent.origin[i] = ent.oldorigin[i] = cent->prev.origin[i] + cl.lerpfrac * 
					(cent->current.origin[i] - cent->prev.origin[i]);
			}
		}

		// create a new entity
	
		// tweak the color of beams
		if ( renderfx & RF_BEAM )
		{	// the four beam colors are encoded in 32 bits of skinnum (hack)
			ent.alpha = 0.30;
			ent.skinnum = (s1->skinnum >> ((rand() % 4)*8)) & 0xff;
			ent.model = NULL;
		}
		else
		{
			// set skin
			if (s1->modelindex == 255)
			{	// use custom player skin
				ent.skinnum = 0;
				ci = &cl.clientinfo[s1->skinnum & 0xff];
				ent.skin = ci->skin;
				ent.model = ci->model;
				if (!ent.skin || !ent.model)
				{
					ent.skin = cl.baseclientinfo.skin;
					ent.model = cl.baseclientinfo.model;
				}

//============
//PGM
				if (renderfx & RF_USE_DISGUISE)
				{
					if(!strncmp((char *)ent.skin, "players/male", 12))
					{
						ent.skin = re.RegisterSkin ("players/male/disguise.pcx");
						ent.model = re.RegisterModel ("players/male/tris.md2");
					}
					else if(!strncmp((char *)ent.skin, "players/female", 14))
					{
						ent.skin = re.RegisterSkin ("players/female/disguise.pcx");
						ent.model = re.RegisterModel ("players/female/tris.md2");
					}
					else if(!strncmp((char *)ent.skin, "players/cyborg", 14))
					{
						ent.skin = re.RegisterSkin ("players/cyborg/disguise.pcx");
						ent.model = re.RegisterModel ("players/cyborg/tris.md2");
					}
				}
//PGM
//============
			}
			else
			{
				ent.skinnum = s1->skinnum;
				ent.skin = NULL;
				ent.model = cl.model_draw[s1->modelindex];
			}
		}

		//**** MODEL SWAPPING ETC *** - per gametype...
		if (ent.model)
		{
			float interpolframe = (float)ent.frame + cl.lerpfrac * (float)(ent.frame - ent.oldframe);
			
			if (modType("gloom"))
			{
				if (	!Q_strcasecmp((char *)ent.model, "sprites/s_firea.sp2")
					||	!Q_strcasecmp((char *)ent.model, "sprites/s_fireb.sp2"))
				{
					CL_FlameTrail (cent->lerp_origin, ent.origin, 25, 25, false);

					VectorCopy (ent.origin, cent->lerp_origin);
					continue;
				}
				else if (!Q_strcasecmp((char *)ent.model, "sprites/s_flame.sp2"))
				{
					CL_Flame (ent.origin, false);

					VectorCopy (ent.origin, cent->lerp_origin);
					continue;
				}

			}
			else if (modType("dday"))
			{
				float size = ( interpolframe )/5.0;

				if (!Q_strcasecmp((char *)ent.model, "models/objects/laser/tris.md2"))
				{
					CL_Tracer(ent.origin, s1->angles, 250, 200, 150, 30, 0.5); //HMG tracer
					
					VectorCopy (ent.origin, cent->lerp_origin);
					continue;
				}
				else if (!Q_strcasecmp((char *)ent.model, "sprites/s_explod.sp2")) //flamethrower
				{
					renderfx |= RF_TRANS_ADDITIVE | RF_TRANSLUCENT;
					ent.alpha = 0.5;
					effects &= ~EF_ROCKET;


					if (size == 0)
					{
						vec3_t vec;
						float len;

						VectorSubtract (cent->lerp_origin, ent.origin, vec);
						len = VectorNormalize (vec);
						VectorScale (vec, 25, vec);
						VectorAdd(vec, cent->lerp_origin, vec);

						CL_BlueFlameTrail(cent->lerp_origin, vec);
					}
					else
					{
						CL_FlameTrail (cent->lerp_origin, ent.origin, 3 + size*25.0, 10.0, true);
					}

					drawEnt = false;	
				}
				else if (!Q_strcasecmp((char *)ent.model, "models/fire/tris.md2")) //flame
				{
					if (ent.frame != 39) //not smoke yet
					{	//make two for flame strength
						CL_Flame (ent.origin, false);
						CL_Flame (ent.origin, false);
						CL_Flame (ent.origin, true);
					}
					else
						CL_SmokeTrail (ent.origin, cent->lerp_origin);

					VectorCopy (ent.origin, cent->lerp_origin);
					continue;
				}
				else if (!Q_strcasecmp((char *)ent.model, "models/objects/laser/tris.md2")) //tracer
				{
					CL_BlasterTracer(ent.origin, s1->angles, 255, 200, 50, 25, 0.5);

					drawEnt = false;
				}

			}
			else if (modType("swq"))
			{
				qboolean lite = s1->frame == 1;

				if (!Q_strcasecmp((char *)ent.model, "models/objects/flame/tris.md2")) //flame
				{
					CL_Flame (ent.origin, true);

					VectorCopy (ent.origin, cent->lerp_origin);
					continue;
				}
				//laser bolts :)
				else if (!Q_strcasecmp((char *)ent.model, "models/objects/swq_laser/tris_beam.md2"))
				{
					if (s1->skinnum==0)			//trooper rifle strong
					{
						if (lite)
							CL_BlasterTracer(ent.origin, s1->angles, 255, 50, 50, 15, 7.5);
						else
							CL_BlasterTracer(ent.origin, s1->angles, 255, 50, 50, 20, 2.5);
						V_AddLight (ent.origin, 130, 1.0, 0.1, 0.1);
					}
					else if (s1->skinnum==1)	//trooper rifle lite
					{
						if (lite)
							CL_BlasterTracer(ent.origin, s1->angles, 255, 100, 100, 12.5, 7.5);
						else
							CL_BlasterTracer(ent.origin, s1->angles, 255, 100, 100, 20, 2.5);
						V_AddLight (ent.origin, 110, 1.0, 0.25, 0.25);
					}
					else if (s1->skinnum==2)	//repeater
					{
						CL_BlasterTracer(ent.origin, s1->angles, 255, 255, 75, 15, 3);
						V_AddLight (ent.origin, 150, 1.0, 1.0, 0.1);
					}
					else if (s1->skinnum==3)	//crossbow
					{
						CL_BlasterTracer(ent.origin, s1->angles, 50, 255, 50, 25, 2.75);
						V_AddLight (ent.origin, 100, 0.1, 1.0, 0.1);
					}
					else if (s1->skinnum==4)	//pistol
					{
						if (lite)
							CL_BlasterTracer(ent.origin, s1->angles, 75, 75, 255, 15, 6);
						else							
							CL_BlasterTracer(ent.origin, s1->angles, 75, 75, 255, 25, 2);
						V_AddLight (ent.origin, 100, 0.1, 0.1, 1.0);
					}
					else	//disruptor
						CL_BlasterTracer(ent.origin, s1->angles, 100, 100, 255, 15, 4);

					VectorCopy (ent.origin, cent->lerp_origin);
					continue;
				}
				else if (!Q_strcasecmp((char *)ent.model, "models/objects/swq_impact/tris.md2")) //impact effect
				{
					float size =  (3- interpolframe )/3.0;

					if (s1->skinnum==0)			//trooper rifle strong
					{
						V_AddLight (ent.origin, 130*size, 1.0, 0.1, 0.1);
						CL_BlasterSplash(ent.origin, 255, 50, 50, 5.0*size);
					}
					else if (s1->skinnum==1)	//trooper rifle lite
					{
						V_AddLight (ent.origin, 110*size, 1.0, 0.25, 0.25);
						CL_BlasterSplash(ent.origin, 255, 100, 100, 5.0*size);
					}
					else if (s1->skinnum==2)	//repeater
					{
						V_AddLight (ent.origin, 150*size, 1.0, 1.0, 0.1);
						CL_BlasterSplash(ent.origin, 255, 255, 75, 7.5*size);
					}
					else if (s1->skinnum==3)	//crossbow
					{
						V_AddLight (ent.origin, 100*size, 0.1, 1.0, 0.1);
						CL_BlasterSplash(ent.origin, 50, 255, 50, 7.5*size);
					}
					else if (s1->skinnum==4)	//pistol
					{
						V_AddLight (ent.origin, 100*size, 0.1, 0.1, 1.0);
						CL_BlasterSplash(ent.origin, 75, 75, 255, 5.0*size);
					}
					else	//disruptor
					{
						V_AddLight (ent.origin, 100*size, 0.1, 0.1, 1.0);
						CL_BlasterSplash(ent.origin, 100, 100, 255, 5.0*size);
					}

					VectorCopy (ent.origin, cent->lerp_origin);
					continue;
				}
				else if (!Q_strcasecmp((char *)ent.model, "sprites/s_inferno.sp2")) //inferno
				{
					float size =  ((interpolframe)/8.0);

					V_AddLight (ent.origin, 200*size, 0.75 + random()*.25, 0.5, 0.1);

					CL_InfernoTrail (cent->lerp_origin, ent.origin , size);

					effects &= ~EF_PLASMA;
					drawEnt = false;
				}
				else if (!Q_strcasecmp((char *)ent.model, "sprites/s_inferno.sp2")) //force inferno
				{
					float size =  ((interpolframe)/8.0);

					V_AddLight (ent.origin, 200*size, 0.75 + random()*.25, 0.5, 0.1);

					CL_InfernoTrail (cent->lerp_origin, ent.origin , size);

					effects &= ~EF_PLASMA;
					drawEnt = false;
				}
				else if (!Q_strcasecmp((char *)ent.model, "sprites/s_f_heal.sp2")) //force heal
				{
					//blue if light, red if dark
					if (s1->frame==0)
					{
						CL_ForceTrail (cent->lerp_origin, ent.origin, true, 4);
						V_AddLight (ent.origin, 150, 0.1, 0.5, 0.75 + random()*.25);
					}
					else						
					{
						CL_ForceTrail (cent->lerp_origin, ent.origin, false, 4);
						V_AddLight (ent.origin, 150, 0.75 + random()*.25, 0.5, 0.1);
					}

					VectorCopy (ent.origin, cent->lerp_origin);
					continue;
				}
			}
			else if (modType("baseq2")) //baseq2 etc
			{
				if (!Q_strcasecmp((char *)ent.model, "models/objects/laser/tris.md2"))
				{
					CL_BlasterTracer(ent.origin, s1->angles, 255, 150, 50, 10, 2);
					drawEnt = false;

				}
			}
		}

		// only used for black hole model right now, FIXME: do better
		if (renderfx == RF_TRANSLUCENT)
			ent.alpha = 0.75;

		// render effects (fullbright, translucent, etc)
		if ((effects & EF_COLOR_SHELL))
			ent.flags = 0;	// renderfx go on color shell entity
		else
			ent.flags = renderfx;

		// RAFAEL
		if (effects & EF_SPINNINGLIGHTS)
		{
			ent.angles[0] = 0;
			ent.angles[1] = anglemod(cl.time/2) + s1->angles[1];
			ent.angles[2] = 180;
			{
				vec3_t forward;
				vec3_t start;

				AngleVectors (ent.angles, forward, NULL, NULL);
				VectorMA (ent.origin, 64, forward, start);
				V_AddLight (start, 100, 1, 0, 0);
			}
		}
		else
		{	// interpolate angles
			float	a1, a2;

			for (i=0 ; i<3 ; i++)
			{
				a1 = cent->current.angles[i];
				a2 = cent->prev.angles[i];
				ent.angles[i] = LerpAngle (a2, a1, cl.lerpfrac);
			}

			if (effects & EF_ROTATE)
			ent.angles[1] += autorotate;
		}

		if (s1->number == cl.playernum+1)
		{
			ent.flags |= RF_VIEWERMODEL;	// only draw from mirrors
			isclientviewer = true;
			// FIXME: still pass to refresh

			if (effects & EF_FLAG1)
				V_AddLight (ent.origin, 225, 1.0, 0.1, 0.1);
			else if (effects & EF_FLAG2)
				V_AddLight (ent.origin, 225, 0.1, 0.1, 1.0);
			else if (effects & EF_TAGTRAIL)						//PGM
				V_AddLight (ent.origin, 225, 1.0, 1.0, 0.0);	//PGM
			else if (effects & EF_TRACKERTRAIL)					//PGM
				V_AddLight (ent.origin, 225, -1.0, -1.0, -1.0);	//PGM
		}


		if (effects & EF_BFG)
		{
		/*	ent.flags |= RF_TRANSLUCENT;
			ent.alpha = 0.30;*/
			
			ent.flags |= RF_TRANSLUCENT | RF_TRANS_ADDITIVE;
			ent.alpha *= 0.5;
		}

		if (effects & EF_SPHERETRANS)
		{
			ent.flags |= RF_TRANSLUCENT;
			// PMM - *sigh*  yet more EF overloading
			if (effects & EF_TRACKERTRAIL)
				ent.alpha = 0.5;
			else
				ent.alpha *= 0.3;
		}

		if (effects & EF_PLASMA)
		{
			ent.flags |= RF_TRANSLUCENT | RF_TRANS_ADDITIVE;
			ent.alpha *= DIV254BY255;
		}

		//flags for shell swtiches etc...
		if (modType("swq"))
		{
			//shields
			{
				vec3_t shieldOrg;
				float rotate = anglemod((float)(cl.time) / 10.0f);
				#define CLIENT_SHIELD_SIZE 60

				if (isclientviewer && !(cl_3dcam->value && ((cl_predict->value) && !(cl.frame.playerstate.pmove.pm_flags & PMF_NO_PREDICTION)) &&!(cl.attractloop && !(cl.cinematictime > 0 && cls.realtime - cl.cinematictime > 1000))))
				{
					//effect for 1st person w/ shield
					if (effects & EF_DOUBLE) //god mode
						CL_Shield_Inside (shieldOrg, rotate, 100, 255, 100, CLIENT_SHIELD_SIZE,	2);
					if (effects & EF_PENT) //light
						CL_Shield_Inside (shieldOrg, rotate, 050, 100, 255, CLIENT_SHIELD_SIZE,	2);
					if (effects & EF_QUAD) //dark
						CL_Shield_Inside (shieldOrg, rotate, 255, 100, 050, CLIENT_SHIELD_SIZE,	2);
				}
				else
				{
					VectorCopy(cent->lerp_origin, shieldOrg);
					shieldOrg[2]+=10;

					if (effects & EF_DOUBLE) //god mode
						CL_Shield (shieldOrg, rotate, 100, 255, 100, CLIENT_SHIELD_SIZE,	2);
					if (effects & EF_PENT) //light
						CL_Shield (shieldOrg, rotate, 050, 100, 255, CLIENT_SHIELD_SIZE,	2);
					if (effects & EF_QUAD) //dark
						CL_Shield (shieldOrg, rotate, 255, 100, 050, CLIENT_SHIELD_SIZE,	2);
				}
			}
		}

		// if set to invisible, skip
		if (!s1->modelindex)
			continue;

		// add to refresh list
		if (drawEnt)
			V_AddEntity (&ent);

		if (modType("swq"))
		{
			if (effects & EF_HALF_DAMAGE) //trail
			{
				if (s1->modelindex && drawEnt)
				{
					vec3_t origin2, lerp_origin2, move, vec;
					float fullLen, len, dec, oldalpha = ent.alpha;
					int oldflags = ent.flags;

					VectorCopy (cent->lerp_origin, lerp_origin2);
					VectorCopy (ent.origin, origin2);

					VectorSubtract( lerp_origin2, origin2, vec);
					fullLen = len = VectorNormalize(vec)*2;

					if (fullLen)
					{
						dec = fullLen*0.1;

						VectorScale(vec, dec, vec);
						VectorCopy (ent.origin, move);

						ent.flags |= RF_TRANSLUCENT;

						while (len>0)
						{
							len-=dec;

							VectorAdd (move, vec, move);
							VectorCopy(move, ent.origin);

							ent.alpha = oldalpha*(len/fullLen);

							V_AddEntity (&ent);
						}
						
						VectorCopy (lerp_origin2, cent->lerp_origin);
						VectorCopy (origin2, ent.origin);
						ent.alpha = oldalpha;
						ent.flags = oldflags;
					}
				}
			}

			if (strstr (cl.configstrings[CS_MODELS+(s1->modelindex3)], "w_sabrbld.md2"))
				ent.renderfx |= RF2_CAMERAMODEL;
		}

		// color shells generate a seperate entity for the main model
		AddEntityShells (&ent, effects, renderfx, isclientviewer, drawEnt);


		ent.skin = NULL;		// never use a custom skin on others
		ent.skinnum = 0;
		ent.flags = 0;
		ent.alpha = 1;

		// duplicate for linked models
		if (s1->modelindex2)
		{
			if (s1->modelindex2 == 255)
			{	// custom weapon
				ci = &cl.clientinfo[s1->skinnum & 0xff];
				i = (s1->skinnum >> 8); // 0 is default weapon model
				if (!cl_vwep->value || i > MAX_CLIENTWEAPONMODELS - 1)
					i = 0;
				ent.model = ci->weaponmodel[i];
				if (!ent.model) {
					if (i != 0)
						ent.model = ci->weaponmodel[0];
					if (!ent.model)
						ent.model = cl.baseclientinfo.weaponmodel[0];
				}
			}
			else
				ent.model = cl.model_draw[s1->modelindex2];

			// PMM - check for the defender sphere shell .. make it translucent
			// replaces the previous version which used the high bit on modelindex2 to determine transparency
			if (!Q_strcasecmp (cl.configstrings[CS_MODELS+(s1->modelindex2)], "models/items/shell/tris.md2"))
			{
				ent.alpha = 0.32;
				ent.flags |= RF_TRANSLUCENT;
			}

			// pmm
			if (isclientviewer)
				ent.flags |= RF_VIEWERMODEL;


			if (drawEnt)
				V_AddEntity (&ent);

			AddEntityShells (&ent, effects, renderfx, isclientviewer, drawEnt);

			//PGM - make sure these get reset.
			ent.flags = 0;
			ent.alpha = 0;
			//PGM
		}
		if (s1->modelindex3)
		{
			if (modType("swq"))
			{
				if (strstr (cl.configstrings[CS_MODELS+(s1->modelindex3)], "w_sabrbld.md2"))
				{
					int i, size = 75;
					vec3_t saber_origin, light_color;

					vec3_t COLOR_BLUE		={0.1, 0.1, 1.0};
					vec3_t COLOR_GREEN		={0.1, 1.0, 0.1};
					vec3_t COLOR_RED		={1.0, 0.1, 0.1};
					vec3_t COLOR_YELLOW		={1.0, 1.0, 0.1};
					vec3_t COLOR_VIOLET		={1.0, 0.1, 1.0};
					vec3_t COLOR_EMERALD	={0.5, 1.0, 0.3};
					vec3_t COLOR_CRIMSON	={7.0, 0.0, 0.0};
					vec3_t COLOR_GOLD		={1.0, 0.7, 0.1};
					vec3_t COLOR_TEAL		={0.1, 1.0, 1.0};
					vec3_t COLOR_PINK		={1.0, 0.8, 0.8};

					ent.alpha = DIV254BY255;
					ent.flags |= RF_TRANSLUCENT | RF_TRANS_ADDITIVE;
					ent.renderfx |= RF2_NOSHADOW | RF2_CAMERAMODEL;
					ent.skinnum = s1->modelindex4;
					
					AngleVectors (ent.angles, saber_origin, NULL, NULL);
					VectorScale(saber_origin, 20, saber_origin);
					VectorAdd(ent.origin, saber_origin, saber_origin); saber_origin[2] += 15;
					VectorCopy(CL_Trace (ent.origin, saber_origin, 0, 1).endpos, saber_origin);

					for (i=0;i<3;i++)
						switch (ent.skinnum)
						{
							case 0: //blue
								VectorCopy(COLOR_BLUE, light_color);
								break;
							case 1: //green
								VectorCopy(COLOR_GREEN, light_color);
								break;
							case 2: //red
								VectorCopy(COLOR_RED, light_color);
								break;
							case 3: //yellow
								VectorCopy(COLOR_YELLOW, light_color);
								break;
							case 4: //violet
								VectorCopy(COLOR_VIOLET, light_color);
								break;
							case 5: //emerald
								VectorCopy(COLOR_EMERALD, light_color);
								break;
							case 6: //crimson
								VectorCopy(COLOR_CRIMSON, light_color);
								break;
							case 7: //gold
								VectorCopy(COLOR_GOLD, light_color);
								break;
							case 8: //teal
								VectorCopy(COLOR_TEAL, light_color);
								break;
							case 9: //pink
								VectorCopy(COLOR_PINK, light_color);
								break;
							default: //white
								VectorSet(light_color ,1,1,1);
								break;
						}

						
						//strong near colored light
						V_AddLight (saber_origin, size, light_color[0], light_color[1], light_color[2]);
					
						//fading colored light
						V_AddLight (saber_origin, size*3.0,	0.05*(light_color[0]+1),
															0.05*(light_color[1]+1),
															0.05*(light_color[2]+1));

						//outer dim light
						V_AddLight (saber_origin, size*5.0, 0.1,0.1,0.1);
				}
				else
					;
			}

			if (isclientviewer)
			{
				ent.flags |= RF_VIEWERMODEL;
			}

			ent.model = cl.model_draw[s1->modelindex3];

			if (drawEnt)	
				V_AddEntity (&ent);

			AddEntityShells (&ent, effects, renderfx, isclientviewer, drawEnt);
		}
		if (s1->modelindex4)
		{
			if (modType("swq") && strstr (cl.configstrings[CS_MODELS+(s1->modelindex3)], "w_sabrbld.md2"))
				;
			else
			{

				if (isclientviewer)
					ent.flags = RF_VIEWERMODEL;

				ent.model = cl.model_draw[s1->modelindex4];

				if (drawEnt)	
					V_AddEntity (&ent);

				AddEntityShells (&ent, effects, renderfx, isclientviewer, drawEnt);
			}
		}

		if ( effects & EF_POWERSCREEN )
		{
			ent.model = cl_mod_powerscreen;
			ent.oldframe = 0;
			ent.frame = 0;
			ent.flags |= (RF_TRANSLUCENT | RF_SHELL_GREEN);
			ent.alpha = 0.30;
			if (drawEnt)	
				V_AddEntity (&ent);
		}

		// add automatic particle trails
		if ( (effects&~EF_ROTATE) )
		{
			if (effects & EF_ROCKET)
			{
				CL_RocketTrail (cent->lerp_origin, ent.origin, cent);
				V_AddLight (ent.origin, 200, 1, 1, 0.25);
			}
			// PGM - Do not reorder EF_BLASTER and EF_HYPERBLASTER. 
			// EF_BLASTER | EF_TRACKER is a special case for EF_BLASTER2... Cheese!
			else if (effects & EF_BLASTER)
			{
//				CL_BlasterTrail (cent->lerp_origin, ent.origin);
//PGM
				if (effects & EF_TRACKER)	// lame... problematic?
				{
					CL_BlasterTrail2 (cent->lerp_origin, ent.origin);
					V_AddLight (ent.origin, 200, 0, 1, 0);		
				}
				else
				{
					CL_BlasterTrail (cent->lerp_origin, ent.origin);
					V_AddLight (ent.origin, 200, 1, 1, 0);
				}
//PGM
			}
			else if (effects & EF_HYPERBLASTER)
			{
				if (effects & EF_TRACKER)						// PGM	overloaded for blaster2.
					V_AddLight (ent.origin, 200, 0, 1, 0);		// PGM
				else											// PGM
					V_AddLight (ent.origin, 200, 1, 1, 0);
			}
			else if (effects & EF_GIB)
			{
				CL_DiminishingTrail (cent->lerp_origin, ent.origin, cent, effects);
			}
			else if (effects & EF_GRENADE)
			{
				CL_DiminishingTrail (cent->lerp_origin, ent.origin, cent, effects);
			}
			else if (effects & EF_FLIES)
			{
				CL_FlyEffect (cent, ent.origin);
			}
			else if (effects & EF_BFG)
			{
				static int bfg_lightramp[6] = {300, 400, 600, 300, 150, 75};

				if (effects & EF_ANIM_ALLFAST)
				{
					CL_BfgParticles (&ent);
					i = 200;
				}
				else
				{
					i = bfg_lightramp[s1->frame];
				}
				V_AddLight (ent.origin, i, 0, 1, 0);
			}
			// RAFAEL
			else if (effects & EF_TRAP)
			{
				ent.origin[2] += 32;
				CL_TrapParticles (&ent);
				i = (rand()%100) + 100;
				V_AddLight (ent.origin, i, 1, 0.8, 0.1);
			}
			else if (effects & EF_FLAG1)
			{
				CL_FlagTrail (cent->lerp_origin, ent.origin, true);
				V_AddLight (ent.origin, 225, 1, 0.1, 0.1);
			}
			else if (effects & EF_FLAG2)
			{
				CL_FlagTrail (cent->lerp_origin, ent.origin, false);
				V_AddLight (ent.origin, 225, 0.1, 0.1, 1);
			}
//======
//ROGUE
			else if (effects & EF_TAGTRAIL)
			{
				CL_TagTrail (cent->lerp_origin, ent.origin, 220);
				V_AddLight (ent.origin, 225, 1.0, 1.0, 0.0);
			}
			else if (effects & EF_TRACKERTRAIL)
			{
				if (effects & EF_TRACKER)
				{
					float intensity;

					intensity = 50 + (500 * (sin(cl.time/500.0) + 1.0));
					V_AddLight (ent.origin, intensity, -1, -1, -1);
				}
				else
				{
					CL_Tracker_Shell (cent->lerp_origin);
					V_AddLight (ent.origin, 155, -1, -1, -1);
				}
			}
			else if (effects & EF_TRACKER)
			{
				CL_TrackerTrail (cent->lerp_origin, ent.origin);
				V_AddLight (ent.origin, 200, -1, -1, -1);
			}
//ROGUE
//======
			// RAFAEL
			else if (effects & EF_GREENGIB)
			{
				CL_DiminishingTrail (cent->lerp_origin, ent.origin, cent, effects);	
			}
			// RAFAEL
			else if (effects & EF_IONRIPPER)
			{
				CL_IonripperTrail (cent->lerp_origin, ent.origin);
				V_AddLight (ent.origin, 100, 1, 0.5, 0.5);
			}
			// RAFAEL
			else if (effects & EF_BLUEHYPERBLASTER)
			{
				V_AddLight (ent.origin, 200, 0, 0, 1);
			}
			// RAFAEL
			else if (effects & EF_PLASMA)
			{
				if (effects & EF_ANIM_ALLFAST)
				{
					CL_BlasterTrail (cent->lerp_origin, ent.origin);
				}
				V_AddLight (ent.origin, 130, 1, 0.5, 0.5);
			}
		}

		VectorCopy (ent.origin, cent->lerp_origin);
	}
}



/*
==============
CL_AddViewWeapon
==============
*/
void CL_AddViewWeapon (player_state_t *ps, player_state_t *ops)
{
	entity_t	gun;		// view model
	int			i;

	clientGun = NULL;

	//dont draw if outside body...
	if (cl_3dcam->value && ((cl_predict->value) && !(cl.frame.playerstate.pmove.pm_flags & PMF_NO_PREDICTION)) && !(cl.attractloop && !(cl.cinematictime > 0 && cls.realtime - cl.cinematictime > 1000)))
		return;
	// allow the gun to be completely removed
	if (!cl_gun->value)
		return;

	// don't draw gun if in too wide angle view
	if (ps->fov > 180)
		return;

	memset (&gun, 0, sizeof(gun));

	if (gun_model)
		gun.model = gun_model;	// development tool
	else
	{
		gun.model = cl.model_draw[ps->gunindex];
	}
	if (!gun.model)
		return;

	// set up gun position & angles
	for (i=0 ; i<3 ; i++)
	{
		gun.origin[i] = cl.refdef.vieworg[i] + ops->gunoffset[i]
			+ cl.lerpfrac * (ps->gunoffset[i] - ops->gunoffset[i]);
		gun.angles[i] = cl.refdef.viewangles[i] + LerpAngle (ops->gunangles[i],
			ps->gunangles[i], cl.lerpfrac);
	}

	if (gun_frame)
	{
		gun.frame = gun_frame;	// development tool
		gun.oldframe = gun_frame;	// development tool
	}
	else
	{
		gun.frame = ps->gunframe;
		if (gun.frame == 0)
			gun.oldframe = 0;	// just changed weapons, don't lerp from old
		else
			gun.oldframe = ops->gunframe;
	}

	gun.scale = 1;
	gun.flags = RF_MINLIGHT | RF_DEPTHHACK | RF_WEAPONMODEL;
	gun.backlerp = 1.0 - cl.lerpfrac;
	VectorCopy (gun.origin, gun.oldorigin);	// don't lerp at all
	V_AddEntity (&gun);


	//add shells for viewweaps (all of em!)
	{
		int oldeffects = gun.flags, pnum;
		entity_state_t	*s1;

		for (pnum = 0 ; pnum<cl.frame.num_entities ; pnum++)
			if ((s1=&cl_parse_entities[(cl.frame.parse_entities+pnum)&(MAX_PARSE_ENTITIES-1)])->number == cl.playernum+1)
			{
				int effects = s1->renderfx;

				if (effects & (RF_SHELL_RED|RF_SHELL_BLUE|RF_SHELL_GREEN) || s1->effects&(EF_PENT|EF_QUAD))
				{
					if (effects & RF_SHELL_RED)
						gun.flags |= RF_SHELL_RED;
					if (effects & RF_SHELL_BLUE)
						gun.flags |= RF_SHELL_BLUE;
					if (effects & RF_SHELL_GREEN)
						gun.flags |= RF_SHELL_GREEN;

					gun.flags |= RF_TRANSLUCENT;
					gun.alpha = 0.30;
										
					if (s1->effects & EF_COLOR_SHELL && gun.flags & (RF_SHELL_RED|RF_SHELL_BLUE|RF_SHELL_GREEN))
					{
						gun.skin = re.RegisterSkin ("gfx/shell.pcx");

						V_AddEntity (&gun);
					}
					if (s1->effects & EF_PENT)
					{
						gun.skin = re.RegisterSkin ("gfx/shell_pent.pcx");
						gun.flags = oldeffects | RF_TRANSLUCENT | RF_SHELL_RED;

						V_AddEntity (&gun);
					}
					if (s1->effects & EF_QUAD)
					{
						gun.skin = re.RegisterSkin ("gfx/shell_quad.pcx");
						gun.flags = oldeffects | RF_TRANSLUCENT | RF_SHELL_BLUE;

						V_AddEntity (&gun);
					}
				}
			}

		gun.flags = oldeffects;
	}

	clientGun = gun.model;
}


/*
===============
CL_CalcViewValues

Sets cl.refdef view values
===============
*/
void CalcViewerCamTrans(float dist);

void SetUpCamera (void)
{
	if (cl_3dcam->modified)
	{
		if (modType("dday") && cl_3dcam->value)
			Cvar_SetValue("cl_3dcam", 0);
	}

	if (cl_3dcam->value && ((cl_predict->value) && !(cl.frame.playerstate.pmove.pm_flags & PMF_NO_PREDICTION)) && !(cl.attractloop && !(cl.cinematictime > 0 && cls.realtime - cl.cinematictime > 1000)))
	{
		vec3_t end, oldorg, _3dcamposition, _3dcamforward;
		float dist_up, dist_back, angle;

		if (cl_3dcam_dist->value<0)
			Cvar_SetValue( "cl_3dcam_dist", 0 );

		//trig stuff
		angle = M_PI * cl_3dcam_angle->value/180.0f;
		dist_up = cl_3dcam_dist->value * sin( angle  );//* 0.5;
		dist_back =  cl_3dcam_dist->value * cos ( angle );// * 0.5;
		//finish polar vector

		VectorCopy(cl.refdef.vieworg, oldorg);
		if (cl_3dcam_chase->value)
		{
			VectorMA(cl.refdef.vieworg, -dist_back, cl.v_forward, end);
			VectorMA(end, dist_up, cl.v_up, end);

			//move back so looking straight down want make us look towards ourself
		/*	{
				vec3_t temp, temp2;

				vectoangles2(cl.v_forward, temp);
				temp[PITCH]=0;
				temp[ROLL]=0;
				AngleVectors(temp, temp2, NULL, NULL);
				VectorMA(end, -(dist_back), temp2, end);
			}
		*/
			ClipCam (cl.refdef.vieworg, end, _3dcamposition);
		}
		else
		{
			vec3_t temp, viewForward, viewUp;

			vectoangles2(cl.v_forward, temp);
			temp[PITCH]=0;
			temp[ROLL]=0;
			AngleVectors(temp, viewForward, NULL, viewUp);

			VectorScale(viewForward, dist_up*0.5f, _3dcamforward);

			VectorMA(cl.refdef.vieworg, -dist_back, viewForward, end);
			VectorMA(end, dist_up, viewUp, end);
			
			ClipCam (cl.refdef.vieworg, end, _3dcamposition);
		}


		VectorSubtract(_3dcamposition, oldorg, end);

		CalcViewerCamTrans(VectorLength(end));

		if (!cl_3dcam_chase->value)
		{
			vec3_t newDir[2], newPos;

			VectorSubtract(cl.predicted_origin, _3dcamposition, newDir[0]);
			VectorNormalize(newDir[0]);
			vectoangles2(newDir[0],newDir[1]);
			VectorCopy(newDir[1], cl.refdef.viewangles);

			VectorAdd(_3dcamforward, cl.refdef.vieworg, newPos);
			ClipCam (cl.refdef.vieworg, newPos, cl.refdef.vieworg);
		}
		else //now aim at where ever client is...
		{
			vec3_t newDir, dir;

			if (cl_3dcam_adjust->value)
			{
				VectorMA(cl.refdef.vieworg, 8000, cl.v_forward, dir);
				ClipCam (cl.refdef.vieworg, dir, newDir);

				VectorSubtract(newDir, _3dcamposition, dir);
				VectorNormalize(dir);
				vectoangles2(dir, newDir);

				AngleVectors(newDir, cl.v_forward, cl.v_right, cl.v_up);
				VectorCopy(newDir, cl.refdef.viewangles);
			}

			VectorCopy(_3dcamposition, cl.refdef.vieworg);
		}
	}
}

void CL_CalcViewValues (void)
{
	int			i;
	float		lerp, backlerp;
	centity_t	*ent;
	frame_t		*oldframe;
	player_state_t	*ps, *ops;

	// find the previous frame to interpolate from
	ps = &cl.frame.playerstate;
	i = (cl.frame.serverframe - 1) & UPDATE_MASK;
	oldframe = &cl.frames[i];
	if (oldframe->serverframe != cl.frame.serverframe-1 || !oldframe->valid)
		oldframe = &cl.frame;		// previous frame was dropped or involid
	ops = &oldframe->playerstate;

	// see if the player entity was teleported this frame
	if ( fabs(ops->pmove.origin[0] - ps->pmove.origin[0]) > 256*8
		|| abs(ops->pmove.origin[1] - ps->pmove.origin[1]) > 256*8
		|| abs(ops->pmove.origin[2] - ps->pmove.origin[2]) > 256*8)
		ops = ps;		// don't interpolate

	ent = &cl_entities[cl.playernum+1];
	lerp = cl.lerpfrac;

	// calculate the origin
	if ((cl_predict->value) && !(cl.frame.playerstate.pmove.pm_flags & PMF_NO_PREDICTION))
	{	// use predicted values
		unsigned	delta;

		backlerp = 1.0 - lerp;
		for (i=0 ; i<3 ; i++)
		{
			cl.refdef.vieworg[i] = cl.predicted_origin[i] + ops->viewoffset[i] 
				+ cl.lerpfrac * (ps->viewoffset[i] - ops->viewoffset[i])
				- backlerp * cl.prediction_error[i];

			//this smooths out platform riding
			cl.predicted_origin[i] -= backlerp * cl.prediction_error[i];
		}

		// smooth out stair climbing
		delta = cls.realtime - cl.predicted_step_time;
		if (delta < 100)
		{
			cl.refdef.vieworg[2] -= cl.predicted_step * (100 - delta) * 0.01;
			cl.predicted_origin[2] -= cl.predicted_step * (100 - delta) * 0.01;
		}
	}
	else
	{	// just use interpolated values
		for (i=0 ; i<3 ; i++)
			cl.refdef.vieworg[i] = ops->pmove.origin[i]*0.125 + ops->viewoffset[i] 
				+ lerp * (ps->pmove.origin[i]*0.125 + ps->viewoffset[i] 
				- (ops->pmove.origin[i]*0.125 + ops->viewoffset[i]) );
	}

	// if not running a demo or on a locked frame, add the local angle movement
	if ( cl.frame.playerstate.pmove.pm_type < PM_DEAD )
	{	// use predicted values
		for (i=0 ; i<3 ; i++)
			cl.refdef.viewangles[i] = cl.predicted_angles[i];
	}
	else
	{	// just use interpolated values
		for (i=0 ; i<3 ; i++)
			cl.refdef.viewangles[i] = LerpAngle (ops->viewangles[i], ps->viewangles[i], lerp);
	}

	for (i=0 ; i<3 ; i++)
		cl.refdef.viewangles[i] += LerpAngle (ops->kick_angles[i], ps->kick_angles[i], lerp);

	AngleVectors (cl.refdef.viewangles, cl.v_forward, cl.v_right, cl.v_up);

	// interpolate field of view
	cl.refdef.fov_x = ops->fov + lerp * (ps->fov - ops->fov);

	// don't interpolate blend color
	for (i=0 ; i<4 ; i++)
		cl.refdef.blend[i] = ps->blend[i];

	// add the weapon
	CL_AddViewWeapon (ps, ops);
	SetUpCamera();
}

/*
===============
CL_AddEntities

Emits all entities, particles, and lights to the refresh
===============
*/
void CL_AddEntities (void)
{
	if (cls.state != ca_active)
		return;

	if (cl.time > cl.frame.servertime)
	{
		if (cl_showclamp->value)
			Com_Printf ("high clamp %i\n", cl.time - cl.frame.servertime);
		cl.time = cl.frame.servertime;
		cl.lerpfrac = 1.0;
	}
	else if (cl.time < cl.frame.servertime - 100)
	{
		if (cl_showclamp->value)
			Com_Printf ("low clamp %i\n", cl.frame.servertime-100 - cl.time);
		cl.time = cl.frame.servertime - 100;
		cl.lerpfrac = 0;
	}
	else
		cl.lerpfrac = 1.0 - (cl.frame.servertime - cl.time) * 0.01;

	if (cl_timedemo->value)
		cl.lerpfrac = 1.0;

//	CL_AddPacketEntities (&cl.frame);
//	CL_AddTEnts ();
//	CL_AddParticles ();
//	CL_AddDLights ();
//	CL_AddLightStyles ();

	CL_CalcViewValues ();

	// PMM - moved this here so the heat beam has the right values for the vieworg, and can lock the beam to the gun
	CL_AddPacketEntities (&cl.frame);

#if 0
	CL_AddProjectiles ();
#endif

	CL_AddTEnts ();
	CL_AddParticles ();
/*
	if (cl.refdef.rdflags & RDF_IRGOGGLES)
	{	
		CL_ClearDlights();
		CL_AddHeatDLights ();
	}
	else*/
		CL_AddDLights ();

	CL_AddLightStyles ();
}



/*
===============
CL_GetEntitySoundOrigin

Called to get the sound spatialization origin
===============
*/
void CL_GetEntitySoundOrigin (int ent, vec3_t org)
{
	centity_t	*old;

	if (ent < 0 || ent >= MAX_EDICTS)
		Com_Error (ERR_DROP, "CL_GetEntitySoundOrigin: bad ent");
	old = &cl_entities[ent];
	VectorCopy (old->lerp_origin, org);

	// FIXME: bmodel issues...
}
