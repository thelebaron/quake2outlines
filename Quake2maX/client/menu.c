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
#include <ctype.h>
#ifdef _WIN32
#include <io.h>
#endif
#include "client.h"
#include "../client/qmenu.h"

static int	m_main_cursor;

#define NUM_CURSOR_FRAMES 15

#define MOUSEBUTTON1 0
#define MOUSEBUTTON2 1

static char *menu_in_sound		= "misc/menu1.wav";
static char *menu_move_sound	= "misc/menu2.wav";
static char *menu_out_sound		= "misc/menu3.wav";

void Options_MenuDraw (void);
int mouseOverAlpha( menucommon_s *m );

void M_Menu_Main_f (void);
	void M_Menu_Game_f (void);
		void M_Menu_LoadGame_f (void);
		void M_Menu_SaveGame_f (void);
		void M_Menu_PlayerConfig_f (void);
			void M_Menu_DownloadOptions_f (void);
		void M_Menu_Credits_f( void );
	void M_Menu_Multiplayer_f( void );
		void M_Menu_JoinServer_f (void);
			void M_Menu_AddressBook_f( void );
		void M_Menu_StartServer_f (void);
			void M_Menu_DMOptions_f (void);
	void M_Menu_Video_f (void);
	void M_Menu_Options_f (void);
		void M_Menu_Keys_f (void);
	void M_Menu_Quit_f (void);

	void M_Menu_Credits( void );

qboolean	m_entersound;		// play after drawing a frame, so caching
								// won't disrupt the sound
cvar_t		*options_menu;

void	(*m_drawfunc) (void);
const char *(*m_keyfunc) (int key);

//=============================================================================
/* Support Routines */

#define	MAX_MENU_DEPTH	8

typedef struct
{
	void	(*draw) (void);
	const char *(*key) (int k);
} menulayer_t;


typedef struct
{
	float	min[2];
	float max[2];
	int index;
} buttonmenuobject_t;

menulayer_t	m_layers[MAX_MENU_DEPTH];
int		m_menudepth;

float scaledVideo (float param)
{
	return param*menuScale.avg;
}
float videoScale (void)
{
	return menuScale.avg;
}

void addPlayerButton (buttonmenuobject_t *thisObj, int index, float x, float y, float w, float h)
{
	thisObj->min[0] = x;
	thisObj->max[0] = x + w;
	thisObj->min[1] = y;
	thisObj->max[1] = y + h;
	thisObj->index = index;
}

void ActionStartMod (char *mod)
{
	//killserver, start mod, exec configs, and start demos

	Cbuf_AddText ("killserver\n");
	Cbuf_AddText (va("game %s\n", mod));
	Cbuf_AddText ("exec default\n");
	Cbuf_AddText ("exec maxconfig\n");
	Cbuf_AddText ("exec autoexec\n");
	Cbuf_AddText ("d1\n");
}

void M_Banner( char *name )
{
	float ratio;
	int w, h;

	re.DrawGetPicSize (&w, &h, name );
	if (w)
	{
		ratio = 45.0/(float)h;
		h = 45;
		w *= ratio;
	}
	re.DrawStretchPic (
		viddef.width / 2 - scaledVideo(w)/2,
		viddef.height/ 2 - scaledVideo(150),
		scaledVideo(w), scaledVideo(h), name);
}

void refreshCursorButtons(void)
{
	cursor.buttonused[MOUSEBUTTON2] = true;
	cursor.buttonclicks[MOUSEBUTTON2] = 0;
	cursor.buttonused[MOUSEBUTTON1] = true;
	cursor.buttonclicks[MOUSEBUTTON1] = 0;
}

void M_PushMenu ( void (*draw) (void), const char *(*key) (int k) )
{
	int		i;

	if (Cvar_VariableValue ("maxclients") == 1 && Com_ServerState () && !cls.consoleActive)
		Cvar_Set ("paused", "1");

	//clear all sound before menu open....
	if (!m_menudepth)
		CL_Snd_Restart_f();

	// if this menu is already present, drop back to that level
	// to avoid stacking menus by hotkeys
	for (i=0 ; i<m_menudepth ; i++)
		if (m_layers[i].draw == draw && m_layers[i].key == key)
		{
			m_menudepth = i;
		}

	if (i == m_menudepth)
	{
		if (m_menudepth >= MAX_MENU_DEPTH)
			Com_Error (ERR_FATAL, "M_PushMenu: MAX_MENU_DEPTH");
		m_layers[m_menudepth].draw = m_drawfunc;
		m_layers[m_menudepth].key = m_keyfunc;
		m_menudepth++;
	}

	m_drawfunc = draw;
	m_keyfunc = key;

	m_entersound = true;

	refreshCursorLink();
	refreshCursorButtons();

	cls.key_dest = key_menu;
}

void M_ForceMenuOff (void)
{
	refreshCursorLink();
	m_drawfunc = 0;
	m_keyfunc = 0;
	cls.key_dest = key_game;
	m_menudepth = 0;
	Key_ClearStates ();

	if (!cls.consoleActive)
		Cvar_Set ("paused", "0");
}

void M_PopMenu (void)
{
	S_StartLocalSound( menu_out_sound );
	if (m_menudepth < 1)
		Com_Error (ERR_FATAL, "M_PopMenu: depth < 1");
	m_menudepth--;

	m_drawfunc = m_layers[m_menudepth].draw;
	m_keyfunc = m_layers[m_menudepth].key;

	refreshCursorLink();
	refreshCursorButtons();

	if (!m_menudepth)
		M_ForceMenuOff ();
}

void FreeFileList( char **list, int n )
{
	int i;

	for ( i = 0; i < n; i++ )
	{
		if ( list[i] )
		{
			free( list[i] );
			list[i] = 0;
		}
	}
	free( list );
}

const char *Default_MenuKey( menuframework_s *m, int key )
{
	const char *sound = NULL;
	menucommon_s *item;

	if ( m )
	{
		if ( ( item = Menu_ItemAtCursor( m ) ) != 0 )
		{
			if ( item->type == MTYPE_FIELD )
			{
				if ( Field_Key( ( menufield_s * ) item, key ) )
					return NULL;
			}
		}
	}

	switch ( key )
	{
	case K_ESCAPE:
		M_PopMenu();
		return menu_out_sound;
	case K_KP_UPARROW:
	case K_UPARROW:
		if ( m )
		{
			m->cursor--;
			refreshCursorLink();

			Menu_AdjustCursor( m, -1 );
			sound = menu_move_sound;
		}
		break;
	case K_TAB:
	case K_KP_DOWNARROW:
	case K_DOWNARROW:
		if ( m )
		{
			m->cursor++;
			refreshCursorLink();

			Menu_AdjustCursor( m, 1 );
			sound = menu_move_sound;
		}
		break;
	case K_KP_LEFTARROW:
	case K_LEFTARROW:
		if ( m )
		{
			Menu_SlideItem( m, -1 );
			sound = menu_move_sound;
		}
		break;
	case K_KP_RIGHTARROW:
	case K_RIGHTARROW:
		if ( m )
		{
			Menu_SlideItem( m, 1 );
			sound = menu_move_sound;
		}
		break;

	case K_JOY1:
	case K_JOY2:
	case K_JOY3:
	case K_JOY4:
	case K_AUX1:
	case K_AUX2:
	case K_AUX3:
	case K_AUX4:
	case K_AUX5:
	case K_AUX6:
	case K_AUX7:
	case K_AUX8:
	case K_AUX9:
	case K_AUX10:
	case K_AUX11:
	case K_AUX12:
	case K_AUX13:
	case K_AUX14:
	case K_AUX15:
	case K_AUX16:
	case K_AUX17:
	case K_AUX18:
	case K_AUX19:
	case K_AUX20:
	case K_AUX21:
	case K_AUX22:
	case K_AUX23:
	case K_AUX24:
	case K_AUX25:
	case K_AUX26:
	case K_AUX27:
	case K_AUX28:
	case K_AUX29:
	case K_AUX30:
	case K_AUX31:
	case K_AUX32:
		
	case K_KP_ENTER:
	case K_ENTER:
		if ( m )
			Menu_SelectItem( m );
		sound = menu_move_sound;
		break;
	}

	return sound;
}

//=============================================================================

/*
================
M_DrawCharacter

Draws one solid graphics character
cx and cy are in 320*240 coordinates, and will be centered on
higher res screens.
================
*/
void M_DrawCharacter (int cx, int cy, int num)
{
	re.DrawChar ( cx + ((viddef.width - 320)>>1), cy + ((viddef.height - 240)>>1), num, 256);
}

void M_Print (int cx, int cy, char *str)
{
	DrawAltString (cx, cy, str, 255);
}

void M_PrintWhite (int cx, int cy, char *str)
{
	DrawString (cx, cy, str, 255);
}


/*
=============
M_DrawCursor

Draws an animating cursor with the point at
x,y.  The pic will extend to the left of x,
and both above and below y.
=============
*/
void M_DrawCursor( int x, int y, int f )
{
	float ratio;
	int w,h;

	re.DrawGetPicSize( &w, &h, "m_cursor" );
	if (w)
	{
		ratio = 21.0/(float)h;
		h = 21;
		w *= ratio;
	}
	re.DrawStretchPic (	x,	y,	scaledVideo(w), scaledVideo(h), "m_cursor");
}
		
/*
=======================================================================

MAIN MENU

=======================================================================
*/
#define	MAIN_ITEMS	5

char *main_names[] =
{
	"m_main_game",
	"m_main_multiplayer",
	"m_main_options",
	"m_main_video",
	"m_main_quit",
	0
};

void findMenuCoords (int *xoffset, int *ystart, int *totalheight, int *widest)
{
	float ratio;
	int w, h, i;

	*totalheight = 0;
	*widest = -1;

	for ( i = 0; main_names[i] != 0; i++ )
	{
		re.DrawGetPicSize( &w, &h, main_names[i] );

		if (w)
		{
			ratio = 32.0/(float)h;
			h = 32;
			w *= ratio;
		}

		if ( w > *widest )
			*widest = w;
		*totalheight += ( h + 12 );
	}

	*ystart = ( viddef.height / 2 - scaledVideo(110) );
	*xoffset = ( viddef.width  - scaledVideo(*widest) )/ 2 + scaledVideo(30);
}

void M_Main_Draw (void)
{
	int i;
	int w, h, last_h;
	int ystart;
	int	xoffset;
	int widest = -1;
	int totalheight = 0;
	char litname[80];
	float ratio;

	findMenuCoords(&xoffset, &ystart, &totalheight, &widest);

	for ( i = 0; main_names[i] != 0; i++ )
	{
		if ( i != m_main_cursor )
		{
			re.DrawGetPicSize( &w, &h, main_names[i] );
			if (w)
			{
				ratio = 32.0/(float)h;
				h = 32;
				w *= ratio;
			}
			re.DrawStretchPic (
				xoffset,
				ystart + scaledVideo(i * 40 + 13),
				scaledVideo(w), scaledVideo(h), main_names[i]);
		}
	}
	strcpy( litname, main_names[m_main_cursor] );
	strcat( litname, "_sel" );

	re.DrawGetPicSize( &w, &h, litname );
	if (w)
	{
		ratio = 32.0/(float)h;
		h = 32;
		w *= ratio;
	}

	re.DrawStretchPic (
		xoffset,
		ystart + scaledVideo(m_main_cursor * 40 + 13),
		scaledVideo(w+2), scaledVideo(h+2), litname);

	//now add top plaque
	re.DrawGetPicSize( &w, &h, "m_main_plaque" );
	if (w)
	{
		ratio = 38.0/(float)w;
		w = 38;
		h *= ratio;
	}
	re.DrawStretchPic (
		xoffset - scaledVideo(w/2 + 50),
		ystart + scaledVideo(10),
		scaledVideo(w), scaledVideo(h), "m_main_plaque");
	last_h = h;

	re.DrawGetPicSize( &w, &h, "m_main_back" );
	if (w)
	{
		ratio = 512.0/(float)w;
		w = 512;
		h *= ratio;
	}
	re.DrawStretchPic (
		viddef.width/2 - scaledVideo(w/2 + 10), 
		viddef.height/2 - scaledVideo(h/2),
		scaledVideo(w), scaledVideo(h), "m_main_back");

	re.DrawGetPicSize( &w, &h, "m_main_logo" );
	re.DrawStretchPic (
		xoffset - scaledVideo(w/2 + 50),
		ystart + scaledVideo(last_h) + scaledVideo(30),
		scaledVideo(w), scaledVideo(h), "m_main_logo");

//	M_DrawCursor( xoffset - scaledVideo(25), 
//		ystart + scaledVideo(m_main_cursor * 40 + 13), (int)(cls.realtime / 100)%NUM_CURSOR_FRAMES );
}

typedef struct
{
	int	min[2];
	int max[2];

	void (*OpenMenu)(void);
} mainmenuobject_t;

void addButton (mainmenuobject_t *thisObj, int index, int x, int y)
{
	float ratio;
	char *name;
	int w, h;

	re.DrawGetPicSize( &w, &h, main_names[index]);
	
	if (w)
	{
		ratio = 32.0/(float)h;
		h = 32;
		w *= ratio;
	}

	thisObj->min[0] = x; thisObj->max[0] = x + scaledVideo(w);
	thisObj->min[1] = y; thisObj->max[1] = y + scaledVideo(h);

	switch (index)
	{
	case 0:
		thisObj->OpenMenu = M_Menu_Game_f;
	case 1:
		thisObj->OpenMenu = M_Menu_Multiplayer_f;
	case 2:
		thisObj->OpenMenu = M_Menu_Options_f;
	case 3:
		thisObj->OpenMenu = M_Menu_Video_f;
	case 4:
		thisObj->OpenMenu = M_Menu_Quit_f;
	}
}

void openMenuFromMain (void)
{
	switch (m_main_cursor)
	{
		case 0:
			M_Menu_Game_f ();
			break;

		case 1:
			M_Menu_Multiplayer_f();
			break;

		case 2:
			M_Menu_Options_f ();
			break;

		case 3:
			M_Menu_Video_f ();
			break;

		case 4:
			M_Menu_Quit_f ();
			break;
	}
}

int MainMenuMouseHover;
void CheckMainMenuMouse (void)
{
	int ystart;
	int	xoffset;
	int widest;
	int totalheight;
	int i, oldhover;
	char *sound = NULL;
	mainmenuobject_t buttons[MAIN_ITEMS];

	oldhover = MainMenuMouseHover;
	MainMenuMouseHover = 0;

	findMenuCoords(&xoffset, &ystart, &totalheight, &widest);
	for ( i = 0; main_names[i] != 0; i++ )
		addButton (&buttons[i], i, xoffset, ystart + scaledVideo(i * 40 + 13));

	//Exit with double click 2nd mouse button
	if (!cursor.buttonused[MOUSEBUTTON2] && cursor.buttonclicks[MOUSEBUTTON2]==2)
	{
		M_PopMenu();
		sound = menu_out_sound;
		cursor.buttonused[MOUSEBUTTON2] = true;
		cursor.buttonclicks[MOUSEBUTTON2] = 0;
	}


	for (i=MAIN_ITEMS-1;i>=0;i--)
	{
		if (cursor.x>=buttons[i].min[0] && cursor.x<=buttons[i].max[0] &&
			cursor.y>=buttons[i].min[1] && cursor.y<=buttons[i].max[1])
		{
			if (cursor.mouseaction)
				m_main_cursor = i;

			MainMenuMouseHover = 1 + i;

			if (oldhover == MainMenuMouseHover && MainMenuMouseHover-1 == m_main_cursor &&
				!cursor.buttonused[MOUSEBUTTON1] && cursor.buttonclicks[MOUSEBUTTON1]==1)
			{
				openMenuFromMain();
				sound = menu_move_sound;
				cursor.buttonused[MOUSEBUTTON1] = true;
				cursor.buttonclicks[MOUSEBUTTON1] = 0;
			}
			break;
		}
	}

	if (!MainMenuMouseHover)
	{
		cursor.buttonused[MOUSEBUTTON1] = false;
		cursor.buttonclicks[MOUSEBUTTON1] = 0;
		cursor.buttontime[MOUSEBUTTON1] = 0;
	}

	cursor.mouseaction = false;

	if ( sound )
		S_StartLocalSound( sound );
}

const char *M_Main_Key (int key)
{
	const char *sound = menu_move_sound;

	switch (key)
	{
	case K_ESCAPE:
		M_PopMenu ();
		break;

	case K_KP_DOWNARROW:
	case K_DOWNARROW:
		if (++m_main_cursor >= MAIN_ITEMS)
			m_main_cursor = 0;
		return sound;

	case K_KP_UPARROW:
	case K_UPARROW:
		if (--m_main_cursor < 0)
			m_main_cursor = MAIN_ITEMS - 1;
		return sound;

	case K_KP_ENTER:
	case K_ENTER:
		m_entersound = true;
		openMenuFromMain();
	}

	return NULL;
}


void M_Menu_Main_f (void)
{
	M_PushMenu (M_Main_Draw, M_Main_Key);
}

/*
=======================================================================

MULTIPLAYER MENU

=======================================================================
*/
static menuframework_s	s_multiplayer_menu;
static menuaction_s		s_join_network_server_action;
static menuaction_s		s_start_network_server_action;
static menuaction_s		s_player_setup_action;

static void Multiplayer_MenuDraw (void)
{
	M_Banner( "m_banner_multiplayer" );

	Menu_AdjustCursor( &s_multiplayer_menu, 1 );
	Menu_Draw( &s_multiplayer_menu );
}

static void PlayerSetupFunc( void *unused )
{
	M_Menu_PlayerConfig_f();
}

static void JoinNetworkServerFunc( void *unused )
{
	M_Menu_JoinServer_f();
}

static void StartNetworkServerFunc( void *unused )
{
	M_Menu_StartServer_f ();
}

void Multiplayer_MenuInit( void )
{
	s_multiplayer_menu.x = viddef.width * 0.50 - scaledVideo(64);
	s_multiplayer_menu.y = viddef.height * 0.50 - scaledVideo(58);
	s_multiplayer_menu.nitems = 0;

	s_join_network_server_action.generic.type	= MTYPE_ACTION;
	s_join_network_server_action.generic.flags  = QMF_LEFT_JUSTIFY;
	s_join_network_server_action.generic.x		= 0;
	s_join_network_server_action.generic.y		= 0;
	s_join_network_server_action.generic.name	= " join network server";
	s_join_network_server_action.generic.callback = JoinNetworkServerFunc;

	s_start_network_server_action.generic.type	= MTYPE_ACTION;
	s_start_network_server_action.generic.flags  = QMF_LEFT_JUSTIFY;
	s_start_network_server_action.generic.x		= 0;
	s_start_network_server_action.generic.y		= 1.25*MENU_FONT_SIZE;
	s_start_network_server_action.generic.name	= " start network server";
	s_start_network_server_action.generic.callback = StartNetworkServerFunc;

	s_player_setup_action.generic.type	= MTYPE_ACTION;
	s_player_setup_action.generic.flags  = QMF_LEFT_JUSTIFY;
	s_player_setup_action.generic.x		= 0;
	s_player_setup_action.generic.y		= 2.5*MENU_FONT_SIZE;
	s_player_setup_action.generic.name	= " player setup";
	s_player_setup_action.generic.callback = PlayerSetupFunc;

	Menu_AddItem( &s_multiplayer_menu, ( void * ) &s_join_network_server_action );
	Menu_AddItem( &s_multiplayer_menu, ( void * ) &s_start_network_server_action );
	Menu_AddItem( &s_multiplayer_menu, ( void * ) &s_player_setup_action );

	Menu_SetStatusBar( &s_multiplayer_menu, NULL );

	Menu_Center( &s_multiplayer_menu );
}

const char *Multiplayer_MenuKey( int key )
{
	return Default_MenuKey( &s_multiplayer_menu, key );
}

void M_Menu_Multiplayer_f( void )
{
	Multiplayer_MenuInit();
	M_PushMenu( Multiplayer_MenuDraw, Multiplayer_MenuKey );
}

/*
=======================================================================

KEYS MENU

=======================================================================
*/
char *bindnames[][2] =
{
{"+attack", 		"attack"},
{"weapnext", 		"next weapon"},
{"weapprev", 		"previous weapon"},

{"MENUSPACE",""},

{"+forward", 		"walk forward"},
{"+back", 			"walk back"},
{"+moveleft", 		"step left"},
{"+moveright", 		"step right"},

{"MENUSPACE",""},

{"+left", 			"turn left"},
{"+right", 			"turn right"},
{"+strafe", 		"sidestep"},

{"MENUSPACE",""},

{"+speed", 			"run"},
{"+moveup",			"up / jump"},
{"+movedown",		"down / crouch"},

{"MENUSPACE",""},

{"+lookup", 		"look up"},
{"+lookdown", 		"look down"},
{"centerview", 		"center view"},
{"+mlook", 			"mouse look"},
{"+klook", 			"keyboard look"},

{"MENUSPACE",""},

{"inven",			"inventory"},
{"invuse",			"use item"},
{"invdrop",			"drop item"},
{"invprev",			"prev item"},
{"invnext",			"next item"},

{"MENUSPACE",""},

{"cmd help", 		"help computer" }, 
{ 0, 0 }
};

char *ddaybindnames[][2] =
{
{"+attack", 		"fire weapon"},
{"use weapon", 			"truesight"},
{"reload", 			"reload"},
{"weapnext", 		"next weapon"},
{"weapprev", 		"previous weapon"},

{"MENUSPACE",""},

{"+forward", 		"move forward"},
{"+back", 			"move backwards"},
{"+moveleft", 		"move left"},
{"+moveright", 		"move right"},

{"MENUSPACE",""},

{"+speed", 			"run"},
{"+moveup",			"jump"},
{"stance",		"stance"},

{"MENUSPACE",""},

{"messagemode",			"general chat"},
{"messagemode2",		"team chat"},

{"MENUSPACE",""},

{"dday_menu_main", 	"menu open: main" }, 
{"dday_menu_team", 	"menu open: team" }, 
{"dday_menu_class", "menu open: class" }, 
{"invuse",			"menu select"},
{"invprev",			"menu next"},
{"invnext",			"menu prev"},

{"MENUSPACE",""},

{"scoreboard", 		"scoreboard" }, 
{ 0, 0 }
};

