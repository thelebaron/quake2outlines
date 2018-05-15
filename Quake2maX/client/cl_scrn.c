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
// cl_scrn.c -- master for refresh, status bar, console, chat, notify, etc

/*

  full screen console
  put up loading plaque
  blanked background with loading plaque
  blanked background with menu
  cinematics
  full screen image for quit and victory

  end of unit intermissions

  */

#include "client.h"
#include "qmenu.h"

float		scr_con_current;	// aproaches scr_conlines at scr_conspeed
float		scr_conlines;		// 0.0 to 1.0 lines of console to display

qboolean	scr_initialized;		// ready to draw

int			scr_draw_loading;

vrect_t		scr_vrect;		// position of render window on screen


cvar_t		*scr_viewsize;
cvar_t		*scr_conspeed;
cvar_t		*scr_centertime;
cvar_t		*scr_showturtle;
cvar_t		*scr_showpause;
cvar_t		*scr_printspeed;

cvar_t		*scr_netgraph;
cvar_t		*scr_netgraph_pos;
cvar_t		*scr_timegraph;
cvar_t		*scr_debuggraph;
cvar_t		*scr_graphheight;
cvar_t		*scr_graphscale;
cvar_t		*scr_graphshift;
cvar_t		*scr_drawall;

cvar_t		*inven_pos;

cvar_t		*cl_drawfps;
cvar_t		*cl_demomessage;
cvar_t		*cl_loadpercent;

typedef struct
{
	int		x1, y1, x2, y2;
} dirty_t;

dirty_t		scr_dirty, scr_old_dirty[2];

char		crosshair_pic[MAX_QPATH];
int			crosshair_width, crosshair_height;

void SCR_TimeRefresh_f (void);
void SCR_Loading_f (void);

float hud_char_size = 8.0;

#define HUD_FONT_SIZE hud_char_size/con_font_size->value
#define HUD_FONT_SCALE hud_char_size

void DrawHudString( int x, int y, const char *string, int alpha, float scale)
{
	unsigned i, j;
	char modifier, character;
	int len, red, green, blue, italic, shadow, bold, reset;
	qboolean modified;

	//default
	red = 255;
	green = 255;
	blue = 255;
	italic = false;
	shadow = false;
	bold = false;

	len = strlen( string );
	for ( i = 0, j = 0; i < len; i++ )
	{
		modifier = string[i];
		if (modifier&128) modifier &= ~128;

		if (modifier == '^' && i < len)
		{
			i++;

			reset = 0;
			modifier = string[i];
			if (modifier&128) modifier &= ~128;

			if (modifier!='^')
			{
				modified = setParams(modifier, &red, &green, &blue, &bold, &shadow, &italic, &reset);

				if (reset)
				{
					red = 255;
					green = 255;
					blue = 255;
					italic = false;
					shadow = false;
					bold = false;
				}
				if (modified)
					continue;
				else
					i--;
			}
		}
		j++;

		character = string[i];
		if (bold && character<128)
			character += 128;
		else if (bold && character>128)
			character -= 128;

		if (shadow)
			re.DrawScaledChar( ( x + j*HUD_FONT_SCALE*scale+HUD_FONT_SCALE*scale/4 ), y+1,  character, HUD_FONT_SIZE*scale, 0, 0, 0, alpha, italic);
		re.DrawScaledChar( ( x + j*HUD_FONT_SCALE*scale ), y, character , HUD_FONT_SIZE*scale, red, green, blue, alpha, italic);
	}
}

void DrawAltHudString( int x, int y, const char *string, int alpha, float scale)
{
	unsigned i, j;
	char modifier, character;
	int len, red, green, blue, italic, shadow, bold, reset;
	qboolean modified;


	//default
	red = 255;
	green = 255;
	blue = 255;
	italic = false;
	shadow = false;
	bold = true;

	len = strlen( string );
	for ( i = 0, j = 0; i < len; i++ )
	{
		modifier = string[i];
		if (modifier&128) modifier &= ~128;

		if (modifier == '^' && i < len)
		{
			i++;

			reset = 0;
			modifier = string[i];
			if (modifier&128) modifier &= ~128;

			if (modifier!='^')
			{
				modified = setParams(modifier, &red, &green, &blue, &bold, &shadow, &italic, &reset);

				if (reset)
				{
					red = 255;
					green = 255;
					blue = 255;
					italic = false;
					shadow = false;
					bold = true;
				}
				if (modified)
					continue;
				else
					i--;
			}
		}
		j++;

		character = string[i];
		if (bold && character<128)
			character += 128;
		else if (bold && character>128)
			character -= 128;

		if (shadow)
			re.DrawScaledChar( ( x + j*HUD_FONT_SCALE*scale+HUD_FONT_SCALE*scale/4 ), y+1,  character, HUD_FONT_SIZE*scale, 0, 0, 0, alpha, italic);
		re.DrawScaledChar( ( x + j*HUD_FONT_SCALE*scale ), y, character , HUD_FONT_SIZE*scale, red, green, blue, alpha, italic);
	}
}

/*
================
SCR_showFPS
================
*/
int		fpscounter;
char		fpsText[32];

void SCR_showFPS(void)
{
	int calc;

	if ((cl.time + 1000) < fpscounter)
		fpscounter = cl.time + 100;

	if (cl.time > fpscounter)
	{
		Com_sprintf(fpsText, sizeof(fpsText),"^b^s%3.0ffps", 1/cls.frametime);
		fpscounter = cl.time + 100;
	}
	DrawHudString(viddef.width - 64, 0 , fpsText, 255, 1);
}


/*
===============================================================================

BAR GRAPHS

===============================================================================
*/

/*
==============
CL_AddNetgraph

A new packet was just parsed
==============
*/

