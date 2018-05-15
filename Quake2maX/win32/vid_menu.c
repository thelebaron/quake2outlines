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
#include "../client/client.h"
#include "../client/qmenu.h"

extern cvar_t *vid_ref;
extern cvar_t *vid_fullscreen;
extern cvar_t *vid_gamma;
extern cvar_t *scr_viewsize;

cvar_t	*r_overbrightbits;
cvar_t	*r_celshading;

static cvar_t *gl_mode;
static cvar_t *gl_driver;
static cvar_t *gl_picmip;
static cvar_t *gl_texturemode;
static cvar_t *gl_anisotropic;
static cvar_t *gl_finish;
static cvar_t *con_font_size;

extern void M_ForceMenuOff( void );

float scaledVideo (float param);
float videoScale (void);

/*
====================================================================

MENU INTERACTION

====================================================================
*/

static menuframework_s	s_opengl_menu;

static menulist_s		s_mode_list;
static menulist_s		s_texqual_box;
static menuslider_s		s_screensize_slider;
static menuslider_s		s_brightness_slider;
static menulist_s		s_tfilter_box;
static menulist_s  		s_fs_box;
static menulist_s  		s_finish_box;
static menuaction_s		s_apply_action;
static menuaction_s		s_defaults_action;


static menuslider_s		s_lightmapscale_slider;
static menulist_s  		s_overbrights_box;
static menulist_s  		s_vertexlight_box;
static menulist_s  		s_shaders_box;
static menulist_s  		s_texcompress_box;
static menuaction_s		s_back_action;

static float ClampCvar( float min, float max, float value )
{
	if ( value < min ) return min;
	if ( value > max ) return max;
	return value;
}

static void ScreenSizeCallback( void *s )
{
	menuslider_s *slider = ( menuslider_s * ) s;

	Cvar_SetValue( "viewsize", slider->curvalue * 10 );
}

static void BrightnessCallback( void *s )
{
	menuslider_s *slider = ( menuslider_s * ) s;

	Cvar_SetValue( "vid_gamma", ( 0.8 - ( s_brightness_slider.curvalue/10.0 - 0.5 ) ) + 0.5 );
}

static void ResetDefaults( void *unused )
{
	VID_MenuInit();
}

static void prepareVideoRefresh( void )
{
	//set the right mode for refresh
	Cvar_Set( "vid_ref", "gl" );
	Cvar_Set( "gl_driver", "opengl32" );

	//tell them theyr're modified so they refresh
	if ( vid_gamma->modified )
		vid_ref->modified = true;
	if ( gl_driver->modified )
		vid_ref->modified = true;
}

static void ApplyChanges( void *unused )
{

	/*
	** invert sense so greater = brighter, and scale to a range of 0.5 to 1.3
	*/

	if (s_tfilter_box.curvalue == 0)
		Cvar_Set("gl_texturemode", "GL_NEAREST");
	else if (s_tfilter_box.curvalue == 1)
		Cvar_Set("gl_texturemode", "GL_NEAREST_MIPMAP_NEAREST");
	else if (s_tfilter_box.curvalue == 2)
		Cvar_Set("gl_texturemode", "GL_LINEAR");
	else if (s_tfilter_box.curvalue == 3)
		Cvar_Set("gl_texturemode", "GL_LINEAR_MIPMAP_NEAREST");
	else if (s_tfilter_box.curvalue == 4)
		Cvar_Set("gl_texturemode", "GL_LINEAR_MIPMAP_LINEAR");
	else if (s_tfilter_box.curvalue == 5)
		Cvar_Set("gl_texturemode", "GL_LINEAR_MIPMAP_NEAREST");
	else //if (s_tfilter_box.curvalue == 6)
		Cvar_Set("gl_texturemode", "GL_LINEAR_MIPMAP_LINEAR");

	Cvar_SetValue( "gl_anisotropic", (s_tfilter_box.curvalue > 4) );
	Cvar_SetValue( "gl_picmip", 3-s_texqual_box.curvalue );
	Cvar_SetValue( "vid_fullscreen", s_fs_box.curvalue );
	Cvar_SetValue( "gl_finish", s_finish_box.curvalue );
	Cvar_SetValue( "gl_mode", s_mode_list.curvalue );
	Cvar_SetValue( "gl_ext_texture_compression", s_texcompress_box.curvalue );
	Cvar_SetValue( "r_shaders", s_shaders_box.curvalue );
	Cvar_SetValue( "r_overbrightbits", (int)(s_overbrights_box.curvalue)<<1 );

	prepareVideoRefresh ();

	M_ForceMenuOff();
}

static void CancelChanges( void *unused )
{
	extern void M_PopMenu( void );

	M_PopMenu();
}

/*
** VID_MenuInit
*/