char *swqbindnames[][2] =
{
{"+attack", 		"primary attack"},
{"secondary", 		"secondary attack"},
{"reload", 			"reload"},
{"weapnext", 		"next weapon"},
{"weapprev", 		"previous weapon"},

{"MENUSPACE",""},

{"+forward", 		"move forward"},
{"+back", 			"move backwards"},
{"+moveleft", 		"move left"},
{"+moveright", 		"move right"},

{"MENUSPACE",""},

{"+speed", 			"run"},
{"+moveup",			"jump"},
{"+movedown",		"crouch"},

{"MENUSPACE",""},

{"+use",			"use force power"},
{"chasecam",		"chasecam toggle"},
{"chaselock",		"chasecam lock"},

{"MENUSPACE",""},

{"cmd help", 		"menu open" }, 
{"invuse",			"menu select"},
{"invprev",			"menu next"},
{"invnext",			"menu prev"},

{"MENUSPACE",""},

{"kill", 			"suicide" }, 
{ 0, 0 }
};

int				keys_cursor;
static int		bind_grab;

static menuframework_s	s_keys_menu;
static menuaction_s s_keys_binds[64];

static menuaction_s		s_keys_attack_action;
static menuaction_s		s_keys_change_weapon_action;
static menuaction_s		s_keys_walk_forward_action;
static menuaction_s		s_keys_backpedal_action;
static menuaction_s		s_keys_turn_left_action;
static menuaction_s		s_keys_turn_right_action;
static menuaction_s		s_keys_run_action;
static menuaction_s		s_keys_step_left_action;
static menuaction_s		s_keys_step_right_action;
static menuaction_s		s_keys_sidestep_action;
static menuaction_s		s_keys_look_up_action;
static menuaction_s		s_keys_look_down_action;
static menuaction_s		s_keys_center_view_action;
static menuaction_s		s_keys_mouse_look_action;
static menuaction_s		s_keys_keyboard_look_action;
static menuaction_s		s_keys_move_up_action;
static menuaction_s		s_keys_move_down_action;
static menuaction_s		s_keys_inventory_action;
static menuaction_s		s_keys_inv_use_action;
static menuaction_s		s_keys_inv_drop_action;
static menuaction_s		s_keys_inv_prev_action;
static menuaction_s		s_keys_inv_next_action;

static menuaction_s		s_keys_help_computer_action;

static void M_UnbindCommand (char *command)
{
	int		j;
	int		l;
	char	*b;

	l = strlen(command);

	for (j=0 ; j<256 ; j++)
	{
		b = keybindings[j];
		if (!b)
			continue;

		if (!strncmp (b, command, l) )
			Key_SetBinding (j, "");
	}
}

static void M_FindKeysForCommand (char *command, int *twokeys)
{
	int		count;
	int		j;
	int		l;
	char	*b;

	twokeys[0] = twokeys[1] = -1;
	l = strlen(command);
	count = 0;

	for (j=0 ; j<256 ; j++)
	{
		b = keybindings[j];
		if (!b)
			continue;
		if (!strncmp (b, command, l) )
		{
			twokeys[count] = j;
			count++;
			if (count == 2)
				break;
		}
	}
}

static void KeyCursorDrawFunc( menuframework_s *menu )
{
	if ( bind_grab )
		re.DrawScaledChar( 
			menu->x,
			menu->y + scaledVideo(menu->cursor * MENU_FONT_SIZE),
			'=' , videoScale(), 255,255,255,255,false);
	else
		re.DrawScaledChar( 
			menu->x,
			menu->y + scaledVideo(menu->cursor * MENU_FONT_SIZE),
			12 + ( ( int ) ( Sys_Milliseconds() / 250 ) & 1 ) , videoScale(), 255,255,255,255,false);
}

static void DrawKeyBindingFunc( void *self )
{
	int keys[2];
	menuaction_s *a = ( menuaction_s * ) self;

	if (modType("dday"))
		M_FindKeysForCommand( ddaybindnames[a->generic.localdata[0]][0], keys );
	else if (modType("swq"))
		M_FindKeysForCommand( swqbindnames[a->generic.localdata[0]][0], keys );
	else
		M_FindKeysForCommand( bindnames[a->generic.localdata[0]][0], keys );
		
	if (keys[0] == -1)
	{
		Menu_DrawString( 
			scaledVideo(a->generic.x + 16) + a->generic.parent->x, 
			scaledVideo(a->generic.y) + a->generic.parent->y,
			"???", 255 );
	}
	else
	{
		int x;
		const char *name;

		name = Key_KeynumToString (keys[0]);

		Menu_DrawString( 
			scaledVideo(a->generic.x+16) + a->generic.parent->x,
			scaledVideo(a->generic.y) + a->generic.parent->y,
			name, 255 );

		x = scaledVideo(strlen(name) * MENU_FONT_SIZE);

		if (keys[1] != -1)
		{
			Menu_DrawString( 
				scaledVideo(a->generic.x + MENU_FONT_SIZE*3 + x) + a->generic.parent->x, 
				scaledVideo(a->generic.y) + a->generic.parent->y,
				"or", 255 );
			Menu_DrawString( 
				scaledVideo(a->generic.x + MENU_FONT_SIZE*6 + x) + a->generic.parent->x,
				scaledVideo(a->generic.y) + a->generic.parent->y,
				Key_KeynumToString (keys[1]), 255 );
		}
	}
}

static void KeyBindingFunc( void *self )
{
	menuaction_s *a = ( menuaction_s * ) self;
	int keys[2];

	if (modType("dday"))
	{
		M_FindKeysForCommand( ddaybindnames[a->generic.localdata[0]][0], keys );

		if (keys[1] != -1)
			M_UnbindCommand( ddaybindnames[a->generic.localdata[0]][0]);
	}
	else if (modType("swq"))
	{
		M_FindKeysForCommand( swqbindnames[a->generic.localdata[0]][0], keys );

		if (keys[1] != -1)
			M_UnbindCommand( swqbindnames[a->generic.localdata[0]][0]);
	}
	else
	{
		M_FindKeysForCommand( bindnames[a->generic.localdata[0]][0], keys );

		if (keys[1] != -1)
			M_UnbindCommand( bindnames[a->generic.localdata[0]][0]);
	}

	bind_grab = true;

	Menu_SetStatusBar( &s_keys_menu, "press a key or button for this action" );
}

int listSize (char* list[][2])
{
	int i=0;
	while (list[i][1])
		i++;

	return i;	
}

void addBindOption(int i, char* list[][2])
{
	s_keys_binds[i].generic.type	= MTYPE_ACTION;
	s_keys_binds[i].generic.flags  = QMF_GRAYED;
	s_keys_binds[i].generic.x		= 0;
	s_keys_binds[i].generic.y		= MENU_FONT_SIZE*i;
	s_keys_binds[i].generic.ownerdraw = DrawKeyBindingFunc;
	s_keys_binds[i].generic.localdata[0] = i;
	s_keys_binds[i].generic.name	= list[s_keys_binds[i].generic.localdata[0]][1];
	s_keys_binds[i].generic.callback = KeyBindingFunc;

	if (strstr ("MENUSPACE", list[i][0]))
		s_keys_binds[i].generic.type	= MTYPE_SEPARATOR;
}


static void Keys_MenuInit( void )
{	
	int BINDS_MAX;
	int y = 0;
	int i = 0;
	char * BindList;


	s_keys_menu.x = viddef.width * 0.50;
	s_keys_menu.y = viddef.height * 0.50 - scaledVideo(58);
	s_keys_menu.nitems = 0;
	s_keys_menu.cursordraw = KeyCursorDrawFunc;

	if (modType("dday"))
	{
		BINDS_MAX = listSize(&ddaybindnames);
		for (i=0;i<BINDS_MAX;i++)
			addBindOption(i, &ddaybindnames);
	}
	else if (modType("swq"))
	{
		BINDS_MAX = listSize(&swqbindnames);
		for (i=0;i<BINDS_MAX;i++)
			addBindOption(i, &swqbindnames);
	}
	else
	{
		BINDS_MAX = listSize(&bindnames);
		for (i=0;i<BINDS_MAX;i++)
			addBindOption(i, &bindnames);
	}

	for (i=0;i<BINDS_MAX;i++)
		Menu_AddItem( &s_keys_menu, ( void * ) &s_keys_binds[i] );

	Menu_SetStatusBar( &s_keys_menu, "enter to change, backspace to clear" );
	Menu_Center( &s_keys_menu );

	s_keys_menu.y = scaledVideo(MENU_FONT_SIZE*7);
}

static void Keys_MenuDraw (void)
{
	Menu_AdjustCursor( &s_keys_menu, 1 );
	Menu_Draw( &s_keys_menu );
}

static const char *Keys_MenuKey( int key )
{
	menuaction_s *item = ( menuaction_s * ) Menu_ItemAtCursor( &s_keys_menu );

	//pressing mouse1 to pick a new bind wont force bind/unbind itself - spaz
	if ( bind_grab && !(cursor.buttonused[MOUSEBUTTON1]&&key==K_MOUSE1))
	{	
		if ( key != K_ESCAPE && key != '`' )
		{
			char cmd[1024];


			if (modType("dday"))
				Com_sprintf (cmd, sizeof(cmd), "bind \"%s\" \"%s\"\n", Key_KeynumToString(key), ddaybindnames[item->generic.localdata[0]][0]);
			else if (modType("swq"))
				Com_sprintf (cmd, sizeof(cmd), "bind \"%s\" \"%s\"\n", Key_KeynumToString(key), swqbindnames[item->generic.localdata[0]][0]);
			else
				Com_sprintf (cmd, sizeof(cmd), "bind \"%s\" \"%s\"\n", Key_KeynumToString(key), bindnames[item->generic.localdata[0]][0]);

			Cbuf_InsertText (cmd);
		}
		
		//dont let selecting with mouse buttons screw everything up
		refreshCursorButtons();
		if (key==K_MOUSE1)
			cursor.buttonclicks[MOUSEBUTTON1] = -1;

		Menu_SetStatusBar( &s_keys_menu, "enter to change, backspace to clear" );
		bind_grab = false;
		return menu_out_sound;
	}

	switch ( key )
	{
	case K_KP_ENTER:
	case K_ENTER:
		KeyBindingFunc( item );
		return menu_in_sound;
	case K_BACKSPACE:		// delete bindings
	case K_DEL:				// delete bindings
	case K_KP_DEL:
			if (modType("dday"))
				M_UnbindCommand( ddaybindnames[item->generic.localdata[0]][0] );
			else if (modType("swq"))
				M_UnbindCommand( swqbindnames[item->generic.localdata[0]][0] );
			else
				M_UnbindCommand( bindnames[item->generic.localdata[0]][0] );
		return menu_out_sound;
	default:
		return Default_MenuKey( &s_keys_menu, key );
	}
}

void M_Menu_Keys_f (void)
{
	Keys_MenuInit();
	M_PushMenu( Keys_MenuDraw, Keys_MenuKey );
}


/*
=======================================================================

CONTROLS MENU

=======================================================================
*/
static cvar_t *win_noalttab;
extern cvar_t *in_joystick;

static int	currentOptionsMenu;

static menuframework_s	s_options_menu;
static menuframework_s	s_options_sound_menu;
static menuframework_s	s_options_controls_menu;
static menuframework_s	s_options_interface_menu;
static menuframework_s	s_options_misc_menu;


static menuaction_s		s_options_sound_section;
static menuaction_s		s_options_controls_section;
static menuaction_s		s_options_interface_section;
static menuaction_s		s_options_misc_section;

static menuaction_s		s_options_defaults_action;
static menulist_s		s_options_noalttab_box;
static menulist_s		s_options_console_action;

//sound
static menulist_s		s_options_cdvolume_box;
static menulist_s		s_options_quality_list;
static menulist_s		s_options_compatibility_list;
static menuslider_s		s_options_sfxvolume_slider;

//controls
static menuslider_s		s_options_sensitivity_slider;
static menulist_s		s_options_thirdperson_box;
static menulist_s		s_options_alwaysrun_box;
static menulist_s		s_options_invertmouse_box;
static menulist_s		s_options_lookspring_box;
static menulist_s		s_options_lookstrafe_box;
static menulist_s		s_options_freelook_box;
static menulist_s		s_options_joystick_box;
static menuaction_s		s_options_customize_options_action;

//interface
static menulist_s		s_options_font_box;
static menuslider_s		s_options_fontsize_slider;
static menuslider_s		s_options_menumouse_slider;
static menulist_s		s_options_hud_resolution_box;


//ambience

//miscellaneous
static menuslider_s		s_options_crosshairscale_slider;
static menulist_s		s_options_crosshair_box;
static menulist_s		s_options_railtrail_box;


static void CrosshairFunc( void *unused )
{
	Cvar_SetValue( "crosshair", s_options_crosshair_box.curvalue );
}

static void CrosshairSizeFunc( void *unused )
{
	Cvar_SetValue( "crosshair_scale", s_options_crosshairscale_slider.curvalue*0.25);
}

static void RailTrailFunc( void *unused )
{
	Cvar_SetValue( "cl_railtype", s_options_railtrail_box.curvalue );
}

static void JoystickFunc( void *unused )
{
	Cvar_SetValue( "in_joystick", s_options_joystick_box.curvalue );
}

static void CustomizeControlsFunc( void *unused )
{
	M_Menu_Keys_f();
}

static void AlwaysRunFunc( void *unused )
{
	Cvar_SetValue( "cl_run", s_options_alwaysrun_box.curvalue );
}

static void FreeLookFunc( void *unused )
{
	Cvar_SetValue( "freelook", s_options_freelook_box.curvalue );
}

static void MouseSpeedFunc( void *unused )
{
	Cvar_SetValue( "sensitivity", s_options_sensitivity_slider.curvalue / 2.0F );
}

static void MouseMenuFunc( void *unused )
{
	Cvar_SetValue( "menu_sensitivity", s_options_menumouse_slider.curvalue / 3.0F );
}

static void NoAltTabFunc( void *unused )
{
	Cvar_SetValue( "win_noalttab", s_options_noalttab_box.curvalue );
}

static float ClampCvar( float min, float max, float value )
{
	if ( value < min ) return min;
	if ( value > max ) return max;
	return value;
}

cvar_t *con_font;
#define MAX_FONTS 32
char **font_names;
int	numfonts;

int GetHudRes( void )
{
	int res = cl_hudres->value;

	if (res<=320)
		return 0;
	else if (res<=400)
		return 1;
	else if (res<=640)
		return 2;
	else if (res<=800)
		return 3;

	return 4;
}

static void HudResFunc( void *unused )
{
	if (s_options_hud_resolution_box.curvalue == 0)
		Cvar_SetValue( "cl_hudres", 320 );
	else if (s_options_hud_resolution_box.curvalue == 1)
		Cvar_SetValue( "cl_hudres", 400 );
	else if (s_options_hud_resolution_box.curvalue == 2)
		Cvar_SetValue( "cl_hudres", 640 );
	else if (s_options_hud_resolution_box.curvalue == 3)
		Cvar_SetValue( "cl_hudres", 800 );
	else if (s_options_hud_resolution_box.curvalue == 4)
		Cvar_SetValue( "cl_hudres", 1024 );
}

static void FontSizeFunc( void *unused )
{
	Cvar_SetValue( "con_font_size", s_options_fontsize_slider.curvalue * 4 );
}
static void FontFunc( void *unused )
{
	Cvar_Set( "con_font", font_names[s_options_font_box.curvalue] );
}

void SetFontCursor (void)
{
	int i;
	s_options_font_box.curvalue = 0;

	if (!con_font)
		con_font = Cvar_Get ("con_font", "default", CVAR_ARCHIVE);

	if (numfonts>1)
		for (i=0; font_names[i]; i++)
		{
			if (!Q_strcasecmp(con_font->string, font_names[i]))
			{
				s_options_font_box.curvalue = i;
				return;
			}
		}
}

qboolean fontInList (char *check, int num, char **list)
{
	int i;
	for (i=0;i<num;i++)
		if (!Q_strcasecmp(check, list[i]))
			return true;
	return false;
}

void insertFont (char ** list, char *insert, int len )
{
	int i, j;

	//i=1 so default stays first!
	for (i=1;i<len; i++)
	{
		if (!list[i])
			break;

		if (strcmp( list[i], insert ))
		{
			for (j=len; j>i ;j--)
				list[j] = list[j-1];

			list[i] = strdup(insert);

			return;
		}
	}

	list[len] = strdup(insert);
}

char **SetFontNames (void)
{
	char *curFont;
	char **list = 0, *p, *s;
	char findname[1024];
	int nfonts = 0, nfontnames;
	char **fontfiles;
	char *path = NULL;
	int i, j;
	extern char **FS_ListFiles( char *, int *, unsigned, unsigned );

	list = malloc( sizeof( char * ) * MAX_FONTS );
	memset( list, 0, sizeof( char * ) * MAX_FONTS );

	list[0] = strdup("default");

	nfontnames = 1;

	path = FS_NextPath( path );
	while (path) 
	{
		Com_sprintf( findname, sizeof(findname), "%s/fonts/*.*", path );
		fontfiles = FS_ListFiles( findname, &nfonts, 0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM );

		for (i=0;i<nfonts && nfontnames<MAX_FONTS;i++)
		{
			int num;

			if (!fontfiles || !fontfiles[i])
				continue;

			p = strstr(fontfiles[i], "/fonts/"); p++;
			p = strstr(p, "/"); p++;

			if (	!strstr(p, ".png")
				&&	!strstr(p, ".tga")
				&&	!strstr(p, ".jpg")
				&&	!strstr(p, ".pcx")
				)
				continue;

			num = strlen(p)-4;
			p[num] = NULL;

			curFont = p;

			if (!fontInList(curFont, nfontnames, list))
			{
				insertFont(list, strdup(curFont),nfontnames);
				nfontnames++;
			}
			
			//set back so whole string get deleted.
			p[num] = '.';
		}
		if (nfonts && fontfiles)
			free( fontfiles );
		
		path = FS_NextPath( path );
	}

	//check pak after
	if (fontfiles = FS_ListPak("fonts/", &nfonts))
	{
		for (i=0;i<nfonts && nfontnames<MAX_FONTS;i++)
		{

			int num;

			if (!fontfiles || !fontfiles[i])
				continue;

			p = strstr(fontfiles[i], "/"); p++;

			if (	!strstr(p, ".png")
				&&	!strstr(p, ".tga")
				&&	!strstr(p, ".jpg")
				&&	!strstr(p, ".pcx")
				)
				continue;

			num = strlen(p)-4;
			p[num] = NULL;

			curFont = p;

			if (!fontInList(curFont, nfontnames, list))
			{
				insertFont(list, strdup(curFont),nfontnames);
				nfontnames++;
			}
			
			//set back so whole string get deleted.
			p[num] = '.';

		}
	}
	if (nfonts && fontfiles)
		free( fontfiles );

	numfonts = nfontnames;

	return list;		
}

static void ControlsSetMenuItemValues( void )
{
	s_options_sfxvolume_slider.curvalue		= Cvar_VariableValue( "s_volume" ) * 10;
	s_options_cdvolume_box.curvalue 		= !Cvar_VariableValue("cd_nocd");
	s_options_quality_list.curvalue			= !Cvar_VariableValue( "s_loadas8bit" );
	s_options_sensitivity_slider.curvalue	= ( sensitivity->value ) * 2;
	s_options_menumouse_slider.curvalue		= menu_sensitivity->value * 3;
	s_options_fontsize_slider.curvalue		= ( con_font_size->value ) * 0.25;
	s_options_hud_resolution_box.curvalue	= GetHudRes();

	SetFontCursor();

	Cvar_SetValue( "cl_run", ClampCvar( 0, 1, cl_run->value ) );
	s_options_alwaysrun_box.curvalue		= cl_run->value;

	s_options_invertmouse_box.curvalue		= m_pitch->value < 0;

	Cvar_SetValue( "lookspring", ClampCvar( 0, 1, lookspring->value ) );
	s_options_lookspring_box.curvalue		= lookspring->value;

	Cvar_SetValue( "lookstrafe", ClampCvar( 0, 1, lookstrafe->value ) );
	s_options_lookstrafe_box.curvalue		= lookstrafe->value;

	Cvar_SetValue( "freelook", ClampCvar( 0, 1, freelook->value ) );
	s_options_freelook_box.curvalue			= freelook->value;

	Cvar_SetValue( "crosshair", ClampCvar( 0, 10, crosshair->value ) );
	s_options_crosshair_box.curvalue		= crosshair->value;
	s_options_crosshairscale_slider.curvalue		= crosshair_scale->value*4;

	Cvar_SetValue( "cl_railtype", ClampCvar( 0, 1, cl_railtype->value ) );
	s_options_railtrail_box.curvalue		= cl_railtype->value;

	Cvar_SetValue( "in_joystick", ClampCvar( 0, 1, in_joystick->value ) );
	s_options_joystick_box.curvalue		= in_joystick->value;

	s_options_noalttab_box.curvalue			= win_noalttab->value;
}

static void ControlsResetDefaultsFunc( void *unused )
{
	Cbuf_AddText ("exec default\n");
	Cbuf_Execute();

	ControlsSetMenuItemValues();
}

static void InvertMouseFunc( void *unused )
{
	Cvar_SetValue( "m_pitch", -m_pitch->value );
}

static void LookspringFunc( void *unused )
{
	Cvar_SetValue( "lookspring", !lookspring->value );
}

static void LookstrafeFunc( void *unused )
{
	Cvar_SetValue( "lookstrafe", !lookstrafe->value );
}

static void UpdateVolumeFunc( void *unused )
{
	Cvar_SetValue( "s_volume", s_options_sfxvolume_slider.curvalue / 10 );
}

static void UpdateCDVolumeFunc( void *unused )
{
	Cvar_SetValue( "cd_nocd", !s_options_cdvolume_box.curvalue );
}

static void setMenuOptionsMain ( void *unused )
{
	Cvar_SetValue( "options_menu", 0 );
	refreshCursorLink();
}
static void setMenuSound ( void *unused )
{
	Cvar_SetValue( "options_menu", 1 );
	refreshCursorLink();
}
static void setMenuControls ( void *unused )
{
	Cvar_SetValue( "options_menu", 2 );
	refreshCursorLink();
}
static void setMenuInterface ( void *unused )
{
	Cvar_SetValue( "options_menu", 3 );
	refreshCursorLink();
}
static void setMenuMisc ( void *unused )
{
	Cvar_SetValue( "options_menu", 4 );
	refreshCursorLink();
}

static void ConsoleFunc( void *unused )
{
	/*
	** the proper way to do this is probably to have ToggleConsole_f accept a parameter
	*/
	extern void Key_ClearTyping( void );

	if ( cl.attractloop )
	{
		Cbuf_AddText ("killserver\n");
		return;
	}

	Key_ClearTyping ();
	Con_ClearNotify ();

//	M_ForceMenuOff ();
	cls.consoleActive = true;
//	cls.key_dest = key_console;
}