int currentping;
void CL_AddNetgraph (void)
{
	int		i;
	int		in;
	int		ping;

	in = cls.netchan.incoming_acknowledged & (CMD_BACKUP-1);
	currentping = cls.realtime - cl.cmd_time[in];

	// if using the debuggraph for something else, don't
	// add the net lines
	if (scr_debuggraph->value || scr_timegraph->value)
		return;

	for (i=0 ; i<cls.netchan.dropped ; i++)
		SCR_DebugGraph (30, 0x40);

	for (i=0 ; i<cl.surpressCount ; i++)
		SCR_DebugGraph (30, 0xdf);

	// see what the latency was on this packet

	ping = currentping/30;
	if (ping > 30)
		ping = 30;
	SCR_DebugGraph (ping, 0xd0);
}


typedef struct
{
	float	value;
	int		color;
} graphsamp_t;

static	int			current;
static	graphsamp_t	values[1024];

/*
==============
SCR_DebugGraph
==============
*/
void SCR_DebugGraph (float value, int color)
{
	values[current&1023].value = value;
	values[current&1023].color = color;
	current++;
}

/*
==============
SCR_DrawDebugGraph
==============
*/
void SCR_DrawDebugGraph (void)
{
	int		a, x, y, w, i, h, min, max;
	float	v;
	int		color;
	static	float lasttime = 0;
	static	int fps, ping;

	h = w = 100;

	if (scr_netgraph_pos->value == 0) //bottom right
	{
		x = scr_vrect.width - (w+2) - 1;
		y = scr_vrect.height - (h+2) - 1;
	}	
	else if (scr_netgraph_pos->value == 1) //bottom left
	{
		x = 0;
		y = scr_vrect.height - (h+2) - 1;
	}	
	else if (scr_netgraph_pos->value == 2) //top right
	{
		x = scr_vrect.width - (w+2) - 1;
		y = 0;
	}
	else //if (scr_netgraph_pos->value == 3) //top left
	{
		x = 0;
		y = 0;
	}

	re.DrawStretchPic (x, y, w+2, h+2, "netgraph");

	for (a=0 ; a<w ; a++)
	{
		i = (current-1-a+1024) & 1023;
		v = values[i].value;
		color = values[i].color;
		v = v*scr_graphscale->value;
		
		if (v < 1)
			v += h * (1+(int)(-v/h));

		max = (int)v % h + 1;
		min = y + h - max - scr_graphshift->value;

		//bind to box!
		if (min<y+1) min = y+1;
		if (min>y+h) min = y+h;
		if (min+max > y+h) max = y+h-max;

		re.DrawFill (x+w-a, min, 1, max, color);
	}

	if (cls.realtime - lasttime > 50)
	{
		lasttime = cls.realtime;
		fps = (cls.frametime)? 1/cls.frametime: 0;
		ping = currentping;
	}

	DrawString(x, y + 5 + FONT_SIZE * 0, va("^sfps: %3i", fps), 255);
	DrawString(x, y + 5 + FONT_SIZE * 1, va("^sping:%3i", ping), 255);

	//border...
	re.DrawFill (x,			y,			(w+2),	1,		0);
	re.DrawFill (x,			y+(h+2),	(w+2),	1,		0);
	re.DrawFill (x,			y,			1,		(h+2),	0);
	re.DrawFill (x+(w+2),	y,			1,		(h+2),	0);
}

/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

char		scr_centerstring[1024];
float		scr_centertime_start;	// for slow victory printing
float		scr_centertime_off;
float		scr_centertime_end;
int			scr_center_lines;
int			scr_erase_center;

/*
==============
SCR_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void SCR_CenterPrint (char *str)
{
	char	*s;
	char	line[64];
	int		i, j, l;

	strncpy (scr_centerstring, str, sizeof(scr_centerstring)-1);
	scr_centertime_off = scr_centertime->value;
	scr_centertime_end = scr_centertime_off;
	scr_centertime_start = cl.time;

	// count the number of lines for centering
	scr_center_lines = 1;
	s = str;
	while (*s)
	{
		if (*s == '\n')
			scr_center_lines++;
		s++;
	}

	// echo it to the console
	Com_Printf("\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");

	s = str;
	do	
	{
	// scan the width of the line
		for (l=0 ; l<40 ; l++)
			if (s[l] == '\n' || !s[l])
				break;
		for (i=0 ; i<(40-l)/2 ; i++)
			line[i] = ' ';

		for (j=0 ; j<l ; j++)
		{
			line[i++] = s[j];
		}

		line[i] = '\n';
		line[i+1] = 0;

		Com_Printf ("%s", line);

		while (*s && *s != '\n')
			s++;

		if (!*s)
			break;
		s++;		// skip the \n
	} while (1);
	Com_Printf("\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");
	Con_ClearNotify ();
}


void SCR_DrawCenterString (void)
{
	char	*start, line[512];
	int		l;
	int		j;
	int		x, y;
	int		remaining;
	//fading centerstring :D
	int		alpha = 254 * sqrt( 1 - (((cl.time-scr_centertime_start)/1000.0)/(scr_centertime_end)));

// the finale prints the characters one at a time
	remaining = 9999;

	scr_erase_center = 0;
	start = scr_centerstring;

	if (scr_center_lines <= 4)
		y = viddef.height*0.35;
	else
		y = 48;


	do	
	{
	// scan the width of the line
		for (l=0 ; l<40 ; l++)
			if (start[l] == '\n' || !start[l])
				break;

		Com_sprintf (line, sizeof(line), "");
		for (j=0 ; j<l ; j++)
		{
			
			Com_sprintf (line, sizeof(line), "%s%c", line, start[j]);
			
			if (!remaining--)
				return;
		}
		DrawString((viddef.width-strlen(line)*FONT_SIZE)*0.5, y, line, alpha);
			
		y += FONT_SIZE;

		while (*start && *start != '\n')
			start++;

		if (!*start)
			break;
		start++;	// skip the \n
	} while (1);

}

void SCR_CheckDrawCenterString (void)
{
	
	scr_centertime_off -= cls.frametime;

	if (scr_centertime_off <= 0)
		return;

	SCR_DrawCenterString ();

}

//=============================================================================

/*
=================
SCR_CalcVrect

Sets scr_vrect, the coordinates of the rendered window
=================
*/
static void SCR_CalcVrect (void)
{
	int		size;

	// bound viewsize
	if (scr_viewsize->value < 40)
		Cvar_Set ("viewsize","40");
	if (scr_viewsize->value > 100)
		Cvar_Set ("viewsize","100");

	size = scr_viewsize->value;

	scr_vrect.width = viddef.width*size/100;
	scr_vrect.width &= ~7;

	scr_vrect.height = viddef.height*size/100;
	scr_vrect.height &= ~1;

	scr_vrect.x = (viddef.width - scr_vrect.width)/2;
	scr_vrect.y = (viddef.height - scr_vrect.height)/2;
}


