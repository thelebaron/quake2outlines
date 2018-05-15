#include "client.h"

/************************************
	LIBRARY SETUP ETC...
************************************/

client_export_t *GetClientAPI (client_import_t *import)
{
	ci = *import;

	cglobals.apiversion = GAME_API_VERSION;

	cglobals.clientEnts = NULL;
	cglobals.clientEntsNum = 0;

	cglobals.setParticleImages = NULL;

	return &cglobals;
}