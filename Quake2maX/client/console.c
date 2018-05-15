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
// console.c

#include "client.h"

console_t	con;

cvar_t		*con_notifytime;


#define		MAXCMDLINE	256
extern	char	key_lines[32][MAXCMDLINE];
extern	int		edit_line;
extern	int		key_linepos;

int stringLength( const char *string );
int stringLengthExtra ( const char *string);
int stringLen (char *string)
{
	return stringLength(string);
}
		
qboolean setParams(char modifier, int *red, int *green, int *blue, int *bold, int *shadow, int *italic, int *reset)
{
	switch (modifier)
	{
		case 'R':
		case 'r':
			*reset = true;
			return true;
		case 'B':
		case 'b':
			if (*bold) 
				*bold = false;
			else 
				*bold=true;
			return true;
		case 'S':
		case 's':
			if (*shadow) 
				*shadow=false; 
			else 
				*shadow=true;
			return true;
		case 'I':
		case 'i':
			if (*italic) 
				*italic=false; 
			else 
				*italic=true;
			return true;
		case '1':	//red
			*red =	255;
			*green=	0;
			*blue =	0;
			return true;
		case '2':	//green
			*red =	0;
			*green=	255;
			*blue =	0;
			return true;
		case '3':	//yellow
			*red =	255;
			*green=	255;
			*blue =	0;
			return true;
		case '4':	//blue
			*red =	0;
			*green=	0;
			*blue =	255;
			return true;
		case '5':	//teal
			*red =	0;
			*green=	255;
			*blue =	255;
			return true;
		case '6':	//pink
			*red =	255;
			*green=	0;
			*blue =	255;
			return true;
		case '7':	//white
			*red =	255;
			*green=	255;
			*blue =	255;
			return true;
		case '8':	//black
			*red =	0;
			*green=	0;
			*blue =	0;
			return true;
		case '9':	//dark red
			*red =	185;
			*green=	0;
			*blue =	0;
			return true;
		case '0':	//gray
			*red =	185;
			*green=	185;
			*blue =	185;
			return true;
	}
	
	return false;
}


void DrawString( int x, int y, const char *string, int alpha )
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
			re.DrawScaledChar( ( x + j*FONT_SIZE+FONT_SIZE/4 ), y+1,  character,1.0f, 0, 0, 0, alpha, italic);
		re.DrawScaledChar( ( x + j*FONT_SIZE ), y, character ,1.0f, red, green, blue, alpha, italic);
	}
}

void DrawAltString( int x, int y, const char *string, int alpha )
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
			re.DrawScaledChar( ( x + j*FONT_SIZE+FONT_SIZE/4 ), y+1,  character,1.0f, 0, 0, 0, alpha, italic);
		re.DrawScaledChar( ( x + j*FONT_SIZE ), y, character ,1.0f, red, green, blue, alpha, italic);
	}
}

void Key_ClearTyping (void)
{
	key_lines[edit_line][1] = 0;	// clear any typing
	key_linepos = 1;
	con.backedit = 0;
}

/*
================
Con_ToggleConsole_f
================
*/
void Con_ToggleConsole_f (void)
{
//	SCR_EndLoadingPlaque ();	// get rid of loading plaque

	if (cls.state == ca_disconnected)
	{
		Cbuf_AddText ("d1\n");
		return;
	}

	Key_ClearTyping ();
	Con_ClearNotify ();

	if (cls.consoleActive)
	{
		cls.consoleActive = false;
		
		if (cls.key_dest != key_menu)
		Cvar_Set ("paused", "0");
	}
	else
	{
		cls.consoleActive = true;

		if (Cvar_VariableValue ("maxclients") == 1 && Com_ServerState () && cls.key_dest != key_menu)
			Cvar_Set ("paused", "1");
	}
}

/*
================
Con_ToggleChat_f
================
*/
void Con_ToggleChat_f (void)
{
	Key_ClearTyping ();

	if (cls.consoleActive)
	{
		if (cls.state == ca_active)
		{
			M_ForceMenuOff ();
			cls.consoleActive = false;
			cls.key_dest = key_game;
		}
	}
	else
		cls.consoleActive = true;
	
	Con_ClearNotify ();
}