/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
void SCR_SizeUp_f (void)
{
	Cvar_SetValue ("viewsize",scr_viewsize->value+10);
}


/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
void SCR_SizeDown_f (void)
{
	Cvar_SetValue ("viewsize",scr_viewsize->value-10);
}

/*
=================
SCR_Sky_f

Set a specific sky and rotation speed
=================
*/
void SCR_Sky_f (void)
{
	float	rotate;
	vec3_t	axis;

	if (Cmd_Argc() < 2)
	{
		Com_Printf ("Usage: sky <basename> <rotate> <axis x y z>\n");
		return;
	}
	if (Cmd_Argc() > 2)
		rotate = atof(Cmd_Argv(2));
	else
		rotate = 0;
	if (Cmd_Argc() == 6)
	{
		axis[0] = atof(Cmd_Argv(3));
		axis[1] = atof(Cmd_Argv(4));
		axis[2] = atof(Cmd_Argv(5));
	}
	else
	{
		axis[0] = 0;
		axis[1] = 0;
		axis[2] = 1;
	}

	re.SetSky (Cmd_Argv(1), rotate, axis);
}

//============================================================================

/*
==================
SCR_Init
==================
*/
void SCR_Init (void)
{
	cl_drawfps = Cvar_Get ("cl_drawfps", "0", CVAR_ARCHIVE);
	cl_demomessage = Cvar_Get ("cl_demomessage", "1", CVAR_ARCHIVE);
	cl_loadpercent = Cvar_Get ("cl_loadpercent", "0", CVAR_ARCHIVE);

	inven_pos = Cvar_Get ("inven_pos", "0", CVAR_ARCHIVE);

	scr_viewsize = Cvar_Get ("viewsize", "100", CVAR_ARCHIVE);
	scr_conspeed = Cvar_Get ("scr_conspeed", "3", 0);
	scr_showturtle = Cvar_Get ("scr_showturtle", "0", 0);
	scr_showpause = Cvar_Get ("scr_showpause", "1", 0);
	scr_centertime = Cvar_Get ("scr_centertime", "2.5", 0);
	scr_printspeed = Cvar_Get ("scr_printspeed", "8", 0);

	scr_netgraph = Cvar_Get ("netgraph", "0", CVAR_ARCHIVE);
	scr_netgraph_pos = Cvar_Get ("netgraph_pos", "0", CVAR_ARCHIVE);
	scr_timegraph = Cvar_Get ("timegraph", "0", 0);
	scr_debuggraph = Cvar_Get ("debuggraph", "0", 0);
	scr_graphheight = Cvar_Get ("graphheight", "32", 0);
	scr_graphscale = Cvar_Get ("graphscale", "2", 0);
	scr_graphshift = Cvar_Get ("graphshift", "10", 0);

	scr_drawall = Cvar_Get ("scr_drawall", "0", 0);
//
// register our commands
//
	Cmd_AddCommand ("timerefresh",SCR_TimeRefresh_f);
	Cmd_AddCommand ("loading",SCR_Loading_f);
	Cmd_AddCommand ("sizeup",SCR_SizeUp_f);
	Cmd_AddCommand ("sizedown",SCR_SizeDown_f);
	Cmd_AddCommand ("sky",SCR_Sky_f);

	scr_initialized = true;
}


/*
==============
SCR_DrawNet
==============
*/
void SCR_DrawNet (void)
{
	if (cls.netchan.outgoing_sequence - cls.netchan.incoming_acknowledged 
		< CMD_BACKUP-1)
		return;

	re.DrawPic (scr_vrect.x+64, scr_vrect.y, "net");
}

/*
==============
SCR_DrawPause
==============
*/
void SCR_DrawAlertMessagePicture (char *name, qboolean center)
{
	float ratio, scale;
	int w, h;

	scale = viddef.width/MENU_STATIC_WIDTH;

	re.DrawGetPicSize (&w, &h, name );
	if (w)
	{
		ratio = 35.0/(float)h;
		h = 35;
		w *= ratio;
	}
	else
		return;

	if(center)
		re.DrawStretchPic (
			viddef.width / 2 - w*0.5*scale,
			viddef.height/ 2 - h*0.5*scale,
			scale*w, scale*h, name);
	else
		re.DrawStretchPic (
			viddef.width / 2 - w*0.5*scale,
			scale * 100,
			scale*w, scale*h, name);
}

void SCR_DrawPause (void)
{
	int		w, h;

	if (!scr_showpause->value)		// turn off for screenshots
		return;

	if (!cl_paused->value)
		return;

	if (cls.key_dest == key_menu)
		return;

	SCR_DrawAlertMessagePicture("pause", true);
}

/*
==============
SCR_DrawLoading
==============
*/