static void UpdateSoundQualityFunc( void *unused )
{
	if ( s_options_quality_list.curvalue )
	{
		if (s_options_quality_list.curvalue == 2)
			Cvar_SetValue( "s_khz", 44 );
		else
			Cvar_SetValue( "s_khz", 22 );

		Cvar_SetValue( "s_loadas8bit", false );
	}
	else
	{
		Cvar_SetValue( "s_khz", 11 );
		Cvar_SetValue( "s_loadas8bit", true );
	}
	
	Cvar_SetValue( "s_primary", s_options_compatibility_list.curvalue );

	CL_Snd_Restart_f();
}

int QualFromKHZ (int khz)
{
	if (khz = 44)
		return 2;
	else if (khz = 22)
		return 1;
	else 
		return 0;
}

void Options_MenuInit( void )
{
	static const char *null_names[] =
	{
		"",
		0
	};

	static const char *cd_music_items[] =
	{
		"disabled",
		"enabled",
		0
	};
	static const char *quality_items[] =
	{
		"low (11khz 8bit)", 
		"medium (22khz 16bit)", 
		"high (44khz 16bit)", 
		0
	};

	static const char *compatibility_items[] =
	{
		"max compatibility", "max performance", 0
	};

	static const char *yesno_names[] =
	{
		"no",
		"yes",
		0
	};

	static const char *crosshair_names[] =
	{
		"none",
		"accuracy",
		"cartoon",
		"sniper",
		"custom",
		"ringed",
		"cross",
		"plus",
		"disc",
		"classic",
		"dot",
		0
	};
	static const char *railtrail_names[] =
	{
		"colored spiral",
		"colored beam",
		0
	};
	static const char *blood_names[] =
	{
		"splat",
		"bleed",
		"gore",
		0
	};
	static const char *explode_names[] =
	{
		"blast",
		"blast & smoke",
		0
	};
	static const char *hud_res[] =
	{
		"[320 240  ]",
		"[400 300  ]",
		"[640 480  ]",
		"[800 600  ]",
		"[1024 768 ]",
		0
	};
	

	if (!options_menu)
		options_menu = Cvar_Get ("options_menu", "0", 0);

	win_noalttab = Cvar_Get( "win_noalttab", "0", CVAR_ARCHIVE );

	/*
	** configure controls menu and menu items
	*/

	s_options_menu.x = viddef.width / 2;
	s_options_sound_menu.x = s_options_menu.x;
	s_options_controls_menu.x = s_options_menu.x;
	s_options_interface_menu.x = s_options_menu.x;
	s_options_misc_menu.x = s_options_menu.x;

	s_options_menu.y = viddef.height / 2 - scaledVideo(58);
	s_options_sound_menu.y = s_options_menu.y;
	s_options_controls_menu.y = s_options_menu.y;
	s_options_interface_menu.y = s_options_menu.y;
	s_options_misc_menu.y = s_options_menu.y;

	s_options_menu.nitems = 0;
	s_options_sound_menu.nitems = 0;
	s_options_controls_menu.nitems = 0;
	s_options_interface_menu.nitems = 0;
	s_options_misc_menu.nitems = 0;


	//SECTION SELECTORS
	{
		s_options_sound_section.generic.type				= MTYPE_ACTION;
		s_options_sound_section.generic.name				= "sound";
		s_options_sound_section.generic.x					= -MENU_FONT_SIZE * 12;
		s_options_sound_section.generic.y					= 0;
		s_options_sound_section.generic.callback			= setMenuSound;
		s_options_sound_section.generic.cursor_offset		= s_options_sound_section.generic.x - 
			MENU_FONT_SIZE*(strlen(s_options_sound_section.generic.name)+3);
		
		s_options_controls_section.generic.type				= MTYPE_ACTION;
		s_options_controls_section.generic.name				= "controls";
		s_options_controls_section.generic.x				= -MENU_FONT_SIZE * 1;
		s_options_controls_section.generic.y				= 0;
		s_options_controls_section.generic.callback			= setMenuControls;
		s_options_controls_section.generic.cursor_offset	= s_options_controls_section.generic.x - 
			MENU_FONT_SIZE*(strlen(s_options_controls_section.generic.name)+3);
		
		s_options_interface_section.generic.type			= MTYPE_ACTION;
		s_options_interface_section.generic.name			= "interface";
		s_options_interface_section.generic.x				= MENU_FONT_SIZE * 11;
		s_options_interface_section.generic.y				= 0;
		s_options_interface_section.generic.callback		= setMenuInterface;
		s_options_interface_section.generic.cursor_offset	= s_options_interface_section.generic.x - 
			MENU_FONT_SIZE*(strlen(s_options_interface_section.generic.name)+3);
		
		s_options_misc_section.generic.type					= MTYPE_ACTION;
		s_options_misc_section.generic.name					= "misc";
		s_options_misc_section.generic.x					= MENU_FONT_SIZE * 18;
		s_options_misc_section.generic.y					= 0;
		s_options_misc_section.generic.callback				= setMenuMisc;
		s_options_misc_section.generic.cursor_offset		= s_options_misc_section.generic.x - 
			MENU_FONT_SIZE*(strlen(s_options_misc_section.generic.name)+3);
	}
	//BEGIN SOUND OPTIONS
	{
		
		s_options_sfxvolume_slider.generic.type	= MTYPE_SLIDER;
		s_options_sfxvolume_slider.generic.x	= 0;
		s_options_sfxvolume_slider.generic.y	= MENU_FONT_SIZE*3+2;
		s_options_sfxvolume_slider.generic.name	= "effects volume";
		s_options_sfxvolume_slider.generic.callback	= UpdateVolumeFunc;
		s_options_sfxvolume_slider.minvalue		= 0;
		s_options_sfxvolume_slider.maxvalue		= 10;
		s_options_sfxvolume_slider.curvalue		= Cvar_VariableValue( "s_volume" ) * 10;

		s_options_cdvolume_box.generic.type		= MTYPE_SPINCONTROL;
		s_options_cdvolume_box.generic.x		= 0;
		s_options_cdvolume_box.generic.y		= MENU_FONT_SIZE*5+2;
		s_options_cdvolume_box.generic.name		= "CD music";
		s_options_cdvolume_box.generic.callback	= UpdateCDVolumeFunc;
		s_options_cdvolume_box.itemnames		= cd_music_items;
		s_options_cdvolume_box.curvalue 		= !Cvar_VariableValue("cd_nocd");

		s_options_quality_list.generic.type	= MTYPE_SPINCONTROL;
		s_options_quality_list.generic.x		= 0;
		s_options_quality_list.generic.y		= MENU_FONT_SIZE*7+2;
		s_options_quality_list.generic.name		= "sound quality";
		s_options_quality_list.generic.callback = UpdateSoundQualityFunc;
		s_options_quality_list.itemnames		= quality_items;
		s_options_quality_list.curvalue			= QualFromKHZ(Cvar_VariableValue( "s_khz" ));
		
		s_options_compatibility_list.generic.type	= MTYPE_SPINCONTROL;
		s_options_compatibility_list.generic.x		= 0;
		s_options_compatibility_list.generic.y		= MENU_FONT_SIZE*8+2;
		s_options_compatibility_list.generic.name	= "sound compatibility";
		s_options_compatibility_list.generic.callback = UpdateSoundQualityFunc;
		s_options_compatibility_list.itemnames		= compatibility_items;
		s_options_compatibility_list.curvalue		= Cvar_VariableValue( "s_primary" );
	}
	//BEGIN CONTROL OPTIONS
	{
		s_options_sensitivity_slider.generic.type	= MTYPE_SLIDER;
		s_options_sensitivity_slider.generic.x		= 0;
		s_options_sensitivity_slider.generic.y		= MENU_FONT_SIZE*3+2;
		s_options_sensitivity_slider.generic.name	= "mouse speed";
		s_options_sensitivity_slider.generic.callback = MouseSpeedFunc;
		s_options_sensitivity_slider.minvalue		= 2;
		s_options_sensitivity_slider.maxvalue		= 22;

		s_options_invertmouse_box.generic.type = MTYPE_SPINCONTROL;
		s_options_invertmouse_box.generic.x	= 0;
		s_options_invertmouse_box.generic.y	= MENU_FONT_SIZE*4+2;
		s_options_invertmouse_box.generic.name	= "invert mouse";
		s_options_invertmouse_box.generic.callback = InvertMouseFunc;
		s_options_invertmouse_box.itemnames = yesno_names;

		s_options_lookspring_box.generic.type = MTYPE_SPINCONTROL;
		s_options_lookspring_box.generic.x	= 0;
		s_options_lookspring_box.generic.y	= MENU_FONT_SIZE*5+2;
		s_options_lookspring_box.generic.name	= "lookspring";
		s_options_lookspring_box.generic.callback = LookspringFunc;
		s_options_lookspring_box.itemnames = yesno_names;

		s_options_lookstrafe_box.generic.type = MTYPE_SPINCONTROL;
		s_options_lookstrafe_box.generic.x	= 0;
		s_options_lookstrafe_box.generic.y	= MENU_FONT_SIZE*6+2;
		s_options_lookstrafe_box.generic.name	= "lookstrafe";
		s_options_lookstrafe_box.generic.callback = LookstrafeFunc;
		s_options_lookstrafe_box.itemnames = yesno_names;

		s_options_freelook_box.generic.type = MTYPE_SPINCONTROL;
		s_options_freelook_box.generic.x	= 0;
		s_options_freelook_box.generic.y	= MENU_FONT_SIZE*7+2;
		s_options_freelook_box.generic.name	= "free look";
		s_options_freelook_box.generic.callback = FreeLookFunc;
		s_options_freelook_box.itemnames = yesno_names;

		s_options_joystick_box.generic.type = MTYPE_SPINCONTROL;
		s_options_joystick_box.generic.x	= 0;
		s_options_joystick_box.generic.y	= MENU_FONT_SIZE*8+2;
		s_options_joystick_box.generic.name	= "use joystick";
		s_options_joystick_box.generic.callback = JoystickFunc;
		s_options_joystick_box.itemnames = yesno_names;	

		s_options_alwaysrun_box.generic.type = MTYPE_SPINCONTROL;
		s_options_alwaysrun_box.generic.x	= 0;
		s_options_alwaysrun_box.generic.y	= MENU_FONT_SIZE*9+2;
		s_options_alwaysrun_box.generic.name	= "always run";
		s_options_alwaysrun_box.generic.callback = AlwaysRunFunc;
		s_options_alwaysrun_box.itemnames = yesno_names;

		s_options_customize_options_action.generic.type	= MTYPE_ACTION;
		s_options_customize_options_action.generic.x		= 0;
		s_options_customize_options_action.generic.y		= MENU_FONT_SIZE*11+2;
		s_options_customize_options_action.generic.name	= "customize controls";
		s_options_customize_options_action.generic.callback = CustomizeControlsFunc;
	}
	//BEGIN INTERFACE
	{
		font_names = SetFontNames ();
		s_options_font_box.generic.type = MTYPE_SPINCONTROL;
		s_options_font_box.generic.x	= 0;
		s_options_font_box.generic.y	= MENU_FONT_SIZE*3+2;
		s_options_font_box.generic.name	= "font";
		s_options_font_box.generic.callback = FontFunc;
		s_options_font_box.itemnames = font_names;
		s_options_font_box.generic.statusbar	= "select your font";

		s_options_fontsize_slider.generic.type		= MTYPE_SLIDER;
		s_options_fontsize_slider.generic.x			= 0;
		s_options_fontsize_slider.generic.y			= MENU_FONT_SIZE*4+2;
		s_options_fontsize_slider.generic.name		= "font size";
		s_options_fontsize_slider.generic.callback	= FontSizeFunc;
		s_options_fontsize_slider.minvalue			= 2;
		s_options_fontsize_slider.maxvalue			= 6;
		s_options_fontsize_slider.generic.statusbar	= "console font size";

		s_options_menumouse_slider.generic.type		= MTYPE_SLIDER;
		s_options_menumouse_slider.generic.x		= 0;
		s_options_menumouse_slider.generic.y		= MENU_FONT_SIZE*6+2;
		s_options_menumouse_slider.generic.name		= "mouse speed";
		s_options_menumouse_slider.generic.callback = MouseMenuFunc;
		s_options_menumouse_slider.minvalue			= 1;
		s_options_menumouse_slider.maxvalue			= 6;
		s_options_menumouse_slider.generic.statusbar	= "menu mouse speed";

		s_options_hud_resolution_box.generic.type		= MTYPE_SPINCONTROL;
		s_options_hud_resolution_box.generic.x			= 0;
		s_options_hud_resolution_box.generic.y			= MENU_FONT_SIZE*8+2;
		s_options_hud_resolution_box.generic.name		= "hud resolution";
		s_options_hud_resolution_box.generic.callback	= HudResFunc;
		s_options_hud_resolution_box.itemnames = hud_res;
	}
	//BEGIN MISC.
	{

		s_options_crosshair_box.generic.type = MTYPE_SPINCONTROL;
		s_options_crosshair_box.generic.x	= 0;
		s_options_crosshair_box.generic.y	= MENU_FONT_SIZE*3+2;
		s_options_crosshair_box.generic.name	= "crosshair";
		s_options_crosshair_box.generic.callback = CrosshairFunc;
		s_options_crosshair_box.itemnames = crosshair_names;

		s_options_crosshairscale_slider.generic.type	= MTYPE_SLIDER;
		s_options_crosshairscale_slider.generic.x		= 0;
		s_options_crosshairscale_slider.generic.y		= MENU_FONT_SIZE*4+2;
		s_options_crosshairscale_slider.generic.name	= "crosshair scale";
		s_options_crosshairscale_slider.generic.callback = CrosshairSizeFunc;
		s_options_crosshairscale_slider.minvalue		= 1;
		s_options_crosshairscale_slider.maxvalue		= 12;

		s_options_railtrail_box.generic.type = MTYPE_SPINCONTROL;
		s_options_railtrail_box.generic.x	= 0;
		s_options_railtrail_box.generic.y	= MENU_FONT_SIZE*12+2;
		s_options_railtrail_box.generic.name	= "railtrail type";
		s_options_railtrail_box.generic.callback = RailTrailFunc;
		s_options_railtrail_box.itemnames = railtrail_names;
		s_options_railtrail_box.generic.statusbar	= "railtrail render style";
	}


	s_options_noalttab_box.generic.type = MTYPE_SPINCONTROL;
	s_options_noalttab_box.generic.x	= 0;
	s_options_noalttab_box.generic.y	= MENU_FONT_SIZE*14+2;
	s_options_noalttab_box.generic.name	= "disable alt-tab";
	s_options_noalttab_box.generic.callback = NoAltTabFunc;
	s_options_noalttab_box.itemnames = yesno_names;

	s_options_defaults_action.generic.type	= MTYPE_ACTION;
	s_options_defaults_action.generic.x		= 0;
	s_options_defaults_action.generic.y		= MENU_FONT_SIZE*15+2;
	s_options_defaults_action.generic.name	= "reset defaults";
	s_options_defaults_action.generic.callback = ControlsResetDefaultsFunc;

	s_options_console_action.generic.type	= MTYPE_ACTION;
	s_options_console_action.generic.x		= 0;
	s_options_console_action.generic.y		= MENU_FONT_SIZE*16+2;
	s_options_console_action.generic.name	= "go to console";
	s_options_console_action.generic.callback = ConsoleFunc;

	ControlsSetMenuItemValues();
	//sound
		Menu_AddItem( &s_options_sound_menu,			( void * ) &s_options_sound_section );
		Menu_AddItem( &s_options_sound_menu,			( void * ) &s_options_controls_section );
		Menu_AddItem( &s_options_sound_menu,			( void * ) &s_options_interface_section );
		Menu_AddItem( &s_options_sound_menu,			( void * ) &s_options_misc_section );

		Menu_AddItem( &s_options_sound_menu, ( void * ) &s_options_sfxvolume_slider );
		Menu_AddItem( &s_options_sound_menu, ( void * ) &s_options_cdvolume_box );
		Menu_AddItem( &s_options_sound_menu, ( void * ) &s_options_quality_list );
		Menu_AddItem( &s_options_sound_menu, ( void * ) &s_options_compatibility_list );

	//controls
		Menu_AddItem( &s_options_controls_menu,			( void * ) &s_options_sound_section );
		Menu_AddItem( &s_options_controls_menu,			( void * ) &s_options_controls_section );
		Menu_AddItem( &s_options_controls_menu,			( void * ) &s_options_interface_section );
		Menu_AddItem( &s_options_controls_menu,			( void * ) &s_options_misc_section );

		Menu_AddItem( &s_options_controls_menu, ( void * ) &s_options_sensitivity_slider );
		Menu_AddItem( &s_options_controls_menu, ( void * ) &s_options_invertmouse_box );
		Menu_AddItem( &s_options_controls_menu, ( void * ) &s_options_lookspring_box );
		Menu_AddItem( &s_options_controls_menu, ( void * ) &s_options_lookstrafe_box );
		Menu_AddItem( &s_options_controls_menu, ( void * ) &s_options_freelook_box );
		Menu_AddItem( &s_options_controls_menu, ( void * ) &s_options_joystick_box );
		Menu_AddItem( &s_options_controls_menu, ( void * ) &s_options_alwaysrun_box );
		Menu_AddItem( &s_options_controls_menu, ( void * ) &s_options_customize_options_action );

	//interface
		Menu_AddItem( &s_options_interface_menu,			( void * ) &s_options_sound_section );
		Menu_AddItem( &s_options_interface_menu,			( void * ) &s_options_controls_section );
		Menu_AddItem( &s_options_interface_menu,			( void * ) &s_options_interface_section );
		Menu_AddItem( &s_options_interface_menu,			( void * ) &s_options_misc_section );

		Menu_AddItem( &s_options_interface_menu, ( void * ) &s_options_font_box );
		Menu_AddItem( &s_options_interface_menu, ( void * ) &s_options_fontsize_slider );
		Menu_AddItem( &s_options_interface_menu, ( void * ) &s_options_menumouse_slider );
		Menu_AddItem( &s_options_interface_menu, ( void * ) &s_options_hud_resolution_box );

	//misc
		Menu_AddItem( &s_options_misc_menu,			( void * ) &s_options_sound_section );
		Menu_AddItem( &s_options_misc_menu,			( void * ) &s_options_controls_section );
		Menu_AddItem( &s_options_misc_menu,			( void * ) &s_options_interface_section );
		Menu_AddItem( &s_options_misc_menu,			( void * ) &s_options_misc_section );

		if (!modType("dday"))
		{
			Menu_AddItem( &s_options_misc_menu, ( void * ) &s_options_crosshair_box );
			Menu_AddItem( &s_options_misc_menu, ( void * ) &s_options_crosshairscale_slider );
			Menu_AddItem( &s_options_misc_menu, ( void * ) &s_options_railtrail_box );
		}

	Menu_AddItem( &s_options_sound_menu, ( void * ) &s_options_defaults_action );
	Menu_AddItem( &s_options_sound_menu, ( void * ) &s_options_console_action );

	Menu_AddItem( &s_options_controls_menu, ( void * ) &s_options_defaults_action );
	Menu_AddItem( &s_options_controls_menu, ( void * ) &s_options_console_action );

	Menu_AddItem( &s_options_interface_menu, ( void * ) &s_options_defaults_action );
	Menu_AddItem( &s_options_interface_menu, ( void * ) &s_options_console_action );

	Menu_AddItem( &s_options_misc_menu, ( void * ) &s_options_defaults_action );
	Menu_AddItem( &s_options_misc_menu, ( void * ) &s_options_console_action );
}


void OptionsMisc_MouseClick( void )
{
	char *sound = NULL;
	buttonmenuobject_t buttons[5];
	int i, count = 0, cross_x, cross_y, cross_offset;
	int value, cross_size;
	char crossPic[64];
	float cross_scale;

	cross_size = 50;

	cross_x = viddef.width  - viddef.width*0.5 - scaledVideo(cross_size)*2.5;
	cross_y = viddef.height - scaledVideo(260);

	value = s_options_crosshair_box.curvalue - 1;

	if (value>7)
		i=5;
	else
		i=value-2;

	for (count = 0, cross_offset = 0; i<10 && count<5; i++)
	{
		if (i<0)
			continue;

		cross_scale = (i==value) ? 1.5: 1;

		addPlayerButton (&buttons[count], i, 
			cross_x + cross_offset,
			cross_y,
			scaledVideo(cross_size)*cross_scale, 
			scaledVideo(cross_size)*cross_scale);

		cross_offset += scaledVideo(cross_size)*cross_scale;

		count++;
	}

	for (i=0;i<5;i++)
	{
		if (buttons[i].index==-1)
			continue;

		if (cursor.x>=buttons[i].min[0] && cursor.x<=buttons[i].max[0] &&
			cursor.y>=buttons[i].min[1] && cursor.y<=buttons[i].max[1])
		{
			if (!cursor.buttonused[MOUSEBUTTON1] && cursor.buttonclicks[MOUSEBUTTON1]==1)
			{
				s_options_crosshair_box.curvalue = buttons[i].index + 1;

				sound = menu_move_sound;
				cursor.buttonused[MOUSEBUTTON1] = true;
				cursor.buttonclicks[MOUSEBUTTON1] = 0;

				if ( sound )
					S_StartLocalSound( sound );

				return;
			}
			break;
		}
	}
}

void OptionsMiscDraw (void)
{
	int i, count = 0, cross_x, cross_y, cross_offset;
	int value, cross_size;
	char crossPic[64];
	float cross_scale;

	value = s_options_crosshair_box.curvalue - 1;

	cross_size = 50;

	cross_x = viddef.width  - viddef.width*0.5 - scaledVideo(cross_size)*2.5;
	cross_y = viddef.height - scaledVideo(260);


	if (value>7)
		i=5;
	else
		i=value-2;

	for (count = 0, cross_offset = 0; i<10 && count<5; i++)
	{
		if (i<0)
			continue;

		cross_scale = (i==value) ? 1.5: 1;

		re.DrawFill (
			cross_x + cross_offset - 1,
			cross_y - 1,
			scaledVideo(cross_size)*cross_scale + 2, 
			scaledVideo(cross_size)*cross_scale + 2, 
			0);

		cross_offset += scaledVideo(cross_size)*cross_scale;

		count++;
	}

	if (value>7)
		i=5;
	else
		i=value-2;

	for (count = 0, cross_offset = 0; i<10 && count<5; i++)
	{
		if (i<0)
			continue;

		Com_sprintf (crossPic, sizeof(crossPic), "ch%i", i);
		
		cross_scale = (i==value) ? 1.5: 1;

		re.DrawFill (
			cross_x + cross_offset,
			cross_y,
			scaledVideo(cross_size)*cross_scale, 
			scaledVideo(cross_size)*cross_scale, 
			4);

		re.DrawStretchPic (
			cross_x + cross_offset,
			cross_y,
			scaledVideo(cross_size)*cross_scale, 
			scaledVideo(cross_size)*cross_scale, 
			crossPic);

		cross_offset += scaledVideo(cross_size)*cross_scale;

		count++;
	}
}