/*
================
Con_Clear_f
================
*/
void Con_Clear_f (void)
{
	memset (con.text, ' ', CON_TEXTSIZE);
}

						
/*
================
Con_Dump_f

Save the console contents out to a file
================
*/
void Con_Dump_f (void)
{
	int		l, x;
	char	*line;
	FILE	*f;
	char	buffer[1024];
	char	name[MAX_OSPATH];

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("usage: condump <filename>\n");
		return;
	}

	Com_sprintf (name, sizeof(name), "%s/%s.txt", FS_Gamedir(), Cmd_Argv(1));

	Com_Printf ("Dumped console text to %s.\n", name);
	FS_CreatePath (name);
	f = fopen (name, "w");
	if (!f)
	{
		Com_Printf ("^2ERROR: couldn't open.\n");
		return;
	}

	// skip empty lines
	for (l = con.current - con.totallines + 1 ; l <= con.current ; l++)
	{
		line = con.text + (l%con.totallines)*con.linewidth;
		for (x=0 ; x<con.linewidth ; x++)
			if (line[x] != ' ')
				break;
		if (x != con.linewidth)
			break;
	}

	// write the remaining lines
	buffer[con.linewidth] = 0;
	for ( ; l <= con.current ; l++)
	{
		line = con.text + (l%con.totallines)*con.linewidth;
		strncpy (buffer, line, con.linewidth);
		for (x=con.linewidth-1 ; x>=0 ; x--)
		{
			if (buffer[x] == ' ')
				buffer[x] = 0;
			else
				break;
		}
		for (x=0; buffer[x]; x++)
			buffer[x] &= 0x7f;

		fprintf (f, "%s\n", buffer);
	}

	fclose (f);
}

						
/*
================
Con_ClearNotify
================
*/
void Con_ClearNotify (void)
{
	int		i;
	
	for (i=0 ; i<NUM_CON_TIMES ; i++)
		con.times[i] = 0;
}

						
/*
================
Con_MessageMode_f
================
*/
void Con_MessageMode_f (void)
{
	chat_team = false;
	cls.key_dest = key_message;
	cls.consoleActive = false;
}

/*
================
Con_MessageMode2_f
================
*/
void Con_MessageMode2_f (void)
{
	chat_team = true;
	cls.key_dest = key_message;
	cls.consoleActive = false;
}

/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize (void)
{
	int		i, j, width, oldwidth, oldtotallines, numlines, numchars;
	char	tbuf[CON_TEXTSIZE];

	if (con_font_size)
		width = viddef.width/con_font_size->value - 2;
	else
		width = (viddef.width >> 3) - 2;

	if (width == con.linewidth)
		return;

	if (width < 1)			// video hasn't been initialized yet
	{
		width = 38;
		con.linewidth = width;
		con.backedit = 0;
		con.totallines = CON_TEXTSIZE / con.linewidth;
		memset (con.text, ' ', CON_TEXTSIZE);
	}
	else
	{
		oldwidth = con.linewidth;
		con.linewidth = width;
		con.backedit = 0;
		oldtotallines = con.totallines;
		con.totallines = CON_TEXTSIZE / con.linewidth;
		numlines = oldtotallines;

		if (con.totallines < numlines)
			numlines = con.totallines;

		numchars = oldwidth;
	
		if (con.linewidth < numchars)
			numchars = con.linewidth;

		memcpy (tbuf, con.text, CON_TEXTSIZE);
		memset (con.text, ' ', CON_TEXTSIZE);

		for (i=0 ; i<numlines ; i++)
		{
			for (j=0 ; j<numchars ; j++)
			{
				con.text[(con.totallines - 1 - i) * con.linewidth + j] =
						tbuf[((con.current - i + oldtotallines) %
							  oldtotallines) * oldwidth + j];
			}
		}

		Con_ClearNotify ();
	}

	con.current = con.totallines - 1;
	con.display = con.current;
}


/*
================
Con_Init
================
*/
void Con_Init (void)
{
	con.linewidth = -1;
	con.backedit = 0;

	Con_CheckResize ();
	
	Com_Printf ("Console initialized.\n");

//
// register our commands
//
	con_notifytime = Cvar_Get ("con_notifytime", "5", 0);

	Cmd_AddCommand ("toggleconsole", Con_ToggleConsole_f);
	Cmd_AddCommand ("togglechat", Con_ToggleChat_f);
	Cmd_AddCommand ("messagemode", Con_MessageMode_f);
	Cmd_AddCommand ("messagemode2", Con_MessageMode2_f);
	Cmd_AddCommand ("clear", Con_Clear_f);
	Cmd_AddCommand ("condump", Con_Dump_f);
	con.initialized = true;
}


/*
===============
Con_Linefeed
===============
*/
void Con_Linefeed (void)
{
	con.x = 0;
	if (con.display == con.current)
		con.display++;
	con.current++;
	memset (&con.text[(con.current%con.totallines)*con.linewidth]
	, ' ', con.linewidth);
}