void SCR_DrawLoadingBar (float percent, float scale)
{
	re.DrawFill (
		viddef.width*.5-scale*15,	viddef.height*.5 + scale*5, 
		scale*30, scale*2, 0);

	if (re.RegisterPic("bar_background") && re.RegisterPic("bar_loading"))
	{
		re.DrawStretchPic (
			viddef.width*.5-scale*15 + 1,viddef.height*.5 + scale*5+1, 
			scale*30-2, scale*2-2, "bar_background");
		re.DrawStretchPic (
			viddef.width*.5-scale*15 + 1,viddef.height*.5 + scale*5+1, 
			(scale*30-2)*percent*0.01, scale*2-2, "bar_loading");
	}
	else
	{
		re.DrawFill (
			viddef.width*.5-scale*15 + 1,viddef.height*.5 + scale*5+1, 
			scale*30-2, scale*2-2, 3);
		re.DrawFill (
			viddef.width*.5-scale*15 + 1,viddef.height*.5 + scale*5+1, 
			(scale*30-2)*percent*0.01, scale*2-2, 7);
	}
}

void Menu_DrawString( int x, int y, const char *string, int alpha );
int stringLen (char *string);
void SCR_DrawLoading (void)
{
	int		w, h;
	char	mapfile[32];
	qboolean isMap = false;
	float	font_size;

	if (!scr_draw_loading)
		return;
	scr_draw_loading = false;

	//menu font scale stuff...
	menuScale.x = viddef.width;
	menuScale.y = viddef.height;
	menuScale.avg = viddef.width/MENU_STATIC_WIDTH;

	font_size = MENU_FONT_SIZE * menuScale.avg;

	//loading a map...
	if (loadingMessage && cl.configstrings[CS_MODELS+1][0])
	{
		strcpy (mapfile, cl.configstrings[CS_MODELS+1] + 5);	// skip "maps/"
		mapfile[strlen(mapfile)-4] = 0;		// cut off ".bsp"

		if (re.RegisterPic(va("/levelshots/%s.pcx", mapfile)))
			re.DrawStretchPic (0, 0, viddef.width, viddef.height, va("/levelshots/%s.pcx", mapfile));
		else			
			re.DrawFill (0, 0, viddef.width, viddef.height, 0);

		isMap = true;

	}
	else
		re.DrawFill (0, 0, viddef.width, viddef.height, 0);

	re.DrawStretchPic (0, 0, viddef.width, viddef.height, "m_background");

	//loading message stuff...
	if (isMap)
	{
		char *mapmsg;
			
		mapmsg = va("^bLoading Map [^2%s^7]", mapfile);
		Menu_DrawString(
			viddef.width*.5 - font_size*0.5*stringLen(mapmsg), 
			viddef.height*.5 - font_size*5, 
			mapmsg, 255);

		mapmsg = va("^b[^2%s^7]", cl.configstrings[CS_NAME]);
		Menu_DrawString(
			viddef.width*.5 - font_size*0.5*stringLen(mapmsg), 
			viddef.height*.5 - font_size*4, 
			mapmsg, 255);

		Menu_DrawString(
			viddef.width*.5 - font_size*15, 
			viddef.height*.5 - font_size*1, 
			loadingMessages[0], 255);
		Menu_DrawString(
			viddef.width*.5 - font_size*15, 
			viddef.height*.5 - font_size*0, 
			loadingMessages[1], 255);
		Menu_DrawString(
			viddef.width*.5 - font_size*15, 
			viddef.height*.5 + font_size*1, 
			loadingMessages[2], 255);
		Menu_DrawString(
			viddef.width*.5 - font_size*15, 
			viddef.height*.5 + font_size*2, 
			loadingMessages[3], 255);
		Menu_DrawString(
			viddef.width*.5 - font_size*15, 
			viddef.height*.5 + font_size*3, 
			loadingMessages[4], 255);

		SCR_DrawAlertMessagePicture("loading", false);
	}
	else
	{
		char *msg = va("^b^2Awaiting Connection...");

		//draw centered
		Menu_DrawString(
				viddef.width*.5 - font_size*0.5*stringLen(msg), 
				viddef.height*.5 - font_size*0.5, 
				msg, 255);

		SCR_DrawAlertMessagePicture("loading", false);
	}

	// Add Download info stuff...
	if (cls.download)
	{
		char *download = va("Downloading [^2%s^7]", cls.downloadname);

		Menu_DrawString(
			viddef.width*.5 - font_size*0.5*stringLen(download), 
			viddef.height*.5 + font_size*4, 
			download, 255);

		SCR_DrawLoadingBar(cls.downloadpercent, font_size);

		Menu_DrawString(
			viddef.width*.5 - font_size*3, 
			viddef.height*.5 + font_size*5.5, 
			va("%3d%%", (int)cls.downloadpercent), 255);
	}
	else if (isMap) //loading bar...
	{

		SCR_DrawLoadingBar(loadingPercent, font_size);
		
		Menu_DrawString(
			viddef.width*.5 - font_size*3, 
			viddef.height*.5 + font_size*5.5, 
			va("%3d%%", (int)loadingPercent), 255);
	}
}

//=============================================================================

/*
==================
SCR_RunConsole

Scroll it up or down
==================
*/
void SCR_RunConsole (void)
{
// decide on the height of the console
	if (cls.consoleActive)
		scr_conlines = 0.5;		// half screen
	else
		scr_conlines = 0;				// none visible
	
	if (scr_conlines < scr_con_current)
	{
		scr_con_current -= scr_conspeed->value*cls.frametime;
		if (scr_conlines > scr_con_current)
			scr_con_current = scr_conlines;

	}
	else if (scr_conlines > scr_con_current)
	{
		scr_con_current += scr_conspeed->value*cls.frametime;
		if (scr_conlines < scr_con_current)
			scr_con_current = scr_conlines;
	}

}