void Options_MenuDraw (void)
{
	M_Banner( "m_banner_options" );

	if (options_menu->value==2)			//CONTROLS
	{
		s_options_sound_section.generic.name		= "sound";
		s_options_controls_section.generic.name		= "^2controls";
		s_options_interface_section.generic.name	= "interface";
		s_options_misc_section.generic.name			= "misc";

		s_options_sound_section.generic.x					= MENU_FONT_SIZE * -12;
		s_options_controls_section.generic.x				= MENU_FONT_SIZE * (-1+1);
		s_options_interface_section.generic.x				= MENU_FONT_SIZE * 11;
		s_options_misc_section.generic.x					= MENU_FONT_SIZE * 18;

		Menu_AdjustCursor( &s_options_controls_menu, 1 );
		Menu_Draw( &s_options_controls_menu );
	}
	else if (options_menu->value==3)	//INTERFACE
	{
		s_options_sound_section.generic.name		= "sound";
		s_options_controls_section.generic.name		= "controls";
		s_options_interface_section.generic.name	= "^2interface";
		s_options_misc_section.generic.name			= "misc";

		s_options_sound_section.generic.x					= MENU_FONT_SIZE * -12;
		s_options_controls_section.generic.x				= MENU_FONT_SIZE * -1;
		s_options_interface_section.generic.x				= MENU_FONT_SIZE * (11+1);
		s_options_misc_section.generic.x					= MENU_FONT_SIZE * 18;

		Menu_AdjustCursor( &s_options_interface_menu, 1 );
		Menu_Draw( &s_options_interface_menu );
	}
	else if (options_menu->value==4)	//MISC
	{
		s_options_sound_section.generic.name		= "sound";
		s_options_controls_section.generic.name		= "controls";
		s_options_interface_section.generic.name	= "interface";
		s_options_misc_section.generic.name			= "^2misc";

		s_options_sound_section.generic.x					= MENU_FONT_SIZE * -12;
		s_options_controls_section.generic.x				= MENU_FONT_SIZE * -1;
		s_options_interface_section.generic.x				= MENU_FONT_SIZE * 11;
		s_options_misc_section.generic.x					= MENU_FONT_SIZE * (18+1);

		OptionsMiscDraw();

		Menu_AdjustCursor( &s_options_misc_menu, 1 );
		Menu_Draw( &s_options_misc_menu );
	}	
	else //if (options_menu->value==1)	//SOUND
	{
		s_options_sound_section.generic.name		= "^2sound";
		s_options_controls_section.generic.name		= "controls";
		s_options_interface_section.generic.name	= "interface";
		s_options_misc_section.generic.name			= "misc";

		s_options_sound_section.generic.x					= MENU_FONT_SIZE * (-12+1);
		s_options_controls_section.generic.x				= MENU_FONT_SIZE * -1;
		s_options_interface_section.generic.x				= MENU_FONT_SIZE * 11;
		s_options_misc_section.generic.x					= MENU_FONT_SIZE * 18;

		Menu_AdjustCursor( &s_options_sound_menu, 1 );
		Menu_Draw( &s_options_sound_menu );
	}
}

const char *Options_MenuKey( int key )
{
	if (options_menu->value==2)
		return Default_MenuKey( &s_options_controls_menu, key );
	else if  (options_menu->value==3)
		return Default_MenuKey( &s_options_interface_menu, key );
	else if  (options_menu->value==4)
		return Default_MenuKey( &s_options_misc_menu, key );
	else //if (options_menu->value==1)
		return Default_MenuKey( &s_options_sound_menu, key );
}

void M_Menu_Options_f (void)
{
	Options_MenuInit();
	M_PushMenu ( Options_MenuDraw, Options_MenuKey );
}

/*
=======================================================================

VIDEO MENU

=======================================================================
*/

void M_Menu_Video_f (void)
{
	VID_MenuInit();
	M_PushMenu( VID_MenuDraw, VID_MenuKey );
}

/*
=============================================================================

END GAME MENU

=============================================================================
*/

static int credits_start_time;
static const char **credits;
static char *creditsIndex[256];
static char *creditsBuffer;
static const char *q2mcredits[] =
{
	""
	"^b^s^1Quake2maX",
	"^b^s^3http://www.planetquake.com/quake2max/",
	"",
	"^b^s^2PROGRAMMING",
	"psychospaz",
	"",
	"^b^s^2ART",
	"psychospaz",
	"FuShanks",
	"",
	"^b^s^2PROPEGANDA",
	"RipVTide",
	"",
	"^b^s^2CODE HELP/TUTORIALS/CUT'N'PASTE",
	"Vic",
	"MrG",
	"Sul",
	"Heffo",
	"BramBo",
	"Ion_Pulse",
	"Discoloda",
	"Knightmare",
	"David Pochron",
	"QMB - thekcah@start.com.au",
	"",
	"^b^s^2FORUM MODERATOR",
	"Karen",
	""
/*	0
};
static const char *idcredits[] =
{*/
	"",
	"^b^s^2QUAKE II BY ID SOFTWARE",
	"",
	"^b^s^2PROGRAMMING",
	"John Carmack",
	"John Cash",
	"Brian Hook",
	"",
	"^b^s^2ART",
	"Adrian Carmack",
	"Kevin Cloud",
	"Paul Steed",
	"",
	"^b^s^2LEVEL DESIGN",
	"Tim Willits",
	"American McGee",
	"Christian Antkow",
	"Paul Jaquays",
	"Brandon James",
	"",
	"^b^s^2BIZ",
	"Todd Hollenshead",
	"Barrett (Bear) Alexander",
	"Donna Jackson",
	"",
	"",
	"^b^s^2SPECIAL THANKS",
	"Ben Donges for beta testing",
	"",
	"",
	"",
	"",
	"",
	"",
	"^b^s^2ADDITIONAL SUPPORT",
	"",
	"^b^s^2LINUX PORT AND CTF",
	"Dave \"Zoid\" Kirsch",
	"",
	"^b^s^2CINEMATIC SEQUENCES",
	"Ending Cinematic by Blur Studio - ",
	"Venice, CA",
	"",
	"Environment models for Introduction",
	"Cinematic by Karl Dolgener",
	"",
	"Assistance with environment design",
	"by Cliff Iwai",
	"",
	"^b^s^2SOUND EFFECTS AND MUSIC",
	"Sound Design by Soundelux Media Labs.",
	"Music Composed and Produced by",
	"Soundelux Media Labs.  Special thanks",
	"to Bill Brown, Tom Ozanich, Brian",
	"Celano, Jeff Eisner, and The Soundelux",
	"Players.",
	"",
	"\"Level Music\" by Sonic Mayhem",
	"www.sonicmayhem.com",
	"",
	"\"Quake II Theme Song\"",
	"(C) 1997 Rob Zombie. All Rights",
	"Reserved.",
	"",
	"Track 10 (\"Climb\") by Jer Sypult",
	"",
	"Voice of computers by",
	"Carly Staehlin-Taylor",
	"",
	"^b^s^2THANKS TO ACTIVISION",
	"^b^s^2IN PARTICULAR:",
	"",
	"John Tam",
	"Steve Rosenthal",
	"Marty Stratton",
	"Henk Hartong",
	"",
	"Quake II(tm) (C)1997 Id Software, Inc.",
	"All Rights Reserved.  Distributed by",
	"Activision, Inc. under license.",
	"Quake II(tm), the Id Software name,",
	"the \"Q II\"(tm) logo and id(tm)",
	"logo are trademarks of Id Software,",
	"Inc. Activision(R) is a registered",
	"trademark of Activision, Inc. All",
	"other trademarks and trade names are",
	"properties of their respective owners.",
	0
};

static const char *xatcredits[] =
{
	"^b^s^2QUAKE II MISSION PACK: THE RECKONING",
	"^b^s^2BY",
	"^b^s^2XATRIX ENTERTAINMENT, INC.",
	"",
	"^b^s^2DESIGN AND DIRECTION",
	"Drew Markham",
	"",
	"^b^s^2PRODUCED BY",
	"Greg Goodrich",
	"",
	"^b^s^2PROGRAMMING",
	"Rafael Paiz",
	"",
	"^b^s^2LEVEL DESIGN / ADDITIONAL GAME DESIGN",
	"Alex Mayberry",
	"",
	"^b^s^2LEVEL DESIGN",
	"Mal Blackwell",
	"Dan Koppel",
	"",
	"^b^s^2ART DIRECTION",
	"Michael \"Maxx\" Kaufman",
	"",
	"^b^s^2COMPUTER GRAPHICS SUPERVISOR AND",
	"^b^s^2CHARACTER ANIMATION DIRECTION",
	"Barry Dempsey",
	"",
	"^b^s^2SENIOR ANIMATOR AND MODELER",
	"Jason Hoover",
	"",
	"^b^s^2CHARACTER ANIMATION AND",
	"^b^s^2MOTION CAPTURE SPECIALIST",
	"Amit Doron",
	"",
	"^b^s^2ART",
	"Claire Praderie-Markham",
	"Viktor Antonov",
	"Corky Lehmkuhl",
	"",
	"^b^s^2INTRODUCTION ANIMATION",
	"Dominique Drozdz",
	"",
	"^b^s^2ADDITIONAL LEVEL DESIGN",
	"Aaron Barber",
	"Rhett Baldwin",
	"",
	"^b^s^23D CHARACTER ANIMATION TOOLS",
	"Gerry Tyra, SA Technology",
	"",
	"^b^s^2ADDITIONAL EDITOR TOOL PROGRAMMING",
	"Robert Duffy",
	"",
	"^b^s^2ADDITIONAL PROGRAMMING",
	"Ryan Feltrin",
	"",
	"^b^s^2PRODUCTION COORDINATOR",
	"Victoria Sylvester",
	"",
	"^b^s^2SOUND DESIGN",
	"Gary Bradfield",
	"",
	"^b^s^2MUSIC BY",
	"Sonic Mayhem",
	"",
	"",
	"",
	"^b^s^2SPECIAL THANKS",
	"^b^s^2TO",
	"^b^s^2OUR FRIENDS AT ID SOFTWARE",
	"",
	"John Carmack",
	"John Cash",
	"Brian Hook",
	"Adrian Carmack",
	"Kevin Cloud",
	"Paul Steed",
	"Tim Willits",
	"Christian Antkow",
	"Paul Jaquays",
	"Brandon James",
	"Todd Hollenshead",
	"Barrett (Bear) Alexander",
	"Dave \"Zoid\" Kirsch",
	"Donna Jackson",
	"",
	"",
	"",
	"^b^s^2THANKS TO ACTIVISION",
	"^b^s^2IN PARTICULAR:",
	"",
	"Marty Stratton",
	"Henk \"The Original Ripper\" Hartong",
	"Kevin Kraff",
	"Jamey Gottlieb",
	"Chris Hepburn",
	"",
	"^b^s^2AND THE GAME TESTERS",
	"",
	"Tim Vanlaw",
	"Doug Jacobs",
	"Steven Rosenthal",
	"David Baker",
	"Chris Campbell",
	"Aaron Casillas",
	"Steve Elwell",
	"Derek Johnstone",
	"Igor Krinitskiy",
	"Samantha Lee",
	"Michael Spann",
	"Chris Toft",
	"Juan Valdes",
	"",
	"^b^s^2THANKS TO INTERGRAPH COMPUTER SYTEMS",
	"^b^s^2IN PARTICULAR:",
	"",
	"Michael T. Nicolaou",
	"",
	"",
	"Quake II Mission Pack: The Reckoning",
	"(tm) (C)1998 Id Software, Inc. All",
	"Rights Reserved. Developed by Xatrix",
	"Entertainment, Inc. for Id Software,",
	"Inc. Distributed by Activision Inc.",
	"under license. Quake(R) is a",
	"registered trademark of Id Software,",
	"Inc. Quake II Mission Pack: The",
	"Reckoning(tm), Quake II(tm), the Id",
	"Software name, the \"Q II\"(tm) logo",
	"and id(tm) logo are trademarks of Id",
	"Software, Inc. Activision(R) is a",
	"registered trademark of Activision,",
	"Inc. Xatrix(R) is a registered",
	"trademark of Xatrix Entertainment,",
	"Inc. All other trademarks and trade",
	"names are properties of their",
	"respective owners.",
	0
};

static const char *roguecredits[] =
{
	"^b^s^2QUAKE II MISSION PACK 2: GROUND ZERO",
	"^b^s^2BY",
	"^b^s^2ROGUE ENTERTAINMENT, INC.",
	"",
	"^b^s^2PRODUCED BY",
	"Jim Molinets",
	"",
	"^b^s^2PROGRAMMING",
	"Peter Mack",
	"Patrick Magruder",
	"",
	"^b^s^2LEVEL DESIGN",
	"Jim Molinets",
	"Cameron Lamprecht",
	"Berenger Fish",
	"Robert Selitto",
	"Steve Tietze",
	"Steve Thoms",
	"",
	"^b^s^2ART DIRECTION",
	"Rich Fleider",
	"",
	"^b^s^2ART",
	"Rich Fleider",
	"Steve Maines",
	"Won Choi",
	"",
	"^b^s^2ANIMATION SEQUENCES",
	"Creat Studios",
	"Steve Maines",
	"",
	"^b^s^2ADDITIONAL LEVEL DESIGN",
	"Rich Fleider",
	"Steve Maines",
	"Peter Mack",
	"",
	"^b^s^2SOUND",
	"James Grunke",
	"",
	"^b^s^2GROUND ZERO THEME",
	"^b^s^2AND",
	"^b^s^2MUSIC BY",
	"Sonic Mayhem",
	"",
	"^b^s^2VWEP MODELS",
	"Brent \"Hentai\" Dill",
	"",
	"",
	"",
	"^b^s^2SPECIAL THANKS",
	"^b^s^2TO",
	"^b^s^2OUR FRIENDS AT ID SOFTWARE",
	"",
	"John Carmack",
	"John Cash",
	"Brian Hook",
	"Adrian Carmack",
	"Kevin Cloud",
	"Paul Steed",
	"Tim Willits",
	"Christian Antkow",
	"Paul Jaquays",
	"Brandon James",
	"Todd Hollenshead",
	"Barrett (Bear) Alexander",
	"Katherine Anna Kang",
	"Donna Jackson",
	"Dave \"Zoid\" Kirsch",
	"",
	"",
	"",
	"+THANKS TO ACTIVISION",
	"+IN PARTICULAR:",
	"",
	"Marty Stratton",
	"Henk Hartong",
	"Mitch Lasky",
	"Steve Rosenthal",
	"Steve Elwell",
	"",
	"^b^s^2AND THE GAME TESTERS",
	"",
	"The Ranger Clan",
	"Dave \"Zoid\" Kirsch",
	"Nihilistic Software",
	"Robert Duffy",
	"",
	"And Countless Others",
	"",
	"",
	"",
	"Quake II Mission Pack 2: Ground Zero",
	"(tm) (C)1998 Id Software, Inc. All",
	"Rights Reserved. Developed by Rogue",
	"Entertainment, Inc. for Id Software,",
	"Inc. Distributed by Activision Inc.",
	"under license. Quake(R) is a",
	"registered trademark of Id Software,",
	"Inc. Quake II Mission Pack 2: Ground",
	"Zero(tm), Quake II(tm), the Id",
	"Software name, the \"Q II\"(tm) logo",
	"and id(tm) logo are trademarks of Id",
	"Software, Inc. Activision(R) is a",
	"registered trademark of Activision,",
	"Inc. Rogue(R) is a registered",
	"trademark of Rogue Entertainment,",
	"Inc. All other trademarks and trade",
	"names are properties of their",
	"respective owners.",
	0
};

int stringLengthExtra ( const char *string);
void M_Credits_MenuDraw( void )
{
	float alpha, time = (cls.realtime - credits_start_time) * 0.05;
	int i, y, x, len;

	/*
	** draw the credits
	*/
	y = viddef.height - time;

	for ( i = 0; credits[i] ; y += MENU_FONT_SIZE*1.25, i++ )
	{
		int j, stringoffset = 0;
		int bold = false;

		if ( y <= -MENU_FONT_SIZE )
			continue;
		if ( y > viddef.height )
			continue;

		if ( credits[i][0] == '+' )
		{
			bold = true;
			stringoffset = 1;
		}
		else
		{
			bold = false;
			stringoffset = 0;
		}

		if (y > 3*viddef.height/4)
		{
			float y_test, h_test;
			y_test = y - (3.0/4.0)*viddef.height;
			h_test = viddef.height/4;

			alpha = 1-(y_test/h_test);

			if (alpha>1)alpha=1; if (alpha<0)alpha=0;
		}
		else if (y < viddef.height/4)
		{
			float y_test, h_test;
			y_test = y;
			h_test = viddef.height/4;

			alpha = y_test/h_test;

			if (alpha>1)alpha=1; if (alpha<0)alpha=0;
		}
		else 
			alpha = 1;

		len = strlen(credits[i]) - stringLengthExtra(credits[i]);

		x = ( viddef.width - len * MENU_FONT_SIZE - stringoffset * MENU_FONT_SIZE ) / 2 + ( stringoffset ) * MENU_FONT_SIZE;
		DrawString(x, y, credits[i], alpha*255);
	}

	if ( y < 0 )
		credits_start_time = cls.realtime;
}

const char *M_Credits_Key( int key )
{
	switch (key)
	{
	case K_ESCAPE:
		if (creditsBuffer)
			FS_FreeFile (creditsBuffer);
		M_PopMenu ();
		break;
	default:
		return NULL;
	}

	return menu_out_sound;

}

extern int Developer_searchpath (int who);

void M_Menu_Credits_f( void )
{
	int		n;
	int		count;
	char	*p;
	int		isdeveloper = 0;

	creditsBuffer = NULL;
	count = FS_LoadFile ("credits", &creditsBuffer);
	if (count != -1)
	{
		p = creditsBuffer;
		for (n = 0; n < 255; n++)
		{
			creditsIndex[n] = p;
			while (*p != '\r' && *p != '\n')
			{
				p++;
				if (--count == 0)
					break;
			}
			if (*p == '\r')
			{
				*p++ = 0;
				if (--count == 0)
					break;
			}
			*p++ = 0;
			if (--count == 0)
				break;
		}
		creditsIndex[++n] = 0;
		credits = creditsIndex;
	}
	else
	{
		isdeveloper = Developer_searchpath (1);
		
		if (isdeveloper == 1)			// xatrix
			credits = xatcredits;
		else if (isdeveloper == 2)		// ROGUE
			credits = roguecredits;
		else
		{
		//	credits = idcredits;
			credits = q2mcredits;
		}
	}

	credits_start_time = cls.realtime;
	M_PushMenu( M_Credits_MenuDraw, M_Credits_Key);
}

/*
=============================================================================

GAME MENU

=============================================================================
*/

static int		m_game_cursor;

static menuframework_s	s_game_menu;
static menuaction_s		s_easy_game_action;
static menuaction_s		s_medium_game_action;
static menuaction_s		s_hard_game_action;
static menuaction_s		s_nitemare_game_action;
static menuaction_s		s_load_game_action;
static menuaction_s		s_save_game_action;
static menuaction_s		s_credits_action;

static void StartGame( void )
{
	// disable updates and start the cinematic going
	cl.servercount = -1;
	M_ForceMenuOff ();
	Cvar_SetValue( "deathmatch", 0 );
	Cvar_SetValue( "coop", 0 );

	Cvar_SetValue( "gamerules", 0 );		//PGM

	Cbuf_AddText ("loading ; killserver ; wait ; newgame\n");
	cls.key_dest = key_game;
}

static void EasyGameFunc( void *data )
{
	Cvar_ForceSet( "skill", "0" );
	StartGame();
}

static void MediumGameFunc( void *data )
{
	Cvar_ForceSet( "skill", "1" );
	StartGame();
}

static void HardGameFunc( void *data )
{
	Cvar_ForceSet( "skill", "2" );
	StartGame();
}

static void NightmareGameFunc( void *data )
{
	Cvar_ForceSet( "skill", "3" );
	StartGame();
}

static void LoadGameFunc( void *unused )
{
	M_Menu_LoadGame_f ();
}

static void SaveGameFunc( void *unused )
{
	M_Menu_SaveGame_f();
}

static void CreditsFunc( void *unused )
{
	M_Menu_Credits_f();
}