void VID_MenuInit( void )
{	
	static const char *resolutions[] = 
	{
		"[320 240  ]",
		"[400 300  ]",
		"[512 384  ]",
		"[640 480  ]",
		"[800 600  ]",
		"[960 720  ]",
		"[1024 768 ]",
		"[1152 864 ]",
		"[1280 960 ]",
		"[1600 1200]",
		"[2048 1536]",
		0
	};
	static const char *yesno_names[] =
	{
		"no",
		"yes",
		0
	};
	static const char *mip_names[] =
	{
		"none",
		"nearest",
		"linear",
		"bilinear",
		"trilinear",
		"bilinear anisotropic",
		"trilinear anisotopic",		
		0
	};
	static const char *lmh_names[] =
	{
		"low",
		"medium",
		"high",
		"highest",
		0
	};

	if ( !gl_driver )
		gl_driver = Cvar_Get( "gl_driver", "opengl32", 0 );
	if ( !gl_picmip )
		gl_picmip = Cvar_Get( "gl_picmip", "0", 0 );
	if ( !gl_mode )
		gl_mode = Cvar_Get( "gl_mode", "0", 0 );
	if ( !gl_finish )
		gl_finish = Cvar_Get( "gl_finish", "0", CVAR_ARCHIVE );
	if ( !gl_texturemode )
		gl_texturemode = Cvar_Get( "gl_texturemode", "GL_LINEAR_MIPMAP_NEAREST", CVAR_ARCHIVE );
	if ( !gl_anisotropic )
		gl_anisotropic = Cvar_Get( "gl_anisotropic", "0", CVAR_ARCHIVE );
	if ( !r_overbrightbits )
		r_overbrightbits = Cvar_Get( "r_overbrightbits", "2", CVAR_ARCHIVE );


	s_mode_list.curvalue = gl_mode->value;

	if ( !scr_viewsize )
		scr_viewsize = Cvar_Get ("viewsize", "100", CVAR_ARCHIVE);

	if ( !con_font_size )
		con_font_size = Cvar_Get ("con_font_size", "8", CVAR_ARCHIVE);

	s_screensize_slider.curvalue = scr_viewsize->value/10;

	s_opengl_menu.x = viddef.width/2;
	s_opengl_menu.y = viddef.height/2 - scaledVideo(58);
	s_opengl_menu.nitems = 0;

	s_mode_list.generic.type = MTYPE_SPINCONTROL;
	s_mode_list.generic.name = "video mode";
	s_mode_list.generic.x = 0;
	s_mode_list.generic.y = MENU_FONT_SIZE*2+2;
	s_mode_list.itemnames = resolutions;
	s_mode_list.generic.statusbar	= "video resolution";
	
	s_fs_box.generic.type = MTYPE_SPINCONTROL;
	s_fs_box.generic.x	= 0;
	s_fs_box.generic.y	= MENU_FONT_SIZE*3+2;
	s_fs_box.generic.name	= "fullscreen";
	s_fs_box.itemnames = yesno_names;
	s_fs_box.curvalue = vid_fullscreen->value;

	s_screensize_slider.generic.type	= MTYPE_SLIDER;
	s_screensize_slider.generic.x		= 0;
	s_screensize_slider.generic.y		= MENU_FONT_SIZE*4+2;
	s_screensize_slider.generic.name	= "screen size";
	s_screensize_slider.minvalue = 3;
	s_screensize_slider.maxvalue = 12;
	s_screensize_slider.generic.callback = ScreenSizeCallback;

	s_brightness_slider.generic.type	= MTYPE_SLIDER;
	s_brightness_slider.generic.x	= 0;
	s_brightness_slider.generic.y	= MENU_FONT_SIZE*5+2;
	s_brightness_slider.generic.name	= "brightness";
	s_brightness_slider.generic.callback = BrightnessCallback;
	s_brightness_slider.minvalue = 5;
	s_brightness_slider.maxvalue = 13;
	s_brightness_slider.curvalue = ( 1.3 - vid_gamma->value + 0.5 ) * 10;

	s_overbrights_box.generic.type = MTYPE_SPINCONTROL;
	s_overbrights_box.generic.x	= 0;
	s_overbrights_box.generic.y	= MENU_FONT_SIZE*6+2;
	s_overbrights_box.generic.name	= "overbrights";
	s_overbrights_box.itemnames = lmh_names;
	s_overbrights_box.curvalue = (int)(r_overbrightbits->value)>>1;

	s_tfilter_box.generic.type = MTYPE_SPINCONTROL;
	s_tfilter_box.generic.x	= 0;
	s_tfilter_box.generic.y	= MENU_FONT_SIZE*8+2;
	s_tfilter_box.generic.name	= "texture filter";
	s_tfilter_box.itemnames = mip_names;
	if (!Q_strcasecmp(gl_texturemode->string, "GL_NEAREST"))
		s_tfilter_box.curvalue = 0;
	else if (!Q_strcasecmp(gl_texturemode->string, "GL_NEAREST_MIPMAP_NEAREST"))
		s_tfilter_box.curvalue = 1;
	else if (!Q_strcasecmp(gl_texturemode->string, "GL_LINEAR"))
		s_tfilter_box.curvalue = 2;
	else if (!Q_strcasecmp(gl_texturemode->string, "GL_LINEAR_MIPMAP_NEAREST"))
		s_tfilter_box.curvalue = (gl_anisotropic->value)? 5 :3;
	else // if (!Q_strcasecmp(gl_texturemode->string, "GL_LINEAR_MIPMAP_LINEAR"))
		s_tfilter_box.curvalue = (gl_anisotropic->value)? 6 :4;

	s_texqual_box.generic.type	= MTYPE_SPINCONTROL;
	s_texqual_box.generic.x		= 0;
	s_texqual_box.generic.y		= MENU_FONT_SIZE*9+2;
	s_texqual_box.generic.name	= "texture quality";
	s_texqual_box.itemnames = lmh_names;
	s_texqual_box.curvalue = ClampCvar( 0, 3, 3-gl_picmip->value );

	s_texcompress_box.generic.type		= MTYPE_SPINCONTROL;
	s_texcompress_box.generic.x			= 0;
	s_texcompress_box.generic.y			= MENU_FONT_SIZE*10+2;
	s_texcompress_box.generic.name		= "texture compression";
	s_texcompress_box.itemnames			= yesno_names;
	s_texcompress_box.curvalue			= Cvar_VariableValue("gl_ext_texture_compression");
	s_texcompress_box.generic.statusbar	= "low quality textures";

	s_shaders_box.generic.type		= MTYPE_SPINCONTROL;
	s_shaders_box.generic.x			= 0;
	s_shaders_box.generic.y			= MENU_FONT_SIZE*11+2;
	s_shaders_box.generic.name		= "shaders";
	s_shaders_box.itemnames			= yesno_names;
	s_shaders_box.curvalue			= Cvar_VariableValue("r_shaders");
	s_shaders_box.generic.statusbar	= "dynamic texturing";

	s_finish_box.generic.type = MTYPE_SPINCONTROL;
	s_finish_box.generic.x	= 0;
	s_finish_box.generic.y	= MENU_FONT_SIZE*13+2;
	s_finish_box.generic.name	= "sync every frame";
	s_finish_box.curvalue = gl_finish->value;
	s_finish_box.itemnames = yesno_names;


	s_defaults_action.generic.type = MTYPE_ACTION;
	s_defaults_action.generic.name = "reset to defaults";
	s_defaults_action.generic.x    = 0;
	s_defaults_action.generic.y    = MENU_FONT_SIZE*15+2;
	s_defaults_action.generic.callback = ResetDefaults;

	s_apply_action.generic.type = MTYPE_ACTION;
	s_apply_action.generic.name = "apply changes";
	s_apply_action.generic.x    = 0;
	s_apply_action.generic.y    = MENU_FONT_SIZE*16+2;
	s_apply_action.generic.callback = ApplyChanges;

	Menu_AddItem( &s_opengl_menu, ( void * ) &s_mode_list );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_fs_box );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_screensize_slider );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_brightness_slider );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_overbrights_box );

	Menu_AddItem( &s_opengl_menu, ( void * ) &s_tfilter_box );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_texqual_box );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_texcompress_box );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_shaders_box );

	Menu_AddItem( &s_opengl_menu, ( void * ) &s_finish_box );

	Menu_AddItem( &s_opengl_menu, ( void * ) &s_defaults_action );
	Menu_AddItem( &s_opengl_menu, ( void * ) &s_apply_action );

	Menu_Center( &s_opengl_menu );

	s_opengl_menu.x -= 8;
}