/*
==================
SCR_DrawConsole
==================
*/
void SCR_DrawConsole (void)
{
	Con_CheckResize ();
	
	//psychospaz -- added black behind console incase trans

	if ((cls.key_dest != key_menu) && (cls.state == ca_disconnected || cls.state == ca_connecting))
	{	// forced full screen console
		re.DrawFill (0, 0, viddef.width, viddef.height, 0);
		Con_DrawConsole (1.0, false);
		return;
	}

	//dont do this if .cin is running
	if ((cls.state != ca_active || !cl.refresh_prepped) && cl.cinematicframe<=0)
	{	// connected, but can't render
		if (scr_draw_loading)
		{
			Con_DrawConsole (0.5, true);
		}
		else
		{
			re.DrawFill (0, 0, viddef.width, viddef.height, 0);
			Con_DrawConsole (0.5, false);
		}
		return;
	}

	if (scr_con_current && cls.consoleActive)
		Con_DrawConsole (scr_con_current, true);
	else if (!cls.consoleActive && !cl.cinematictime && 
		(cls.key_dest == key_game || cls.key_dest == key_message))
		Con_DrawNotify ();	// only draw notify in game

}

//=============================================================================

/*
================
SCR_BeginLoadingPlaque
================
*/
qboolean needLoadingPlaque (void)
{
	if (!cls.disable_screen || !scr_draw_loading)
		return true;
	return false;
}

void SCR_BeginLoadingPlaque (void)
{
	S_StopAllSounds ();
	cl.sound_prepped = false;		// don't play ambients
	CDAudio_Stop ();

	if (developer->value)
		return;

	if (cl.cinematictime > 0)
		scr_draw_loading = 2;	// clear to balack first
	else
		scr_draw_loading = 1;

	SCR_UpdateScreen ();
	cls.disable_screen = Sys_Milliseconds ();
	cls.disable_servercount = cl.servercount;
}

/*
================
SCR_EndLoadingPlaque
================
*/
void SCR_EndLoadingPlaque (void)
{
	cls.disable_screen = 0;
	scr_draw_loading = 0;
	Con_ClearNotify ();

	if (cls.consoleActive)
		Cvar_Set ("paused", "1");
	else
		Cvar_Set ("paused", "0");
}

/*
================
SCR_Loading_f
================
*/
void SCR_Loading_f (void)
{
	SCR_BeginLoadingPlaque ();
}

/*
================
SCR_TimeRefresh_f
================
*/
int entitycmpfnc( const entity_t *a, const entity_t *b )
{
	/*
	** all other models are sorted by model then skin
	*/
	if ( a->model == b->model )
	{
		return ( ( int ) a->skin - ( int ) b->skin );
	}
	else
	{
		return ( ( int ) a->model - ( int ) b->model );
	}
}

void SCR_TimeRefresh_f (void)
{
	int		i;
	int		start, stop;
	float	time;

	if ( cls.state != ca_active )
		return;

	start = Sys_Milliseconds ();

	if (Cmd_Argc() == 2)
	{	// run without page flipping
		re.BeginFrame( 0 );
		for (i=0 ; i<128 ; i++)
		{
			cl.refdef.viewangles[1] = i/128.0*360.0;
			re.RenderFrame (&cl.refdef);
		}
		re.EndFrame();
	}
	else
	{
		for (i=0 ; i<128 ; i++)
		{
			cl.refdef.viewangles[1] = i/128.0*360.0;

			re.BeginFrame( 0 );
			re.RenderFrame (&cl.refdef);
			re.EndFrame();
		}
	}

	stop = Sys_Milliseconds ();
	time = (stop-start)/1000.0;
	Com_Printf ("%f seconds (%f fps)\n", time, 128/time);
}

/*
=================
SCR_AddDirtyPoint
=================
*/
void SCR_AddDirtyPoint (int x, int y)
{
	if (x < scr_dirty.x1)
		scr_dirty.x1 = x;
	if (x > scr_dirty.x2)
		scr_dirty.x2 = x;
	if (y < scr_dirty.y1)
		scr_dirty.y1 = y;
	if (y > scr_dirty.y2)
		scr_dirty.y2 = y;
}

void SCR_DirtyScreen (void)
{
	SCR_AddDirtyPoint (0, 0);
	SCR_AddDirtyPoint (viddef.width-1, viddef.height-1);
}

/*
==============
SCR_TileClear

Clear any parts of the tiled background that were drawn on last frame
==============
*/
void SCR_TileClear (void)
{
	int		i;
	int		top, bottom, left, right;
	dirty_t	clear;

	if (scr_drawall->value)
		SCR_DirtyScreen ();	// for power vr or broken page flippers...

	if (scr_viewsize->value == 100)
		return;		// full screen rendering
	if (cl.cinematictime > 0)
		return;		// full screen cinematic

	// erase rect will be the union of the past three frames
	// so tripple buffering works properly
	clear = scr_dirty;
	for (i=0 ; i<2 ; i++)
	{
		if (scr_old_dirty[i].x1 < clear.x1)
			clear.x1 = scr_old_dirty[i].x1;
		if (scr_old_dirty[i].x2 > clear.x2)
			clear.x2 = scr_old_dirty[i].x2;
		if (scr_old_dirty[i].y1 < clear.y1)
			clear.y1 = scr_old_dirty[i].y1;
		if (scr_old_dirty[i].y2 > clear.y2)
			clear.y2 = scr_old_dirty[i].y2;
	}

	scr_old_dirty[1] = scr_old_dirty[0];
	scr_old_dirty[0] = scr_dirty;

	scr_dirty.x1 = 9999;
	scr_dirty.x2 = -9999;
	scr_dirty.y1 = 9999;
	scr_dirty.y2 = -9999;

	// don't bother with anything convered by the console)
	top = scr_con_current*viddef.height;
	if (top >= clear.y1)
		clear.y1 = top;

	if (clear.y2 <= clear.y1)
		return;		// nothing disturbed

	top = scr_vrect.y;
	bottom = top + scr_vrect.height-1;
	left = scr_vrect.x;
	right = left + scr_vrect.width-1;

	if (clear.y1 < top)
	{	// clear above view screen
		i = clear.y2 < top-1 ? clear.y2 : top-1;
		re.DrawTileClear (clear.x1 , clear.y1,
			clear.x2 - clear.x1 + 1, i - clear.y1+1, "backtile");
		clear.y1 = top;
	}
	if (clear.y2 > bottom)
	{	// clear below view screen
		i = clear.y1 > bottom+1 ? clear.y1 : bottom+1;
		re.DrawTileClear (clear.x1, i,
			clear.x2-clear.x1+1, clear.y2-i+1, "backtile");
		clear.y2 = bottom;
	}
	if (clear.x1 < left)
	{	// clear left of view screen
		i = clear.x2 < left-1 ? clear.x2 : left-1;
		re.DrawTileClear (clear.x1, clear.y1,
			i-clear.x1+1, clear.y2 - clear.y1 + 1, "backtile");
		clear.x1 = left;
	}
	if (clear.x2 > right)
	{	// clear left of view screen
		i = clear.x1 > right+1 ? clear.x1 : right+1;
		re.DrawTileClear (i, clear.y1,
			clear.x2-i+1, clear.y2 - clear.y1 + 1, "backtile");
		clear.x2 = right;
	}

}