void Game_MenuInit( void )
{
	s_game_menu.y = viddef.height * 0.50 - scaledVideo(64);
	s_game_menu.x = viddef.width * 0.50;
	s_game_menu.nitems = 0;

	/*************************************************************/

	s_easy_game_action.generic.type			= MTYPE_ACTION;
	s_easy_game_action.generic.flags		= QMF_LEFT_JUSTIFY;
	s_easy_game_action.generic.x			= 0;
	s_easy_game_action.generic.y			= MENU_FONT_SIZE +2;
	s_easy_game_action.generic.name			= "easy";
	s_easy_game_action.generic.callback		= EasyGameFunc;

	s_medium_game_action.generic.type		= MTYPE_ACTION;
	s_medium_game_action.generic.flags		= QMF_LEFT_JUSTIFY;
	s_medium_game_action.generic.x			= 0;
	s_medium_game_action.generic.y			= MENU_FONT_SIZE*2 +2;
	s_medium_game_action.generic.name		= "medium";
	s_medium_game_action.generic.callback	= MediumGameFunc;

	s_hard_game_action.generic.type			= MTYPE_ACTION;
	s_hard_game_action.generic.flags		= QMF_LEFT_JUSTIFY;
	s_hard_game_action.generic.x			= 0;
	s_hard_game_action.generic.y			= MENU_FONT_SIZE*3 +2;
	s_hard_game_action.generic.name			= "hard";
	s_hard_game_action.generic.callback		= HardGameFunc;

	s_nitemare_game_action.generic.type		= MTYPE_ACTION;
	s_nitemare_game_action.generic.flags	= QMF_LEFT_JUSTIFY;
	s_nitemare_game_action.generic.x		= 0;
	s_nitemare_game_action.generic.y		= MENU_FONT_SIZE*4 +2;
	s_nitemare_game_action.generic.name		= "nightmare";
	s_nitemare_game_action.generic.callback	= NightmareGameFunc;

	/*************************************************************/

	s_load_game_action.generic.type	= MTYPE_ACTION;
	s_load_game_action.generic.flags  = QMF_LEFT_JUSTIFY;
	s_load_game_action.generic.x		= 0;
	s_load_game_action.generic.y		= MENU_FONT_SIZE*6+2;
	s_load_game_action.generic.name	= "load game";
	s_load_game_action.generic.callback = LoadGameFunc;

	s_save_game_action.generic.type	= MTYPE_ACTION;
	s_save_game_action.generic.flags  = QMF_LEFT_JUSTIFY;
	s_save_game_action.generic.x		= 0;
	s_save_game_action.generic.y		= MENU_FONT_SIZE*7 +2;
	s_save_game_action.generic.name	= "save game";
	s_save_game_action.generic.callback = SaveGameFunc;

	/*************************************************************/

	s_credits_action.generic.type	= MTYPE_ACTION;
	s_credits_action.generic.flags  = QMF_LEFT_JUSTIFY;
	s_credits_action.generic.x		= 0;
	s_credits_action.generic.y		= MENU_FONT_SIZE*9 +2;
	s_credits_action.generic.name	= "credits";
	s_credits_action.generic.callback = CreditsFunc;

	/*************************************************************/

	Menu_AddItem( &s_game_menu, ( void * ) &s_easy_game_action );
	Menu_AddItem( &s_game_menu, ( void * ) &s_medium_game_action );
	Menu_AddItem( &s_game_menu, ( void * ) &s_hard_game_action );
	Menu_AddItem( &s_game_menu, ( void * ) &s_nitemare_game_action );


	Menu_AddItem( &s_game_menu, ( void * ) &s_load_game_action );
	Menu_AddItem( &s_game_menu, ( void * ) &s_save_game_action );

	Menu_AddItem( &s_game_menu, ( void * ) &s_credits_action );

	Menu_Center( &s_game_menu );
}

void Game_MenuDraw( void )
{
	M_Banner( "m_banner_game" );
	Menu_AdjustCursor( &s_game_menu, 1 );
	Menu_Draw( &s_game_menu );
}

const char *Game_MenuKey( int key )
{
	return Default_MenuKey( &s_game_menu, key );
}

void M_Menu_Game_f (void)
{
	Game_MenuInit();
	M_PushMenu( Game_MenuDraw, Game_MenuKey );
	m_game_cursor = 1;
}

/*
=============================================================================

LOADGAME MENU

=============================================================================
*/

#define	MAX_SAVEGAMES	15

static menuframework_s	s_savegame_menu;

static menuframework_s	s_loadgame_menu;
static menuaction_s		s_loadgame_actions[MAX_SAVEGAMES];

char		m_savestrings[MAX_SAVEGAMES][32];
qboolean	m_savevalid[MAX_SAVEGAMES];

void Create_Savestrings (void)
{
	int		i;
	FILE	*f;
	char	name[MAX_OSPATH];

	for (i=0 ; i<MAX_SAVEGAMES ; i++)
	{
		Com_sprintf (name, sizeof(name), "%s/save/save%i/server.ssv", FS_Gamedir(), i);
		f = fopen (name, "rb");
		if (!f)
		{
			strcpy (m_savestrings[i], "<EMPTY>");
			m_savevalid[i] = false;
		}
		else
		{
			FS_Read (m_savestrings[i], sizeof(m_savestrings[i]), f);
			fclose (f);
			m_savevalid[i] = true;
		}
	}
}

void LoadGameCallback( void *self )
{
	menuaction_s *a = ( menuaction_s * ) self;

	if ( m_savevalid[ a->generic.localdata[0] ] )
		Cbuf_AddText (va("load save%i\n",  a->generic.localdata[0] ) );
	M_ForceMenuOff ();
}

void LoadGame_MenuInit( void )
{
	int i;

	s_loadgame_menu.x = viddef.width / 2 - scaledVideo(MENU_FONT_SIZE*15);
	s_loadgame_menu.y = viddef.height / 2 - scaledVideo(MENU_FONT_SIZE*7.25);
	s_loadgame_menu.nitems = 0;

	Create_Savestrings();

	for ( i = 0; i < MAX_SAVEGAMES; i++ )
	{
		s_loadgame_actions[i].generic.name			= m_savestrings[i];
		s_loadgame_actions[i].generic.flags			= QMF_LEFT_JUSTIFY;
		s_loadgame_actions[i].generic.localdata[0]	= i;
		s_loadgame_actions[i].generic.callback		= LoadGameCallback;

		s_loadgame_actions[i].generic.x = 0;
		s_loadgame_actions[i].generic.y = ( i ) * (MENU_FONT_SIZE+2);
		if (i>0)	// separate from autosave
			s_loadgame_actions[i].generic.y += MENU_FONT_SIZE+5;

		s_loadgame_actions[i].generic.type = MTYPE_ACTION;

		Menu_AddItem( &s_loadgame_menu, &s_loadgame_actions[i] );
	}
}

void LoadGame_MenuDraw( void )
{
	M_Banner( "m_banner_load_game" );
//	Menu_AdjustCursor( &s_loadgame_menu, 1 );
	Menu_Draw( &s_loadgame_menu );
}

const char *LoadGame_MenuKey( int key )
{
	if ( key == K_ESCAPE || key == K_ENTER )
	{
		s_savegame_menu.cursor = s_loadgame_menu.cursor - 1;
		if ( s_savegame_menu.cursor < 0 )
			s_savegame_menu.cursor = 0;
	}
	return Default_MenuKey( &s_loadgame_menu, key );
}

void M_Menu_LoadGame_f (void)
{
	LoadGame_MenuInit();
	M_PushMenu( LoadGame_MenuDraw, LoadGame_MenuKey );
}


/*
=============================================================================

SAVEGAME MENU

=============================================================================
*/
static menuframework_s	s_savegame_menu;
static menuaction_s		s_savegame_actions[MAX_SAVEGAMES];

void SaveGameCallback( void *self )
{
	menuaction_s *a = ( menuaction_s * ) self;

	Cbuf_AddText (va("save save%i\n", a->generic.localdata[0] ));
	M_ForceMenuOff ();
}

void SaveGame_MenuDraw( void )
{
	M_Banner( "m_banner_save_game" );
	Menu_AdjustCursor( &s_savegame_menu, 1 );
	Menu_Draw( &s_savegame_menu );
}

void SaveGame_MenuInit( void )
{
	int i;

	s_savegame_menu.x = viddef.width / 2 - scaledVideo(15*MENU_FONT_SIZE);
	s_savegame_menu.y = viddef.height / 2 - scaledVideo(MENU_FONT_SIZE*7.25);
	s_savegame_menu.nitems = 0;

	Create_Savestrings();

	// don't include the autosave slot
	for ( i = 0; i < MAX_SAVEGAMES-1; i++ )
	{
		s_savegame_actions[i].generic.name = m_savestrings[i+1];
		s_savegame_actions[i].generic.localdata[0] = i+1;
		s_savegame_actions[i].generic.flags = QMF_LEFT_JUSTIFY;
		s_savegame_actions[i].generic.callback = SaveGameCallback;

		s_savegame_actions[i].generic.x = 0;
		s_savegame_actions[i].generic.y = ( i ) * (MENU_FONT_SIZE+2);

		s_savegame_actions[i].generic.type = MTYPE_ACTION;

		Menu_AddItem( &s_savegame_menu, &s_savegame_actions[i] );
	}
}

const char *SaveGame_MenuKey( int key )
{
	if ( key == K_ENTER || key == K_ESCAPE )
	{
		s_loadgame_menu.cursor = s_savegame_menu.cursor - 1;
		if ( s_loadgame_menu.cursor < 0 )
			s_loadgame_menu.cursor = 0;
	}
	return Default_MenuKey( &s_savegame_menu, key );
}

void M_Menu_SaveGame_f (void)
{
	if (!Com_ServerState())
		return;		// not playing a game

	SaveGame_MenuInit();
	M_PushMenu( SaveGame_MenuDraw, SaveGame_MenuKey );
	Create_Savestrings ();
}


/*
=============================================================================

JOIN SERVER MENU

=============================================================================
*/
#define MAX_LOCAL_SERVERS 8

static menuframework_s	s_joinserver_menu;
static menuseparator_s	s_joinserver_server_title;
static menuaction_s		s_joinserver_search_action;
static menuaction_s		s_joinserver_address_book_action;
static menuaction_s		s_joinserver_server_actions[MAX_LOCAL_SERVERS];

int		m_num_servers;
#define	NO_SERVER_STRING	"<no server>"

// user readable information
static char local_server_names[MAX_LOCAL_SERVERS][80];

// network address
static netadr_t local_server_netadr[MAX_LOCAL_SERVERS];

void M_AddToServerList (netadr_t adr, char *info)
{
	int		i;

	if (m_num_servers == MAX_LOCAL_SERVERS)
		return;
	while ( *info == ' ' )
		info++;

	// ignore if duplicated
	for (i=0 ; i<m_num_servers ; i++)
		if (!strcmp(info, local_server_names[i]))
			return;

	local_server_netadr[m_num_servers] = adr;
	strncpy (local_server_names[m_num_servers], info, sizeof(local_server_names[0])-1);
	m_num_servers++;
}


void JoinServerFunc( void *self )
{
	char	buffer[128];
	int		index;

	index = ( menuaction_s * ) self - s_joinserver_server_actions;

	if ( Q_stricmp( local_server_names[index], NO_SERVER_STRING ) == 0 )
		return;

	if (index >= m_num_servers)
		return;

	Com_sprintf (buffer, sizeof(buffer), "connect %s\n", NET_AdrToString (local_server_netadr[index]));
	Cbuf_AddText (buffer);
	M_ForceMenuOff ();
}

void AddressBookFunc( void *self )
{
	M_Menu_AddressBook_f();
}

void NullCursorDraw( void *self )
{
}

void SearchLocalGames( void )
{
	int		i;

	m_num_servers = 0;
	for (i=0 ; i<MAX_LOCAL_SERVERS ; i++)
		strcpy (local_server_names[i], NO_SERVER_STRING);

	// the text box won't show up unless we do a buffer swap
	re.EndFrame();

	// send out info packets
	CL_PingServers_f();
}

void SearchLocalGamesFunc( void *self )
{
	SearchLocalGames();
}

void JoinServer_MenuInit( void )
{
	int i;

	s_joinserver_menu.x = viddef.width * 0.50 - scaledVideo(120);
	s_joinserver_menu.y = viddef.height * 0.50 - scaledVideo(58);
	s_joinserver_menu.nitems = 0;

	s_joinserver_address_book_action.generic.type	= MTYPE_ACTION;
	s_joinserver_address_book_action.generic.name	= "address book";
	s_joinserver_address_book_action.generic.flags	= QMF_LEFT_JUSTIFY;
	s_joinserver_address_book_action.generic.x		= 0;
	s_joinserver_address_book_action.generic.y		= 0;
	s_joinserver_address_book_action.generic.callback = AddressBookFunc;

	s_joinserver_search_action.generic.type = MTYPE_ACTION;
	s_joinserver_search_action.generic.name	= "refresh server list";
	s_joinserver_search_action.generic.flags	= QMF_LEFT_JUSTIFY;
	s_joinserver_search_action.generic.x	= 0;
	s_joinserver_search_action.generic.y	= MENU_FONT_SIZE*2+2;
	s_joinserver_search_action.generic.callback = SearchLocalGamesFunc;
	s_joinserver_search_action.generic.statusbar = "search for servers";

	s_joinserver_server_title.generic.type = MTYPE_SEPARATOR;
	s_joinserver_server_title.generic.name = "connect to...";
	s_joinserver_server_title.generic.x    = 80;
	s_joinserver_server_title.generic.y	   = MENU_FONT_SIZE*3+2;

	for ( i = 0; i < MAX_LOCAL_SERVERS; i++ )
	{
		s_joinserver_server_actions[i].generic.type	= MTYPE_ACTION;
		strcpy (local_server_names[i], NO_SERVER_STRING);
		s_joinserver_server_actions[i].generic.name	= local_server_names[i];
		s_joinserver_server_actions[i].generic.flags	= QMF_LEFT_JUSTIFY;
		s_joinserver_server_actions[i].generic.x		= 0;
		s_joinserver_server_actions[i].generic.y		= MENU_FONT_SIZE*4+2 + i*(MENU_FONT_SIZE+2);
		s_joinserver_server_actions[i].generic.callback = JoinServerFunc;
		s_joinserver_server_actions[i].generic.statusbar = "press ENTER to connect";
	}

	Menu_AddItem( &s_joinserver_menu, &s_joinserver_address_book_action );
	Menu_AddItem( &s_joinserver_menu, &s_joinserver_server_title );
	Menu_AddItem( &s_joinserver_menu, &s_joinserver_search_action );

	for ( i = 0; i < 8; i++ )
		Menu_AddItem( &s_joinserver_menu, &s_joinserver_server_actions[i] );

	Menu_Center( &s_joinserver_menu );

	SearchLocalGames();
}

void JoinServer_MenuDraw(void)
{
	M_Banner( "m_banner_join_server" );
	Menu_Draw( &s_joinserver_menu );
}


const char *JoinServer_MenuKey( int key )
{
	return Default_MenuKey( &s_joinserver_menu, key );
}

void M_Menu_JoinServer_f (void)
{
	JoinServer_MenuInit();
	M_PushMenu( JoinServer_MenuDraw, JoinServer_MenuKey );
}


/*
=============================================================================

START SERVER MENU

=============================================================================
*/
static menuframework_s s_startserver_menu;
static char **mapnames;
static int	  nummaps;

static menuaction_s	s_startserver_start_action;
static menuaction_s	s_startserver_dmoptions_action;
static menufield_s	s_timelimit_field;
static menufield_s	s_fraglimit_field;
static menufield_s	s_maxclients_field;
static menufield_s	s_hostname_field;
static menulist_s	s_startmap_list;
static menulist_s	s_rules_box;

void DMOptionsFunc( void *self )
{
	if (s_rules_box.curvalue == 1)
		return;
	M_Menu_DMOptions_f();
}

void RulesChangeFunc ( void *self )
{
	// DM
	if (s_rules_box.curvalue == 0)
	{
		s_maxclients_field.generic.statusbar = NULL;
		s_startserver_dmoptions_action.generic.statusbar = NULL;
	}
	else if(s_rules_box.curvalue == 1)		// coop				// PGM
	{
		s_maxclients_field.generic.statusbar = "4 maximum for cooperative";
		if (atoi(s_maxclients_field.buffer) > 4)
			strcpy( s_maxclients_field.buffer, "4" );
		s_startserver_dmoptions_action.generic.statusbar = "N/A for cooperative";
	}
//=====
//PGM
	// ROGUE GAMES
	else if(Developer_searchpath(2) == 2)
	{
		if (s_rules_box.curvalue == 2)			// tag	
		{
			s_maxclients_field.generic.statusbar = NULL;
			s_startserver_dmoptions_action.generic.statusbar = NULL;
		}
/*
		else if(s_rules_box.curvalue == 3)		// deathball
		{
			s_maxclients_field.generic.statusbar = NULL;
			s_startserver_dmoptions_action.generic.statusbar = NULL;
		}
*/
	}
//PGM
//=====
}

void StartServerActionFunc( void *self )
{
	char	startmap[1024];
	int		timelimit;
	int		fraglimit;
	int		maxclients;
	char	*spot;

	strcpy( startmap, strchr( mapnames[s_startmap_list.curvalue], '\n' ) + 1 );

	maxclients  = atoi( s_maxclients_field.buffer );
	timelimit	= atoi( s_timelimit_field.buffer );
	fraglimit	= atoi( s_fraglimit_field.buffer );

	Cvar_SetValue( "maxclients", ClampCvar( 0, maxclients, maxclients ) );
	Cvar_SetValue ("timelimit", ClampCvar( 0, timelimit, timelimit ) );
	Cvar_SetValue ("fraglimit", ClampCvar( 0, fraglimit, fraglimit ) );
	Cvar_Set("hostname", s_hostname_field.buffer );
//	Cvar_SetValue ("deathmatch", !s_rules_box.curvalue );
//	Cvar_SetValue ("coop", s_rules_box.curvalue );

//PGM
	if((s_rules_box.curvalue < 2) || (Developer_searchpath(2) != 2))
	{
		Cvar_SetValue ("deathmatch", !s_rules_box.curvalue );
		Cvar_SetValue ("coop", s_rules_box.curvalue );
		Cvar_SetValue ("gamerules", 0 );
	}
	else
	{
		Cvar_SetValue ("deathmatch", 1 );	// deathmatch is always true for rogue games, right?
		Cvar_SetValue ("coop", 0 );			// FIXME - this might need to depend on which game we're running
		Cvar_SetValue ("gamerules", s_rules_box.curvalue );
	}
//PGM

	spot = NULL;
	if (s_rules_box.curvalue == 1)		// PGM
	{
 		if(Q_stricmp(startmap, "bunk1") == 0)
  			spot = "start";
 		else if(Q_stricmp(startmap, "mintro") == 0)
  			spot = "start";
 		else if(Q_stricmp(startmap, "fact1") == 0)
  			spot = "start";
 		else if(Q_stricmp(startmap, "power1") == 0)
  			spot = "pstart";
 		else if(Q_stricmp(startmap, "biggun") == 0)
  			spot = "bstart";
 		else if(Q_stricmp(startmap, "hangar1") == 0)
  			spot = "unitstart";
 		else if(Q_stricmp(startmap, "city1") == 0)
  			spot = "unitstart";
 		else if(Q_stricmp(startmap, "boss1") == 0)
			spot = "bosstart";
	}

	if (spot)
	{
		if (Com_ServerState())
			Cbuf_AddText ("disconnect\n");
		Cbuf_AddText (va("gamemap \"*%s$%s\"\n", startmap, spot));
	}
	else
	{
		Cbuf_AddText (va("map %s\n", startmap));
	}

	M_ForceMenuOff ();
}

void StartServer_MenuInit( void )
{
	static const char *dm_coop_names[] =
	{
		"deathmatch",
		"cooperative",
		0
	};
//=======
//PGM
	static const char *dm_coop_names_rogue[] =
	{
		"deathmatch",
		"cooperative",
		"tag",
//		"deathball",
		0
	};
//PGM
//=======
	char *buffer;
	char  mapsname[1024];
	char *s;
	int length;
	int i;
	FILE *fp;

	/*
	** load the list of map names
	*/
	Com_sprintf( mapsname, sizeof( mapsname ), "%s/maps.lst", FS_Gamedir() );
	if ( ( fp = fopen( mapsname, "rb" ) ) == 0 )
	{
		if ( ( length = FS_LoadFile( "maps.lst", ( void ** ) &buffer ) ) == -1 )
			Com_Error( ERR_DROP, "couldn't find maps.lst\n" );
	}
	else
	{
#ifdef _WIN32
		length = filelength( fileno( fp  ) );
#else
		fseek(fp, 0, SEEK_END);
		length = ftell(fp);
		fseek(fp, 0, SEEK_SET);
#endif
		buffer = malloc( length );
		fread( buffer, length, 1, fp );
	}

	s = buffer;

	i = 0;
	while ( i < length )
	{
		if ( s[i] == '\r' )
			nummaps++;
		i++;
	}

	if ( nummaps == 0 )
		Com_Error( ERR_DROP, "no maps in maps.lst\n" );

	mapnames = malloc( sizeof( char * ) * ( nummaps + 1 ) );
	memset( mapnames, 0, sizeof( char * ) * ( nummaps + 1 ) );

	s = buffer;

	for ( i = 0; i < nummaps; i++ )
	{
    char  shortname[MAX_TOKEN_CHARS];
    char  longname[MAX_TOKEN_CHARS];
		char  scratch[200];
		int		j, l;

		strcpy( shortname, COM_Parse( &s ) );
		l = strlen(shortname);
		for (j=0 ; j<l ; j++)
			shortname[j] = toupper(shortname[j]);
		strcpy( longname, COM_Parse( &s ) );
		Com_sprintf( scratch, sizeof( scratch ), "%s\n%s", longname, shortname );

		mapnames[i] = malloc( strlen( scratch ) + 1 );
		strcpy( mapnames[i], scratch );
	}
	mapnames[nummaps] = 0;

	if ( fp != 0 )
	{
		fp = 0;
		free( buffer );
	}
	else
	{
		FS_FreeFile( buffer );
	}

	/*
	** initialize the menu stuff
	*/
	s_startserver_menu.y = viddef.height * 0.50 - scaledVideo(58);
	s_startserver_menu.x = viddef.width * 0.50;
	s_startserver_menu.nitems = 0;

	s_startmap_list.generic.type = MTYPE_SPINCONTROL;
	s_startmap_list.generic.x	= 0;
	s_startmap_list.generic.y	= 0;
	s_startmap_list.generic.name	= "initial map";
	s_startmap_list.itemnames = mapnames;

	s_rules_box.generic.type = MTYPE_SPINCONTROL;
	s_rules_box.generic.x	= 0;
	s_rules_box.generic.y	= MENU_FONT_SIZE*3+4;
	s_rules_box.generic.name	= "rules";
	
//PGM - rogue games only available with rogue DLL.
	if(Developer_searchpath(2) == 2)
		s_rules_box.itemnames = dm_coop_names_rogue;
	else
		s_rules_box.itemnames = dm_coop_names;
//PGM

	if (Cvar_VariableValue("coop"))
		s_rules_box.curvalue = 1;
	else
		s_rules_box.curvalue = 0;
	s_rules_box.generic.callback = RulesChangeFunc;

	s_timelimit_field.generic.type = MTYPE_FIELD;
	s_timelimit_field.generic.name = "time limit";
	s_timelimit_field.generic.flags = QMF_NUMBERSONLY;
	s_timelimit_field.generic.x	= 0;
	s_timelimit_field.generic.y	= MENU_FONT_SIZE*5+4;
	s_timelimit_field.generic.statusbar = "0 = no limit";
	s_timelimit_field.length = 3;
	s_timelimit_field.visible_length = 3;
	strcpy( s_timelimit_field.buffer, Cvar_VariableString("timelimit") );

	s_fraglimit_field.generic.type = MTYPE_FIELD;
	s_fraglimit_field.generic.name = "frag limit";
	s_fraglimit_field.generic.flags = QMF_NUMBERSONLY;
	s_fraglimit_field.generic.x	= 0;
	s_fraglimit_field.generic.y	= MENU_FONT_SIZE*7+4;
	s_fraglimit_field.generic.statusbar = "0 = no limit";
	s_fraglimit_field.length = 3;
	s_fraglimit_field.visible_length = 3;
	strcpy( s_fraglimit_field.buffer, Cvar_VariableString("fraglimit") );

	/*
	** maxclients determines the maximum number of players that can join
	** the game.  If maxclients is only "1" then we should default the menu
	** option to 8 players, otherwise use whatever its current value is. 
	** Clamping will be done when the server is actually started.
	*/
	s_maxclients_field.generic.type = MTYPE_FIELD;
	s_maxclients_field.generic.name = "max players";
	s_maxclients_field.generic.flags = QMF_NUMBERSONLY;
	s_maxclients_field.generic.x	= 0;
	s_maxclients_field.generic.y	= MENU_FONT_SIZE*9+4;
	s_maxclients_field.generic.statusbar = NULL;
	s_maxclients_field.length = 3;
	s_maxclients_field.visible_length = 3;
	if ( Cvar_VariableValue( "maxclients" ) == 1 )
		strcpy( s_maxclients_field.buffer, "8" );
	else 
		strcpy( s_maxclients_field.buffer, Cvar_VariableString("maxclients") );

	s_hostname_field.generic.type = MTYPE_FIELD;
	s_hostname_field.generic.name = "hostname";
	s_hostname_field.generic.flags = 0;
	s_hostname_field.generic.x	= 0;
	s_hostname_field.generic.y	= MENU_FONT_SIZE*11+4;
	s_hostname_field.generic.statusbar = NULL;
	s_hostname_field.length = 12;
	s_hostname_field.visible_length = 12;
	strcpy( s_hostname_field.buffer, Cvar_VariableString("hostname") );

	s_startserver_dmoptions_action.generic.type = MTYPE_ACTION;
	s_startserver_dmoptions_action.generic.name	= " deathmatch flags";
	s_startserver_dmoptions_action.generic.flags= QMF_LEFT_JUSTIFY;
	s_startserver_dmoptions_action.generic.x	= 24;
	s_startserver_dmoptions_action.generic.y	= MENU_FONT_SIZE*13+4;
	s_startserver_dmoptions_action.generic.statusbar = NULL;
	s_startserver_dmoptions_action.generic.callback = DMOptionsFunc;

	s_startserver_start_action.generic.type = MTYPE_ACTION;
	s_startserver_start_action.generic.name	= " begin";
	s_startserver_start_action.generic.flags= QMF_LEFT_JUSTIFY;
	s_startserver_start_action.generic.x	= 24;
	s_startserver_start_action.generic.y	= MENU_FONT_SIZE*15+4;
	s_startserver_start_action.generic.callback = StartServerActionFunc;

	Menu_AddItem( &s_startserver_menu, &s_startmap_list );
	Menu_AddItem( &s_startserver_menu, &s_rules_box );
	Menu_AddItem( &s_startserver_menu, &s_timelimit_field );
	Menu_AddItem( &s_startserver_menu, &s_fraglimit_field );
	Menu_AddItem( &s_startserver_menu, &s_maxclients_field );
	Menu_AddItem( &s_startserver_menu, &s_hostname_field );
	Menu_AddItem( &s_startserver_menu, &s_startserver_dmoptions_action );
	Menu_AddItem( &s_startserver_menu, &s_startserver_start_action );

	Menu_Center( &s_startserver_menu );

	// call this now to set proper inital state
	RulesChangeFunc ( NULL );
}