/*
================
VID_MenuDraw
================
*/
void M_Banner( char *name );

void VID_MenuDraw (void)
{
	int w, h;

	/*
	** draw the banner
	*/
	M_Banner("m_banner_video");
	/*
	** move cursor to a reasonable starting position
	*/
	Menu_AdjustCursor( &s_opengl_menu, 1 );

	/*
	** draw the menu
	*/
	Menu_Draw( &s_opengl_menu );
}

/*
================
VID_MenuKey
================
*/
const char *VID_MenuKey( int key )
{
	menuframework_s *m = &s_opengl_menu;
	static const char *sound = "misc/menu1.wav";

	switch ( key )
	{
	case K_ESCAPE:
		CancelChanges( 0 );
		return NULL;
	case K_KP_UPARROW:
	case K_UPARROW:
		m->cursor--;
		Menu_AdjustCursor( m, -1 );
		break;
	case K_KP_DOWNARROW:
	case K_DOWNARROW:
		m->cursor++;
		Menu_AdjustCursor( m, 1 );
		break;
	case K_KP_LEFTARROW:
	case K_LEFTARROW:
		Menu_SlideItem( m, -1 );
		break;
	case K_KP_RIGHTARROW:
	case K_RIGHTARROW:
		Menu_SlideItem( m, 1 );
		break;
	case K_KP_ENTER:
	case K_ENTER:
		if ( !Menu_SelectItem( m ) )
			ApplyChanges( NULL );
		break;
	}

	return sound;
}