//===============================================================


#define STAT_MINUS		10	// num frame for '-' stats digit
char		*sb_nums[2][11] = 
{
	{"num_0", "num_1", "num_2", "num_3", "num_4", "num_5",
	"num_6", "num_7", "num_8", "num_9", "num_minus"},
	{"anum_0", "anum_1", "anum_2", "anum_3", "anum_4", "anum_5",
	"anum_6", "anum_7", "anum_8", "anum_9", "anum_minus"}
};

#define	ICON_WIDTH	24
#define	ICON_HEIGHT	24
#define	CHAR_WIDTH	16
#define	ICON_SPACE	8



/*
================
SizeHUDString

Allow embedded \n in the string
================
*/

void SizeHUDString (char *string, int *w, int *h)
{
	int		lines, width, current;

	lines = 1;
	width = 0;

	current = 0;
	while (*string)
	{
		if (*string == '\n')
		{
			lines++;
			current = 0;
		}
		else
		{
			current++;
			if (current > width)
				width = current;
		}
		string++;
	}

	*w = width * HUD_FONT_SCALE;
	*h = lines * HUD_FONT_SCALE;
}

void _DrawHUDString (char *string, int x, int y, int centerwidth, int xor, float scale)
{
	int		margin;
	char	line[1024];
	int		width;
	int		i, len;

	margin = x;

	len = strlen(string);


	while (*string)
	{
		// scan out one line of text from the string
		width = 0;
		while (*string && *string != '\n')
			line[width++] = *string++;

		line[width] = 0;

		if (centerwidth)
			x = margin + (centerwidth - width*HUD_FONT_SCALE*scale)/2;
		else
			x = margin;
		
		if (xor)
			DrawAltHudString(x, y, line, 255, scale);
		else
			DrawHudString(x, y, line, 255, scale);

		if (*string)
		{
			string++;	// skip the \n
			x = margin;
			y += HUD_FONT_SCALE*scale;
		}
	}
}


/*
==============
SCR_DrawField
==============
*/
void SCR_DrawField (int x, int y, int color, int width, int value, float scale)
{
	char	num[16], *ptr;
	int		l;
	int		frame;

	if (width < 1)
		return;

	// draw number string
	if (width > 5)
		width = 5;

	SCR_AddDirtyPoint (x, y);
	SCR_AddDirtyPoint (x+width*CHAR_WIDTH+2, y+23);

	Com_sprintf (num, sizeof(num), "%i", value);
	l = strlen(num);
	if (l > width)
		l = width;
	x += (2 + CHAR_WIDTH*(width - l))*scale;

	ptr = num;
	while (*ptr && l)
	{
		if (*ptr == '-')
			frame = STAT_MINUS;
		else
			frame = *ptr -'0';

		re.DrawScaledPic (x,y,scale,sb_nums[color][frame]);
		x += CHAR_WIDTH*scale;
		ptr++;
		l--;
	}
}


/*
===============
SCR_TouchPics

Allows rendering code to cache all needed sbar graphics
===============
*/
void SCR_TouchPics (void)
{
	int		i, j;

	for (i=0 ; i<2 ; i++)
		for (j=0 ; j<11 ; j++)
			re.RegisterPic (sb_nums[i][j]);

	if (crosshair->value>0)
	{
		if (crosshair->value > 10)
			crosshair->value = 10;

		Com_sprintf (crosshair_pic, sizeof(crosshair_pic), "ch%i", (int)(crosshair->value-1));
		re.DrawGetPicSize (&crosshair_width, &crosshair_height, crosshair_pic);
		if (!crosshair_width)
			crosshair_pic[0] = 0;
	}
}