void StartServer_MenuDraw(void)
{

	M_Banner( "m_banner_start_server" );
	Menu_Draw( &s_startserver_menu );
}

const char *StartServer_MenuKey( int key )
{
	if ( key == K_ESCAPE )
	{
		if ( mapnames )
		{
			int i;

			for ( i = 0; i < nummaps; i++ )
				free( mapnames[i] );
			free( mapnames );
		}
		mapnames = 0;
		nummaps = 0;
	}

	return Default_MenuKey( &s_startserver_menu, key );
}

void M_Menu_StartServer_f (void)
{
	StartServer_MenuInit();
	M_PushMenu( StartServer_MenuDraw, StartServer_MenuKey );
}

/*
=============================================================================

DMOPTIONS BOOK MENU

=============================================================================
*/
static char dmoptions_statusbar[128];

static menuframework_s s_dmoptions_menu;

static menulist_s	s_friendlyfire_box;
static menulist_s	s_falls_box;
static menulist_s	s_weapons_stay_box;
static menulist_s	s_instant_powerups_box;
static menulist_s	s_powerups_box;
static menulist_s	s_health_box;
static menulist_s	s_spawn_farthest_box;
static menulist_s	s_teamplay_box;
static menulist_s	s_samelevel_box;
static menulist_s	s_force_respawn_box;
static menulist_s	s_armor_box;
static menulist_s	s_allow_exit_box;
static menulist_s	s_infinite_ammo_box;
static menulist_s	s_fixed_fov_box;
static menulist_s	s_quad_drop_box;

//ROGUE
static menulist_s	s_no_mines_box;
static menulist_s	s_no_nukes_box;
static menulist_s	s_stack_double_box;
static menulist_s	s_no_spheres_box;
//ROGUE

static void DMFlagCallback( void *self )
{
	menulist_s *f = ( menulist_s * ) self;
	int flags;
	int bit = 0;

	flags = Cvar_VariableValue( "dmflags" );

	if ( f == &s_friendlyfire_box )
	{
		if ( f->curvalue )
			flags &= ~DF_NO_FRIENDLY_FIRE;
		else
			flags |= DF_NO_FRIENDLY_FIRE;
		goto setvalue;
	}
	else if ( f == &s_falls_box )
	{
		if ( f->curvalue )
			flags &= ~DF_NO_FALLING;
		else
			flags |= DF_NO_FALLING;
		goto setvalue;
	}
	else if ( f == &s_weapons_stay_box ) 
	{
		bit = DF_WEAPONS_STAY;
	}
	else if ( f == &s_instant_powerups_box )
	{
		bit = DF_INSTANT_ITEMS;
	}
	else if ( f == &s_allow_exit_box )
	{
		bit = DF_ALLOW_EXIT;
	}
	else if ( f == &s_powerups_box )
	{
		if ( f->curvalue )
			flags &= ~DF_NO_ITEMS;
		else
			flags |= DF_NO_ITEMS;
		goto setvalue;
	}
	else if ( f == &s_health_box )
	{
		if ( f->curvalue )
			flags &= ~DF_NO_HEALTH;
		else
			flags |= DF_NO_HEALTH;
		goto setvalue;
	}
	else if ( f == &s_spawn_farthest_box )
	{
		bit = DF_SPAWN_FARTHEST;
	}
	else if ( f == &s_teamplay_box )
	{
		if ( f->curvalue == 1 )
		{
			flags |=  DF_SKINTEAMS;
			flags &= ~DF_MODELTEAMS;
		}
		else if ( f->curvalue == 2 )
		{
			flags |=  DF_MODELTEAMS;
			flags &= ~DF_SKINTEAMS;
		}
		else
		{
			flags &= ~( DF_MODELTEAMS | DF_SKINTEAMS );
		}

		goto setvalue;
	}
	else if ( f == &s_samelevel_box )
	{
		bit = DF_SAME_LEVEL;
	}
	else if ( f == &s_force_respawn_box )
	{
		bit = DF_FORCE_RESPAWN;
	}
	else if ( f == &s_armor_box )
	{
		if ( f->curvalue )
			flags &= ~DF_NO_ARMOR;
		else
			flags |= DF_NO_ARMOR;
		goto setvalue;
	}
	else if ( f == &s_infinite_ammo_box )
	{
		bit = DF_INFINITE_AMMO;
	}
	else if ( f == &s_fixed_fov_box )
	{
		bit = DF_FIXED_FOV;
	}
	else if ( f == &s_quad_drop_box )
	{
		bit = DF_QUAD_DROP;
	}

//=======
//ROGUE
	else if (Developer_searchpath(2) == 2)
	{
		if ( f == &s_no_mines_box)
		{
			bit = DF_NO_MINES;
		}
		else if ( f == &s_no_nukes_box)
		{
			bit = DF_NO_NUKES;
		}
		else if ( f == &s_stack_double_box)
		{
			bit = DF_NO_STACK_DOUBLE;
		}
		else if ( f == &s_no_spheres_box)
		{
			bit = DF_NO_SPHERES;
		}
	}
//ROGUE
//=======

	if ( f )
	{
		if ( f->curvalue == 0 )
			flags &= ~bit;
		else
			flags |= bit;
	}

setvalue:
	Cvar_SetValue ("dmflags", flags);

	Com_sprintf( dmoptions_statusbar, sizeof( dmoptions_statusbar ), "dmflags = %d", flags );

}

void DMOptions_MenuInit( void )
{
	static const char *yes_no_names[] =
	{
		"no", "yes", 0
	};
	static const char *teamplay_names[] = 
	{
		"disabled", "by skin", "by model", 0
	};
	int dmflags = Cvar_VariableValue( "dmflags" );
	int y = 0;

	s_dmoptions_menu.x = viddef.width * 0.50;
	s_dmoptions_menu.y = viddef.height * 0.50 - scaledVideo(58);
	s_dmoptions_menu.nitems = 0;

	s_falls_box.generic.type = MTYPE_SPINCONTROL;
	s_falls_box.generic.x	= 0;
	s_falls_box.generic.y	= y;
	s_falls_box.generic.name	= "falling damage";
	s_falls_box.generic.callback = DMFlagCallback;
	s_falls_box.itemnames = yes_no_names;
	s_falls_box.curvalue = ( dmflags & DF_NO_FALLING ) == 0;

	s_weapons_stay_box.generic.type = MTYPE_SPINCONTROL;
	s_weapons_stay_box.generic.x	= 0;
	s_weapons_stay_box.generic.y	= y += MENU_FONT_SIZE+2;
	s_weapons_stay_box.generic.name	= "weapons stay";
	s_weapons_stay_box.generic.callback = DMFlagCallback;
	s_weapons_stay_box.itemnames = yes_no_names;
	s_weapons_stay_box.curvalue = ( dmflags & DF_WEAPONS_STAY ) != 0;

	s_instant_powerups_box.generic.type = MTYPE_SPINCONTROL;
	s_instant_powerups_box.generic.x	= 0;
	s_instant_powerups_box.generic.y	= y += MENU_FONT_SIZE+2;
	s_instant_powerups_box.generic.name	= "instant powerups";
	s_instant_powerups_box.generic.callback = DMFlagCallback;
	s_instant_powerups_box.itemnames = yes_no_names;
	s_instant_powerups_box.curvalue = ( dmflags & DF_INSTANT_ITEMS ) != 0;

	s_powerups_box.generic.type = MTYPE_SPINCONTROL;
	s_powerups_box.generic.x	= 0;
	s_powerups_box.generic.y	= y += MENU_FONT_SIZE+2;
	s_powerups_box.generic.name	= "allow powerups";
	s_powerups_box.generic.callback = DMFlagCallback;
	s_powerups_box.itemnames = yes_no_names;
	s_powerups_box.curvalue = ( dmflags & DF_NO_ITEMS ) == 0;

	s_health_box.generic.type = MTYPE_SPINCONTROL;
	s_health_box.generic.x	= 0;
	s_health_box.generic.y	= y += MENU_FONT_SIZE+2;
	s_health_box.generic.callback = DMFlagCallback;
	s_health_box.generic.name	= "allow health";
	s_health_box.itemnames = yes_no_names;
	s_health_box.curvalue = ( dmflags & DF_NO_HEALTH ) == 0;

	s_armor_box.generic.type = MTYPE_SPINCONTROL;
	s_armor_box.generic.x	= 0;
	s_armor_box.generic.y	= y += MENU_FONT_SIZE+2;
	s_armor_box.generic.name	= "allow armor";
	s_armor_box.generic.callback = DMFlagCallback;
	s_armor_box.itemnames = yes_no_names;
	s_armor_box.curvalue = ( dmflags & DF_NO_ARMOR ) == 0;

	s_spawn_farthest_box.generic.type = MTYPE_SPINCONTROL;
	s_spawn_farthest_box.generic.x	= 0;
	s_spawn_farthest_box.generic.y	= y += MENU_FONT_SIZE+2;
	s_spawn_farthest_box.generic.name	= "spawn farthest";
	s_spawn_farthest_box.generic.callback = DMFlagCallback;
	s_spawn_farthest_box.itemnames = yes_no_names;
	s_spawn_farthest_box.curvalue = ( dmflags & DF_SPAWN_FARTHEST ) != 0;

	s_samelevel_box.generic.type = MTYPE_SPINCONTROL;
	s_samelevel_box.generic.x	= 0;
	s_samelevel_box.generic.y	= y += MENU_FONT_SIZE+2;
	s_samelevel_box.generic.name	= "same map";
	s_samelevel_box.generic.callback = DMFlagCallback;
	s_samelevel_box.itemnames = yes_no_names;
	s_samelevel_box.curvalue = ( dmflags & DF_SAME_LEVEL ) != 0;

	s_force_respawn_box.generic.type = MTYPE_SPINCONTROL;
	s_force_respawn_box.generic.x	= 0;
	s_force_respawn_box.generic.y	= y += MENU_FONT_SIZE+2;
	s_force_respawn_box.generic.name	= "force respawn";
	s_force_respawn_box.generic.callback = DMFlagCallback;
	s_force_respawn_box.itemnames = yes_no_names;
	s_force_respawn_box.curvalue = ( dmflags & DF_FORCE_RESPAWN ) != 0;

	s_teamplay_box.generic.type = MTYPE_SPINCONTROL;
	s_teamplay_box.generic.x	= 0;
	s_teamplay_box.generic.y	= y += MENU_FONT_SIZE+2;
	s_teamplay_box.generic.name	= "teamplay";
	s_teamplay_box.generic.callback = DMFlagCallback;
	s_teamplay_box.itemnames = teamplay_names;

	s_allow_exit_box.generic.type = MTYPE_SPINCONTROL;
	s_allow_exit_box.generic.x	= 0;
	s_allow_exit_box.generic.y	= y += MENU_FONT_SIZE+2;
	s_allow_exit_box.generic.name	= "allow exit";
	s_allow_exit_box.generic.callback = DMFlagCallback;
	s_allow_exit_box.itemnames = yes_no_names;
	s_allow_exit_box.curvalue = ( dmflags & DF_ALLOW_EXIT ) != 0;

	s_infinite_ammo_box.generic.type = MTYPE_SPINCONTROL;
	s_infinite_ammo_box.generic.x	= 0;
	s_infinite_ammo_box.generic.y	= y += MENU_FONT_SIZE+2;
	s_infinite_ammo_box.generic.name	= "infinite ammo";
	s_infinite_ammo_box.generic.callback = DMFlagCallback;
	s_infinite_ammo_box.itemnames = yes_no_names;
	s_infinite_ammo_box.curvalue = ( dmflags & DF_INFINITE_AMMO ) != 0;

	s_fixed_fov_box.generic.type = MTYPE_SPINCONTROL;
	s_fixed_fov_box.generic.x	= 0;
	s_fixed_fov_box.generic.y	= y += MENU_FONT_SIZE+2;
	s_fixed_fov_box.generic.name	= "fixed FOV";
	s_fixed_fov_box.generic.callback = DMFlagCallback;
	s_fixed_fov_box.itemnames = yes_no_names;
	s_fixed_fov_box.curvalue = ( dmflags & DF_FIXED_FOV ) != 0;

	s_quad_drop_box.generic.type = MTYPE_SPINCONTROL;
	s_quad_drop_box.generic.x	= 0;
	s_quad_drop_box.generic.y	= y += MENU_FONT_SIZE+2;
	s_quad_drop_box.generic.name	= "quad drop";
	s_quad_drop_box.generic.callback = DMFlagCallback;
	s_quad_drop_box.itemnames = yes_no_names;
	s_quad_drop_box.curvalue = ( dmflags & DF_QUAD_DROP ) != 0;

	s_friendlyfire_box.generic.type = MTYPE_SPINCONTROL;
	s_friendlyfire_box.generic.x	= 0;
	s_friendlyfire_box.generic.y	= y += MENU_FONT_SIZE+2;
	s_friendlyfire_box.generic.name	= "friendly fire";
	s_friendlyfire_box.generic.callback = DMFlagCallback;
	s_friendlyfire_box.itemnames = yes_no_names;
	s_friendlyfire_box.curvalue = ( dmflags & DF_NO_FRIENDLY_FIRE ) == 0;

//============
//ROGUE
	if(Developer_searchpath(2) == 2)
	{
		s_no_mines_box.generic.type = MTYPE_SPINCONTROL;
		s_no_mines_box.generic.x	= 0;
		s_no_mines_box.generic.y	= y += MENU_FONT_SIZE+2;
		s_no_mines_box.generic.name	= "remove mines";
		s_no_mines_box.generic.callback = DMFlagCallback;
		s_no_mines_box.itemnames = yes_no_names;
		s_no_mines_box.curvalue = ( dmflags & DF_NO_MINES ) != 0;

		s_no_nukes_box.generic.type = MTYPE_SPINCONTROL;
		s_no_nukes_box.generic.x	= 0;
		s_no_nukes_box.generic.y	= y += MENU_FONT_SIZE+2;
		s_no_nukes_box.generic.name	= "remove nukes";
		s_no_nukes_box.generic.callback = DMFlagCallback;
		s_no_nukes_box.itemnames = yes_no_names;
		s_no_nukes_box.curvalue = ( dmflags & DF_NO_NUKES ) != 0;

		s_stack_double_box.generic.type = MTYPE_SPINCONTROL;
		s_stack_double_box.generic.x	= 0;
		s_stack_double_box.generic.y	= y += MENU_FONT_SIZE+2;
		s_stack_double_box.generic.name	= "2x/4x stacking off";
		s_stack_double_box.generic.callback = DMFlagCallback;
		s_stack_double_box.itemnames = yes_no_names;
		s_stack_double_box.curvalue = ( dmflags & DF_NO_STACK_DOUBLE ) != 0;

		s_no_spheres_box.generic.type = MTYPE_SPINCONTROL;
		s_no_spheres_box.generic.x	= 0;
		s_no_spheres_box.generic.y	= y += MENU_FONT_SIZE+2;
		s_no_spheres_box.generic.name	= "remove spheres";
		s_no_spheres_box.generic.callback = DMFlagCallback;
		s_no_spheres_box.itemnames = yes_no_names;
		s_no_spheres_box.curvalue = ( dmflags & DF_NO_SPHERES ) != 0;

	}
//ROGUE
//============

	Menu_AddItem( &s_dmoptions_menu, &s_falls_box );
	Menu_AddItem( &s_dmoptions_menu, &s_weapons_stay_box );
	Menu_AddItem( &s_dmoptions_menu, &s_instant_powerups_box );
	Menu_AddItem( &s_dmoptions_menu, &s_powerups_box );
	Menu_AddItem( &s_dmoptions_menu, &s_health_box );
	Menu_AddItem( &s_dmoptions_menu, &s_armor_box );
	Menu_AddItem( &s_dmoptions_menu, &s_spawn_farthest_box );
	Menu_AddItem( &s_dmoptions_menu, &s_samelevel_box );
	Menu_AddItem( &s_dmoptions_menu, &s_force_respawn_box );
	Menu_AddItem( &s_dmoptions_menu, &s_teamplay_box );
	Menu_AddItem( &s_dmoptions_menu, &s_allow_exit_box );
	Menu_AddItem( &s_dmoptions_menu, &s_infinite_ammo_box );
	Menu_AddItem( &s_dmoptions_menu, &s_fixed_fov_box );
	Menu_AddItem( &s_dmoptions_menu, &s_quad_drop_box );
	Menu_AddItem( &s_dmoptions_menu, &s_friendlyfire_box );

//=======
//ROGUE
	if(Developer_searchpath(2) == 2)
	{
		Menu_AddItem( &s_dmoptions_menu, &s_no_mines_box );
		Menu_AddItem( &s_dmoptions_menu, &s_no_nukes_box );
		Menu_AddItem( &s_dmoptions_menu, &s_stack_double_box );
		Menu_AddItem( &s_dmoptions_menu, &s_no_spheres_box );
	}
//ROGUE
//=======

	Menu_Center( &s_dmoptions_menu );

	// set the original dmflags statusbar
	DMFlagCallback( 0 );
	Menu_SetStatusBar( &s_dmoptions_menu, dmoptions_statusbar );
}

void DMOptions_MenuDraw(void)
{
	Menu_Draw( &s_dmoptions_menu );
}

const char *DMOptions_MenuKey( int key )
{
	return Default_MenuKey( &s_dmoptions_menu, key );
}

void M_Menu_DMOptions_f (void)
{
	DMOptions_MenuInit();
	M_PushMenu( DMOptions_MenuDraw, DMOptions_MenuKey );
}

/*
=============================================================================

DOWNLOADOPTIONS BOOK MENU

=============================================================================
*/
static menuframework_s s_downloadoptions_menu;

static menuseparator_s	s_download_title;
static menulist_s	s_allow_download_box;
static menulist_s	s_allow_download_maps_box;
static menulist_s	s_allow_download_models_box;
static menulist_s	s_allow_download_players_box;
static menulist_s	s_allow_download_sounds_box;

static void DownloadCallback( void *self )
{
	menulist_s *f = ( menulist_s * ) self;

	if (f == &s_allow_download_box)
	{
		Cvar_SetValue("allow_download", f->curvalue);
	}

	else if (f == &s_allow_download_maps_box)
	{
		Cvar_SetValue("allow_download_maps", f->curvalue);
	}

	else if (f == &s_allow_download_models_box)
	{
		Cvar_SetValue("allow_download_models", f->curvalue);
	}

	else if (f == &s_allow_download_players_box)
	{
		Cvar_SetValue("allow_download_players", f->curvalue);
	}

	else if (f == &s_allow_download_sounds_box)
	{
		Cvar_SetValue("allow_download_sounds", f->curvalue);
	}
}