/*
================
Con_Print

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the text will appear at the top of the game window
================
*/
void Con_Print (char *txt)
{
	int		y;
	int		c, l;
	static int	cr;
	int		mask;

	if (!con.initialized)
		return;

	if (txt[0] == 1 || txt[0] == 2)
	{
		mask = 128;		// go to colored text
		txt++;
	}
	else
		mask = 0;


	while ( (c = *txt) )
	{
	// count word length
		for (l=0 ; l< con.linewidth ; l++)
			if ( txt[l] <= ' ')
				break;

	// word wrap
		if (l != con.linewidth && (con.x + l > con.linewidth) )
			con.x = 0;

		txt++;

		if (cr)
		{
			con.current--;
			cr = false;
		}

		
		if (!con.x)
		{
			Con_Linefeed ();
		// mark time for transparent overlay
			if (con.current >= 0)
				con.times[con.current % NUM_CON_TIMES] = cls.realtime;
		}

		switch (c)
		{
		case '\n':
			con.x = 0;
			break;

		case '\r':
			con.x = 0;
			cr = 1;
			break;

		default:	// display character and advance
			y = con.current % con.totallines;
			con.text[y*con.linewidth+con.x] = c | mask | con.ormask;
			con.x++;
			if (con.x >= con.linewidth)
				con.x = 0;
			break;
		}
		
	}
}


/*
==============
Con_CenteredPrint
==============
*/
void Con_CenteredPrint (char *text)
{
	int		l;
	char	buffer[1024];

	l = strlen(text);
	l = (con.linewidth-l)/2;
	if (l < 0)
		l = 0;
	memset (buffer, ' ', l);
	strcpy (buffer+l, text);
	strcat (buffer, "\n");
	Con_Print (buffer);
}

/*
==============================================================================

DRAWING

==============================================================================
*/


/*
================
Con_DrawInput

The input line scrolls horizontally if typing goes beyond the right edge
================
*/
void Con_DrawInput (void)
{
	int		y;
	int		i;
	char	*text, output[2048];

	if (!cls.consoleActive && cls.state == ca_active)
		return;		// don't draw anything (always draw if not active)

	text = key_lines[edit_line];

// fill out remainder with spaces
	for (i=key_linepos ; i< con.linewidth ; i++)
		text[i] = ' ';

//	prestep if horizontally scrolling
	if (key_linepos >= con.linewidth)
		text += 1 + key_linepos - con.linewidth;
		
// draw it
	y = con.vislines-FONT_SIZE*2;
	
	Com_sprintf (output, sizeof(output), "");
	for (i=0 ; i<con.linewidth ; i++)
	{
		if (con.backedit == key_linepos-i && ((int)(cls.realtime>>8)&1))
			Com_sprintf (output, sizeof(output), "%s%c", output, 11 );
		else
			Com_sprintf (output, sizeof(output), "%s%c", output, text[i]);
	}



	DrawString ( FONT_SIZE, con.vislines - 2.75*FONT_SIZE, output, 256);

// remove cursor
	key_lines[edit_line][key_linepos] = 0;
}


/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/

void Con_DrawNotify (void)
{
	int		x, xtra;
	char	*text, output[2048];
	int		i,j;
	char	*s;
	int		skip;
	int		alpha;
	float	v, time, lines;

	lines = 0;

	v = 0;
	
	Com_sprintf (output, sizeof(output), "");

	//this is the say msg while typeing...
	if (cls.key_dest == key_message)
	{
		if (chat_team)
			Com_sprintf (output, sizeof(output), "%s", " say_team: ");
		else
			Com_sprintf (output, sizeof(output), "%s", " say: ");

		s = chat_buffer;
		x = 0;
		if (chat_bufferlen > (viddef.width/FONT_SIZE)-(strlen(output)+1))
			x += chat_bufferlen - (int)((viddef.width/FONT_SIZE)-(strlen(output)+1));

		while(s[x])
		{
			if (chat_backedit && chat_backedit == chat_bufferlen-x && ((int)(cls.realtime>>8)&1))
				Com_sprintf (output, sizeof(output), "%s%c", output, 11 );
			else
				Com_sprintf (output, sizeof(output), "%s%c", output, (char)s[x]);

			x++;
		}

		if (!chat_backedit)
			Com_sprintf (output, sizeof(output), "%s%c", output, 10+((int)(cls.realtime>>8)&1) );		

		DrawString (0, v, output, 255);

		v += FONT_SIZE*2; //make extra space so we have room
	}

	for (i= con.current-NUM_CON_TIMES+1 ; i<=con.current ; i++)
	{
		if (i < 0)
			continue;
		time = con.times[i % NUM_CON_TIMES];
		if (time == 0)
			continue;
		time = cls.realtime - time;
		if (time > con_notifytime->value*1000)
			continue;

		//vertical offset set by closest to dissapearing
		lines++;
	}

	if (lines)
		for (j=0, i= con.current-NUM_CON_TIMES+1 ; i<=con.current ; i++, j++)
		{
			if (i < 0)
				continue;
			time = con.times[i % NUM_CON_TIMES];
			if (time == 0)
				continue;
			time = cls.realtime - time;
			if (time > con_notifytime->value*1000)
				continue;

			text = con.text + (i % con.totallines)*con.linewidth;
			

			alpha = 255 * sqrt( (1.0-time/(con_notifytime->value*1000.0+1.0)) * (((float)v+8.0)) / (8.0*lines) );
			if (alpha<0)alpha=0; if (alpha>255)alpha=255;


			Com_sprintf (output, sizeof(output), "");
			for (x = 0 ; x < con.linewidth ; x++)
				Com_sprintf (output, sizeof(output), "%s%c", output, (char)text[x]);

			DrawString (FONT_SIZE/2, v, output, alpha);

			v += FONT_SIZE;
		}
	
	if (v)
	{
		SCR_AddDirtyPoint (0,0);
		SCR_AddDirtyPoint (viddef.width-1, v);
	}
}