/*
================
SCR_ExecuteLayoutString 

================
*/
void SCR_ExecuteLayoutString (char *s, qboolean scaled)
{
	int		x, y;
	int		value;
	char	*token;
	float		width;
	int		index;
	clientinfo_t	*ci;
	float	scale;

	if (cls.state != ca_active || !cl.refresh_prepped)
		return;

	if (!s[0])
		return;

	x = 0;
	y = 0;
	width = 3;

	if (scaled)
		scale = viddef.width/cl_hudres->value;
	else
		scale = 1;

	while (s)
	{
		token = COM_Parse (&s);
		if (!strcmp(token, "xl"))
		{
			token = COM_Parse (&s);
			x = atof(token)*scale;
			continue;
		}
		if (!strcmp(token, "xr"))
		{
			token = COM_Parse (&s);
			x = viddef.width + atof(token)*scale;
			continue;
		}
		if (!strcmp(token, "xv"))
		{
			token = COM_Parse (&s);
			x = viddef.width*0.5 - 160.0*scale + atof(token)*scale;
			continue;
		}

		if (!strcmp(token, "yt"))
		{
			token = COM_Parse (&s);
			y = atof(token)*scale;
			continue;
		}
		if (!strcmp(token, "yb"))
		{
			token = COM_Parse (&s);
			y = viddef.height + atof(token)*scale;
			continue;
		}
		if (!strcmp(token, "yv"))
		{
			token = COM_Parse (&s);
			y = viddef.height*0.5 - 120.0*scale + atof(token)*scale;
			continue;
		}

		if (!strcmp(token, "pic"))
		{	// draw a pic from a stat number
			token = COM_Parse (&s);
			value = cl.frame.playerstate.stats[atoi(token)];
			if (value >= MAX_IMAGES)
				Com_Error (ERR_DROP, "Pic >= MAX_IMAGES");
			if (cl.configstrings[CS_IMAGES+value])
			{
				SCR_AddDirtyPoint (x, y);
				SCR_AddDirtyPoint (x+23.0*scale, y+23.0*scale);
				re.DrawScaledPic (x, y, scale, cl.configstrings[CS_IMAGES+value]);
			}
			continue;
		}

		if (!strcmp(token, "client"))
		{	// draw a deathmatch client block
			int		score, ping, time;

			token = COM_Parse (&s);
			x = viddef.width/2 - 160.0*scale + atoi(token)*scale;
			token = COM_Parse (&s);
			y = viddef.height/2 - 120.0*scale + atoi(token)*scale;
			SCR_AddDirtyPoint (x, y);
			SCR_AddDirtyPoint (x+159.0*scale, y+31.0*scale);

			token = COM_Parse (&s);
			value = atoi(token);
			if (value >= MAX_CLIENTS || value < 0)
				Com_Error (ERR_DROP, "client >= MAX_CLIENTS");
			ci = &cl.clientinfo[value];

			token = COM_Parse (&s);
			score = atoi(token);

			token = COM_Parse (&s);
			ping = atoi(token);

			token = COM_Parse (&s);
			time = atoi(token);

			DrawAltHudString (	x+8*4*scale,				y, ci->name, 255, scale);
			DrawHudString (		x+8*4*scale,				y+8*scale,  "Score: ", 255, scale);
			DrawAltHudString (	x+8*2*scale+7*8*scale,		y+8*scale,  va("%i", score), 255, scale);
			DrawHudString (		x+8*4*scale,				y+8*2*scale, va("Ping:  %i", ping), 255, scale);
			DrawHudString (		x+8*4*scale,				y+8*3*scale, va("Time:  %i", time), 255, scale);

			if (!ci->icon)
				ci = &cl.baseclientinfo;
			re.DrawPic (x, y, ci->iconname);
			continue;
		}

		if (!strcmp(token, "ctf"))
		{	// draw a ctf client block
			int		score, ping;
			char	block[80];

			token = COM_Parse (&s);
			x = viddef.width/2 - 160.0*scale + atoi(token)*scale;
			token = COM_Parse (&s);
			y = viddef.height/2 - 120.0*scale + atoi(token)*scale;
			SCR_AddDirtyPoint (x, y);
			SCR_AddDirtyPoint (x+159.0*scale, y+31.0*scale);

			token = COM_Parse (&s);
			value = atoi(token);
			if (value >= MAX_CLIENTS || value < 0)
				Com_Error (ERR_DROP, "client >= MAX_CLIENTS");
			ci = &cl.clientinfo[value];

			token = COM_Parse (&s);
			score = atoi(token);

			token = COM_Parse (&s);
			ping = atoi(token);
			if (ping > 999)
				ping = 999;

			sprintf(block, "%3d %3d %-12.12s", score, ping, ci->name);

			if (value == cl.playernum)
				DrawAltHudString (x, y, block, 255, scale);
			else
				DrawHudString (x, y, block, 255, scale);
			continue;
		}

		if (!strcmp(token, "picn"))
		{	// draw a pic from a name
			token = COM_Parse (&s);
			SCR_AddDirtyPoint (x, y);
			SCR_AddDirtyPoint (x+23.0*scale, y+23.0*scale);
			re.DrawScaledPic (x, y, scale, token);
			continue;
		}

		if (!strcmp(token, "num"))
		{	// draw a number
			token = COM_Parse (&s);
			width = atoi(token);
			token = COM_Parse (&s);
			value = cl.frame.playerstate.stats[atoi(token)];
			SCR_DrawField (x, y, 0, width, value, scale);
			continue;
		}

		if (!strcmp(token, "hnum"))
		{	// health number
			int		color;

			width = 3;
			value = cl.frame.playerstate.stats[STAT_HEALTH];
			if (value > 25)
				color = 0;	// green
			else
				color = 1;	// red

			if (cl.frame.playerstate.stats[STAT_FLASHES] & 1)
				re.DrawScaledPic (x, y, scale, "field_3");

			SCR_DrawField (x, y, color, width, value, scale);
			continue;
		}

		if (!strcmp(token, "anum"))
		{	// ammo number
			int		color;

			width = 3;
			value = cl.frame.playerstate.stats[STAT_AMMO];
			if (value > 10)
				color = 0;	// green
			else if (value >= 0)
				color = 1;	//red
			else
				continue;	// negative number = don't show

			if (cl.frame.playerstate.stats[STAT_FLASHES] & 4)
				re.DrawScaledPic (x, y, scale, "field_3");

			SCR_DrawField (x, y, color, width, value, scale);
			continue;
		}

		if (!strcmp(token, "rnum"))
		{	// armor number
			int		color;

			width = 3;
			value = cl.frame.playerstate.stats[STAT_ARMOR];
			if (value < 1)
				continue;

			if (value > 25)
				color = 0;	// green
			else
				color = 1;	//red

			if (cl.frame.playerstate.stats[STAT_FLASHES] & 2)
				re.DrawScaledPic (x, y, scale, "field_3");

			SCR_DrawField (x, y, color, width, value, scale);
			continue;
		}


		if (!strcmp(token, "stat_string"))
		{
			token = COM_Parse (&s);
			index = atoi(token);
			if (index < 0 || index >= MAX_CONFIGSTRINGS)
				Com_Error (ERR_DROP, "Bad stat_string index");
			index = cl.frame.playerstate.stats[index];
			if (index < 0 || index >= MAX_CONFIGSTRINGS)
				Com_Error (ERR_DROP, "Bad stat_string index");
			DrawHudString (x, y, cl.configstrings[index], 255, scale);
			continue;
		}

		if (!strcmp(token, "cstring"))
		{
			token = COM_Parse (&s);
			_DrawHUDString (token, x, y, 320, 0, scale);
			continue;
		}

		if (!strcmp(token, "string"))
		{
			token = COM_Parse (&s);
			DrawHudString (x, y, token, 255, scale);
			continue;
		}

		if (!strcmp(token, "cstring2"))
		{
			token = COM_Parse (&s);
			_DrawHUDString (token, x, y, 320,0x80, scale);
			continue;
		}

		if (!strcmp(token, "string2"))
		{
			token = COM_Parse (&s);
			DrawAltHudString (x, y, token, 255, scale);
			continue;
		}

		if (!strcmp(token, "if"))
		{	// draw a number
			token = COM_Parse (&s);
			value = cl.frame.playerstate.stats[atoi(token)];
			if (!value)
			{	// skip to endif
				while (s && strcmp(token, "endif") )
				{
					token = COM_Parse (&s);
				}
			}

			continue;
		}


	}
}