void DownloadOptions_MenuInit( void )
{
	static const char *yes_no_names[] =
	{
		"no", "yes", 0
	};
	int y = 0;

	s_downloadoptions_menu.x = viddef.width * 0.50;
	s_downloadoptions_menu.y = viddef.height * 0.50 - scaledVideo(58);
	s_downloadoptions_menu.nitems = 0;

	s_download_title.generic.type = MTYPE_SEPARATOR;
	s_download_title.generic.name = "Download Options";
	s_download_title.generic.x    = 48;
	s_download_title.generic.y	 = y;

	s_allow_download_box.generic.type = MTYPE_SPINCONTROL;
	s_allow_download_box.generic.x	= 0;
	s_allow_download_box.generic.y	= y += MENU_FONT_SIZE+2;
	s_allow_download_box.generic.name	= "allow downloading";
	s_allow_download_box.generic.callback = DownloadCallback;
	s_allow_download_box.itemnames = yes_no_names;
	s_allow_download_box.curvalue = (Cvar_VariableValue("allow_download") != 0);

	s_allow_download_maps_box.generic.type = MTYPE_SPINCONTROL;
	s_allow_download_maps_box.generic.x	= 0;
	s_allow_download_maps_box.generic.y	= y += MENU_FONT_SIZE*2+2;
	s_allow_download_maps_box.generic.name	= "maps";
	s_allow_download_maps_box.generic.callback = DownloadCallback;
	s_allow_download_maps_box.itemnames = yes_no_names;
	s_allow_download_maps_box.curvalue = (Cvar_VariableValue("allow_download_maps") != 0);

	s_allow_download_players_box.generic.type = MTYPE_SPINCONTROL;
	s_allow_download_players_box.generic.x	= 0;
	s_allow_download_players_box.generic.y	= y += MENU_FONT_SIZE+2;
	s_allow_download_players_box.generic.name	= "player models/skins";
	s_allow_download_players_box.generic.callback = DownloadCallback;
	s_allow_download_players_box.itemnames = yes_no_names;
	s_allow_download_players_box.curvalue = (Cvar_VariableValue("allow_download_players") != 0);

	s_allow_download_models_box.generic.type = MTYPE_SPINCONTROL;
	s_allow_download_models_box.generic.x	= 0;
	s_allow_download_models_box.generic.y	= y += MENU_FONT_SIZE+2;
	s_allow_download_models_box.generic.name	= "models";
	s_allow_download_models_box.generic.callback = DownloadCallback;
	s_allow_download_models_box.itemnames = yes_no_names;
	s_allow_download_models_box.curvalue = (Cvar_VariableValue("allow_download_models") != 0);

	s_allow_download_sounds_box.generic.type = MTYPE_SPINCONTROL;
	s_allow_download_sounds_box.generic.x	= 0;
	s_allow_download_sounds_box.generic.y	= y += MENU_FONT_SIZE+2;
	s_allow_download_sounds_box.generic.name	= "sounds";
	s_allow_download_sounds_box.generic.callback = DownloadCallback;
	s_allow_download_sounds_box.itemnames = yes_no_names;
	s_allow_download_sounds_box.curvalue = (Cvar_VariableValue("allow_download_sounds") != 0);

	Menu_AddItem( &s_downloadoptions_menu, &s_download_title );
	Menu_AddItem( &s_downloadoptions_menu, &s_allow_download_box );
	Menu_AddItem( &s_downloadoptions_menu, &s_allow_download_maps_box );
	Menu_AddItem( &s_downloadoptions_menu, &s_allow_download_players_box );
	Menu_AddItem( &s_downloadoptions_menu, &s_allow_download_models_box );
	Menu_AddItem( &s_downloadoptions_menu, &s_allow_download_sounds_box );

	Menu_Center( &s_downloadoptions_menu );

	// skip over title
	if (s_downloadoptions_menu.cursor == 0)
		s_downloadoptions_menu.cursor = 1;
}

void DownloadOptions_MenuDraw(void)
{
	Menu_Draw( &s_downloadoptions_menu );
}

const char *DownloadOptions_MenuKey( int key )
{
	return Default_MenuKey( &s_downloadoptions_menu, key );
}

void M_Menu_DownloadOptions_f (void)
{
	DownloadOptions_MenuInit();
	M_PushMenu( DownloadOptions_MenuDraw, DownloadOptions_MenuKey );
}
/*
=============================================================================

ADDRESS BOOK MENU

=============================================================================
*/
#define NUM_ADDRESSBOOK_ENTRIES 9

static menuframework_s	s_addressbook_menu;
static menufield_s		s_addressbook_fields[NUM_ADDRESSBOOK_ENTRIES];

void AddressBook_MenuInit( void )
{
	int i;

	s_addressbook_menu.x = viddef.width / 2 - scaledVideo(MENU_FONT_SIZE*20);
	s_addressbook_menu.y = viddef.height / 2 - scaledVideo(6*MENU_FONT_SIZE);
	s_addressbook_menu.nitems = 0;

	for ( i = 0; i < NUM_ADDRESSBOOK_ENTRIES; i++ )
	{
		cvar_t *adr;
		char buffer[20];

		Com_sprintf( buffer, sizeof( buffer ), "adr%d", i );

		adr = Cvar_Get( buffer, "", CVAR_ARCHIVE );

		s_addressbook_fields[i].generic.type = MTYPE_FIELD;
		s_addressbook_fields[i].generic.name = 0;
		s_addressbook_fields[i].generic.callback = 0;
		s_addressbook_fields[i].generic.x		= 0;
		s_addressbook_fields[i].generic.y		= i * MENU_FONT_SIZE * 2;
		s_addressbook_fields[i].generic.localdata[0] = i;
		s_addressbook_fields[i].cursor			= 0;
		s_addressbook_fields[i].length			= 60;
		s_addressbook_fields[i].visible_length	= 30;

		strcpy( s_addressbook_fields[i].buffer, adr->string );

		Menu_AddItem( &s_addressbook_menu, &s_addressbook_fields[i] );
	}
}

const char *AddressBook_MenuKey( int key )
{
	if ( key == K_ESCAPE )
	{
		int index;
		char buffer[20];

		for ( index = 0; index < NUM_ADDRESSBOOK_ENTRIES; index++ )
		{
			Com_sprintf( buffer, sizeof( buffer ), "adr%d", index );
			Cvar_Set( buffer, s_addressbook_fields[index].buffer );
		}
	}
	return Default_MenuKey( &s_addressbook_menu, key );
}

void AddressBook_MenuDraw(void)
{
	M_Banner( "m_banner_addressbook" );
	Menu_Draw( &s_addressbook_menu );
}

void M_Menu_AddressBook_f(void)
{
	AddressBook_MenuInit();
	M_PushMenu( AddressBook_MenuDraw, AddressBook_MenuKey );
}

/*
=============================================================================

PLAYER CONFIG MENU

=============================================================================
*/
static menuframework_s	s_player_config_menu;
static menufield_s		s_player_name_field;
static menulist_s		s_player_model_box;
static menulist_s		s_player_skin_box;
static menulist_s		s_player_handedness_box;
static menulist_s		s_player_rate_box;
static menuseparator_s	s_player_skin_title;
static menuseparator_s	s_player_model_title;
static menuseparator_s	s_player_hand_title;
static menuseparator_s	s_player_rate_title;
static menuaction_s		s_player_download_action;

static cvar_t *hand;

#define MAX_DISPLAYNAME 16
#define MAX_PLAYERMODELS 1024

typedef struct
{
	int		nskins;
	char	**skindisplaynames;
	char	displayname[MAX_DISPLAYNAME];
	char	directory[MAX_QPATH];
} playermodelinfo_s;

static playermodelinfo_s s_pmi[MAX_PLAYERMODELS];
static char *s_pmnames[MAX_PLAYERMODELS];
static int s_numplayermodels;

static int rate_tbl[] = { 2500, 3200, 5000, 10000, 25000, 0 };
static const char *rate_names[] = { "28.8 Modem", "33.6 Modem", "Single ISDN",
	"Dual ISDN/Cable", "T1/LAN", "User defined", 0 };

void DownloadOptionsFunc( void *self )
{
	M_Menu_DownloadOptions_f();
}

static void HandednessCallback( void *unused )
{
	Cvar_SetValue( "hand", s_player_handedness_box.curvalue );
}

static void RateCallback( void *unused )
{
	if (s_player_rate_box.curvalue != sizeof(rate_tbl) / sizeof(*rate_tbl) - 1)
		Cvar_SetValue( "rate", rate_tbl[s_player_rate_box.curvalue] );
}

static void ModelCallback( void *unused )
{
	s_player_skin_box.itemnames = s_pmi[s_player_model_box.curvalue].skindisplaynames;
	s_player_skin_box.curvalue = 0;
}

static qboolean IconOfSkinExists( char *skin, char **files, int nfiles, char *suffix )
{
	int i;
	char scratch[1024];

	//since i send *.*, i have to make sure *skin is valid
	if (!strstr( skin, ".pcx")
		&& !strstr( skin, ".jpg")
		&& !strstr( skin, ".tga")
		&& !strstr( skin, ".png") 
		)
		return false;

	strcpy( scratch, skin );
	*strrchr( scratch, '.' ) = 0;
	strcat( scratch, suffix );

	for ( i = 0; i < nfiles; i++ )
	{
		if ( strcmp( files[i], scratch) == 0 )
			return true;
	}

	return false;
}

static qboolean PlayerConfig_ScanDirectories( void )
{
	char findname[1024];
	char scratch[1024];
	int ndirs = 0, npms = 0;
	char **dirnames;
	char *path = NULL;
	int i;

	extern char **FS_ListFiles( char *, int *, unsigned, unsigned );

	s_numplayermodels = 0;

	/*
	** get a list of directories
	*/
	do 
	{
		path = FS_NextPath( path );
		Com_sprintf( findname, sizeof(findname), "%s/players/*.*", path );

		if ( ( dirnames = FS_ListFiles( findname, &ndirs, SFF_SUBDIR, 0 ) ) != 0 )
			break;

	} while ( path );

	if ( !dirnames )
		return false;

	/*
	** go through the subdirectories
	*/
	npms = ndirs;
	if ( npms > MAX_PLAYERMODELS )
		npms = MAX_PLAYERMODELS;

	for ( i = 0; i < npms; i++ )
	{
		int k, s;
		char *a, *b, *c;
		char **skinnames;

		char	**imagenames;
		int		nimagefiles;
		int nskins = 0;

		if ( dirnames[i] == 0 )
			continue;

		// verify the existence of tris.md2
		strcpy( scratch, dirnames[i] );
		strcat( scratch, "/tris.md2" );
		if ( !Sys_FindFirst( scratch, 0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM ) )
		{
			free( dirnames[i] );
			dirnames[i] = 0;
			Sys_FindClose();
			continue;
		}
		Sys_FindClose();

		// verify the existence of at least one skin
		strcpy( scratch, va("%s%s", dirnames[i], "/*.*" ));
		//Con_Print (va( "%s\n", scratch ));

		imagenames = FS_ListFiles( scratch, &nimagefiles, 0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM );

		if ( !imagenames )
		{
			dirnames[i] = 0;
			continue;
		}

		// count valid skins, which consist of a skin with a matching "_i" icon
		for ( k = 0; k < nimagefiles-1; k++ )
			if (	!strstr( imagenames[k], "_i.pcx")
				||  !strstr( imagenames[k], "_i.jpg" )
				||  !strstr( imagenames[k], "_i.tga")
				||  !strstr( imagenames[k], "_i.png") 
				)
				if (	IconOfSkinExists( imagenames[k], imagenames, nimagefiles - 1 , "_i.pcx")
					||	IconOfSkinExists( imagenames[k], imagenames, nimagefiles - 1 , "_i.jpg")
					||	IconOfSkinExists( imagenames[k], imagenames, nimagefiles - 1 , "_i.tga")
					||	IconOfSkinExists( imagenames[k], imagenames, nimagefiles - 1 , "_i.png")
					)
					nskins++;

		if ( !nskins )
			continue;

		skinnames = malloc( sizeof( char * ) * ( nskins + 1 ) );
		memset( skinnames, 0, sizeof( char * ) * ( nskins + 1 ) );

		s = 0;

		// copy the valid skins
		if (nimagefiles)
			for (k = 0; k < nimagefiles-1; k++ )
			{
				char *a, *b, *c;

				if (	!strstr( imagenames[k], "_i.pcx")
					||  !strstr( imagenames[k], "_i.jpg" )
					||  !strstr( imagenames[k], "_i.tga")
					||  !strstr( imagenames[k], "_i.png") 
					)
				{
					if (	IconOfSkinExists( imagenames[k], imagenames, nimagefiles - 1 , "_i.pcx")
						||	IconOfSkinExists( imagenames[k], imagenames, nimagefiles - 1 , "_i.jpg")
						||	IconOfSkinExists( imagenames[k], imagenames, nimagefiles - 1 , "_i.tga")
						||	IconOfSkinExists( imagenames[k], imagenames, nimagefiles - 1 , "_i.png")
						)
					{
						a = strrchr( imagenames[k], '/' );
						b = strrchr( imagenames[k], '\\' );

						if ( a > b )
							c = a;
						else
							c = b;

						strcpy( scratch, c + 1 );

						if ( strrchr( scratch, '.' ) )
							*strrchr( scratch, '.' ) = 0;

						skinnames[s] = strdup( scratch );
						s++;
					}
				}
			}

		// at this point we have a valid player model
		s_pmi[s_numplayermodels].nskins = nskins;
		s_pmi[s_numplayermodels].skindisplaynames = skinnames;

		// make short name for the model
		a = strrchr( dirnames[i], '/' );
		b = strrchr( dirnames[i], '\\' );

		if ( a > b )
			c = a;
		else
			c = b;

		strncpy( s_pmi[s_numplayermodels].displayname, c + 1, MAX_DISPLAYNAME-1 );
		strcpy( s_pmi[s_numplayermodels].directory, c + 1 );

		FreeFileList( imagenames, nimagefiles );

		s_numplayermodels++;
	}
	if ( dirnames )
		FreeFileList( dirnames, ndirs );

	return true;
}

static int pmicmpfnc( const void *_a, const void *_b )
{
	const playermodelinfo_s *a = ( const playermodelinfo_s * ) _a;
	const playermodelinfo_s *b = ( const playermodelinfo_s * ) _b;

	/*
	** sort by male, female, then alphabetical
	*/
	if ( strcmp( a->directory, "male" ) == 0 )
		return -1;
	else if ( strcmp( b->directory, "male" ) == 0 )
		return 1;

	if ( strcmp( a->directory, "female" ) == 0 )
		return -1;
	else if ( strcmp( b->directory, "female" ) == 0 )
		return 1;

	return strcmp( a->directory, b->directory );
}


qboolean PlayerConfig_MenuInit( void )
{
	extern cvar_t *name;
	extern cvar_t *team;
	extern cvar_t *skin;
	char currentdirectory[1024];
	char currentskin[1024];
	int i = 0;

	int currentdirectoryindex = 0;
	int currentskinindex = 0;

	static const char *handedness[] = { "right", "left", "center", 0 };

	hand = Cvar_Get( "hand", "0", CVAR_USERINFO | CVAR_ARCHIVE );

	PlayerConfig_ScanDirectories();

	if (s_numplayermodels == 0)
		return false;

	if ( hand->value < 0 || hand->value > 2 )
		Cvar_SetValue( "hand", 0 );

	strcpy( currentdirectory, skin->string );

	if ( strchr( currentdirectory, '/' ) )
	{
		strcpy( currentskin, strchr( currentdirectory, '/' ) + 1 );
		*strchr( currentdirectory, '/' ) = 0;
	}
	else if ( strchr( currentdirectory, '\\' ) )
	{
		strcpy( currentskin, strchr( currentdirectory, '\\' ) + 1 );
		*strchr( currentdirectory, '\\' ) = 0;
	}
	else
	{
		strcpy( currentdirectory, "male" );
		strcpy( currentskin, "grunt" );
	}

	qsort( s_pmi, s_numplayermodels, sizeof( s_pmi[0] ), pmicmpfnc );

	memset( s_pmnames, 0, sizeof( s_pmnames ) );
	for ( i = 0; i < s_numplayermodels; i++ )
	{
		s_pmnames[i] = s_pmi[i].displayname;
		if ( Q_stricmp( s_pmi[i].directory, currentdirectory ) == 0 )
		{
			int j;

			currentdirectoryindex = i;

			for ( j = 0; j < s_pmi[i].nskins; j++ )
			{
				if ( Q_stricmp( s_pmi[i].skindisplaynames[j], currentskin ) == 0 )
				{
					currentskinindex = j;
					break;
				}
			}
		}
	}

	s_player_config_menu.x = scaledVideo(150.0);
	s_player_config_menu.y = viddef.height / 2 - scaledVideo(70.0);
	s_player_config_menu.nitems = 0;

	s_player_name_field.generic.type = MTYPE_FIELD;
	s_player_name_field.generic.flags = QMF_LEFT_JUSTIFY;
	s_player_name_field.generic.name = "name";
	s_player_name_field.generic.callback = 0;
	s_player_name_field.generic.x		= -MENU_FONT_SIZE;
	s_player_name_field.generic.y		= 0;
	s_player_name_field.length	= 20;
	s_player_name_field.visible_length = 20;
	strcpy( s_player_name_field.buffer, name->string );
	s_player_name_field.cursor = strlen( name->string );

	s_player_model_title.generic.type = MTYPE_SEPARATOR;
	s_player_model_title.generic.flags = QMF_LEFT_JUSTIFY;
	s_player_model_title.generic.name = "model";
	s_player_model_title.generic.x    = -MENU_FONT_SIZE;
	s_player_model_title.generic.y	 = MENU_FONT_SIZE*3;

	s_player_model_box.generic.type = MTYPE_SPINCONTROL;
	s_player_model_box.generic.x	= -7*MENU_FONT_SIZE;
	s_player_model_box.generic.y	= MENU_FONT_SIZE*4;
	s_player_model_box.generic.callback = ModelCallback;
	s_player_model_box.generic.cursor_offset = -6*MENU_FONT_SIZE;
	s_player_model_box.curvalue = currentdirectoryindex;
	s_player_model_box.itemnames = s_pmnames;

	s_player_skin_title.generic.type = MTYPE_SEPARATOR;
	s_player_skin_title.generic.flags = QMF_LEFT_JUSTIFY;
	s_player_skin_title.generic.name = "skin";
	s_player_skin_title.generic.x    = -2*MENU_FONT_SIZE;
	s_player_skin_title.generic.y	 = MENU_FONT_SIZE*6;

	s_player_skin_box.generic.type = MTYPE_SPINCONTROL;
	s_player_skin_box.generic.x	= -7*MENU_FONT_SIZE;
	s_player_skin_box.generic.y	= MENU_FONT_SIZE*7;
	s_player_skin_box.generic.name	= 0;
	s_player_skin_box.generic.callback = 0;
	s_player_skin_box.generic.cursor_offset = -6*MENU_FONT_SIZE;
	s_player_skin_box.curvalue = currentskinindex;
	s_player_skin_box.itemnames = s_pmi[currentdirectoryindex].skindisplaynames;

	s_player_hand_title.generic.type = MTYPE_SEPARATOR;
	s_player_hand_title.generic.flags = QMF_LEFT_JUSTIFY;
	s_player_hand_title.generic.name = "handedness";
	s_player_hand_title.generic.x    = 4*MENU_FONT_SIZE;
	s_player_hand_title.generic.y	 = MENU_FONT_SIZE*9;

	s_player_handedness_box.generic.type = MTYPE_SPINCONTROL;
	s_player_handedness_box.generic.x	= -7*MENU_FONT_SIZE;
	s_player_handedness_box.generic.y	= MENU_FONT_SIZE*10;
	s_player_handedness_box.generic.name	= 0;
	s_player_handedness_box.generic.cursor_offset = -6*MENU_FONT_SIZE;
	s_player_handedness_box.generic.callback = HandednessCallback;
	s_player_handedness_box.curvalue = Cvar_VariableValue( "hand" );
	s_player_handedness_box.itemnames = handedness;

	for (i = 0; i < sizeof(rate_tbl) / sizeof(*rate_tbl) - 1; i++)
		if (Cvar_VariableValue("rate") == rate_tbl[i])
			break;

	s_player_rate_title.generic.type = MTYPE_SEPARATOR;
	s_player_rate_title.generic.flags = QMF_LEFT_JUSTIFY;
	s_player_rate_title.generic.name = "connect speed";
	s_player_rate_title.generic.x    = 7*MENU_FONT_SIZE;
	s_player_rate_title.generic.y	 = MENU_FONT_SIZE*12;

	s_player_rate_box.generic.type = MTYPE_SPINCONTROL;
	s_player_rate_box.generic.x	= -7*MENU_FONT_SIZE;
	s_player_rate_box.generic.y	= MENU_FONT_SIZE*13;
	s_player_rate_box.generic.name	= 0;
	s_player_rate_box.generic.cursor_offset = -6*MENU_FONT_SIZE;
	s_player_rate_box.generic.callback = RateCallback;
	s_player_rate_box.curvalue = i;
	s_player_rate_box.itemnames = rate_names;

	s_player_download_action.generic.type = MTYPE_ACTION;
	s_player_download_action.generic.name	= "download options";
	s_player_download_action.generic.flags = QMF_LEFT_JUSTIFY;
	s_player_download_action.generic.x	= -5*MENU_FONT_SIZE;
	s_player_download_action.generic.y	= MENU_FONT_SIZE*15;
	s_player_download_action.generic.statusbar = NULL;
	s_player_download_action.generic.callback = DownloadOptionsFunc;

	Menu_AddItem( &s_player_config_menu, &s_player_name_field );
	Menu_AddItem( &s_player_config_menu, &s_player_model_title );
	Menu_AddItem( &s_player_config_menu, &s_player_model_box );
	if ( s_player_skin_box.itemnames )
	{
		Menu_AddItem( &s_player_config_menu, &s_player_skin_title );
		Menu_AddItem( &s_player_config_menu, &s_player_skin_box );
	}
	Menu_AddItem( &s_player_config_menu, &s_player_hand_title );
	Menu_AddItem( &s_player_config_menu, &s_player_handedness_box );
	Menu_AddItem( &s_player_config_menu, &s_player_rate_title );
	Menu_AddItem( &s_player_config_menu, &s_player_rate_box );
	Menu_AddItem( &s_player_config_menu, &s_player_download_action );

	return true;
}

qboolean PlayerConfig_CheckIncerement(int dir, float x, float y)
{
	float min[2], max[2];
	char *sound = NULL;

	min[0] = x;
	max[0] = x + 2*scaledVideo(MENU_FONT_SIZE);
	min[1] = y;
	max[1] = y + 2*scaledVideo(MENU_FONT_SIZE);

	if (cursor.x>=min[0] && cursor.x<=max[0] &&
		cursor.y>=min[1] && cursor.y<=max[1] &&
		!cursor.buttonused[MOUSEBUTTON1] &&
		cursor.buttonclicks[MOUSEBUTTON1]==1)
	{
		if (dir) //dir==1 is left
		{
			if (s_player_skin_box.curvalue>0)
				s_player_skin_box.curvalue--;
		}
		else
		{
			if (s_player_skin_box.curvalue<s_pmi[s_player_model_box.curvalue].nskins)
				s_player_skin_box.curvalue++;
		}

		sound = menu_move_sound;
		cursor.buttonused[MOUSEBUTTON1] = true;
		cursor.buttonclicks[MOUSEBUTTON1] = 0;

		if ( sound )
			S_StartLocalSound( sound );

		return true;
	}
	return false;
}