/*
================
Con_DrawConsole

Draws the console with the solid background
================
*/
void Con_DrawConsole (float frac, qboolean ingame)
{
	int				i, j, x, y, n;
	int				rows;
	char			*text, output[1024];
	int				row;
	int				lines;
	char			version[64];
	char			dlbar[1024];


	lines = viddef.height * frac;
	if (lines <= 0)
		return;

	if (lines > viddef.height)
		lines = viddef.height;

// draw the background
	re.DrawStretchPic (0, -viddef.height+lines, viddef.width, viddef.height, "conback");
	SCR_AddDirtyPoint (0,0);
	SCR_AddDirtyPoint (viddef.width-1,lines-1);

	Com_sprintf (version, sizeof(version), "^b^s^1%s v%s", GAMENAME,  VERSION);

	DrawString(viddef.width-FONT_SIZE*(stringLen(&version)+1), lines-FONT_SIZE-1, version, 255);

// draw the text
	con.vislines = lines;
	

	rows = (lines-22)/FONT_SIZE;		// rows of text to draw
	y = lines - 3.75*FONT_SIZE;

// draw from the bottom up
	if (con.display != con.current)
	{
	// draw arrows to show the buffer is backscrolled
		for (x=0 ; x<con.linewidth ; x+=FONT_SIZE*2)
			re.DrawChar ( (x+1)*FONT_SIZE, y, '^', 256);
	
		y -= FONT_SIZE;
		rows--;
	}
	


	row = con.display;
	for (i=0 ; i<rows ; i++, y-=FONT_SIZE, row--)
	{
		if (row < 0)
			break;
		if (con.current - row >= con.totallines)
			break;		// past scrollback wrap point
			
		text = con.text + (row % con.totallines)*con.linewidth;


		Com_sprintf (output, sizeof(output), "");
		for (x=0 ; x<con.linewidth ; x++)
			Com_sprintf (output, sizeof(output), "%s%c", output, text[x]);

		DrawString ( 4, y, output, 256);
	}

//ZOID
	// draw the download bar
	// figure out width
	if (cls.download) 
	{
		if ((text = strrchr(cls.downloadname, '/')) != NULL)
			text++;
		else
			text = cls.downloadname;

		x = con.linewidth - ((con.linewidth * 7) / 40);
		y = x - strlen(text) - FONT_SIZE;
		i = con.linewidth/3;
		if (strlen(text) > i) {
			y = x - i - 11;
			strncpy(dlbar, text, i);
			dlbar[i] = 0;
			strcat(dlbar, "...");
		} else
			strcpy(dlbar, text);
		strcat(dlbar, ": ");
		i = strlen(dlbar);
		dlbar[i++] = '\x80';
		// where's the dot go?
		if (cls.downloadpercent == 0)
			n = 0;
		else
			n = y * cls.downloadpercent / 100;
			
		for (j = 0; j < y; j++)
			if (j == n)
				dlbar[i++] = '\x83';
			else
				dlbar[i++] = '\x81';
		dlbar[i++] = '\x82';
		dlbar[i] = 0;

		sprintf(dlbar + strlen(dlbar), " %02d%%", cls.downloadpercent);

		// draw it
		y = con.vislines-12;

		DrawString ( 4, y, dlbar, 256);
	}
//ZOID

// draw the input prompt, user text, and cursor if desired
	Con_DrawInput ();
}