/*
================
SCR_DrawStats

The status bar is a small layout program that
is based on the stats array
================
*/
void SCR_DrawStats (void)
{
	SCR_ExecuteLayoutString (cl.configstrings[CS_STATUSBAR], true);
}


/*
================
SCR_DrawLayout

================
*/
#define	STAT_LAYOUTS		13

void SCR_DrawLayout (void)
{
	if (!cl.frame.playerstate.stats[STAT_LAYOUTS])
		return;
	SCR_ExecuteLayoutString (cl.layout, false);
}

//=======================================================

void DrawDemoMessage (void)
{
	//running demo message
	if ( cl.attractloop && !(cl.cinematictime > 0 && cls.realtime - cl.cinematictime > 1000))
	{
		int i, len;
		char *message = "Running Demo";

		re.DrawFill( 0, viddef.height-FONT_SIZE, viddef.width, FONT_SIZE, 4 ); //back
		re.DrawFill( 0, viddef.height-FONT_SIZE-1, viddef.width, 1, 0 ); //border
		len = strlen(message);
		DrawString(viddef.width/2-len*4, viddef.height - FONT_SIZE, message, 255);

	}
}

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.
==================
*/
void SCR_UpdateScreen (void)
{
	int numframes;
	int i;
	float separation[2] = { 0, 0 };

	// if the screen is disabled (loading plaque is up, or vid mode changing)
	// do nothing at all
	if (cls.disable_screen)
	{
		if (Sys_Milliseconds() - cls.disable_screen > 120000)
		{
			cls.disable_screen = 0;
			Com_Printf ("Loading plaque timed out.\n");
			return;
		}
		scr_draw_loading = 2;
	}

	if (!scr_initialized || !con.initialized)
		return;				// not initialized yet

	/*
	** range check cl_camera_separation so we don't inadvertently fry someone's
	** brain
	*/
	if ( cl_stereo_separation->value > 1.0 )
		Cvar_SetValue( "cl_stereo_separation", 1.0 );
	else if ( cl_stereo_separation->value < 0 )
		Cvar_SetValue( "cl_stereo_separation", 0.0 );

	if ( cl_stereo->value )
	{
		numframes = 2;
		separation[0] = -cl_stereo_separation->value / 2;
		separation[1] =  cl_stereo_separation->value / 2;
	}		
	else
	{
		separation[0] = 0;
		separation[1] = 0;
		numframes = 1;
	}

	for ( i = 0; i < numframes; i++ )
	{
		re.BeginFrame( separation[i] );

		if (scr_draw_loading == 2)
		{	//  loading plaque over black screen
			int		w, h;

			re.CinematicSetPalette(NULL);
			SCR_DrawLoading ();

			if (cls.disable_screen)
				scr_draw_loading = 2;

			if (cls.consoleActive)
				Con_DrawConsole (0.5, false);

			//NO CONSOLE!!!
			continue;
		}
		// if a cinematic is supposed to be running, handle menus
		// and console specially
		else if (cl.cinematictime > 0)
		{
			if (cls.key_dest == key_menu)
			{
				if (cl.cinematicpalette_active)
				{
					re.CinematicSetPalette(NULL);
					cl.cinematicpalette_active = false;
				}
				M_Draw ();
			}
			else
				SCR_DrawCinematic();

		}
		else 
		{

			// make sure the game palette is active
			if (cl.cinematicpalette_active)
			{
				re.CinematicSetPalette(NULL);
				cl.cinematicpalette_active = false;
			}

			// do 3D refresh drawing, and then update the screen
			SCR_CalcVrect ();

			// clear any dirty part of the background
			SCR_TileClear ();

			V_RenderView ( separation[i] );

			SCR_DrawStats ();
			if (cl.frame.playerstate.stats[STAT_LAYOUTS] & 1)
				SCR_DrawLayout ();
			if (cl.frame.playerstate.stats[STAT_LAYOUTS] & 2)
				CL_DrawInventory ();

			SCR_DrawNet ();
			SCR_CheckDrawCenterString ();

			if (scr_timegraph->value)
				SCR_DebugGraph (cls.frametime*300, 0);

			if (scr_debuggraph->value || scr_timegraph->value || scr_netgraph->value)
				SCR_DrawDebugGraph ();
			else if(cl_drawfps->value)
				SCR_showFPS();

			SCR_DrawPause ();

			if (cl_demomessage->value)
				DrawDemoMessage();

			M_Draw ();

			SCR_DrawLoading ();
		}

		SCR_DrawConsole ();
	}
	re.EndFrame();
}