void PlayerConfig_MouseClick( void )
{
	int ystart;
	int	xoffset;
	int widest;
	int totalheight;
	int i, oldhover, w, h, count;
	char *sound = NULL;
	buttonmenuobject_t buttons[7];
	float	icon_x = viddef.width  - scaledVideo(325),
			icon_y = viddef.height - scaledVideo(75),
			icon_offset = 0, icon_scale;

	for (i=0;i<7;i++)
		buttons[i].index=-1;

	if (s_pmi[s_player_model_box.curvalue].nskins<7 || s_player_skin_box.curvalue<4)
		i=0;
	else if (s_player_skin_box.curvalue>s_pmi[s_player_model_box.curvalue].nskins-4)
		i=s_pmi[s_player_model_box.curvalue].nskins-7;
	else
		i=s_player_skin_box.curvalue-3;

	if (i>0)
		if (PlayerConfig_CheckIncerement(true, 
											icon_x - scaledVideo(45),	
											icon_y - scaledVideo(30)))
			return;

	for (count=0; count<7;i++,count++)
	{
		if (i<0 || i>=s_pmi[s_player_model_box.curvalue].nskins)
			continue;

		icon_scale = (i==s_player_skin_box.curvalue)? 2: 1;

		re.DrawGetPicSize ( &w, &h, va("/players/%s/%s_i.pcx", 
			s_pmi[s_player_model_box.curvalue].directory,
			s_pmi[s_player_model_box.curvalue].skindisplaynames[i]));
		addPlayerButton (&buttons[count], i, 
			icon_x + icon_offset,
			icon_y - scaledVideo(h),
			scaledVideo(w)*icon_scale, 
			scaledVideo(h)*icon_scale);
		icon_offset += scaledVideo(w)*icon_scale;
	}

	if (s_pmi[s_player_model_box.curvalue].nskins-i>0)
		if (PlayerConfig_CheckIncerement(false,
												icon_x + icon_offset + scaledVideo(5), 
												icon_y - scaledVideo(30)))
			return;

	for (i=0;i<7;i++)
	{
		if (buttons[i].index==-1)
			continue;

		if (cursor.x>=buttons[i].min[0] && cursor.x<=buttons[i].max[0] &&
			cursor.y>=buttons[i].min[1] && cursor.y<=buttons[i].max[1])
		{
			if (!cursor.buttonused[MOUSEBUTTON1] && cursor.buttonclicks[MOUSEBUTTON1]==1)
			{
				s_player_skin_box.curvalue = buttons[i].index;

				sound = menu_move_sound;
				cursor.buttonused[MOUSEBUTTON1] = true;
				cursor.buttonclicks[MOUSEBUTTON1] = 0;

				if ( sound )
					S_StartLocalSound( sound );

				return;
			}
			break;
		}
	}
}

void PlayerConfig_MenuDraw( void )
{
	extern float CalcFov( float fov_x, float w, float h );
	refdef_t refdef;
	char scratch[MAX_QPATH];

	M_Banner( "m_banner_customize" );

	memset( &refdef, 0, sizeof( refdef ) );

	refdef.width = viddef.width;
	refdef.height = viddef.height;
	refdef.x = 0;
	refdef.y = 0;
	refdef.fov_x = 50;
	refdef.fov_y = CalcFov( refdef.fov_x, refdef.width, refdef.height );
	refdef.time = cls.realtime*0.001;

	if ( s_pmi[s_player_model_box.curvalue].skindisplaynames )
	{
		int yaw;
		int maxframe = 29, w, h, bordercolor;
		vec3_t modelOrg;
		entity_t entity[2], *ent;

		refdef.num_entities = 0;
		refdef.entities = &entity[0];

		yaw = anglemod(cl.time/10);

		VectorSet(modelOrg,
			150,
			(hand->value == 1)? 35 : -35,
			0);
			

		//Set Up Player Model
		ent = &entity[0];
		{
			memset( &entity[0], 0, sizeof( entity[0] ) );

			Com_sprintf( scratch, sizeof( scratch ), "players/%s/tris.md2", s_pmi[s_player_model_box.curvalue].directory );
			ent->model = re.RegisterModel( scratch );
		
			Com_sprintf( scratch, sizeof( scratch ), "players/%s/%s.pcx", s_pmi[s_player_model_box.curvalue].directory, s_pmi[s_player_model_box.curvalue].skindisplaynames[s_player_skin_box.curvalue] );
			ent->skin = re.RegisterSkin( scratch );

			ent->flags = RF_FULLBRIGHT|RF_DEPTHHACK;
			if (hand->value == 1)
				ent->flags |= RF_MIRRORMODEL;
			ent->origin[0] = modelOrg[0];
			ent->origin[1] = modelOrg[1];
			ent->origin[2] = modelOrg[2];

			VectorCopy( ent->origin, ent->oldorigin );
			ent->frame = 0;
			ent->oldframe = 0;
			ent->backlerp = 0.0;
			ent->angles[1] = yaw;
			
			ent->scale = 1;

			if (hand->value == 1)
				ent->angles[1] = 360 - ent->angles[1];

			refdef.num_entities++;
		}
		ent = &entity[1];
		{
			memset( &entity[1], 0, sizeof( entity[1] ) );

			Com_sprintf( scratch, sizeof( scratch ), "players/%s/weapon.md2", s_pmi[s_player_model_box.curvalue].directory );
			ent->model = re.RegisterModel( scratch );

			if (ent->model)
			{
				ent->skinnum = 0;

				ent->flags = RF_FULLBRIGHT|RF_DEPTHHACK;
				if (hand->value == 1)
					ent->flags |= RF_MIRRORMODEL;
				ent->origin[0] = modelOrg[0];
				ent->origin[1] = modelOrg[1];
				ent->origin[2] = modelOrg[2];

				VectorCopy( ent->origin, ent->oldorigin );
				ent->frame = 0;
				ent->oldframe = 0;
				ent->backlerp = 0.0;
				ent->angles[1] = yaw;

				ent->scale = 1;

				if (hand->value == 1)
					ent->angles[1] = 360 - ent->angles[1];

				refdef.num_entities++;
			}
		}


		refdef.areabits = 0;

		refdef.rdflags = RDF_NOWORLDMODEL;

		Menu_Draw( &s_player_config_menu );

		re.RenderFrame( &refdef );
		//skin selection preview
		{
			float	icon_x = viddef.width  - scaledVideo(325),
					icon_y = viddef.height - scaledVideo(75),
					icon_offset = 0, icon_scale;
			int i, count;
			vec3_t color =	{
								(int)(cl.time/10)%255,
								255,
								(int)(cl.time/10)%255
							};

			if (s_pmi[s_player_model_box.curvalue].nskins<7 || s_player_skin_box.curvalue<4)
				i=0;
			else if (s_player_skin_box.curvalue>s_pmi[s_player_model_box.curvalue].nskins-4)
				i=s_pmi[s_player_model_box.curvalue].nskins-7;
			else
				i=s_player_skin_box.curvalue-3;

			if (i>0)
				re.DrawScaledChar( 
					icon_x - scaledVideo(45),
					icon_y - scaledVideo(30),
					'<' , 2*scaledVideo(MENU_FONT_SIZE)/con_font_size->value, color[0],color[1],color[2],255,false);
				
			for (count=0; count<7;i++,count++)
			{
				if (i<0 || i>=s_pmi[s_player_model_box.curvalue].nskins)
					continue;

				icon_scale = (i==s_player_skin_box.curvalue)? 2: 1;

				Com_sprintf (scratch, sizeof(scratch), "/players/%s/%s_i.pcx", 
					s_pmi[s_player_model_box.curvalue].directory,
					s_pmi[s_player_model_box.curvalue].skindisplaynames[i] );
				re.DrawGetPicSize ( &w, &h, scratch);
				re.DrawStretchPic (
					icon_x + icon_offset,
					icon_y - scaledVideo(h),
					scaledVideo(w)*icon_scale, scaledVideo(h)*icon_scale, scratch);
				icon_offset += scaledVideo(w)*icon_scale;
			}

			if (s_pmi[s_player_model_box.curvalue].nskins-i>0)
				re.DrawScaledChar( 
					icon_x + icon_offset + scaledVideo(5),
					icon_y - scaledVideo(30),
					'>' , 2*scaledVideo(MENU_FONT_SIZE)/con_font_size->value, color[0],color[1],color[2],255,false);
		}
	}
}

void PConfigAccept (void)
{
	int i;
	char scratch[1024];

	Cvar_Set( "name", s_player_name_field.buffer );

	Com_sprintf( scratch, sizeof( scratch ), "%s/%s", 
		s_pmi[s_player_model_box.curvalue].directory, 
		s_pmi[s_player_model_box.curvalue].skindisplaynames[s_player_skin_box.curvalue] );

	Cvar_Set( "skin", scratch );

	for ( i = 0; i < s_numplayermodels; i++ )
	{
		int j;

		for ( j = 0; j < s_pmi[i].nskins; j++ )
		{
			if ( s_pmi[i].skindisplaynames[j] )
				free( s_pmi[i].skindisplaynames[j] );
			s_pmi[i].skindisplaynames[j] = 0;
		}
		free( s_pmi[i].skindisplaynames );
		s_pmi[i].skindisplaynames = 0;
		s_pmi[i].nskins = 0;
	}
}

const char *PlayerConfig_MenuKey (int key)
{
	if ( key == K_ESCAPE )
		PConfigAccept();

	return Default_MenuKey( &s_player_config_menu, key );
}


void M_Menu_PlayerConfig_f (void)
{
	if (!PlayerConfig_MenuInit())
	{
		Menu_SetStatusBar( &s_multiplayer_menu, "No valid player models found" );
		return;
	}
	Menu_SetStatusBar( &s_multiplayer_menu, NULL );
	M_PushMenu( PlayerConfig_MenuDraw, PlayerConfig_MenuKey );
}


/*
=======================================================================

GALLERY MENU

=======================================================================
*/
#if 0
void M_Menu_Gallery_f( void )
{
	extern void Gallery_MenuDraw( void );
	extern const char *Gallery_MenuKey( int key );

	M_PushMenu( Gallery_MenuDraw, Gallery_MenuKey );
}
#endif

/*
=======================================================================

QUIT MENU

=======================================================================
*/

static menuframework_s	s_quit_menu;
static menuseparator_s	s_quit_question;
static menuaction_s		s_quit_yes_action;
static menuaction_s		s_quit_no_action;

void M_Quit_Draw( void )
{
	M_Banner( "m_banner_quit" );

	Menu_AdjustCursor( &s_quit_menu, 1 );

	Menu_Draw( &s_quit_menu );
}

const char *M_Quit_MenuKey( int key )
{
	return Default_MenuKey( &s_quit_menu, key );
}

void quitActionNo (void *blah)
{
	M_PopMenu();
}
void quitActionYes (void *blah)
{
	CL_Quit_f();
}

void Quit_MenuInit (void)
{

	s_quit_menu.x = viddef.width * 0.50 - scaledVideo(20);
	s_quit_menu.y = viddef.height * 0.50 - scaledVideo(58);
	s_quit_menu.nitems = 0;

	s_quit_question.generic.type	= MTYPE_SEPARATOR;
	s_quit_question.generic.name	= "Are you sure?";
	s_quit_question.generic.x	= strlen(s_quit_question.generic.name)*MENU_FONT_SIZE*0.5;
	s_quit_question.generic.y	= MENU_FONT_SIZE*2;

	s_quit_yes_action.generic.type	= MTYPE_ACTION;
	s_quit_yes_action.generic.flags  = QMF_LEFT_JUSTIFY;
	s_quit_yes_action.generic.x		= 0;
	s_quit_yes_action.generic.y		= MENU_FONT_SIZE*5;
	s_quit_yes_action.generic.name	= "yes";
	s_quit_yes_action.generic.callback = quitActionYes;

	s_quit_no_action.generic.type	= MTYPE_ACTION;
	s_quit_no_action.generic.flags  = QMF_LEFT_JUSTIFY;
	s_quit_no_action.generic.x		= 0;
	s_quit_no_action.generic.y		= MENU_FONT_SIZE*7;
	s_quit_no_action.generic.name	= "no";
	s_quit_no_action.generic.callback = quitActionNo;
	
	Menu_AddItem( &s_quit_menu, ( void * ) &s_quit_question );
	Menu_AddItem( &s_quit_menu, ( void * ) &s_quit_yes_action );
	Menu_AddItem( &s_quit_menu, ( void * ) &s_quit_no_action );

	Menu_SetStatusBar( &s_quit_menu, NULL );

	Menu_Center( &s_quit_menu );
}

void M_Menu_Quit_f (void)
{
	Quit_MenuInit();
	M_PushMenu (M_Quit_Draw, M_Quit_MenuKey);
}



//=============================================================================
/* Menu Subsystem */


/*
=================
M_Init
=================
*/
void M_Init (void)
{
	Cmd_AddCommand ("menu_main", M_Menu_Main_f);
	Cmd_AddCommand ("menu_game", M_Menu_Game_f);
		Cmd_AddCommand ("menu_loadgame", M_Menu_LoadGame_f);
		Cmd_AddCommand ("menu_savegame", M_Menu_SaveGame_f);
		Cmd_AddCommand ("menu_joinserver", M_Menu_JoinServer_f);
			Cmd_AddCommand ("menu_addressbook", M_Menu_AddressBook_f);
		Cmd_AddCommand ("menu_startserver", M_Menu_StartServer_f);
			Cmd_AddCommand ("menu_dmoptions", M_Menu_DMOptions_f);
		Cmd_AddCommand ("menu_playerconfig", M_Menu_PlayerConfig_f);
			Cmd_AddCommand ("menu_downloadoptions", M_Menu_DownloadOptions_f);
		Cmd_AddCommand ("menu_credits", M_Menu_Credits_f );
	Cmd_AddCommand ("menu_multiplayer", M_Menu_Multiplayer_f );
	Cmd_AddCommand ("menu_video", M_Menu_Video_f);
	Cmd_AddCommand ("menu_options", M_Menu_Options_f);
		Cmd_AddCommand ("menu_keys", M_Menu_Keys_f);
	Cmd_AddCommand ("menu_quit", M_Menu_Quit_f);
}

/*
=================================
Menu Mouse Cursor - psychospaz
=================================
*/

void refreshCursorMenu (void)
{
	cursor.menu = NULL;
}
void refreshCursorLink (void)
{
	cursor.menuitem = NULL;
}

int Slider_CursorPositionX ( menuslider_s *s )
{
	float range;

	range = ( s->curvalue - s->minvalue ) / ( float ) ( s->maxvalue - s->minvalue );

	if ( range < 0)
		range = 0;
	if ( range > 1)
		range = 1;

	return ( int )( scaledVideo(MENU_FONT_SIZE) + RCOLUMN_OFFSET + (SLIDER_RANGE)*scaledVideo(MENU_FONT_SIZE) * range );
}

int newSliderValueForX (int x, menuslider_s *s)
{
	float newValue;
	int newValueInt;
	int pos = x - scaledVideo(MENU_FONT_SIZE + RCOLUMN_OFFSET + s->generic.x) - s->generic.parent->x;

	newValue = ((float)pos)/((SLIDER_RANGE-1)*scaledVideo(MENU_FONT_SIZE));
	newValueInt = s->minvalue + newValue * (float)( s->maxvalue - s->minvalue );

	return newValueInt;
}

void Slider_CheckSlide( menuslider_s *s )
{
	if ( s->curvalue > s->maxvalue )
		s->curvalue = s->maxvalue;
	else if ( s->curvalue < s->minvalue )
		s->curvalue = s->minvalue;

	if ( s->generic.callback )
		s->generic.callback( s );
}

void Menu_DragSlideItem (menuframework_s *menu, void *menuitem)
{
	menucommon_s *item = ( menucommon_s * ) menuitem;
	menuslider_s *slider = ( menuslider_s * ) menuitem;

	slider->curvalue = newSliderValueForX(cursor.x, slider);
	Slider_CheckSlide ( slider );
}

void Menu_ClickSlideItem (menuframework_s *menu, void *menuitem)
{
	int min, max;
	menucommon_s *item = ( menucommon_s * ) menuitem;
	menuslider_s *slider = ( menuslider_s * ) menuitem;

	min = menu->x + scaledVideo(item->x + Slider_CursorPositionX(slider) - 4);
	max = menu->x + scaledVideo(item->x + Slider_CursorPositionX(slider) + 4);

	if (cursor.x < min)
		Menu_SlideItem( menu, -1 );
	if (cursor.x > max)
		Menu_SlideItem( menu, 1 );
}

void M_Think_MouseCursor (void)
{
	char * sound = NULL;
	menuframework_s *m = (menuframework_s *)cursor.menu;

	if (m_drawfunc == M_Main_Draw) //have to hack for main menu :p
	{
		CheckMainMenuMouse();
		return;
	}
	if (m_drawfunc == M_Credits_MenuDraw) //have to hack for credits :p
	{
		if (cursor.buttonclicks[MOUSEBUTTON2])
		{
			cursor.buttonused[MOUSEBUTTON2] = true;
			cursor.buttonclicks[MOUSEBUTTON2] = 0;
			cursor.buttonused[MOUSEBUTTON1] = true;
			cursor.buttonclicks[MOUSEBUTTON1] = 0;
			S_StartLocalSound( menu_out_sound );
			if (creditsBuffer)
				FS_FreeFile (creditsBuffer);
			M_PopMenu();
			return;
		}
	}
	//mouse clicking on the player model menu...
	if (m_drawfunc == PlayerConfig_MenuDraw)
		PlayerConfig_MouseClick();
	if (m_drawfunc == Options_MenuDraw && options_menu->value==4)
		OptionsMisc_MouseClick();

	if (!m)
		return;

	//Exit with double click 2nd mouse button

	if (cursor.menuitem)
	{
		//MOUSE1
		if (cursor.buttondown[MOUSEBUTTON1])
		{
			if (cursor.menuitemtype == MENUITEM_SLIDER)
			{
				Menu_DragSlideItem(m, cursor.menuitem);
			}
			else if (!cursor.buttonused[MOUSEBUTTON1] && cursor.buttonclicks[MOUSEBUTTON1])
			{
				if (cursor.menuitemtype == MENUITEM_ROTATE)
				{
					if (menu_rotate->value)					
						Menu_SlideItem( m, -1 );
					else			
						Menu_SlideItem( m, 1 );

					sound = menu_move_sound;
					cursor.buttonused[MOUSEBUTTON1] = true;
				}
				else
				{
					cursor.buttonused[MOUSEBUTTON1] = true;
					Menu_MouseSelectItem( cursor.menuitem );
					sound = menu_move_sound;
				}
			}
		}
		//MOUSE2
		if (cursor.buttondown[MOUSEBUTTON2] && cursor.buttonclicks[MOUSEBUTTON2])
		{
			if (cursor.menuitemtype == MENUITEM_SLIDER && !cursor.buttonused[MOUSEBUTTON2])
			{
				Menu_ClickSlideItem(m, cursor.menuitem);
				sound = menu_move_sound;
				cursor.buttonused[MOUSEBUTTON2] = true;
			}
			else if (!cursor.buttonused[MOUSEBUTTON2])
			{
				if (cursor.menuitemtype == MENUITEM_ROTATE)
				{
					if (menu_rotate->value)					
						Menu_SlideItem( m, 1 );
					else			
						Menu_SlideItem( m, -1 );

					sound = menu_move_sound;
					cursor.buttonused[MOUSEBUTTON2] = true;
				}
			}
		}
	}
	else if (!cursor.buttonused[MOUSEBUTTON2] && cursor.buttonclicks[MOUSEBUTTON2]==2 && cursor.buttondown[MOUSEBUTTON2])
	{
		if (m_drawfunc==PlayerConfig_MenuDraw)
			PConfigAccept();

		M_PopMenu();

		sound = menu_out_sound;
		cursor.buttonused[MOUSEBUTTON2] = true;
		cursor.buttonclicks[MOUSEBUTTON2] = 0;
		cursor.buttonused[MOUSEBUTTON1] = true;
		cursor.buttonclicks[MOUSEBUTTON1] = 0;
	}

	if ( sound )
		S_StartLocalSound( sound );
	else if (m_drawfunc == PlayerConfig_MenuDraw)
	{
		cursor.buttonused[MOUSEBUTTON2] = true;
		cursor.buttonclicks[MOUSEBUTTON2] = 0;
		cursor.buttonused[MOUSEBUTTON1] = true;
		cursor.buttonclicks[MOUSEBUTTON1] = 0;
	}
}
void M_Draw_Cursor (void)
{
	int w,h;

	//get sizing vars
	re.DrawGetPicSize( &w, &h, "m_mouse_cursor" );
	w = scaledVideo(w)*0.5;
	h = scaledVideo(h)*0.5;
	re.DrawStretchPic (cursor.x-w/2, cursor.y-h/2, w, h, "m_mouse_cursor");
}

/*
=================
M_Draw
=================
*/

void M_Draw (void)
{
	int background, n, w, h;
	float alpha;

	if (cls.key_dest != key_menu)
		return;

	//this is for scaling the menu...
	//this is kinda overkill, but whatever...
	menuScale.x = viddef.width;
	menuScale.y = viddef.height;
	menuScale.avg = viddef.width/MENU_STATIC_WIDTH;

	// repaint everything next frame
	SCR_DirtyScreen ();

	// dim everything behind it down
	if (cl.cinematictime > 0 || cls.state == ca_disconnected || cls.state == ca_uninitialized)
		alpha = 1; 
	else 
		alpha = 0.75;

	re.DrawFadeBox(0, 0, viddef.width, viddef.height, 0, 0, 0, alpha);
	re.DrawStretchPic (0, 0, viddef.width, viddef.height, "m_background");

	refreshCursorMenu();

	m_drawfunc ();

	// delay playing the enter sound until after the
	// menu has been drawn, to avoid delay while
	// caching images
	if (m_entersound)
	{
		S_StartLocalSound( menu_in_sound );
		m_entersound = false;
	}

	//menu cursor for mouse usage :)
	M_Draw_Cursor();
}


/*
=================
M_Keydown
=================
*/
void M_Keydown (int key)
{
	const char *s;

	if (m_keyfunc)
		if ( ( s = m_keyfunc( key ) ) != 0 )
			S_StartLocalSound( ( char * ) s );
}


