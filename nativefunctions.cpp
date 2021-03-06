//	Python plugin for SAMP
//	Copyright (C) 2010-2012 Fabsch
//
//	This program is free software: you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "pythonplugin.h"
#include "nativefunctions.h"
#include "pysamp.h"
#include "constants.h"


//-----------------------------------------
// functions for finding native PAWN functions
//-----------------------------------------

amx_function_t _findNative(AMX *amx, const char *name, bool nowarn)
{
	int idx;
	// find the function and check for an error
	if (amx_FindNative(amx, name, &idx) == AMX_ERR_NONE)
	{
		// no error, proceed
		// locate memory address
		AMX_HEADER *header = (AMX_HEADER*)amx->base;
		unsigned int addr = (unsigned int)((AMX_FUNCSTUB*)((char*)header + header->natives + header->defsize * idx))->address;

		if (addr != 0)
			return (amx_function_t)addr;
	}
	if (!nowarn) logprintf("PYTHON: Could not find native %s", name);
	return NULL;
}

// Returns the cell that should be released with amx_Release (or zero)
cell _pyArgsToAMX(cell *amxargs, PyObject *pyargs, unsigned int start_from, bool by_value)
{
	PyObject* current_argument = NULL;
	cell *pawn_address = NULL;
	Py_ssize_t pyargs_count = PyTuple_Size(pyargs) - start_from;
	// +1 because first AMX arg is length of args
	unsigned int current_amx_arg = start_from + 1;
	cell ret = 0;

	for(Py_ssize_t i = 0; i < pyargs_count; i++)
	{
		current_argument = PyTuple_GetItem(pyargs, i + start_from);

		if(PyBool_Check(current_argument))
		{
			if(by_value)
				pawn_address = &(amxargs[current_amx_arg]);
			else
			{
				amx_Allot(m_AMX, 1, &(amxargs[current_amx_arg]), &pawn_address);
				if(ret == 0)
					ret = amxargs[current_amx_arg];
			}
			*pawn_address = PyObject_IsTrue(current_argument);
		}
		else if(PyLong_Check(current_argument))
		{
			if(by_value)
				pawn_address = &(amxargs[current_amx_arg]);
			else
			{
				amx_Allot(m_AMX, 1, &(amxargs[current_amx_arg]), &pawn_address);
				if(ret == 0)
					ret = amxargs[current_amx_arg];
			}
			*pawn_address = PyLong_AsLong(current_argument);
		}
		else if(PyFloat_Check(current_argument))
		{
			float python_float = PyFloat_AsDouble(current_argument);
			if(by_value)
				pawn_address = &(amxargs[current_amx_arg]);
			else
			{
				amx_Allot(m_AMX, 1, &(amxargs[current_amx_arg]), &pawn_address);
				if(ret == 0)
					ret = amxargs[current_amx_arg];
			}
			*pawn_address = amx_ftoc(python_float);
		}
		else if(PyUnicode_Check(current_argument))
		{
			Py_ssize_t python_string_length;
			const char *python_string = PyUnicode_AsUTF8AndSize(
				current_argument,
				&python_string_length
			);
			python_string_length += 1;  // Include final null byte
			amx_Allot(
				m_AMX,
				python_string_length,
				&(amxargs[current_amx_arg]),
				&pawn_address
			);
			amx_SetString(
				pawn_address,
				python_string,
				0,
				0,
				python_string_length
			);
			if(ret == 0)
				ret = amxargs[current_amx_arg];
		}
		else if(by_value && (
			PyTuple_Check(current_argument)
			|| PyList_Check(current_argument)
		))
		{
			cell new_ret = 0;
			new_ret = _pyArgsToAMX(
				&(amxargs[current_amx_arg - 1]),
				current_argument,
				0
			);
			if(ret == 0)
				ret = new_ret;
		}
		else
		{
			char *repr;
			size_t size;
			FILE *bytes_io = open_memstream(&repr, &size);
			PyObject_Print(current_argument, bytes_io, 0);
			fclose(bytes_io);
			logprintf(
				"PYTHON: Could not convert argument %s",
				repr
			);
			free(repr);
		}
		current_amx_arg += 1;
	}
	return ret;
}

// Gets the total size of sequence args recursively (top-level only)
Py_ssize_t _getRecursiveSize(PyObject *args)
{
	Py_ssize_t start_size = PySequence_Size(args);
	Py_ssize_t total_size = start_size;
	PyObject* current_item = NULL;

	for(Py_ssize_t i = 0; i < start_size; ++i)
	{
		current_item = PySequence_GetItem(args, i);
		if(
			PyTuple_Check(current_item)
			|| PyList_Check(current_item)
		)
			total_size += PySequence_Size(current_item);
	}

	return total_size;
}

// Turns a Python string (PyUnicode) object into a cp1252-encoded char*
int _stringToCP1252(PyObject *source, char **destination)
{
	PyObject *bytes = PyUnicode_AsEncodedString(source, "cp1252", "strict");

	if(bytes == NULL)
		return 0;

	char *buffer = NULL;
	Py_ssize_t len;
	int ret = PyBytes_AsStringAndSize(bytes, &buffer, &len);

	if(buffer == NULL || ret == -1)
		return 0;

	*destination = (char *)malloc(len);
	memset(*destination, 0, len);
	memcpy(*destination, buffer, len);
	Py_DECREF(bytes);

	return 1;
}

void _initAMX(AMX *amx)
{
	if (!amx) return;

	_addMenuItem					= _findNative(amx, "AddMenuItem");
	_addPlayerClass					= _findNative(amx, "AddPlayerClass");
	_addPlayerClassEx				= _findNative(amx, "AddPlayerClassEx");
	_addStaticPickup				= _findNative(amx, "AddStaticPickup");
	_addStaticVehicle				= _findNative(amx, "AddStaticVehicle");
	_addStaticVehicleEx				= _findNative(amx, "AddStaticVehicleEx");
	_addVehicleComponent			= _findNative(amx, "AddVehicleComponent");
	_allowAdminTeleport				= _findNative(amx, "AllowAdminTeleport");
	_allowInteriorWeapons			= _findNative(amx, "AllowInteriorWeapons");
	_allowPlayerTeleport			= _findNative(amx, "AllowPlayerTeleport");
	_applyAnimation					= _findNative(amx, "ApplyAnimation");
	_attach3DTextLabelToPlayer		= _findNative(amx, "Attach3DTextLabelToPlayer");
	_attach3DTextLabelToVehicle		= _findNative(amx, "Attach3DTextLabelToVehicle");
	_attachCameraToObject			= _findNative(amx, "AttachCameraToObject");
	_attachCameraToPlayerObject		= _findNative(amx, "AttachCameraToPlayerObject");
	_attachObjectToObject			= _findNative(amx, "AttachObjectToObject");
	_attachObjectToPlayer			= _findNative(amx, "AttachObjectToPlayer");
	_attachObjectToVehicle			= _findNative(amx, "AttachObjectToVehicle");
	_attachPlayerObjectToPlayer		= _findNative(amx, "AttachPlayerObjectToPlayer");
	_attachPlayerObjectToVehicle	= _findNative(amx, "AttachPlayerObjectToVehicle");
	_attachTrailerToVehicle			= _findNative(amx, "AttachTrailerToVehicle");

	_ban							= _findNative(amx, "Ban");
	_banEx							= _findNative(amx, "BanEx");

	_callRemoteFunction					= _findNative(amx, "CallRemoteFunction");

	_cancelEdit						= _findNative(amx, "CancelEdit");
	_cancelSelectTextDraw			= _findNative(amx, "CancelSelectTextDraw");
	_changeVehicleColor				= _findNative(amx, "ChangeVehicleColor");
	_changeVehiclePaintjob			= _findNative(amx, "ChangeVehiclePaintjob");
	_clearAnimations				= _findNative(amx, "ClearAnimations");
	_connectNPC						= _findNative(amx, "ConnectNPC");
	_create3DTextLabel				= _findNative(amx, "Create3DTextLabel");
	_createExplosion				= _findNative(amx, "CreateExplosion");
	_createMenu						= _findNative(amx, "CreateMenu");
	_createObject					= _findNative(amx, "CreateObject");
	_createPickup					= _findNative(amx, "CreatePickup");
	_createPlayer3DTextLabel		= _findNative(amx, "CreatePlayer3DTextLabel");
	_createPlayerObject				= _findNative(amx, "CreatePlayerObject");
	_createVehicle					= _findNative(amx, "CreateVehicle");

	_delete3DTextLabel				= _findNative(amx, "Delete3DTextLabel");
	_deletePVar					= _findNative(amx, "DeletePVar");
	_deletePlayer3DTextLabel		= _findNative(amx, "DeletePlayer3DTextLabel");
	_destroyMenu					= _findNative(amx, "DestroyMenu");
	_destroyObject					= _findNative(amx, "DestroyObject");
	_destroyPickup					= _findNative(amx, "DestroyPickup");
	_destroyPlayerObject			= _findNative(amx, "DestroyPlayerObject");
	_destroyVehicle					= _findNative(amx, "DestroyVehicle");
	_detachTrailerFromVehicle		= _findNative(amx, "DetachTrailerFromVehicle");
	_disableInteriorEnterExits		= _findNative(amx, "DisableInteriorEnterExits");
	_disableMenu					= _findNative(amx, "DisableMenu");
	_disableMenuRow					= _findNative(amx, "DisableMenuRow");
	_disableNameTagLOS				= _findNative(amx, "DisableNameTagLOS");
	_disablePlayerCheckpoint		= _findNative(amx, "DisablePlayerCheckpoint");
	_disablePlayerRaceCheckpoint	= _findNative(amx, "DisablePlayerRaceCheckpoint");

	_editObject						= _findNative(amx, "EditObject");
	_editPlayerObject				= _findNative(amx, "EditPlayerObject");
	_editAttachedObject				= _findNative(amx, "EditAttachedObject");
	_enableStuntBonusForAll			= _findNative(amx, "EnableStuntBonusForAll");
	_enableStuntBonusForPlayer		= _findNative(amx, "EnableStuntBonusForPlayer");
	_enableVehicleFriendlyFire		= _findNative(amx, "EnableVehicleFriendlyFire");
	_forceClassSelection			= _findNative(amx, "ForceClassSelection");

	_gameModeExit					= _findNative(amx, "GameModeExit");
	_gameTextForAll					= _findNative(amx, "GameTextForAll");
	_gameTextForPlayer				= _findNative(amx, "GameTextForPlayer");
	_gangZoneCreate					= _findNative(amx, "GangZoneCreate");
	_gangZoneDestroy				= _findNative(amx, "GangZoneDestroy");
	_gangZoneFlashForAll			= _findNative(amx, "GangZoneFlashForAll");
	_gangZoneFlashForPlayer			= _findNative(amx, "GangZoneFlashForPlayer");
	_gangZoneHideForAll				= _findNative(amx, "GangZoneHideForAll");
	_gangZoneHideForPlayer			= _findNative(amx, "GangZoneHideForPlayer");
	_gangZoneShowForAll				= _findNative(amx, "GangZoneShowForAll");
	_gangZoneShowForPlayer			= _findNative(amx, "GangZoneShowForPlayer");
	_gangZoneStopFlashForAll		= _findNative(amx, "GangZoneStopFlashForAll");
	_gangZoneStopFlashForPlayer		= _findNative(amx, "GangZoneStopFlashForPlayer");
	_getAnimationName				= _findNative(amx, "GetAnimationName");
	_getMaxPlayers					= _findNative(amx, "GetMaxPlayers");
	_getNetworkStats				= _findNative(amx, "GetNetworkStats");
	_getObjectPos					= _findNative(amx, "GetObjectPos");
	_getObjectRot					= _findNative(amx, "GetObjectRot");
	_getPVarFloat					= _findNative(amx, "GetPVarFloat");
	_getPVarInt					= _findNative(amx, "GetPVarInt");
	_getPVarString					= _findNative(amx, "GetPVarString");
	_getPlayerAmmo					= _findNative(amx, "GetPlayerAmmo");
	_getPlayerAnimationIndex		= _findNative(amx, "GetPlayerAnimationIndex");
	_getPlayerArmour				= _findNative(amx, "GetPlayerArmour");
	_getPlayerCameraFrontVector		= _findNative(amx, "GetPlayerCameraFrontVector");
	_getPlayerCameraMode			= _findNative(amx, "GetPlayerCameraMode");
	_getPlayerCameraPos				= _findNative(amx, "GetPlayerCameraPos");
	_getPlayerColor					= _findNative(amx, "GetPlayerColor");
	_getPlayerDistanceFromPoint		= _findNative(amx, "GetPlayerDistanceFromPoint");
	_getPlayerDrunkLevel			= _findNative(amx, "GetPlayerDrunkLevel");
	_getPlayerFacingAngle			= _findNative(amx, "GetPlayerFacingAngle");
	_getPlayerFightingStyle			= _findNative(amx, "GetPlayerFightingStyle");
	_getPlayerHealth				= _findNative(amx, "GetPlayerHealth");
	_getPlayerInterior				= _findNative(amx, "GetPlayerInterior");
	_getPlayerIp					= _findNative(amx, "GetPlayerIp");
	_getPlayerKeys					= _findNative(amx, "GetPlayerKeys");
	_getPlayerMenu					= _findNative(amx, "GetPlayerMenu");
	_getPlayerMoney					= _findNative(amx, "GetPlayerMoney");
	_getPlayerName					= _findNative(amx, "GetPlayerName");
	_getPlayerNetworkStats			= _findNative(amx, "GetPlayerNetworkStats");
	_getPlayerObjectPos				= _findNative(amx, "GetPlayerObjectPos");
	_getPlayerObjectRot				= _findNative(amx, "GetPlayerObjectRot");
	_getPlayerPing					= _findNative(amx, "GetPlayerPing");
	_getPlayerPos					= _findNative(amx, "GetPlayerPos");
	_getPlayerScore					= _findNative(amx, "GetPlayerScore");
	_getPlayerSkin					= _findNative(amx, "GetPlayerSkin");
	_getPlayerSpecialAction			= _findNative(amx, "GetPlayerSpecialAction");
	_getPlayerState					= _findNative(amx, "GetPlayerState");
	_getPlayerSurfingObjectID		= _findNative(amx, "GetPlayerSurfingObjectID");
	_getPlayerSurfingVehicleID		= _findNative(amx, "GetPlayerSurfingVehicleID");
	_getPlayerTargetPlayer			= _findNative(amx, "GetPlayerTargetPlayer");
	_getPlayerTeam					= _findNative(amx, "GetPlayerTeam");
	_getPlayerTime					= _findNative(amx, "GetPlayerTime");
	_getPlayerVehicleID				= _findNative(amx, "GetPlayerVehicleID");
	_getPlayerVehicleSeat			= _findNative(amx, "GetPlayerVehicleSeat");
	_getPlayerVelocity				= _findNative(amx, "GetPlayerVelocity");
	_getPlayerVersion				= _findNative(amx, "GetPlayerVersion");
	_getPlayerVirtualWorld			= _findNative(amx, "GetPlayerVirtualWorld");
	_getPlayerWantedLevel			= _findNative(amx, "GetPlayerWantedLevel");
	_getPlayerWeapon				= _findNative(amx, "GetPlayerWeapon");
	_getPlayerWeaponData			= _findNative(amx, "GetPlayerWeaponData");
	_getPlayerWeaponState			= _findNative(amx, "GetPlayerWeaponState");
	_getTickCount					= _findNative(amx, "GetTickCount");
	_getVehicleComponentInSlot		= _findNative(amx, "GetVehicleComponentInSlot");
	_getVehicleComponentType		= _findNative(amx, "GetVehicleComponentType");
	_getVehicleDamageStatus			= _findNative(amx, "GetVehicleDamageStatus");
	_getVehicleDistanceFromPoint	= _findNative(amx, "GetVehicleDistanceFromPoint");
	_getVehicleHealth				= _findNative(amx, "GetVehicleHealth");
	_getVehicleModel				= _findNative(amx, "GetVehicleModel");
	_getVehicleModelInfo			= _findNative(amx, "GetVehicleModelInfo");
	_getVehiclePos					= _findNative(amx, "GetVehiclePos");
	_getVehicleRotationQuat			= _findNative(amx, "GetVehicleRotationQuat");
	_getVehicleTrailer				= _findNative(amx, "GetVehicleTrailer");
	_getVehicleVelocity				= _findNative(amx, "GetVehicleVelocity");
	_getVehicleVirtualWorld			= _findNative(amx, "GetVehicleVirtualWorld");
	_getVehicleZAngle				= _findNative(amx, "GetVehicleZAngle");
	_getWeaponName					= _findNative(amx, "GetWeaponName");
	_givePlayerMoney				= _findNative(amx, "GivePlayerMoney");
	_givePlayerWeapon				= _findNative(amx, "GivePlayerWeapon");

	_hideMenuForPlayer				= _findNative(amx, "HideMenuForPlayer");

	_interpolateCameraPos			= _findNative(amx, "InterpolateCameraPos");
	_interpolateCameraLookAt		= _findNative(amx, "InterpolateCameraLookAt");
	_isObjectMoving					= _findNative(amx, "IsObjectMoving");
	_isPlayerAdmin					= _findNative(amx, "IsPlayerAdmin");
	_isPlayerAttachedObjectSlotUsed	= _findNative(amx, "IsPlayerAttachedObjectSlotUsed");
	_isPlayerConnected				= _findNative(amx, "IsPlayerConnected");
	_isPlayerHoldingObject			= _findNative(amx, "IsPlayerHoldingObject", true);
	_isPlayerInAnyVehicle			= _findNative(amx, "IsPlayerInAnyVehicle");
	_isPlayerInCheckpoint			= _findNative(amx, "IsPlayerInCheckpoint");
	_isPlayerInRaceCheckpoint		= _findNative(amx, "IsPlayerInRaceCheckpoint");
	_isPlayerInRangeOfPoint			= _findNative(amx, "IsPlayerInRangeOfPoint");
	_isPlayerInVehicle				= _findNative(amx, "IsPlayerInVehicle");
	_isPlayerNPC					= _findNative(amx, "IsPlayerNPC");
	_isPlayerObjectMoving			= _findNative(amx, "IsPlayerObjectMoving");
	_isPlayerStreamedIn				= _findNative(amx, "IsPlayerStreamedIn");
	_isTrailerAttachedToVehicle		= _findNative(amx, "IsTrailerAttachedToVehicle");
	_isValidMenu					= _findNative(amx, "IsValidMenu");
	_isValidObject					= _findNative(amx, "IsValidObject");
	_isValidPlayerObject			= _findNative(amx, "IsValidPlayerObject");
	_isVehicleStreamedIn			= _findNative(amx, "IsVehicleStreamedIn");

	_kick							= _findNative(amx, "Kick");
	_killTimer						= _findNative(amx, "KillTimer");

	_limitGlobalChatRadius			= _findNative(amx, "LimitGlobalChatRadius");
	_limitPlayerMarkerRadius		= _findNative(amx, "LimitPlayerMarkerRadius");
	_linkVehicleToInterior			= _findNative(amx, "LinkVehicleToInterior");

	_manualVehicleEngineAndLights	= _findNative(amx, "ManualVehicleEngineAndLights");
	_moveObject						= _findNative(amx, "MoveObject");
	_movePlayerObject				= _findNative(amx, "MovePlayerObject");

	_playAudioStreamForPlayer		= _findNative(amx, "PlayAudioStreamForPlayer");
	_playCrimeReportForPlayer		= _findNative(amx, "PlayCrimeReportForPlayer");
	_playerPlaySound				= _findNative(amx, "PlayerPlaySound");
	_playerSpectatePlayer			= _findNative(amx, "PlayerSpectatePlayer");
	_playerSpectateVehicle			= _findNative(amx, "PlayerSpectateVehicle");
	_putPlayerInVehicle				= _findNative(amx, "PutPlayerInVehicle");

	_createPlayerTextDraw			= _findNative(amx, "CreatePlayerTextDraw");
	_playerTextDrawDestroy			= _findNative(amx, "PlayerTextDrawDestroy");
	_playerTextDrawLetterSize		= _findNative(amx, "PlayerTextDrawLetterSize");
	_playerTextDrawTextSize			= _findNative(amx, "PlayerTextDrawTextSize");
	_playerTextDrawAlignment		= _findNative(amx, "PlayerTextDrawAlignment");
	_playerTextDrawColor			= _findNative(amx, "PlayerTextDrawColor");
	_playerTextDrawUseBox			= _findNative(amx, "PlayerTextDrawUseBox");
	_playerTextDrawBoxColor			= _findNative(amx, "PlayerTextDrawBoxColor");
	_playerTextDrawSetShadow		= _findNative(amx, "PlayerTextDrawSetShadow");
	_playerTextDrawSetOutline		= _findNative(amx, "PlayerTextDrawSetOutline");
	_playerTextDrawBackgroundColor	= _findNative(amx, "PlayerTextDrawBackgroundColor");
	_playerTextDrawFont				= _findNative(amx, "PlayerTextDrawFont");
	_playerTextDrawSetPreviewModel	= _findNative(amx, "PlayerTextDrawSetPreviewModel");
	_playerTextDrawSetPreviewRot	= _findNative(amx, "PlayerTextDrawSetPreviewRot");
	_playerTextDrawSetPreviewVehCol	= _findNative(amx, "PlayerTextDrawSetPreviewVehCol");
	_playerTextDrawSetProportional	= _findNative(amx, "PlayerTextDrawSetProportional");
	_playerTextDrawSetSelectable	= _findNative(amx, "PlayerTextDrawSetSelectable");
	_playerTextDrawShow				= _findNative(amx, "PlayerTextDrawShow");
	_playerTextDrawHide				= _findNative(amx, "PlayerTextDrawHide");
	_playerTextDrawSetString		= _findNative(amx, "PlayerTextDrawSetString");

	_removeBuildingForPlayer		= _findNative(amx, "RemoveBuildingForPlayer");
	_removePlayerAttachedObject		= _findNative(amx, "RemovePlayerAttachedObject");
	_removePlayerFromVehicle		= _findNative(amx, "RemovePlayerFromVehicle");
	_removePlayerMapIcon			= _findNative(amx, "RemovePlayerMapIcon");
	_removeVehicleComponent			= _findNative(amx, "RemoveVehicleComponent");
	_repairVehicle					= _findNative(amx, "RepairVehicle");
	_resetPlayerMoney				= _findNative(amx, "ResetPlayerMoney");
	_resetPlayerWeapons				= _findNative(amx, "ResetPlayerWeapons");

	_selectObject					= _findNative(amx, "SelectObject");
	_selectTextDraw					= _findNative(amx, "SelectTextDraw");
	_sendClientMessage				= _findNative(amx, "SendClientMessage");
	_sendClientMessageToAll			= _findNative(amx, "SendClientMessageToAll");
	_sendDeathMessage				= _findNative(amx, "SendDeathMessage");
	_sendPlayerMessageToAll			= _findNative(amx, "SendPlayerMessageToAll");
	_sendPlayerMessageToPlayer		= _findNative(amx, "SendPlayerMessageToPlayer");
	_sendRconCommand				= _findNative(amx, "SendRconCommand");
	_setCameraBehindPlayer			= _findNative(amx, "SetCameraBehindPlayer");
	_setGameModeText				= _findNative(amx, "SetGameModeText");
	_setGravity						= _findNative(amx, "SetGravity");
	_setMenuColumnHeader			= _findNative(amx, "SetMenuColumnHeader");
	_setNameTagDrawDistance			= _findNative(amx, "SetNameTagDrawDistance");
	_setObjectMaterial				= _findNative(amx, "SetObjectMaterial");
	_setObjectMaterialText			= _findNative(amx, "SetObjectMaterialText");
	_setObjectPos					= _findNative(amx, "SetObjectPos");
	_setObjectRot					= _findNative(amx, "SetObjectRot");
	_setPVarFloat					= _findNative(amx, "SetPVarFloat");
	_setPVarInt					= _findNative(amx, "SetPVarInt");
	_setPVarString					= _findNative(amx, "SetPVarString");
	_setPlayerAmmo					= _findNative(amx, "SetPlayerAmmo");
	_setPlayerArmedWeapon			= _findNative(amx, "SetPlayerArmedWeapon");
	_setPlayerArmour				= _findNative(amx, "SetPlayerArmour");
	_setPlayerAttachedObject		= _findNative(amx, "SetPlayerAttachedObject");
	_setPlayerCameraLookAt			= _findNative(amx, "SetPlayerCameraLookAt");
	_setPlayerCameraPos				= _findNative(amx, "SetPlayerCameraPos");
	_setPlayerChatBubble			= _findNative(amx, "SetPlayerChatBubble");
	_setPlayerCheckpoint			= _findNative(amx, "SetPlayerCheckpoint");
	_setPlayerColor					= _findNative(amx, "SetPlayerColor");
	_setPlayerDrunkLevel			= _findNative(amx, "SetPlayerDrunkLevel");
	_setPlayerFacingAngle			= _findNative(amx, "SetPlayerFacingAngle");
	_setPlayerFightingStyle			= _findNative(amx, "SetPlayerFightingStyle");
	_setPlayerHealth				= _findNative(amx, "SetPlayerHealth");
	_setPlayerHoldingObject			= _findNative(amx, "SetPlayerHoldingObject", true);
	_setPlayerInterior				= _findNative(amx, "SetPlayerInterior");
	_setPlayerMapIcon				= _findNative(amx, "SetPlayerMapIcon");
	_setPlayerMarkerForPlayer		= _findNative(amx, "SetPlayerMarkerForPlayer");
	_setPlayerName					= _findNative(amx, "SetPlayerName");
	_setPlayerObjectMaterial		= _findNative(amx, "SetPlayerObjectMaterial");
	_setPlayerObjectMaterialText	= _findNative(amx, "SetPlayerObjectMaterialText");
	_setPlayerObjectPos				= _findNative(amx, "SetPlayerObjectPos");
	_setPlayerObjectRot				= _findNative(amx, "SetPlayerObjectRot");
	_setPlayerPos					= _findNative(amx, "SetPlayerPos");
	_setPlayerPosFindZ				= _findNative(amx, "SetPlayerPosFindZ");
	_setPlayerRaceCheckpoint		= _findNative(amx, "SetPlayerRaceCheckpoint");
	_setPlayerScore					= _findNative(amx, "SetPlayerScore");
	_setPlayerShopName				= _findNative(amx, "SetPlayerShopName");
	_setPlayerSkillLevel			= _findNative(amx, "SetPlayerSkillLevel");
	_setPlayerSkin					= _findNative(amx, "SetPlayerSkin");
	_setPlayerSpecialAction			= _findNative(amx, "SetPlayerSpecialAction");
	_setPlayerTeam					= _findNative(amx, "SetPlayerTeam");
	_setPlayerTime					= _findNative(amx, "SetPlayerTime");
	_setPlayerVelocity				= _findNative(amx, "SetPlayerVelocity");
	_setPlayerVirtualWorld			= _findNative(amx, "SetPlayerVirtualWorld");
	_setPlayerWantedLevel			= _findNative(amx, "SetPlayerWantedLevel");
	_setPlayerWeather				= _findNative(amx, "SetPlayerWeather");
	_setPlayerWorldBounds			= _findNative(amx, "SetPlayerWorldBounds");
	_setSpawnInfo					= _findNative(amx, "SetSpawnInfo");
	_setTeamCount					= _findNative(amx, "SetTeamCount");
	_setTimerEx						= _findNative(amx, "SetTimerEx");
	_setVehicleAngularVelocity		= _findNative(amx, "SetVehicleAngularVelocity");
	_setVehicleHealth				= _findNative(amx, "SetVehicleHealth");
	_setVehicleNumberPlate			= _findNative(amx, "SetVehicleNumberPlate");
	_setVehicleParamsEx				= _findNative(amx, "SetVehicleParamsEx");
	_setVehicleParamsForPlayer		= _findNative(amx, "SetVehicleParamsForPlayer");
	_setVehiclePos					= _findNative(amx, "SetVehiclePos");
	_setVehicleToRespawn			= _findNative(amx, "SetVehicleToRespawn");
	_setVehicleVelocity				= _findNative(amx, "SetVehicleVelocity");
	_setVehicleVirtualWorld			= _findNative(amx, "SetVehicleVirtualWorld");
	_setVehicleZAngle				= _findNative(amx, "SetVehicleZAngle");
	_setWeather						= _findNative(amx, "SetWeather");
	_setWorldTime					= _findNative(amx, "SetWorldTime");
	_showMenuForPlayer				= _findNative(amx, "ShowMenuForPlayer");
	_showNameTags					= _findNative(amx, "ShowNameTags");
	_showPlayerDialog				= _findNative(amx, "ShowPlayerDialog");
	_showPlayerMarkers				= _findNative(amx, "ShowPlayerMarkers");
	_showPlayerNameTagForPlayer		= _findNative(amx, "ShowPlayerNameTagForPlayer");
	_spawnPlayer					= _findNative(amx, "SpawnPlayer");
	_startRecordingPlayerData		= _findNative(amx, "StartRecordingPlayerData");
	_stopAudioStreamForPlayer		= _findNative(amx, "StopAudioStreamForPlayer");
	_stopObject						= _findNative(amx, "StopObject");
	_stopPlayerHoldingObject		= _findNative(amx, "StopPlayerHoldingObject", true);
	_stopPlayerObject				= _findNative(amx, "StopPlayerObject");
	_stopRecordingPlayerData		= _findNative(amx, "StopRecordingPlayerData");

	_textDrawAlignment				= _findNative(amx, "TextDrawAlignment");
	_textDrawBackgroundColor		= _findNative(amx, "TextDrawBackgroundColor");
	_textDrawBoxColor				= _findNative(amx, "TextDrawBoxColor");
	_textDrawColor					= _findNative(amx, "TextDrawColor");
	_textDrawCreate					= _findNative(amx, "TextDrawCreate");
	_textDrawDestroy				= _findNative(amx, "TextDrawDestroy");
	_textDrawFont					= _findNative(amx, "TextDrawFont");
	_textDrawHideForAll				= _findNative(amx, "TextDrawHideForAll");
	_textDrawHideForPlayer			= _findNative(amx, "TextDrawHideForPlayer");
	_textDrawLetterSize				= _findNative(amx, "TextDrawLetterSize");
	_textDrawSetOutline				= _findNative(amx, "TextDrawSetOutline");
	_textDrawSetPreviewModel		= _findNative(amx, "TextDrawSetPreviewModel");
	_textDrawSetPreviewRot			= _findNative(amx, "TextDrawSetPreviewRot");
	_textDrawSetPreviewVehCol		= _findNative(amx, "TextDrawSetPreviewVehCol");
	_textDrawSetProportional		= _findNative(amx, "TextDrawSetProportional");
	_textDrawSetSelectable			= _findNative(amx, "TextDrawSetSelectable");
	_textDrawSetShadow				= _findNative(amx, "TextDrawSetShadow");
	_textDrawSetString				= _findNative(amx, "TextDrawSetString");
	_textDrawShowForAll				= _findNative(amx, "TextDrawShowForAll");
	_textDrawShowForPlayer			= _findNative(amx, "TextDrawShowForPlayer");
	_textDrawTextSize				= _findNative(amx, "TextDrawTextSize");
	_textDrawUseBox					= _findNative(amx, "TextDrawUseBox");
	_togglePlayerClock				= _findNative(amx, "TogglePlayerClock");
	_togglePlayerControllable		= _findNative(amx, "TogglePlayerControllable");
	_togglePlayerSpectating			= _findNative(amx, "TogglePlayerSpectating");

	_update3DTextLabelText			= _findNative(amx, "Update3DTextLabelText");
	_updatePlayer3DTextLabelText	= _findNative(amx, "UpdatePlayer3DTextLabelText");
	_updateVehicleDamageStatus		= _findNative(amx, "UpdateVehicleDamageStatus");
	_usePlayerPedAnims				= _findNative(amx, "UsePlayerPedAnims");

	m_AMX = amx;
}

//-----------------------------------------
// AMX function reference definitions
//-----------------------------------------
amx_function_t _addMenuItem;
amx_function_t _addPlayerClass;
amx_function_t _addPlayerClassEx;
amx_function_t _addStaticPickup;
amx_function_t _addStaticVehicle;
amx_function_t _addStaticVehicleEx;
amx_function_t _addVehicleComponent;
amx_function_t _allowAdminTeleport;
amx_function_t _allowInteriorWeapons;
amx_function_t _allowPlayerTeleport;
amx_function_t _applyAnimation;
amx_function_t _attach3DTextLabelToPlayer;
amx_function_t _attach3DTextLabelToVehicle;
amx_function_t _attachCameraToObject;
amx_function_t _attachCameraToPlayerObject;
amx_function_t _attachObjectToObject;
amx_function_t _attachObjectToPlayer;
amx_function_t _attachObjectToVehicle;
amx_function_t _attachPlayerObjectToPlayer;
amx_function_t _attachPlayerObjectToVehicle;
amx_function_t _attachTrailerToVehicle;

amx_function_t _ban;
amx_function_t _banEx;

amx_function_t _callRemoteFunction;

amx_function_t _cancelEdit;
amx_function_t _cancelSelectTextDraw;
amx_function_t _changeVehicleColor;
amx_function_t _changeVehiclePaintjob;
amx_function_t _clearAnimations;
amx_function_t _connectNPC;
amx_function_t _create3DTextLabel;
amx_function_t _createExplosion;
amx_function_t _createMenu;
amx_function_t _createObject;
amx_function_t _createPickup;
amx_function_t _createPlayer3DTextLabel;
amx_function_t _createPlayerObject;
amx_function_t _createVehicle;

amx_function_t _delete3DTextLabel;
amx_function_t _deletePVar;
amx_function_t _deletePlayer3DTextLabel;
amx_function_t _destroyMenu;
amx_function_t _destroyObject;
amx_function_t _destroyPickup;
amx_function_t _destroyPlayerObject;
amx_function_t _destroyVehicle;
amx_function_t _detachTrailerFromVehicle;
amx_function_t _disableInteriorEnterExits;
amx_function_t _disableMenu;
amx_function_t _disableMenuRow;
amx_function_t _disableNameTagLOS;
amx_function_t _disablePlayerCheckpoint;
amx_function_t _disablePlayerRaceCheckpoint;

amx_function_t _editObject;
amx_function_t _editPlayerObject;
amx_function_t _editAttachedObject;
amx_function_t _enableStuntBonusForAll;
amx_function_t _enableStuntBonusForPlayer;
amx_function_t _enableVehicleFriendlyFire;
amx_function_t _forceClassSelection;

amx_function_t _gameModeExit;
amx_function_t _gameTextForAll;
amx_function_t _gameTextForPlayer;
amx_function_t _gangZoneCreate;
amx_function_t _gangZoneDestroy;
amx_function_t _gangZoneFlashForAll;
amx_function_t _gangZoneFlashForPlayer;
amx_function_t _gangZoneHideForAll;
amx_function_t _gangZoneHideForPlayer;
amx_function_t _gangZoneShowForAll;
amx_function_t _gangZoneShowForPlayer;
amx_function_t _gangZoneStopFlashForAll;
amx_function_t _gangZoneStopFlashForPlayer;
amx_function_t _getAnimationName;
amx_function_t _getMaxPlayers;
amx_function_t _getNetworkStats;
amx_function_t _getObjectPos;
amx_function_t _getObjectRot;
amx_function_t _getPVarFloat;
amx_function_t _getPVarInt;
amx_function_t _getPVarString;
amx_function_t _getPlayerAmmo;
amx_function_t _getPlayerAnimationIndex;
amx_function_t _getPlayerArmour;
amx_function_t _getPlayerCameraFrontVector;
amx_function_t _getPlayerCameraMode;
amx_function_t _getPlayerCameraPos;
amx_function_t _getPlayerColor;
amx_function_t _getPlayerDistanceFromPoint;
amx_function_t _getPlayerDrunkLevel;
amx_function_t _getPlayerFacingAngle;
amx_function_t _getPlayerFightingStyle;
amx_function_t _getPlayerHealth;
amx_function_t _getPlayerInterior;
amx_function_t _getPlayerIp;
amx_function_t _getPlayerKeys;
amx_function_t _getPlayerMenu;
amx_function_t _getPlayerMoney;
amx_function_t _getPlayerName;
amx_function_t _getPlayerNetworkStats;
amx_function_t _getPlayerObjectPos;
amx_function_t _getPlayerObjectRot;
amx_function_t _getPlayerPing;
amx_function_t _getPlayerPos;
amx_function_t _getPlayerScore;
amx_function_t _getPlayerSkin;
amx_function_t _getPlayerSpecialAction;
amx_function_t _getPlayerState;
amx_function_t _getPlayerSurfingObjectID;
amx_function_t _getPlayerSurfingVehicleID;
amx_function_t _getPlayerTargetPlayer;
amx_function_t _getPlayerTeam;
amx_function_t _getPlayerTime;
amx_function_t _getPlayerVehicleID;
amx_function_t _getPlayerVehicleSeat;
amx_function_t _getPlayerVelocity;
amx_function_t _getPlayerVersion;
amx_function_t _getPlayerVirtualWorld;
amx_function_t _getPlayerWantedLevel;
amx_function_t _getPlayerWeapon;
amx_function_t _getPlayerWeaponData;
amx_function_t _getPlayerWeaponState;
amx_function_t _getTickCount;
amx_function_t _getVehicleComponentInSlot;
amx_function_t _getVehicleComponentType;
amx_function_t _getVehicleDamageStatus;
amx_function_t _getVehicleDistanceFromPoint;
amx_function_t _getVehicleHealth;
amx_function_t _getVehicleModel;
amx_function_t _getVehicleModelInfo;
amx_function_t _getVehiclePos;
amx_function_t _getVehicleRotationQuat;
amx_function_t _getVehicleTrailer;
amx_function_t _getVehicleVelocity;
amx_function_t _getVehicleVirtualWorld;
amx_function_t _getVehicleZAngle;
amx_function_t _getWeaponName;
amx_function_t _givePlayerMoney;
amx_function_t _givePlayerWeapon;

amx_function_t _hideMenuForPlayer;

amx_function_t _interpolateCameraPos;
amx_function_t _interpolateCameraLookAt;
amx_function_t _isObjectMoving;
amx_function_t _isPlayerAdmin;
amx_function_t _isPlayerAttachedObjectSlotUsed;
amx_function_t _isPlayerConnected;
amx_function_t _isPlayerHoldingObject;
amx_function_t _isPlayerInAnyVehicle;
amx_function_t _isPlayerInCheckpoint;
amx_function_t _isPlayerInRaceCheckpoint;
amx_function_t _isPlayerInRangeOfPoint;
amx_function_t _isPlayerInVehicle;
amx_function_t _isPlayerNPC;
amx_function_t _isPlayerObjectMoving;
amx_function_t _isPlayerStreamedIn;
amx_function_t _isTrailerAttachedToVehicle;
amx_function_t _isValidMenu;
amx_function_t _isValidObject;
amx_function_t _isValidPlayerObject;
amx_function_t _isVehicleStreamedIn;

amx_function_t _kick;
amx_function_t _killTimer;

amx_function_t _limitGlobalChatRadius;
amx_function_t _limitPlayerMarkerRadius;
amx_function_t _linkVehicleToInterior;

amx_function_t _manualVehicleEngineAndLights;
amx_function_t _moveObject;
amx_function_t _movePlayerObject;

amx_function_t _playAudioStreamForPlayer;
amx_function_t _playCrimeReportForPlayer;
amx_function_t _playerPlaySound;
amx_function_t _playerSpectatePlayer;
amx_function_t _playerSpectateVehicle;
amx_function_t _putPlayerInVehicle;

amx_function_t _createPlayerTextDraw;
amx_function_t _playerTextDrawDestroy;
amx_function_t _playerTextDrawLetterSize;
amx_function_t _playerTextDrawTextSize;
amx_function_t _playerTextDrawAlignment;
amx_function_t _playerTextDrawColor;
amx_function_t _playerTextDrawUseBox;
amx_function_t _playerTextDrawBoxColor;
amx_function_t _playerTextDrawSetShadow;
amx_function_t _playerTextDrawSetOutline;
amx_function_t _playerTextDrawBackgroundColor;
amx_function_t _playerTextDrawFont;
amx_function_t _playerTextDrawSetPreviewModel;
amx_function_t _playerTextDrawSetPreviewRot;
amx_function_t _playerTextDrawSetPreviewVehCol;
amx_function_t _playerTextDrawSetProportional;
amx_function_t _playerTextDrawSetSelectable;
amx_function_t _playerTextDrawShow;
amx_function_t _playerTextDrawHide;
amx_function_t _playerTextDrawSetString;

amx_function_t _removeBuildingForPlayer;
amx_function_t _removePlayerAttachedObject;
amx_function_t _removePlayerFromVehicle;
amx_function_t _removePlayerMapIcon;
amx_function_t _removeVehicleComponent;
amx_function_t _repairVehicle;
amx_function_t _resetPlayerMoney;
amx_function_t _resetPlayerWeapons;

amx_function_t _selectObject;
amx_function_t _selectTextDraw;
amx_function_t _sendClientMessage;
amx_function_t _sendClientMessageToAll;
amx_function_t _sendDeathMessage;
amx_function_t _sendPlayerMessageToAll;
amx_function_t _sendPlayerMessageToPlayer;
amx_function_t _sendRconCommand;
amx_function_t _setCameraBehindPlayer;
amx_function_t _setGameModeText;
amx_function_t _setGravity;
amx_function_t _setMenuColumnHeader;
amx_function_t _setNameTagDrawDistance;
amx_function_t _setObjectMaterial;
amx_function_t _setObjectMaterialText;
amx_function_t _setObjectPos;
amx_function_t _setObjectRot;
amx_function_t _setPVarFloat;
amx_function_t _setPVarInt;
amx_function_t _setPVarString;
amx_function_t _setPlayerAmmo;
amx_function_t _setPlayerArmedWeapon;
amx_function_t _setPlayerArmour;
amx_function_t _setPlayerAttachedObject;
amx_function_t _setPlayerCameraLookAt;
amx_function_t _setPlayerCameraPos;
amx_function_t _setPlayerChatBubble;
amx_function_t _setPlayerCheckpoint;
amx_function_t _setPlayerColor;
amx_function_t _setPlayerDrunkLevel;
amx_function_t _setPlayerFacingAngle;
amx_function_t _setPlayerFightingStyle;
amx_function_t _setPlayerHealth;
amx_function_t _setPlayerHoldingObject;
amx_function_t _setPlayerInterior;
amx_function_t _setPlayerMapIcon;
amx_function_t _setPlayerMarkerForPlayer;
amx_function_t _setPlayerName;
amx_function_t _setPlayerObjectMaterial;
amx_function_t _setPlayerObjectMaterialText;
amx_function_t _setPlayerObjectPos;
amx_function_t _setPlayerObjectRot;
amx_function_t _setPlayerPos;
amx_function_t _setPlayerPosFindZ;
amx_function_t _setPlayerRaceCheckpoint;
amx_function_t _setPlayerScore;
amx_function_t _setPlayerShopName;
amx_function_t _setPlayerSkillLevel;
amx_function_t _setPlayerSkin;
amx_function_t _setPlayerSpecialAction;
amx_function_t _setPlayerTeam;
amx_function_t _setPlayerTime;
amx_function_t _setPlayerVelocity;
amx_function_t _setPlayerVirtualWorld;
amx_function_t _setPlayerWantedLevel;
amx_function_t _setPlayerWeather;
amx_function_t _setPlayerWorldBounds;
amx_function_t _setSpawnInfo;
amx_function_t _setTeamCount;
amx_function_t _setTimerEx;
amx_function_t _setVehicleAngularVelocity;
amx_function_t _setVehicleHealth;
amx_function_t _setVehicleNumberPlate;
amx_function_t _setVehicleParamsEx;
amx_function_t _setVehicleParamsForPlayer;
amx_function_t _setVehiclePos;
amx_function_t _setVehicleToRespawn;
amx_function_t _setVehicleVelocity;
amx_function_t _setVehicleVirtualWorld;
amx_function_t _setVehicleZAngle;
amx_function_t _setWeather;
amx_function_t _setWorldTime;
amx_function_t _showMenuForPlayer;
amx_function_t _showNameTags;
amx_function_t _showPlayerDialog;
amx_function_t _showPlayerMarkers;
amx_function_t _showPlayerNameTagForPlayer;
amx_function_t _spawnPlayer;
amx_function_t _startRecordingPlayerData;
amx_function_t _stopAudioStreamForPlayer;
amx_function_t _stopObject;
amx_function_t _stopPlayerHoldingObject;
amx_function_t _stopPlayerObject;
amx_function_t _stopRecordingPlayerData;

amx_function_t _textDrawAlignment;
amx_function_t _textDrawBackgroundColor;
amx_function_t _textDrawBoxColor;
amx_function_t _textDrawColor;
amx_function_t _textDrawCreate;
amx_function_t _textDrawDestroy;
amx_function_t _textDrawFont;
amx_function_t _textDrawHideForAll;
amx_function_t _textDrawHideForPlayer;
amx_function_t _textDrawLetterSize;
amx_function_t _textDrawSetOutline;
amx_function_t _textDrawSetPreviewModel;
amx_function_t _textDrawSetPreviewRot;
amx_function_t _textDrawSetPreviewVehCol;
amx_function_t _textDrawSetProportional;
amx_function_t _textDrawSetSelectable;
amx_function_t _textDrawSetShadow;
amx_function_t _textDrawSetString;
amx_function_t _textDrawShowForAll;
amx_function_t _textDrawShowForPlayer;
amx_function_t _textDrawTextSize;
amx_function_t _textDrawUseBox;
amx_function_t _togglePlayerClock;
amx_function_t _togglePlayerControllable;
amx_function_t _togglePlayerSpectating;

amx_function_t _update3DTextLabelText;
amx_function_t _updatePlayer3DTextLabelText;
amx_function_t _updateVehicleDamageStatus;
amx_function_t _usePlayerPedAnims;

//-----------------------------------------
// function definitions
//-----------------------------------------

PyObject *sPrintf(PyObject *self, PyObject *args)
{
	char *str = NULL;
	PyArg_ParseTuple(args, "s", &str);

	if(PyErr_Occurred() != NULL)
		return NULL;

	logprintf(str);

	//PyMem_Free(str);
	Py_RETURN_NONE;
}

// AddMenuItem(Menu:menuid, column, title[]) -- TODO: test
PyObject *sAddMenuItem(PyObject *self, PyObject *args)
{
	int mid, col;
	char *title;
	PyArg_ParseTuple(args, "iiO&", &mid, &col, _stringToCP1252, &title);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), mid, col, 0 };

	int len = strlen(title) + 1;
	cell *strtitle;
	amx_Allot(m_AMX, len, amxargs + 3, &strtitle);
	amx_SetString(strtitle, title, 0, 0, len);

	_addMenuItem(m_AMX, amxargs);

	free(title);
	amx_Release(m_AMX, amxargs[3]);
	Py_RETURN_NONE;
}
// int AddPlayerClass(skin, Float:x, Float:y, Float:z, Float:Angle, weapon1, weapon1_ammo, weapon2, weapon2_ammo, weapon3, weapon3_ammo)
PyObject *sAddPlayerClass(PyObject *self, PyObject *args)
{
	int skin, w1, w1a, w2, w2a, w3, w3a;
	float x, y, z, angle;
	PyArg_ParseTuple(args, "iffffiiiiii", &skin, &x, &y, &z, &angle, &w1, &w1a, &w2, &w2a, &w3, &w3a);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[12] = { 11 * sizeof(cell), skin, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), amx_ftoc(angle), w1, w1a, w2, w2a, w3, w3a };
	return Py_BuildValue("i", _addPlayerClass(m_AMX, amxargs));
}
// int AddPlayerClassEx(teamid, skin, Float:x, Float:y, Float:z, Float:Angle, weapon1, weapon1_ammo, weapon2, weapon2_ammo, weapon3, weapon3_ammo) -- TODO: test
PyObject *sAddPlayerClassEx(PyObject *self, PyObject *args)
{
	int tid, skin, w1, w1a, w2, w2a, w3, w3a;
	float x, y, z, angle;
	PyArg_ParseTuple(args, "iiffffiiiiii", &tid, &skin, &x, &y, &z, &angle, &w1, &w1a, &w2, &w2a, &w3, &w3a);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[13] = { 12 * sizeof(cell), tid, skin, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), amx_ftoc(angle), w1, w1a, w2, w2a, w3, w3a };
	return Py_BuildValue("i", _addPlayerClassEx(m_AMX, amxargs));
}
// int AddStaticPickup(model, type, Float:X, Float:Y, Float:Z, Virtualworld = 0)
PyObject *sAddStaticPickup(PyObject *self, PyObject *args)
{
	int model, type, virtworld = 0;
	float x, y, z;
	PyArg_ParseTuple(args, "iifff|i", &model, &type, &x, &y, &z, &virtworld);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[7] = { 6 * sizeof(cell), model, type, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), virtworld };
	return Py_BuildValue("i", _addStaticPickup(m_AMX, amxargs));
}
// int AddStaticVehicle(modelid, Float:spawn_x, Float:spawn_y, Float:spawn_z, Float:angle, color1, color2) -- TODO: test
PyObject *sAddStaticVehicle(PyObject *self, PyObject *args)
{
	int modelid, color1, color2;
	float x, y, z, angle;

	PyArg_ParseTuple(args, "iffffii", &modelid, &x, &y, &z, &angle, &color1, &color2);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[8] = { 7 * sizeof(cell), modelid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), amx_ftoc(angle), color1, color2 };
	return Py_BuildValue("i", _addStaticVehicle(m_AMX, amxargs));
}
// int AddStaticVehicleEx(modelid, Float:spawn_x, Float:spawn_y, Float:spawn_z, Float:angle, color1, color2, respawn_delay) -- TODO: test
PyObject *sAddStaticVehicleEx(PyObject *self, PyObject *args)
{
	int modelid, color1, color2, resp_del;
	float x, y, z, angle;

	PyArg_ParseTuple(args, "iffffiii", &modelid, &x, &y, &z, &angle, &color1, &color2, &resp_del);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[9] = { 8 * sizeof(cell), modelid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), amx_ftoc(angle), color1, color2, resp_del };
	return Py_BuildValue("i", _addStaticVehicleEx(m_AMX, amxargs));
}
// AddVehicleComponent(vehicleid, componentid)
PyObject *sAddVehicleComponent(PyObject *self, PyObject *args)
{
	int vehicleid, componentid;
	PyArg_ParseTuple(args, "ii", &vehicleid, &componentid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), vehicleid, componentid };
	_addVehicleComponent(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// AllowAdminTeleport(allow) -- TOOD: test
PyObject *sAllowAdminTeleport(PyObject *self, PyObject *args)
{
	int allow;
	PyArg_ParseTuple(args, "i", &allow);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), allow };
	_allowAdminTeleport(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// AllowInteriorWeapons(allow) -- TODO: test this function after GivePlayerWeapon is implemented
PyObject *sAllowInteriorWeapons(PyObject *self, PyObject *args)
{
	int allow;
	PyArg_ParseTuple(args, "i", &allow);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), allow };
	_allowInteriorWeapons(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// AllowPlayerTeleport(playerid, allow) -- TODO: test
PyObject *sAllowPlayerTeleport(PyObject *self, PyObject *args)
{
	int pid, allow;
	PyArg_ParseTuple(args, "ii", &pid, &allow);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, allow};
	_allowPlayerTeleport(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// ApplyAnimation(playerid, animlib[], animname[], Float:fDelta, loop, lockx, locky, freeze, time, forcesync = 0)
PyObject *sApplyAnimation(PyObject *self, PyObject *args)
{
	int playerid, loop, lockx, locky, freeze, time, forcesync = 0;
	char *animlib, *animname;
	float fdelta;
	// No conversion, should always be ASCII.
	PyArg_ParseTuple(args, "issfiiiii|i", &playerid, &animlib, &animname, &fdelta, &loop, &lockx, &locky, &freeze, &time, &forcesync);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[11] = { 10 * sizeof(cell), playerid, 0, 0, amx_ftoc(fdelta), loop, lockx, locky, freeze, time, forcesync };

	int len = strlen(animlib) + 1;
	cell *stranimlib, *stranimname;
	amx_Allot(m_AMX, len, amxargs + 2, &stranimlib);
	amx_SetString(stranimlib, animlib, 0, 0, len);

	len = strlen(animname) + 1;
	amx_Allot(m_AMX, len, amxargs + 3, &stranimname);
	amx_SetString(stranimname, animname, 0, 0, len);

	_applyAnimation(m_AMX, amxargs);

	//PyMem_Free(animlib); PyMem_Free(animname);
	amx_Release(m_AMX, amxargs[2]); amx_Release(m_AMX, amxargs[3]);

	Py_RETURN_NONE;
}
// Attach3DTextLabelToPlayer(Text3D:id, playerid, Float:OffsetX, Float:OffsetY, Float:OffsetZ) -- TODO: test
PyObject *sAttach3DTextLabelToPlayer(PyObject *self, PyObject *args)
{
	int t3id, pid;
	float ox, oy, oz;

	PyArg_ParseTuple(args, "iifff", &t3id, &pid, &ox, &oy, &oz);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[6] = { 5 * sizeof(cell), t3id, pid, amx_ftoc(ox), amx_ftoc(oy), amx_ftoc(oz) };
	_attach3DTextLabelToPlayer(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// Attach3DTextLabelToVehicle(Text3D:id, vehicleid, Float:OffsetX, Float:OffsetY, Float:OffsetZ) -- TODO: test
PyObject *sAttach3DTextLabelToVehicle(PyObject *self, PyObject *args)
{
	int t3id, vid;
	float ox, oy, oz;

	PyArg_ParseTuple(args, "iifff", &t3id, &vid, &ox, &oy, &oz);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[6] = { 5 * sizeof(cell), t3id, vid, amx_ftoc(ox), amx_ftoc(oy), amx_ftoc(oz) };
	_attach3DTextLabelToVehicle(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// AttachCameraToObject(playerid, objectid) -- TODO: test
PyObject *sAttachCameraToObject(PyObject *self, PyObject *args)
{
	int pid, oid;
	PyArg_ParseTuple(args, "ii", &pid, &oid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, oid};
	_attachCameraToObject(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// AttachCameraToPlayerObject(playerid, playerobjectid) -- TODO: test
PyObject *sAttachCameraToPlayerObject(PyObject *self, PyObject *args)
{
	int pid, poid;
	PyArg_ParseTuple(args, "ii", &pid, &poid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, poid};
	_attachCameraToPlayerObject(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// AttachObjectToObject(objectid, attachtoid, Float:OffsetX, Float:OffsetY, Float:OffsetZ, Float:RotX, Float:RotY, Float:RotZ, SyncRotation = 1) -- TODO: test
PyObject *sAttachObjectToObject(PyObject *self, PyObject *args)
{
	int oid, pid, sync = 1;
	float ox, oy, oz, rx, ry, rz;

	PyArg_ParseTuple(args, "iiffffff|i", &oid, &pid, &ox, &oy, &oz, &rx, &ry, &rz, &sync);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[10] = { 9 * sizeof(cell), oid, pid, amx_ftoc(ox), amx_ftoc(oy), amx_ftoc(oz), amx_ftoc(rx), amx_ftoc(ry), amx_ftoc(rz), sync };
	_attachObjectToObject(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// AttachObjectToPlayer(objectid, playerid, Float:OffsetX, Float:OffsetY, Float:OffsetZ, Float:rX, Float:rY, Float:rZ) -- TODO: test
PyObject *sAttachObjectToPlayer(PyObject *self, PyObject *args)
{
	int oid, pid;
	float ox, oy, oz, rx, ry, rz;

	PyArg_ParseTuple(args, "iiffffff", &oid, &pid, &ox, &oy, &oz, &rx, &ry, &rz);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[9] = { 8 * sizeof(cell), oid, pid, amx_ftoc(ox), amx_ftoc(oy), amx_ftoc(oz), amx_ftoc(rx), amx_ftoc(ry), amx_ftoc(rz) };
	_attachObjectToPlayer(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// AttachObjectToVehicle(objectid, vehicleid, Float:fOffsetX, Float:fOffsetY, Float:fOffsetZ, Float:fRotX, Float:fRotY, Float:RotZ) -- TODO: test / 0.3c
PyObject *sAttachObjectToVehicle(PyObject *self, PyObject *args)
{
	int oid, vid;
	float ox, oy, oz, rx, ry, rz;

	PyArg_ParseTuple(args, "iiffffff", &oid, &vid, &ox, &oy, &oz, &rx, &ry, &rz);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[9] = { 8 * sizeof(cell), oid, vid, amx_ftoc(ox), amx_ftoc(oy), amx_ftoc(oz), amx_ftoc(rx), amx_ftoc(ry), amx_ftoc(rz) };
	_attachObjectToVehicle(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// AttachPlayerObjectToPlayer(objectplayer, objectid, attachplayer, Float:OffsetX, Float:OffsetY, Float:OffsetZ, Float:rX, Float:rY, Float:rZ) -- TODO: test
PyObject *sAttachPlayerObjectToPlayer(PyObject *self, PyObject *args)
{
	int op, oid, ap;
	float ox, oy, oz, rx, ry, rz;

	PyArg_ParseTuple(args, "iiiffffff", &op, &oid, &ap, &ox, &oy, &oz, &rx, &ry, &rz);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[10] = { 9 * sizeof(cell), op, oid, ap, amx_ftoc(ox), amx_ftoc(oy), amx_ftoc(oz), amx_ftoc(rx), amx_ftoc(ry), amx_ftoc(rz) };
	_attachPlayerObjectToPlayer(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// AttachPlayerObjectToVehicle(playerid, objectid, vehicleid, Float:fOffsetX, Float:fOffsetY, Float:fOffsetZ, Float:fRotX, Float:fRotY, Float:RotZ) -- TODO: test
PyObject *sAttachPlayerObjectToVehicle(PyObject *self, PyObject *args)
{
	int pid, oid, ap;
	float ox, oy, oz, rx, ry, rz;

	PyArg_ParseTuple(args, "iiiffffff", &pid, &oid, &ap, &ox, &oy, &oz, &rx, &ry, &rz);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[10] = { 9 * sizeof(cell), pid, oid, ap, amx_ftoc(ox), amx_ftoc(oy), amx_ftoc(oz), amx_ftoc(rx), amx_ftoc(ry), amx_ftoc(rz) };
	_attachPlayerObjectToVehicle(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// AttachTrailerToVehicle(trailerid, vehicleid) -- TODO: test
PyObject *sAttachTrailerToVehicle(PyObject *self, PyObject *args)
{
	int tid, vid;
	PyArg_ParseTuple(args, "ii", &tid, &vid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), tid, vid };
	_attachTrailerToVehicle(m_AMX, amxargs);
	Py_RETURN_NONE;
}

// Ban(playerid)
PyObject *sBan(PyObject *self, PyObject *args)
{
	int playerid;
	PyArg_ParseTuple(args, "i", &playerid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), playerid };
	_ban(m_AMX, amxargs);

	Py_RETURN_NONE;
}
// BanEx(playerid, reason[])
PyObject *sBanEx(PyObject *self, PyObject *args)
{
	int playerid;
	char *reason;
	PyArg_ParseTuple(args, "iO&", &playerid, _stringToCP1252, &reason);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), playerid, 0 };

	int len = strlen(reason) + 1;
	cell *strreason;
	amx_Allot(m_AMX, len, amxargs + 2, &strreason);
	amx_SetString(strreason, reason, 0, 0, len);

	_banEx(m_AMX, amxargs);

	free(reason);
	amx_Release(m_AMX, amxargs[2]);

	Py_RETURN_NONE;
}

// CallLocalFunction -- no

// CallRemoteFunction(function[], format[], {Float,_}:...)
PyObject *sCallRemoteFunction(PyObject *self, PyObject *args)
{
	const char *function = NULL,
		*format = NULL;
	Py_ssize_t function_len = 0,
		format_len = 0,
		function_args_count = 0;

	function = PyUnicode_AsUTF8AndSize(
		PyTuple_GetItem(args, 0),
		&function_len
	);
	if(function == NULL)
		return NULL;
	function_len += 1;

	format = PyUnicode_AsUTF8AndSize(
		PyTuple_GetItem(args, 1),
		&format_len
	);
	if(format == NULL)
		return NULL;
	format_len += 1;

	cell *amxargs = NULL;
	// *3: amx args length, function, format
	size_t amx_args_size = 3 * sizeof(cell);

	// -2 because we already got function and format
	function_args_count = PyTuple_Size(args) - 2;
	amx_args_size += function_args_count * sizeof(cell);

	cell *pawn_address = NULL;

	amxargs = (cell *)malloc(amx_args_size);
	memset(amxargs, 0, amx_args_size);
	// -sizeof(cell) - Does not include the first cell itself
	amxargs[0] = amx_args_size - sizeof(cell);

	amx_Allot(m_AMX, function_len, &(amxargs[1]), &pawn_address);
	amx_SetString(pawn_address, function, 0, 0, function_len);

	amx_Allot(m_AMX, format_len, &(amxargs[2]), &pawn_address);
	amx_SetString(pawn_address, format, 0, 0, format_len);

	cell release = _pyArgsToAMX(amxargs, args, 2);
	cell ret = _callRemoteFunction(m_AMX, amxargs);

	if(release)
		amx_Release(m_AMX, release);
	free(amxargs);

	return Py_BuildValue("i", ret);
}

PyObject *sCallNativeFunction(PyObject *self, PyObject *args)
{
	const char *function = NULL;

	function = PyUnicode_AsUTF8AndSize(PyTuple_GetItem(args, 0), NULL);

	if(function == NULL)
		return NULL;

	amx_function_t amx_function = _findNative(m_AMX, function, true);

	if(amx_function == NULL)
	{
		PyErr_Format(
			PyExc_NameError,
			"PYTHON: Unknown native function %s",
			function
		);
		return NULL;
	}

	cell *amxargs = NULL;
	size_t amx_args_size = sizeof(cell);

	// -1 because we already got function
	Py_ssize_t function_args_count = _getRecursiveSize(args) - 1;
	amx_args_size += function_args_count * sizeof(cell);

	amxargs = (cell *)malloc(amx_args_size);
	memset(amxargs, 0, amx_args_size);
	// -sizeof(cell) - Does not include the first cell itself
	amxargs[0] = amx_args_size - sizeof(cell);

	// -1 because we don't put function in amxargs
	cell release = _pyArgsToAMX(amxargs - 1, args, 1, true);

	// Error in argument conversion - this should be checked everywhere
	if(PyErr_Occurred() != NULL)
		return NULL;

	cell ret = amx_function(m_AMX, amxargs);

	if(release)
		amx_Release(m_AMX, release);

	free(amxargs);

	return Py_BuildValue("i", ret);
}

// CancelEdit(playerid) -- TODO: test
PyObject *sCancelEdit(PyObject *self, PyObject *args)
{
	int playerid;
	PyArg_ParseTuple(args, "i", &playerid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), playerid };
	_cancelEdit(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// CancelSelectTextDraw(playerid) -- TODO: test
PyObject *sCancelSelectTextDraw(PyObject *self, PyObject *args)
{
	int playerid;
	PyArg_ParseTuple(args, "i", &playerid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), playerid };
	_cancelSelectTextDraw(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// ChangeVehicleColor(vehicleid, color1, color2)
PyObject *sChangeVehicleColor(PyObject *self, PyObject *args)
{
	int vehid, color1, color2;
	PyArg_ParseTuple(args, "iii", &vehid, &color1, &color2);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), vehid, color1, color2 };
	_changeVehicleColor(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// ChangeVehiclePaintjob(vehicleid, paintjobid)
PyObject *sChangeVehiclePaintjob(PyObject *self, PyObject *args)
{
	int vehid, pid;
	PyArg_ParseTuple(args, "ii", &vehid, &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), vehid, pid };
	_changeVehiclePaintjob(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// ClearAnimations(playerid)
PyObject *sClearAnimations(PyObject *self, PyObject *args)
{
	int playerid;
	PyArg_ParseTuple(args, "i", &playerid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), playerid };
	_clearAnimations(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// ConnectNPC(name[], script[])
PyObject *sConnectNPC(PyObject *self, PyObject *args)
{
	char *name, *script;
	// No conversion, should always be ASCII.
	PyArg_ParseTuple(args, "ss", &name, &script);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), 0, 0 };

	int len = strlen(name) + 1;
	cell *strname, *strscript;
	
	amx_Allot(m_AMX, len, amxargs + 1, &strname);
	amx_SetString(strname, name, 0, 0, len);

	len = strlen(script) + 1;
	amx_Allot(m_AMX, len, amxargs + 2, &strscript);
	amx_SetString(strscript, script, 0, 0, len);

	_connectNPC(m_AMX, amxargs);

	//PyMem_Free(name); PyMem_Free(script);
	amx_Release(m_AMX, amxargs[1]); amx_Release(m_AMX, amxargs[2]);
	Py_RETURN_NONE;
}
// int Create3DTextLabel(text[], color, Float:X, Float:Y, Float:Z, Float:DrawDistance, virtualworld, testLOS)
PyObject *sCreate3DTextLabel(PyObject *self, PyObject *args)
{
	int virtworld, tlos;
	float x, y, z, drawdist;
	char *text;
	PyObject *color;
	PyArg_ParseTuple(args, "O&Offffib", _stringToCP1252, &text, &color, &x, &y, &z, &drawdist, &virtworld, &tlos);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);

	cell amxargs[9] = { 8 * sizeof(cell), 0, colcode, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), amx_ftoc(drawdist), virtworld, tlos };

	int len = strlen(text) + 1;
	cell *strtext;
	amx_Allot(m_AMX, len, amxargs + 1, &strtext);
	amx_SetString(strtext, text, 0, 0, len);

	cell ret = _create3DTextLabel(m_AMX, amxargs);

	free(text);
	amx_Release(m_AMX, amxargs[1]);

	return Py_BuildValue("i", ret);
}
// CreateExplosion(Float:X, Float:Y, Float:Z, type, Float:radius)
PyObject *sCreateExplosion(PyObject *self, PyObject *args)
{
	int type;
	float x, y, z, radius;
	PyArg_ParseTuple(args, "fffif", &x, &y, &z, &type, &radius);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[6] = { 5 * sizeof(cell), amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), type, amx_ftoc(radius) };
	_createExplosion(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// int CreateMenu(title[], columns, Float:x, Float:y, Float:col1width, Float:col2width) -- TODO. test
PyObject *sCreateMenu(PyObject *self, PyObject *args)
{
	int cols;
	float x, y, c1, c2;
	char *title;
	PyArg_ParseTuple(args, "O&iffff", _stringToCP1252, &title, &cols, &x, &y, &c1, &c2);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[7] = { 6 * sizeof(cell), 0, cols, amx_ftoc(x), amx_ftoc(y), amx_ftoc(c1), amx_ftoc(c2) };

	int len = strlen(title) + 1;
	cell *strtitle;
	amx_Allot(m_AMX, len, amxargs + 1, &strtitle);
	amx_SetString(strtitle, title, 0, 0, len);

	cell ret = _createMenu(m_AMX, amxargs);

	free(title);
	amx_Release(m_AMX, amxargs[1]);

	return Py_BuildValue("i", ret);
}
// int CreateObject(modelid, Float:X, Float:Y, Float:Z, Float:rX, Float:rY, Float:rZ, Float:DrawDistance=0.0)
PyObject *sCreateObject(PyObject *self, PyObject *args)
{
	int modelid;
	float x, y, z, rx, ry, rz, drawdist = 0.0;
	PyArg_ParseTuple(args, "iffffff|f", &modelid, &x, &y, &z, &rx, &ry, &rz, &drawdist);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[9] = { 8 * sizeof(cell), modelid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), amx_ftoc(rx), amx_ftoc(ry), amx_ftoc(rz), amx_ftoc(drawdist) };
	return Py_BuildValue("i", _createObject(m_AMX, amxargs));
}
// int CreatePickup(model, type, Float:X, Float:Y, Float:Z, Virtualworld=0) -- TODO: test
PyObject *sCreatePickup(PyObject *self, PyObject *args)
{
	int model, type, virtworld = 0;
	float x, y, z;
	PyArg_ParseTuple(args, "iifff|i", &model, &type, &x, &y, &z, &virtworld);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[7] = { 6 * sizeof(cell), model, type, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), virtworld };
	return Py_BuildValue("i", _createPickup(m_AMX, amxargs));
}
// int CreatePlayer3DTextLabel(playerid, text[], color, Float:X, Float:Y, Float:Z, Float:DrawDistance, attachedplayer, attachedvehicle, testLOS) -- TODO: test
PyObject *sCreatePlayer3DTextLabel(PyObject *self, PyObject *args)
{
	int pid, ap, av, tlos;
	float x, y, z, drawdist;
	char *text;
	PyObject *color;
	PyArg_ParseTuple(args, "iO&Offffiib", &pid, _stringToCP1252, &text, &color, &x, &y, &z, &drawdist, &ap, &av, &tlos);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);

	cell amxargs[11] = { 10 * sizeof(cell), pid, 0, colcode, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), amx_ftoc(drawdist), ap, av, tlos };

	int len = strlen(text) + 1;
	cell *strtext;
	amx_Allot(m_AMX, len, amxargs + 2, &strtext);
	amx_SetString(strtext, text, 0, 0, len);

	cell ret = _createPlayer3DTextLabel(m_AMX, amxargs);

	free(text);
	amx_Release(m_AMX, amxargs[2]);

	return Py_BuildValue("i", ret);
}
// int CreatePlayerObject(playerid, modelid, Float:X, Float:Y, Float:Z, Float:rX, Float:rY, Float:rZ, Float:DrawDistance=0.0) -- TODO: test
PyObject *sCreatePlayerObject(PyObject *self, PyObject *args)
{
	int playerid, modelid;
	float x, y, z, rx, ry, rz, drawdist = 0.0;
	PyArg_ParseTuple(args, "iiffffff|f", &playerid, &modelid, &x, &y, &z, &rx, &ry, &rz, &drawdist);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[10] = { 9 * sizeof(cell), playerid, modelid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), amx_ftoc(rx), amx_ftoc(ry), amx_ftoc(rz), amx_ftoc(drawdist) };
	return Py_BuildValue("i", _createPlayerObject(m_AMX, amxargs));
}
// int CreateVehicle(modelid, Float:x, Float:y, Float:z, Float:angle, color1, color2, respawn_delay)
PyObject *sCreateVehicle(PyObject *self, PyObject *args)
{
	int modelid, color1, color2, resp_del;
	float x, y, z, angle;

	PyArg_ParseTuple(args, "iffffiii", &modelid, &x, &y, &z, &angle, &color1, &color2, &resp_del);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[9] = { 8 * sizeof(cell), modelid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), amx_ftoc(angle), color1, color2, resp_del };
	return Py_BuildValue("i", _createVehicle(m_AMX, amxargs));
}

// DB functions -- we do not need them
// int Delete3DTextLabel(Text3D:id) -- TODO: test
PyObject *sDelete3DTextLabel(PyObject *self, PyObject *args)
{
	int id;
	PyArg_ParseTuple(args, "i", &id);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), id };
	return Py_BuildValue("i", _delete3DTextLabel(m_AMX, amxargs));
}
// int DeletePVar(playerid, varname[])
PyObject *sDeletePVar(PyObject *self, PyObject *args)
{
	int playerid;
	char *string;

	// No conversion, should always be ASCII.
	PyArg_ParseTuple(args, "is", &playerid, &string);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), playerid, 0 };

	int len = strlen(string) + 1;
	cell *strstring;
	amx_Allot(m_AMX, len, amxargs + 2, &strstring);
	amx_SetString(strstring, string, 0, 0, len);

	cell ret = _deletePVar(m_AMX, amxargs);

	amx_Release(m_AMX, amxargs[2]);

	return Py_BuildValue("i", ret);
}
// int DeletePlayer3DTextLabel(playerid, PlayerText3D:id) -- TODO: test
PyObject *sDeletePlayer3DTextLabel(PyObject *self, PyObject *args)
{
	int pid, id;
	PyArg_ParseTuple(args, "ii", &pid, &id);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, id };
	return Py_BuildValue("i", _deletePlayer3DTextLabel(m_AMX, amxargs));
}
// Deleteproperty -- no
// int DestroyMenu(Menu:menuid) -- TODO: test
PyObject *sDestroyMenu(PyObject *self, PyObject *args)
{
	int mid;
	PyArg_ParseTuple(args, "i", &mid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), mid };
	return Py_BuildValue("i", _destroyMenu(m_AMX, amxargs));
}
// DestroyObject(objectid) -- TODO: test
PyObject *sDestroyObject(PyObject *self, PyObject *args)
{
	int oid;
	PyArg_ParseTuple(args, "i", &oid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), oid };
	_destroyObject(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// DestroyPickup(pickupid) -- TODO: test
PyObject *sDestroyPickup(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	_destroyPickup(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// DestroyPlayerObject(playerid, objectid) -- TODO: test
PyObject *sDestroyPlayerObject(PyObject *self, PyObject *args)
{
	int pid, oid;
	PyArg_ParseTuple(args, "ii", &pid, &oid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, oid };
	_destroyPlayerObject(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// DestroyVehicle(vehicleid) -- TODO: test
PyObject *sDestroyVehicle(PyObject *self, PyObject *args)
{
	int vid;
	PyArg_ParseTuple(args, "i", &vid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), vid };
	_destroyVehicle(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// DetachTrailerFromVehicle(vehicleid) -- TODO: test
PyObject *sDetachTrailerFromVehicle(PyObject *self, PyObject *args)
{
	int vid;
	PyArg_ParseTuple(args, "i", &vid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), vid };
	_detachTrailerFromVehicle(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// DisableInteriorEnterExits() -- TODO: test
PyObject *sDisableInteriorEnterExits(PyObject *self, PyObject *args)
{
	cell amxargs[1] = { 0 };
	_disableInteriorEnterExits(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// DisableMenu(Menu:menuid) -- TODO: test
PyObject *sDisableMenu(PyObject *self, PyObject *args)
{
	int mid;
	PyArg_ParseTuple(args, "i", &mid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), mid };
	_disableMenu(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// DisableMenuRow(Menu:menuid, row) -- TODO: test
PyObject *sDisableMenuRow(PyObject *self, PyObject *args)
{
	int mid, row;
	PyArg_ParseTuple(args, "ii", &mid, &row);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), mid, row };
	_disableMenuRow(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// DisableNameTagLOS() -- TODO: test
PyObject *sDisableNameTagLOS(PyObject *self, PyObject *args)
{
	cell amxargs[1] = { 0 };
	_disableNameTagLOS(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// DisablePlayerCheckpoint(playerid) -- TODO: test
PyObject *sDisablePlayerCheckpoint(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	_disablePlayerCheckpoint(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// DisablePlayerRaceCheckpoint(playerid) -- TODO: test
PyObject *sDisablePlayerRaceCheckpoint(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	_disablePlayerRaceCheckpoint(m_AMX, amxargs);
	Py_RETURN_NONE;
}

// EditObject(playerid, objectid) -- TODO: test
PyObject *sEditObject(PyObject *self, PyObject *args)
{
	int playerid, objectid;
	PyArg_ParseTuple(args, "ii", &playerid, &objectid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), playerid, objectid };
	_editObject(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// EditPlayerObject(playerid, objectid) -- TODO: test
PyObject *sEditPlayerObject(PyObject *self, PyObject *args)
{
	int playerid, objectid;
	PyArg_ParseTuple(args, "ii", &playerid, &objectid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), playerid, objectid };
	_editPlayerObject(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// EditAttachedObject(playerid, index) -- TODO: test
PyObject *sEditAttachedObject(PyObject *self, PyObject *args)
{
	int playerid, idx;
	PyArg_ParseTuple(args, "ii", &playerid, &idx);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), playerid, idx };
	_editAttachedObject(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// EnableStuntBonusForAll(enable) -- TODO: test
PyObject *sEnableStuntBonusForAll(PyObject *self, PyObject *args)
{
	int enable;
	PyArg_ParseTuple(args, "i", &enable);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), enable };
	_enableStuntBonusForAll(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// EnableStuntBonusForPlayer(playerid, enable) -- TODO: test
PyObject *sEnableStuntBonusForPlayer(PyObject *self, PyObject *args)
{
	int playerid, enable;
	PyArg_ParseTuple(args, "ii", &playerid, &enable);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), playerid, enable };
	_enableStuntBonusForPlayer(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// EnableVehicleFriendlyFire() -- TODO: test
PyObject *sEnableVehicleFriendlyFire(PyObject *self, PyObject *args)
{
	cell amxargs[1] = { 0 };
	_enableVehicleFriendlyFire(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// EnableZoneNames -- removed
// Existproperty -- no

// float, file and string functions are not needed
// ForceClassSelection(playerid) -- TODO: test
PyObject *sForceClassSelection(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	_forceClassSelection(m_AMX, amxargs);
	Py_RETURN_NONE;
}

// GameModeExit() -- TODO: test
PyObject *sGameModeExit(PyObject *self, PyObject *args)
{
	cell amxargs[1] = { 0 };
	_gameModeExit(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// GameTextForAll(string[], time, style) -- TODO: test
PyObject *sGameTextForAll(PyObject *self, PyObject *args)
{
	int time, style;
	char *string;
	PyArg_ParseTuple(args, "O&ii", _stringToCP1252, &string, &time, &style);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), 0, time, style };

	int len = strlen(string) + 1;
	cell *strstring;
	amx_Allot(m_AMX, len, amxargs + 1, &strstring);
	amx_SetString(strstring, string, 0, 0, len);

	_gameTextForAll(m_AMX, amxargs);

	free(string);
	amx_Release(m_AMX, amxargs[1]);
	Py_RETURN_NONE;
}
// GameTextForPlayer(playerid, string[], time, style) -- TODO: test
PyObject *sGameTextForPlayer(PyObject *self, PyObject *args)
{
	int playerid, time, style;
	char *string;
	PyArg_ParseTuple(args, "iO&ii", &playerid, _stringToCP1252, &string, &time, &style);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), playerid, 0, time, style };

	int len = strlen(string) + 1;
	cell *strstring;
	amx_Allot(m_AMX, len, amxargs + 2, &strstring);
	amx_SetString(strstring, string, 0, 0, len);

	_gameTextForPlayer(m_AMX, amxargs);

	free(string);
	amx_Release(m_AMX, amxargs[2]);
	Py_RETURN_NONE;
}
// int GangZoneCreate(Float:minx, Float:miny, Float:maxx, Float:maxy) -- TODO: test
PyObject *sGangZoneCreate(PyObject *self, PyObject *args)
{
	float minx, miny, maxx, maxy;
	PyArg_ParseTuple(args, "ffff", &minx, &miny, &maxx, &maxy);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), amx_ftoc(minx), amx_ftoc(miny), amx_ftoc(maxx), amx_ftoc(maxy) };

	return Py_BuildValue("i", _gangZoneCreate(m_AMX, amxargs));
}
// GangZoneDestroy(zone) -- TODO: test
PyObject *sGangZoneDestroy(PyObject *self, PyObject *args)
{
	int zone;
	PyArg_ParseTuple(args, "i", &zone);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), zone };
	_gangZoneDestroy(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// GangZoneFlashForAll(zone, flashcolor) -- TODO: test
PyObject *sGangZoneFlashForAll(PyObject *self, PyObject *args)
{
	int zone;
	PyObject *color;
	PyArg_ParseTuple(args, "iO", &zone, &color);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);

	cell amxargs[3] = { sizeof(cell) * 2, zone, colcode };
	_gangZoneFlashForAll(m_AMX, amxargs);

	//_del(col);
	Py_RETURN_NONE;
}
// GangZoneFlashForPlayer(playerid, zone, flashcolor) -- TODO: test
PyObject *sGangZoneFlashForPlayer(PyObject *self, PyObject *args)
{
	int playerid, zone;
	PyObject *color;
	PyArg_ParseTuple(args, "iiO", &playerid, &zone, &color);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);

	cell amxargs[4] = { sizeof(cell) * 3, playerid, zone, colcode };
	_gangZoneFlashForPlayer(m_AMX, amxargs);

	//_del(col);
	Py_RETURN_NONE;
}
// GangZoneHideForAll(zone) -- TODO: test
PyObject *sGangZoneHideForAll(PyObject *self, PyObject *args)
{
	int zone;
	PyArg_ParseTuple(args, "i", &zone);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), zone };
	_gangZoneHideForAll(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// GangZoneHideForPlayer(playerid, zone) -- TODO: test
PyObject *sGangZoneHideForPlayer(PyObject *self, PyObject *args)
{
	int playerid, zone;
	PyArg_ParseTuple(args, "ii", &playerid, &zone);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), playerid, zone };
	_gangZoneHideForPlayer(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// int GangZoneShowForAll(zone, color) -- TODO: test
PyObject *sGangZoneShowForAll(PyObject *self, PyObject *args)
{
	int zone;
	PyObject *color;
	PyArg_ParseTuple(args, "iO", &zone, &color);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);

	cell amxargs[3] = { sizeof(cell) * 2, zone, colcode };

	cell ret = _gangZoneShowForAll(m_AMX, amxargs);

	//_del(col);
	return Py_BuildValue("i", ret);
}
// GangZoneShowForPlayer(playerid, zone, color) -- TODO: test
PyObject *sGangZoneShowForPlayer(PyObject *self, PyObject *args)
{
	int playerid, zone;
	PyObject *color;
	PyArg_ParseTuple(args, "iiO", &playerid, &zone, &color);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);

	cell amxargs[4] = { sizeof(cell) * 3, playerid, zone, colcode };
	_gangZoneShowForPlayer(m_AMX, amxargs);

	//_del(col);
	Py_RETURN_NONE;
}
// GangZoneStopFlashForAll(zone) -- TODO: test
PyObject *sGangZoneStopFlashForAll(PyObject *self, PyObject *args)
{
	int zone;
	PyArg_ParseTuple(args, "i", &zone);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), zone };
	_gangZoneStopFlashForAll(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// GangZoneStopFlashForPlayer(playerid, zone) -- TODO: test
PyObject *sGangZoneStopFlashForPlayer(PyObject *self, PyObject *args)
{
	int playerid, zone;
	PyArg_ParseTuple(args, "ii", &playerid, &zone);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), playerid, zone };
	_gangZoneStopFlashForPlayer(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// int GetAnimationName(index, animlib[], len1, animname[], len2)
PyObject *sGetAnimationName(PyObject *self, PyObject *args)
{
	int index;
	PyArg_ParseTuple(args, "i", &index);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell *stranimlib, *stranimname;

	cell amxargs[6] = { 5 * sizeof(cell), index, 0, 32, 0, 32 };

	amx_Allot(m_AMX, 32, amxargs + 2, &stranimlib);
	amx_Allot(m_AMX, 32, amxargs + 4, &stranimname);

	cell ret = _getAnimationName(m_AMX, amxargs);

	char *animlib = _getString(m_AMX, amxargs[2]), *animname = _getString(m_AMX, amxargs[4]);

	PyObject *retval = Py_BuildValue("{s:i,s:s,s:s}", "return", ret, "animlib", animlib, "animname", animname);

	amx_Release(m_AMX, amxargs[2]); amx_Release(m_AMX, amxargs[4]);
	_del(animlib); _del(animname);

	return retval;
}
// int GetMaxPlayers()
PyObject *sGetMaxPlayers(PyObject *self, PyObject *args)
{
	cell amxargs[1] = { 0 };
	return Py_BuildValue("i", _getMaxPlayers(m_AMX, amxargs));
}
// GetNetworkStats(retstr[], retstr_size)
PyObject *sGetNetworkStats(PyObject *self, PyObject *args)
{
	cell *strretstr;

	cell amxargs[3] = { 2 * sizeof(cell), 0, 401 };

	amx_Allot(m_AMX, 401, amxargs + 1, &strretstr);

	_getNetworkStats(m_AMX, amxargs);

	char *retstr = _getString(m_AMX, amxargs[1]);

	PyObject *retval = Py_BuildValue("s", retstr);

	amx_Release(m_AMX, amxargs[1]);
	_del(retstr);
	return retval;
}
// GetObjectPos(objectid, &Float:X, &Float:Y, &Float:Z) -- TODO: test
PyObject *sGetObjectPos(PyObject *self, PyObject *args)
{
	int oid;
	cell *ref_addr[3];
	PyArg_ParseTuple(args, "i", &oid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), oid, 0, 0, 0 };
	amx_Allot(m_AMX, 1, amxargs + 2, ref_addr);
	amx_Allot(m_AMX, 1, amxargs + 3, ref_addr + 1);
	amx_Allot(m_AMX, 1, amxargs + 4, ref_addr + 2);

	_getObjectPos(m_AMX, amxargs);

	float p[3] = { amx_ctof(*ref_addr[0]), amx_ctof(*ref_addr[1]), amx_ctof(*ref_addr[2]) };
	amx_Release(m_AMX, amxargs[2]); amx_Release(m_AMX, amxargs[3]); amx_Release(m_AMX, amxargs[4]);

	return Py_BuildValue("{s:f,s:f,s:f}", "x", p[0], "y", p[1], "z", p[2]);
}
// GetObjectRot(objectid, &Float:RotX, &Float:RotY, &Float:RotZ) -- TODO: test
PyObject *sGetObjectRot(PyObject *self, PyObject *args)
{
	int oid;
	cell *ref_addr[3];
	PyArg_ParseTuple(args, "i", &oid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), oid, 0, 0, 0 };
	amx_Allot(m_AMX, 1, amxargs + 2, ref_addr);
	amx_Allot(m_AMX, 1, amxargs + 3, ref_addr + 1);
	amx_Allot(m_AMX, 1, amxargs + 4, ref_addr + 2);

	_getObjectRot(m_AMX, amxargs);

	float p[3] = { amx_ctof(*ref_addr[0]), amx_ctof(*ref_addr[1]), amx_ctof(*ref_addr[2]) };
	amx_Release(m_AMX, amxargs[2]); amx_Release(m_AMX, amxargs[3]); amx_Release(m_AMX, amxargs[4]);

	return Py_BuildValue("{s:f,s:f,s:f}", "x", p[0], "y", p[1], "z", p[2]);
}
// int GetPVarFloat(playerid, varname[])
PyObject *sGetPVarFloat(PyObject *self, PyObject *args)
{
	int playerid;
	char *string;

	// No conversion, should always be ASCII.
	PyArg_ParseTuple(args, "is", &playerid, &string);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), playerid, 0 };

	int len = strlen(string) + 1;
	cell *strstring;
	amx_Allot(m_AMX, len, amxargs + 2, &strstring);
	amx_SetString(strstring, string, 0, 0, len);

	cell ret = _getPVarFloat(m_AMX, amxargs);

	amx_Release(m_AMX, amxargs[2]);

	return Py_BuildValue("f", amx_ctof(ret));
}
// int GetPVarInt(playerid, varname[])
PyObject *sGetPVarInt(PyObject *self, PyObject *args)
{
	int playerid;
	char *string;

	// No conversion, should always be ASCII.
	PyArg_ParseTuple(args, "is", &playerid, &string);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), playerid, 0 };

	int len = strlen(string) + 1;
	cell *strstring;
	amx_Allot(m_AMX, len, amxargs + 2, &strstring);
	amx_SetString(strstring, string, 0, 0, len);

	cell ret = _getPVarInt(m_AMX, amxargs);

	amx_Release(m_AMX, amxargs[2]);

	return Py_BuildValue("i", ret);
}
// int GetPVarString(playerid, varname[])
PyObject *sGetPVarString(PyObject *self, PyObject *args)
{
	int playerid;
	char *string;

	// No conversion, should always be ASCII.
	PyArg_ParseTuple(args, "is", &playerid, &string);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), playerid, 0, 0, 2048 };

	int len = strlen(string) + 1;
	cell *strstring;
	amx_Allot(m_AMX, len, amxargs + 2, &strstring);
	amx_SetString(strstring, string, 0, 0, len);

	cell *retstring;
	amx_Allot(m_AMX, 2048, amxargs + 3, &retstring);

	_getPVarString(m_AMX, amxargs);

	char *pstring = _getString(m_AMX, amxargs[3]);
	PyObject *retval = Py_BuildValue("s", pstring);

	amx_Release(m_AMX, amxargs[2]);
	amx_Release(m_AMX, amxargs[3]);
	_del(pstring);

	return retval;
}
// GetPVarType -- no
// int GetPlayerAmmo(playerid) -- TODO: test
PyObject *sGetPlayerAmmo(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerAmmo(m_AMX, amxargs));
}
// int GetPlayerAnimationIndex(playerid)
PyObject *sGetPlayerAnimationIndex(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };

	return Py_BuildValue("i", _getPlayerAnimationIndex(m_AMX, amxargs));
}
// GetPlayerArmour(playerid, Float:&armour)
PyObject *sGetPlayerArmour(PyObject *self, PyObject *args)
{
	int pid;
	cell *ref_addr;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, 0 };
	amx_Allot(m_AMX, 1, amxargs + 2, &ref_addr);

	_getPlayerArmour(m_AMX, amxargs);
	float arm = amx_ctof(*ref_addr);
	amx_Release(m_AMX, amxargs[2]);
	return Py_BuildValue("f", arm);
}
// GetPlayerCameraFrontVector(playerid, &Float:x, &Float:y, &Float:z)
PyObject *sGetPlayerCameraFrontVector(PyObject *self, PyObject *args)
{
	int playerid;
	cell *ref_addr[3];
	PyArg_ParseTuple(args, "i", &playerid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), playerid, 0, 0, 0 };
	amx_Allot(m_AMX, 1, amxargs + 2, ref_addr);
	amx_Allot(m_AMX, 1, amxargs + 3, ref_addr + 1);
	amx_Allot(m_AMX, 1, amxargs + 4, ref_addr + 2);

	_getPlayerCameraFrontVector(m_AMX, amxargs);

	float p[3] = { amx_ctof(*ref_addr[0]), amx_ctof(*ref_addr[1]), amx_ctof(*ref_addr[2]) };
	amx_Release(m_AMX, amxargs[2]); amx_Release(m_AMX, amxargs[3]); amx_Release(m_AMX, amxargs[4]);

	return Py_BuildValue("{s:f,s:f,s:f}", "x", p[0], "y", p[1], "z", p[2]);
}
// int GetPlayerCameraMode(playerid)
PyObject *sGetPlayerCameraMode(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };

	return Py_BuildValue("i", _getPlayerCameraMode(m_AMX, amxargs));
}
// GetPlayerCameraPos(playerid, Float:&x, Float:&y, Float:&z)
PyObject *sGetPlayerCameraPos(PyObject *self, PyObject *args)
{
	int playerid;
	cell *ref_addr[3];
	PyArg_ParseTuple(args, "i", &playerid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), playerid, 0, 0, 0 };
	amx_Allot(m_AMX, 1, amxargs + 2, ref_addr);
	amx_Allot(m_AMX, 1, amxargs + 3, ref_addr + 1);
	amx_Allot(m_AMX, 1, amxargs + 4, ref_addr + 2);

	_getPlayerCameraPos(m_AMX, amxargs);

	float p[3] = { amx_ctof(*ref_addr[0]), amx_ctof(*ref_addr[1]), amx_ctof(*ref_addr[2]) };
	amx_Release(m_AMX, amxargs[2]); amx_Release(m_AMX, amxargs[3]); amx_Release(m_AMX, amxargs[4]);

	return Py_BuildValue("{s:f,s:f,s:f}", "x", p[0], "y", p[1], "z", p[2]);
}
// GetPlayerCameraUpVector -- removed
// int GetPlayerColor(playerid)
PyObject *sGetPlayerColor(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerColor(m_AMX, amxargs));
}
// float GetPlayerDistanceFromPoint(playerid, Float:X, Float:Y, Float:Z)
PyObject *sGetPlayerDistanceFromPoint(PyObject *self, PyObject *args)
{
	int pid;
	float x, y, z;
	PyArg_ParseTuple(args, "ifff", &pid, &x, &y, &z);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), pid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z) };
	cell ret = _getPlayerDistanceFromPoint(m_AMX, amxargs);
	return Py_BuildValue("f", amx_ctof(ret));
}
// int GetPlayerDrunkLevel(playerid)
PyObject *sGetPlayerDrunkLevel(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerDrunkLevel(m_AMX, amxargs));
}
// GetPlayerFacingAngle(playerid,Float:&Angle)
PyObject *sGetPlayerFacingAngle(PyObject *self, PyObject *args)
{
	int pid;
	cell *ref_addr;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, 0 };
	amx_Allot(m_AMX, 1, amxargs + 2, &ref_addr);

	_getPlayerFacingAngle(m_AMX, amxargs);
	float ang = amx_ctof(*ref_addr);
	amx_Release(m_AMX, amxargs[2]);
	return Py_BuildValue("f", ang);
}
// int GetPlayerFightingStyle(playerid)
PyObject *sGetPlayerFightingStyle(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerFightingStyle(m_AMX, amxargs));
}
// GetPlayerHealth(playerid, &health)
PyObject *sGetPlayerHealth(PyObject *self, PyObject *args)
{
	int pid;
	cell *ref_addr;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, 0 };
	amx_Allot(m_AMX, 1, amxargs + 2, &ref_addr);

	_getPlayerHealth(m_AMX, amxargs);
	float health = amx_ctof(*ref_addr);
	amx_Release(m_AMX, amxargs[2]);
	return Py_BuildValue("f", health);
}
// int GetPlayerInterior(playerid)
PyObject *sGetPlayerInterior(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerInterior(m_AMX, amxargs));
}
// int GetPlayerIp(playerid, name[], len)
PyObject *sGetPlayerIp(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell *strip;
	cell amxargs[4] = { 3 * sizeof(cell), pid, 0, 16 };

	amx_Allot(m_AMX, 16, amxargs + 2, &strip);

	cell ret = _getPlayerIp(m_AMX, amxargs);

	char *ip = _getString(m_AMX, amxargs[2]);
	PyObject *retval = Py_BuildValue("s", ip);

	amx_Release(m_AMX, amxargs[2]);
	_del(ip);

	return retval;
}
// GetPlayerKeys(playerid, &keys, &updown, &leftright)
PyObject *sGetPlayerKeys(PyObject *self, PyObject *args)
{
	int playerid;
	cell *ref_addr[3];
	PyArg_ParseTuple(args, "i", &playerid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), playerid, 0, 0, 0 };
	amx_Allot(m_AMX, 1, amxargs + 2, ref_addr);
	amx_Allot(m_AMX, 1, amxargs + 3, ref_addr + 1);
	amx_Allot(m_AMX, 1, amxargs + 4, ref_addr + 2);

	_getPlayerKeys(m_AMX, amxargs);

	int p[3] = { *ref_addr[0], *ref_addr[1], *ref_addr[2] };
	amx_Release(m_AMX, amxargs[2]); amx_Release(m_AMX, amxargs[3]); amx_Release(m_AMX, amxargs[4]);

	return Py_BuildValue("{s:i,s:i,s:i}", "keys", p[0], "updown", p[1], "leftright", p[2]);
}
// int GetPlayerMenu(playerid) -- TODO: test
PyObject *sGetPlayerMenu(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerMenu(m_AMX, amxargs));
}
// int GetPlayerMoney(playerid)
PyObject *sGetPlayerMoney(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerMoney(m_AMX, amxargs));
}
// int GetPlayerName(playerid, const name[], len)
PyObject *sGetPlayerName(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell *strname;
	cell amxargs[4] = { 3 * sizeof(cell), pid, 0, MAX_PLAYER_NAME };

	amx_Allot(m_AMX, MAX_PLAYER_NAME, amxargs + 2, &strname);

	_getPlayerName(m_AMX, amxargs);

	char *name = _getString(m_AMX, amxargs[2]);
	PyObject *retval = Py_BuildValue("s", name);

	amx_Release(m_AMX, amxargs[2]);
	_del(name);

	return retval;
}
// GetPlayerNetworkStats(playerid, retstr[], retstr_size)
PyObject *sGetPlayerNetworkStats(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell *strretstr;

	cell amxargs[4] = { 3 * sizeof(cell), pid, 0, 401 };

	amx_Allot(m_AMX, 401, amxargs + 2, &strretstr);

	_getPlayerNetworkStats(m_AMX, amxargs);

	char *retstr = _getString(m_AMX, amxargs[2]);

	PyObject *retval = Py_BuildValue("s", retstr);

	amx_Release(m_AMX, amxargs[2]);
	_del(retstr);

	return retval;
}
// GetPlayerObjectPos(playerid, objectid, &Float:X, &Float:Y, &Float:Z) -- TODO: test
PyObject *sGetPlayerObjectPos(PyObject *self, PyObject *args)
{
	int playerid, oid;
	cell *ref_addr[3];
	PyArg_ParseTuple(args, "ii", &playerid, &oid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[6] = { 5 * sizeof(cell), playerid, oid, 0, 0, 0 };
	amx_Allot(m_AMX, 1, amxargs + 3, ref_addr);
	amx_Allot(m_AMX, 1, amxargs + 4, ref_addr + 1);
	amx_Allot(m_AMX, 1, amxargs + 5, ref_addr + 2);

	_getPlayerObjectPos(m_AMX, amxargs);

	float p[3] = { amx_ctof(*ref_addr[0]), amx_ctof(*ref_addr[1]), amx_ctof(*ref_addr[2]) };
	amx_Release(m_AMX, amxargs[3]); amx_Release(m_AMX, amxargs[4]); amx_Release(m_AMX, amxargs[5]);

	return Py_BuildValue("{s:f,s:f,s:f}", "x", p[0], "y", p[1], "z", p[2]);
}
// GetPlayerObjectRot(playerid, objectid, &Float:RotX, &Float:RotY, &Float:RotZ) -- TODO: test
PyObject *sGetPlayerObjectRot(PyObject *self, PyObject *args)
{
	int playerid, oid;
	cell *ref_addr[3];
	PyArg_ParseTuple(args, "ii", &playerid, &oid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[6] = { 5 * sizeof(cell), playerid, oid, 0, 0, 0 };
	amx_Allot(m_AMX, 1, amxargs + 3, ref_addr);
	amx_Allot(m_AMX, 1, amxargs + 4, ref_addr + 1);
	amx_Allot(m_AMX, 1, amxargs + 5, ref_addr + 2);

	_getPlayerObjectRot(m_AMX, amxargs);

	float p[3] = { amx_ctof(*ref_addr[0]), amx_ctof(*ref_addr[1]), amx_ctof(*ref_addr[2]) };
	amx_Release(m_AMX, amxargs[3]); amx_Release(m_AMX, amxargs[4]); amx_Release(m_AMX, amxargs[5]);

	return Py_BuildValue("{s:f,s:f,s:f}", "x", p[0], "y", p[1], "z", p[2]);
}
// int GetPlayerPing(playerid)
PyObject *sGetPlayerPing(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerPing(m_AMX, amxargs));
}
// GetPlayerPos(playerid, Float:&x, Float:&y, Float:&z)
PyObject *sGetPlayerPos(PyObject *self, PyObject *args)
{
	int playerid;
	cell *ref_addr[3];
	PyArg_ParseTuple(args, "i", &playerid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), playerid, 0, 0, 0 };
	amx_Allot(m_AMX, 1, amxargs + 2, ref_addr);
	amx_Allot(m_AMX, 1, amxargs + 3, ref_addr + 1);
	amx_Allot(m_AMX, 1, amxargs + 4, ref_addr + 2);

	_getPlayerPos(m_AMX, amxargs);

	float p[3] = { amx_ctof(*ref_addr[0]), amx_ctof(*ref_addr[1]), amx_ctof(*ref_addr[2]) };
	amx_Release(m_AMX, amxargs[2]); amx_Release(m_AMX, amxargs[3]); amx_Release(m_AMX, amxargs[4]);

	return Py_BuildValue("{s:f,s:f,s:f}", "x", p[0], "y", p[1], "z", p[2]);
}
// int GetPlayerScore(playerid)
PyObject *sGetPlayerScore(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerScore(m_AMX, amxargs));
}
// int GetPlayerSkin(playerid)
PyObject *sGetPlayerSkin(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerSkin(m_AMX, amxargs));
}
// int GetPlayerSpecialAction(playerid)
PyObject *sGetPlayerSpecialAction(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerSpecialAction(m_AMX, amxargs));
}
// int GetPlayerState(playerid)
PyObject *sGetPlayerState(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerState(m_AMX, amxargs));
}
// int GetPlayerSurfingObjectID(playerid) -- TODO: test
PyObject *sGetPlayerSurfingObjectID(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerSurfingObjectID(m_AMX, amxargs));
}
// int GetPlayerSurfingVehicleID(playerid) -- TODO: test
PyObject *sGetPlayerSurfingVehicleID(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerSurfingVehicleID(m_AMX, amxargs));
}
// int GetPlayerTargetPlayer(playerid)
PyObject *sGetPlayerTargetPlayer(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerTargetPlayer(m_AMX, amxargs));
}
// int GetPlayerTeam(playerid)
PyObject *sGetPlayerTeam(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerTeam(m_AMX, amxargs));
}
// GetPlayerTime(playerid, &hour, &minute)
PyObject *sGetPlayerTime(PyObject *self, PyObject *args)
{
	int playerid;
	cell *ref_addr[2];
	PyArg_ParseTuple(args, "i", &playerid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), playerid, 0, 0 };
	amx_Allot(m_AMX, 1, amxargs + 2, ref_addr);
	amx_Allot(m_AMX, 1, amxargs + 3, ref_addr + 1);

	_getPlayerTime(m_AMX, amxargs);

	int p[2] = { *ref_addr[0], *ref_addr[1] };
	amx_Release(m_AMX, amxargs[2]); amx_Release(m_AMX, amxargs[3]);

	return Py_BuildValue("{s:i,s:i}", "hour", p[0], "minute", p[1]);
}
// int GetPlayerVehicleID(playerid) -- TODO: test
PyObject *sGetPlayerVehicleID(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerVehicleID(m_AMX, amxargs));
}
// int GetPlayerVehicleSeat(playerid) -- TODO: test
PyObject *sGetPlayerVehicleSeat(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerVehicleSeat(m_AMX, amxargs));
}
// GetPlayerVelocity(playerid, &Float:x, &Float:y, &Float:z)
PyObject *sGetPlayerVelocity(PyObject *self, PyObject *args)
{
	int playerid;
	cell *ref_addr[3];
	PyArg_ParseTuple(args, "i", &playerid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), playerid, 0, 0, 0 };
	amx_Allot(m_AMX, 1, amxargs + 2, ref_addr);
	amx_Allot(m_AMX, 1, amxargs + 3, ref_addr + 1);
	amx_Allot(m_AMX, 1, amxargs + 4, ref_addr + 2);

	_getPlayerVelocity(m_AMX, amxargs);

	float p[3] = { amx_ctof(*ref_addr[0]), amx_ctof(*ref_addr[1]), amx_ctof(*ref_addr[2]) };
	amx_Release(m_AMX, amxargs[2]); amx_Release(m_AMX, amxargs[3]); amx_Release(m_AMX, amxargs[4]);

	return Py_BuildValue("{s:f,s:f,s:f}", "x", p[0], "y", p[1], "z", p[2]);
}
// GetPlayerVersion(playerid, const version[], len) -- TODO: test
PyObject *sGetPlayerVersion(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell *strretstr;

	cell amxargs[4] = { 3 * sizeof(cell), pid, 0, 16 };

	amx_Allot(m_AMX, 16, amxargs + 2, &strretstr);

	_getPlayerVersion(m_AMX, amxargs);

	char *retstr = _getString(m_AMX, amxargs[2]);

	PyObject *retval = Py_BuildValue("s", retstr);

	amx_Release(m_AMX, amxargs[2]);
	_del(retstr);

	return retval;
}
// int GetPlayerVirtualWorld(playerid)
PyObject *sGetPlayerVirtualWorld(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerVirtualWorld(m_AMX, amxargs));
}
// int GetPlayerWantedLevel(playerid)
PyObject *sGetPlayerWantedLevel(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerWantedLevel(m_AMX, amxargs));
}
// int GetPlayerWeapon(playerid)
PyObject *sGetPlayerWeapon(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerWeapon(m_AMX, amxargs));
}
// GetPlayerWeaponData(playerid, slot, &weapons, &ammo)
PyObject *sGetPlayerWeaponData(PyObject *self, PyObject *args)
{
	int playerid, slot;
	cell *ref_addr[2];
	PyArg_ParseTuple(args, "ii", &playerid, &slot);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), playerid, slot, 0, 0 };
	amx_Allot(m_AMX, 1, amxargs + 3, ref_addr);
	amx_Allot(m_AMX, 1, amxargs + 4, ref_addr + 1);

	_getPlayerWeaponData(m_AMX, amxargs);

	int p[2] = { *ref_addr[0], *ref_addr[1] };
	amx_Release(m_AMX, amxargs[3]); amx_Release(m_AMX, amxargs[4]);

	return Py_BuildValue("{s:i,s:i}", "weapons", p[0], "ammo", p[1]);
}
// int GetPlayerWeaponState(playerid)
PyObject *sGetPlayerWeaponState(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _getPlayerWeaponState(m_AMX, amxargs));
}
// GetServerVarAsBool -- no
// GetServerVarAsInt -- no
// GetServerVarAsString -- no
// int GetTickCount()
PyObject *sGetTickCount(PyObject *self, PyObject *args)
{
	cell amxargs[1] = { 0 };
	return Py_BuildValue("i", _getTickCount(m_AMX, amxargs));
}
// int GetVehicleComponentInSlot(vehicleid, slot) -- TODO: test
PyObject *sGetVehicleComponentInSlot(PyObject *self, PyObject *args)
{
	int pid, slot;
	PyArg_ParseTuple(args, "ii", &pid, &slot);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, slot };
	return Py_BuildValue("i", _getVehicleComponentInSlot(m_AMX, amxargs));
}
// int GetVehicleComponentType(component) -- TODO: test
PyObject *sGetVehicleComponentType(PyObject *self, PyObject *args)
{
	int comp;
	PyArg_ParseTuple(args, "i", &comp);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), comp };
	return Py_BuildValue("i", _getVehicleComponentType(m_AMX, amxargs));
}
// GetVehicleDamageStatus(vehicleid, &panels, &doors, &lights, &tires) -- TODO: test
PyObject *sGetVehicleDamageStatus(PyObject *self, PyObject *args)
{
	int vehicleid;
	cell *ref_addr[2];
	PyArg_ParseTuple(args, "i", &vehicleid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[6] = { 5 * sizeof(cell), vehicleid, 0, 0, 0, 0 };
	amx_Allot(m_AMX, 1, amxargs + 2, ref_addr);
	amx_Allot(m_AMX, 1, amxargs + 3, ref_addr + 1);
	amx_Allot(m_AMX, 1, amxargs + 4, ref_addr + 2);
	amx_Allot(m_AMX, 1, amxargs + 5, ref_addr + 3);

	_getVehicleDamageStatus(m_AMX, amxargs);

	float p[4] = { amx_ctof(*ref_addr[0]), amx_ctof(*ref_addr[1]), amx_ctof(*ref_addr[2]), amx_ctof(*ref_addr[3]) };
	amx_Release(m_AMX, amxargs[2]); amx_Release(m_AMX, amxargs[3]); amx_Release(m_AMX, amxargs[4]); amx_Release(m_AMX, amxargs[5]);

	return Py_BuildValue("{s:i,s:i,s:i,s:i}", "panels", p[0], "doors", p[1], "lights", p[2], "tires", p[3]);
}
// float GetVehicleDistanceFromPoint(vehicleid, Float:X, Float:Y, Float:Z) -- TODO: test
PyObject *sGetVehicleDistanceFromPoint(PyObject *self, PyObject *args)
{
	int vid;
	float x, y, z;
	PyArg_ParseTuple(args, "ifff", &vid, &x, &y, &z);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), vid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z) };
	cell ret = _getVehicleDistanceFromPoint(m_AMX, amxargs);
	return Py_BuildValue("f", amx_ctof(ret));
}
// GetVehicleHealth(vehicleid, &Float:health) -- TODO: test
PyObject *sGetVehicleHealth(PyObject *self, PyObject *args)
{
	int vid;
	cell *ref_addr;
	PyArg_ParseTuple(args, "i", &vid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), vid, 0 };
	amx_Allot(m_AMX, 1, amxargs + 2, &ref_addr);

	_getVehicleHealth(m_AMX, amxargs);
	float h = amx_ctof(*ref_addr);
	amx_Release(m_AMX, amxargs[2]);
	return Py_BuildValue("f", h);
}
// int GetVehicleModel(vehicleid) -- TODO: test
PyObject *sGetVehicleModel(PyObject *self, PyObject *args)
{
	int vid;
	PyArg_ParseTuple(args, "i", &vid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), vid };
	return Py_BuildValue("i", _getVehicleModel(m_AMX, amxargs));
}
// GetVehicleModelInfo(vehiclemodel, infotype, &Float:X, &Float:Y, &Float:Z) -- TODO: test
PyObject *sGetVehicleModelInfo(PyObject *self, PyObject *args)
{
	int vmodel, itype;
	cell *ref_addr[3];
	PyArg_ParseTuple(args, "ii", &vmodel, &itype);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[6] = { 5 * sizeof(cell), vmodel, itype, 0, 0, 0 };
	amx_Allot(m_AMX, 1, amxargs + 3, ref_addr);
	amx_Allot(m_AMX, 1, amxargs + 4, ref_addr + 1);
	amx_Allot(m_AMX, 1, amxargs + 5, ref_addr + 2);

	_getVehicleModelInfo(m_AMX, amxargs);

	float p[3] = { amx_ctof(*ref_addr[0]), amx_ctof(*ref_addr[1]), amx_ctof(*ref_addr[2]) };
	amx_Release(m_AMX, amxargs[3]); amx_Release(m_AMX, amxargs[4]); amx_Release(m_AMX, amxargs[5]);

	return Py_BuildValue("{s:f,s:f,s:f}", "x", p[0], "y", p[1], "z", p[2]);
}
// GetVehiclePos(vehicleid, &Float:X, &Float:Y, &Float:Z) -- TODO: test
PyObject *sGetVehiclePos(PyObject *self, PyObject *args)
{
	int vehicleid;
	cell *ref_addr[3];
	PyArg_ParseTuple(args, "i", &vehicleid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), vehicleid, 0, 0, 0 };
	amx_Allot(m_AMX, 1, amxargs + 2, ref_addr);
	amx_Allot(m_AMX, 1, amxargs + 3, ref_addr + 1);
	amx_Allot(m_AMX, 1, amxargs + 4, ref_addr + 2);

	_getVehiclePos(m_AMX, amxargs);

	float p[3] = { amx_ctof(*ref_addr[0]), amx_ctof(*ref_addr[1]), amx_ctof(*ref_addr[2]) };
	amx_Release(m_AMX, amxargs[2]); amx_Release(m_AMX, amxargs[3]); amx_Release(m_AMX, amxargs[4]);

	return Py_BuildValue("{s:f,s:f,s:f}", "x", p[0], "y", p[1], "z", p[2]);
}
// int GetVehicleRotationQuat(vehicleid, &Float:w, &Float:x, &Float:y, &Float:z) -- TODO: test
PyObject *sGetVehicleRotationQuat(PyObject *self, PyObject *args)
{
	int vehicleid;
	cell *ref_addr[3];
	PyArg_ParseTuple(args, "i", &vehicleid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), vehicleid, 0, 0, 0 };
	amx_Allot(m_AMX, 1, amxargs + 2, ref_addr);
	amx_Allot(m_AMX, 1, amxargs + 3, ref_addr + 1);
	amx_Allot(m_AMX, 1, amxargs + 4, ref_addr + 2);
	amx_Allot(m_AMX, 1, amxargs + 5, ref_addr + 3);

	_getVehicleRotationQuat(m_AMX, amxargs);

	float p[4] = { amx_ctof(*ref_addr[0]), amx_ctof(*ref_addr[1]), amx_ctof(*ref_addr[2]), amx_ctof(*ref_addr[3]) };
	amx_Release(m_AMX, amxargs[2]); amx_Release(m_AMX, amxargs[3]); amx_Release(m_AMX, amxargs[4]); amx_Release(m_AMX, amxargs[5]);

	return Py_BuildValue("{s:f,s:f,s:f,s:f}", "w", p[0], "x", p[1], "y", p[2], "z", p[3]);
}
// int GetVehicleTrailer(vehicleid) -- TODO: test
PyObject *sGetVehicleTrailer(PyObject *self, PyObject *args)
{
	int vid;
	PyArg_ParseTuple(args, "i", &vid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), vid };
	return Py_BuildValue("i", _getVehicleTrailer(m_AMX, amxargs));
}
// GetVehicleVelocity(vehicleid, &Float:x, &Float:y, &Float:z) -- TODO: test
PyObject *sGetVehicleVelocity(PyObject *self, PyObject *args)
{
	int playerid;
	cell *ref_addr[3];
	PyArg_ParseTuple(args, "i", &playerid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), playerid, 0, 0, 0 };
	amx_Allot(m_AMX, 1, amxargs + 2, ref_addr);
	amx_Allot(m_AMX, 1, amxargs + 3, ref_addr + 1);
	amx_Allot(m_AMX, 1, amxargs + 4, ref_addr + 2);

	_getVehicleVelocity(m_AMX, amxargs);

	float p[3] = { amx_ctof(*ref_addr[0]), amx_ctof(*ref_addr[1]), amx_ctof(*ref_addr[2]) };
	amx_Release(m_AMX, amxargs[2]); amx_Release(m_AMX, amxargs[3]); amx_Release(m_AMX, amxargs[4]);

	return Py_BuildValue("{s:f,s:f,s:f}", "x", p[0], "y", p[1], "z", p[2]);
}
// int GetVehicleVirtualWorld(vehicleid) -- TODO: test
PyObject *sGetVehicleVirtualWorld(PyObject *self, PyObject *args)
{
	int vid;
	PyArg_ParseTuple(args, "i", &vid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), vid };
	return Py_BuildValue("i", _getVehicleVirtualWorld(m_AMX, amxargs));
}
// GetVehicleZAngle(vehicleid, &Float:z_angl) -- TODO: test
PyObject *sGetVehicleZAngle(PyObject *self, PyObject *args)
{
	int vid;
	cell *ref_addr[1];
	PyArg_ParseTuple(args, "i", &vid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), vid, 0 };
	amx_Allot(m_AMX, 1, amxargs + 2, ref_addr);

	_getVehicleZAngle(m_AMX, amxargs);

	float p = amx_ctof(*ref_addr[0]);
	amx_Release(m_AMX, amxargs[2]);

	return Py_BuildValue("f", p);
}
// GetWeaponName(weaponid, const weapon[], len)
PyObject *sGetWeaponName(PyObject *self, PyObject *args)
{
	int wid;
	PyArg_ParseTuple(args, "i", &wid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell *strname;
	cell amxargs[4] = { 3 * sizeof(cell), wid, 0, 32 };

	amx_Allot(m_AMX, 32, amxargs + 2, &strname);

	_getWeaponName(m_AMX, amxargs);

	char *name = _getString(m_AMX, amxargs[2]);
	PyObject *retval = Py_BuildValue("s", name);

	amx_Release(m_AMX, amxargs[2]);
	_del(name);

	return retval;
}
// Getdate -- Python has its own date/time functions
// Getproperty -- no
// Gettime -- no
// GivePlayerMoney(playerid, money)
PyObject *sGivePlayerMoney(PyObject *self, PyObject *args)
{
	int pid, money;
	PyArg_ParseTuple(args, "ii", &pid, &money);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, money };
	 _givePlayerMoney(m_AMX, amxargs);
	 Py_RETURN_NONE;
}
// GivePlayerWeapon(playerid, weaponid, ammo)
PyObject *sGivePlayerWeapon(PyObject *self, PyObject *args)
{
	int pid, wid, ammo;
	PyArg_ParseTuple(args, "iii", &pid, &wid, &ammo);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), pid, wid, ammo };
	_givePlayerWeapon(m_AMX, amxargs);
	Py_RETURN_NONE;
}

// HTTP -- no, use Python functions
// HideMenuForPlayer(menuid, playerid) -- TODO: test
PyObject *sHideMenuForPlayer(PyObject *self, PyObject *args)
{
	int mid, pid;
	PyArg_ParseTuple(args, "ii", &mid, &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), mid, pid };
	_hideMenuForPlayer(m_AMX, amxargs);
	Py_RETURN_NONE;
}

// InterpolateCameraPos(playerid, Float:FromX, Float:FromY, Float:FromZ, Float:ToX, Float:ToY, Float:ToZ, time, cut = CAMERA_CUT) -- TODO: test
PyObject *sInterpolateCameraPos(PyObject *self, PyObject *args)
{
	int pid, time, cut = CAMERA_CUT;
	float fx, fy, fz, tx, ty, tz;
	PyArg_ParseTuple(args, "iffffffi|i", &pid, &fx, &fy, &fz, &tx, &ty, &tz, &time, &cut);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[10] = { 9 * sizeof(cell), pid, amx_ftoc(fx), amx_ftoc(fy), amx_ftoc(fz), amx_ftoc(tx), amx_ftoc(ty), amx_ftoc(tz), time, cut };
	_interpolateCameraPos(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// InterpolateCameraLookAt(playerid, Float:FromX, Float:FromY, Float:FromZ, Float:ToX, Float:ToY, Float:ToZ, time, cut = CAMERA_CUT) -- TODO: test
PyObject *sInterpolateCameraLookAt(PyObject *self, PyObject *args)
{
	int pid, time, cut = CAMERA_CUT;
	float fx, fy, fz, tx, ty, tz;
	PyArg_ParseTuple(args, "iffffffi|i", &pid, &fx, &fy, &fz, &tx, &ty, &tz, &time, &cut);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[10] = { 9 * sizeof(cell), pid, amx_ftoc(fx), amx_ftoc(fy), amx_ftoc(fz), amx_ftoc(tx), amx_ftoc(ty), amx_ftoc(tz), time, cut };
	_interpolateCameraLookAt(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// int IsObjectMoving(objectid) -- TODO: test
PyObject *sIsObjectMoving(PyObject *self, PyObject *args)
{
	int oid;
	PyArg_ParseTuple(args, "i", &oid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), oid };
	return Py_BuildValue("i", _isObjectMoving(m_AMX, amxargs));
}
// int IsPlayerAdmin(playerid)
PyObject *sIsPlayerAdmin(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _isPlayerAdmin(m_AMX, amxargs));
}
// int IsPlayerAttachedObjectSlotUsed(playerid, index) -- TODO: test
PyObject *sIsPlayerAttachedObjectSlotUsed(PyObject *self, PyObject *args)
{
	int pid, idx;
	PyArg_ParseTuple(args, "ii", &pid, &idx);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, idx };
	return Py_BuildValue("i", _isPlayerAttachedObjectSlotUsed(m_AMX, amxargs));
}
// int IsPlayerConnected(playerid)
PyObject *sIsPlayerConnected(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _isPlayerConnected(m_AMX, amxargs));
}
// int IsPlayerHoldingObject(playerid) -- TODO: test
PyObject *sIsPlayerHoldingObject(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _isPlayerHoldingObject(m_AMX, amxargs));
}
// int IsPlayerInAnyVehicle(playerid) -- TODO: test
PyObject *sIsPlayerInAnyVehicle(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _isPlayerInAnyVehicle(m_AMX, amxargs));
}
// int IsPlayerInCheckpoint(playerid) -- TODO: test
PyObject *sIsPlayerInCheckpoint(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _isPlayerInCheckpoint(m_AMX, amxargs));
}
// int IsPlayerInRaceCheckpoint(playerid) -- TODO: test
PyObject *sIsPlayerInRaceCheckpoint(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _isPlayerInRaceCheckpoint(m_AMX, amxargs));
}
// int IsPlayerInRangeOfPoint(playerid, Float:range, Float:x, Float:y, Float:z)
PyObject *sIsPlayerInRangeOfPoint(PyObject *self, PyObject *args)
{
	int pid;
	float r, x, y, z;
	PyArg_ParseTuple(args, "iffff", &pid, &r, &x, &y, &z);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[6] = { 5 * sizeof(cell), pid, amx_ftoc(r), amx_ftoc(x), amx_ftoc(y), amx_ftoc(z) };
	return Py_BuildValue("i", _isPlayerInRangeOfPoint(m_AMX, amxargs));
}
// int IsPlayerInVehicle(playerid, vehicleid) -- TODO: test
PyObject *sIsPlayerInVehicle(PyObject *self, PyObject *args)
{
	int pid, vid;
	PyArg_ParseTuple(args, "ii", &pid, &vid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, vid };
	return Py_BuildValue("i", _isPlayerInVehicle(m_AMX, amxargs));
}
// int IsPlayerNPC(playerid)
PyObject *sIsPlayerNPC(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	return Py_BuildValue("i", _isPlayerNPC(m_AMX, amxargs));
}
// int IsPlayerObjectMoving(objectid) -- TODO: test
PyObject *sIsPlayerObjectMoving(PyObject *self, PyObject *args)
{
	int oid;
	PyArg_ParseTuple(args, "i", &oid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), oid };
	return Py_BuildValue("i", _isPlayerObjectMoving(m_AMX, amxargs));
}
// int IsPlayerStreamedIn(playerid, forplayerid)
PyObject *sIsPlayerStreamedIn(PyObject *self, PyObject *args)
{
	int pid, fpid;
	PyArg_ParseTuple(args, "ii", &pid, &fpid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, fpid };
	return Py_BuildValue("i", _isPlayerStreamedIn(m_AMX, amxargs));
}
// int IsTrailerAttachedToVehicle(vehicleid) -- TODO: test
PyObject *sIsTrailerAttachedToVehicle(PyObject *self, PyObject *args)
{
	int vid;
	PyArg_ParseTuple(args, "i", &vid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), vid };
	return Py_BuildValue("i", _isTrailerAttachedToVehicle(m_AMX, amxargs));
}
// int IsValidMenu(Menu:menuid) -- TODO: test
PyObject *sIsValidMenu(PyObject *self, PyObject *args)
{
	int mid;
	PyArg_ParseTuple(args, "i", &mid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), mid };
	return Py_BuildValue("i", _isValidMenu(m_AMX, amxargs));
}
// int IsValidObject(objectid) -- TODO: test
PyObject *sIsValidObject(PyObject *self, PyObject *args)
{
	int oid;
	PyArg_ParseTuple(args, "i", &oid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), oid };
	return Py_BuildValue("i", _isValidObject(m_AMX, amxargs));
}
// int IsValidPlayerObject(playerid, objectid) -- TODO: test
PyObject *sIsValidPlayerObject(PyObject *self, PyObject *args)
{
	int pid, oid;
	PyArg_ParseTuple(args, "ii", &pid, &oid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, oid };
	return Py_BuildValue("i", _isValidPlayerObject(m_AMX, amxargs));
}
// int IsVehicleStreamedIn(vehicleid, forplayerid) -- TODO: test
PyObject *sIsVehicleStreamedIn(PyObject *self, PyObject *args)
{
	int vid, fpid;
	PyArg_ParseTuple(args, "ii", &vid, &fpid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), vid, fpid };
	return Py_BuildValue("i", _isVehicleStreamedIn(m_AMX, amxargs));
}
// Ispacked -- no

// Kick(playerid)
PyObject *sKick(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	_kick(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// KillTimer(timerid)
PyObject *sKillTimer(PyObject *self, PyObject *args)
{
	long tid = 0;
	PyArg_ParseTuple(args, "l", &tid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	if (tid == 0) Py_RETURN_NONE;

	m_MainLock->Lock();
	// search for this timer id
	for (std::deque<timer_data>::iterator i = m_TimerList.begin(); i != m_TimerList.end(); i++)
	{
		if (i->id == tid)
		{
			// remove this one
			clearTimerData(i);
			m_TimerList.erase(i);
			break;
		}
	}
	m_MainLock->Unlock();
	Py_RETURN_NONE;
}

// LimitGlobalChatRadius(Float:chat_radius) -- TODO: test
PyObject *sLimitGlobalChatRadius(PyObject *self, PyObject *args)
{
	float cr;
	PyArg_ParseTuple(args, "f", &cr);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), amx_ftoc(cr) };
	_limitGlobalChatRadius(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// LimitPlayerMarkerRadius(Float:marker_radius) -- TODO: test
PyObject *sLimitPlayerMarkerRadius(PyObject *self, PyObject *args)
{
	float mr;
	PyArg_ParseTuple(args, "f", &mr);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), amx_ftoc(mr) };
	_limitPlayerMarkerRadius(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// LinkVehicleToInterior(vehicleid, interiorid) -- TODO: test
PyObject *sLinkVehicleToInterior(PyObject *self, PyObject *args)
{
	int vid, iid;
	PyArg_ParseTuple(args, "ii", &vid, &iid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), vid, iid };
	_linkVehicleToInterior(m_AMX, amxargs);
	Py_RETURN_NONE;
}

// ManualVehicleEngineAndLights() -- TODO: test / 0.3c
PyObject *sManualVehicleEngineAndLights(PyObject *self, PyObject *args)
{
	cell amxargs[1] = { 0 };
	_manualVehicleEngineAndLights(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// Memcpy -- no
// int MoveObject(objectid, Float:X, Float:Y, Float:Z, Float:Speed, Float:RotX = -1000.0, Float:RotY = -1000.0, Float:RotZ = -1000.0) -- TODO: test
PyObject *sMoveObject(PyObject *self, PyObject *args)
{
	int oid;
	float x, y, z, s, rx = -1000.0, ry = -1000.0, rz = -1000.0;
	PyArg_ParseTuple(args, "iffff|fff", &oid, &x, &y, &z, &s, &rx, &ry, &rz);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[9] = { 8 * sizeof(cell), oid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), amx_ftoc(s), amx_ftoc(rx), amx_ftoc(ry), amx_ftoc(rz) };
	return Py_BuildValue("i", _moveObject(m_AMX, amxargs));
}
// int MovePlayerObject(playerid, objectid, Float:X, Float:Y, Float:Z, Float:Speed, Float:RotX = -1000.0, Float:RotY = -1000.0, Float:RotZ = -1000.0) -- TODO: test
PyObject *sMovePlayerObject(PyObject *self, PyObject *args)
{
	int pid, oid;
	float x, y, z, s, rx = -1000.0, ry = -1000.0, rz = -1000.0;
	PyArg_ParseTuple(args, "iiffff|fff", &pid, &oid, &x, &y, &z, &s, &rx, &ry, &rz);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[10] = { 9 * sizeof(cell), pid, oid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), amx_ftoc(s), amx_ftoc(rx), amx_ftoc(ry), amx_ftoc(rz) };
	return Py_BuildValue("i", _movePlayerObject(m_AMX, amxargs));
}

// we do not need NPC functions

// PlayAudioStreamForPlayer(playerid, url[], Float:posX = 0.0, Float:posY = 0.0, Float:posZ = 0.0, Float:distance = 50.0, usepos = 0) -- TODO: test
PyObject *sPlayAudioStreamForPlayer(PyObject *self, PyObject *args)
{
	int pid, usepos = 0;
	float x = 0.0, y = 0.0, z = 0.0, dist = 50.0;
	char *url = NULL;
	// No conversion, should always be ASCII (urlencoded if need be).
	PyArg_ParseTuple(args, "is|ffffi", &pid, &url, &x, &y, &z, &dist, &usepos);

	if(PyErr_Occurred() != NULL)
		return NULL;
	
	cell amxargs[8] = { 7 * sizeof(cell), pid, 0, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), amx_ftoc(dist), usepos };

	cell *strurl;
	amx_Allot(m_AMX, strlen(url) + 1, amxargs + 2, &strurl);
	amx_SetString(strurl, url, 0, 0, strlen(url) + 1);

	_playAudioStreamForPlayer(m_AMX, amxargs);

	amx_Release(m_AMX, amxargs[2]);
	Py_RETURN_NONE;
}
// PlayCrimeReportForPlayer(playerid, suspectid, crimeid)
PyObject *sPlayCrimeReportForPlayer(PyObject *self, PyObject *args)
{
	int pid, sid, cid;
	PyArg_ParseTuple(args, "iii", &pid, &sid, &cid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), pid, sid, cid };
	_playCrimeReportForPlayer(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// PlayerPlaySound(playerid, soundid, Float:x, Float:y, Float:z)
PyObject *sPlayerPlaySound(PyObject *self, PyObject *args)
{
	int pid, sid;
	float x, y, z;
	PyArg_ParseTuple(args, "iifff", &pid, &sid, &x, &y, &z);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[6] = { 5 * sizeof(cell), pid, sid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z) };
	_playerPlaySound(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// PlayerSpectatePlayer(playerid, targetplayerid, mode = SPECTATE_MODE_NORMAL) -- TODO: test
PyObject *sPlayerSpectatePlayer(PyObject *self, PyObject *args)
{
	int pid, tid, mode = SPECTATE_MODE_NORMAL;
	PyArg_ParseTuple(args, "ii|i", &pid, &tid, &mode);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), pid, tid, mode };
	_playerSpectatePlayer(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// PlayerSpectateVehicle(playerid, targetvehicleid, mode = SPECTATE_MODE_NORMAL) -- TODO: test
PyObject *sPlayerSpectateVehicle(PyObject *self, PyObject *args)
{
	int pid, tid, mode = SPECTATE_MODE_NORMAL;
	PyArg_ParseTuple(args, "ii|i", &pid, &tid, &mode);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), pid, tid, mode };
	_playerSpectateVehicle(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// Print(f) -- used logprintf
// PutPlayerInVehicle(playerid,vehicleid,seatid) -- TODO: test
PyObject *sPutPlayerInVehicle(PyObject *self, PyObject *args)
{
	int pid, vid, sid;
	PyArg_ParseTuple(args, "iii", &pid, &vid, &sid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), pid, vid, sid };
	_putPlayerInVehicle(m_AMX, amxargs);
	Py_RETURN_NONE;
}

// PlayerText:CreatePlayerTextDraw(playerid, Float:x, Float:y, text[])
PyObject *sCreatePlayerTextDraw(PyObject *self, PyObject *args)
{
	int pid;
	float x, y;
	char *txt = NULL;
	PyArg_ParseTuple(args, "iffO&", &pid, &x, &y, _stringToCP1252, &txt);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), pid, amx_ftoc(x), amx_ftoc(y), 0 };

	cell *strtxt;
	amx_Allot(m_AMX, strlen(txt) + 1, amxargs + 4, &strtxt);
	amx_SetString(strtxt, txt, 0, 0, strlen(txt) + 1);

	cell ret = _createPlayerTextDraw(m_AMX, amxargs);

	free(txt);
	amx_Release(m_AMX, amxargs[4]);
	return Py_BuildValue("i", ret);
}
// PlayerTextDrawDestroy(playerid, PlayerText:text) -- TODO: test
PyObject *sPlayerTextDrawDestroy(PyObject *self, PyObject *args)
{
	int pid, txt;
	PyArg_ParseTuple(args, "ii", &pid, &txt);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, txt };
	_playerTextDrawDestroy(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// PlayerTextDrawLetterSize(playerid, PlayerText:text, Float:x, Float:y)
PyObject *sPlayerTextDrawLetterSize(PyObject *self, PyObject *args)
{
	int pid, txt;
	float x, y;
	PyArg_ParseTuple(args, "iiff", &pid, &txt, &x, &y);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), pid, txt, amx_ftoc(x), amx_ftoc(y) };
	_playerTextDrawLetterSize(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// PlayerTextDrawTextSize(playerid, PlayerText:text, Float:x, Float:y) -- TODO: test
PyObject *sPlayerTextDrawTextSize(PyObject *self, PyObject *args)
{
	int pid, txt;
	float x, y;
	PyArg_ParseTuple(args, "iiff", &pid, &txt, &x, &y);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), pid, txt, amx_ftoc(x), amx_ftoc(y) };
	_playerTextDrawTextSize(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// PlayerTextDrawAlignment(playerid, PlayerText:text, alignment) -- TODO: test
PyObject *sPlayerTextDrawAlignment(PyObject *self, PyObject *args)
{
	int pid, text, alig;
	PyArg_ParseTuple(args, "iii", &pid, &text, &alig);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), pid, text, alig };
	_playerTextDrawAlignment(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// PlayerTextDrawColor(playerid, PlayerText:text, color) -- TODO: test
PyObject *sPlayerTextDrawColor(PyObject *self, PyObject *args)
{
	int pid, text;
	PyObject *color;
	PyArg_ParseTuple(args, "iiO", &pid, &text, &color);

	if(PyErr_Occurred() != NULL)
		return NULL;
	_getColor(color);

	cell amxargs[4] = { 3 * sizeof(cell), pid, text, colcode };
	_playerTextDrawColor(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// PlayerTextDrawUseBox(playerid, PlayerText:text, use) -- TODO: test
PyObject *sPlayerTextDrawUseBox(PyObject *self, PyObject *args)
{
	int pid, txt, use;
	PyArg_ParseTuple(args, "iii", &pid, &txt, &use);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), pid, txt, use };
	_playerTextDrawUseBox(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// PlayerTextDrawBoxColor(playerid, PlayerText:text, color) -- TODO: test
PyObject *sPlayerTextDrawBoxColor(PyObject *self, PyObject *args)
{
	int pid, text;
	PyObject *color;
	PyArg_ParseTuple(args, "iiO", &pid, &text, &color);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);

	cell amxargs[4] = { 3 * sizeof(cell), pid, text, colcode };
	_playerTextDrawBoxColor(m_AMX, amxargs);

	Py_RETURN_NONE;
}
// PlayerTextDrawSetPreviewModel(playerid, PlayerText:text, modelindex) -- TODO: test
PyObject *sPlayerTextDrawSetPreviewModel(PyObject *self, PyObject *args)
{
	int pid, txt, midx;
	PyArg_ParseTuple(args, "iii", &pid, &txt, &midx);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), pid, txt, midx };
	_playerTextDrawSetPreviewModel(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// PlayerTextDrawSetPreviewRot(playerid, PlayerText:text, Float:fRotX, Float:fRotY, Float:fRotZ, Float:fZoom) -- TODO: test
PyObject *sPlayerTextDrawSetPreviewRot(PyObject *self, PyObject *args)
{
	int pid, txt;
	float x, y, z, zoom;
	PyArg_ParseTuple(args, "iiffff", &pid, &txt, &x, &y, &z, &zoom);

	if(PyErr_Occurred() != NULL)
		return NULL;
	
	cell amxargs[7] = { 6 * sizeof(cell), pid, txt, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), amx_ftoc(zoom) };

	_playerTextDrawSetPreviewRot(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// PlayerTextDrawSetPreviewVehCol(PlayerText:text, playerid, color1, color2) -- TODO: test
PyObject *sPlayerTextDrawSetPreviewVehCol(PyObject *self, PyObject *args)
{
	int txt, pid, c1, c2;
	PyArg_ParseTuple(args, "iiii", &txt, &pid, &c1, &c2);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), txt, pid, c1, c2 };
	_playerTextDrawSetPreviewVehCol(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// PlayerTextDrawSetShadow(playerid, PlayerText:text, size) -- TODO: test
PyObject *sPlayerTextDrawSetShadow(PyObject *self, PyObject *args)
{
	int pid, txt, size;
	PyArg_ParseTuple(args, "iii", &pid, &txt, &size);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), pid, txt, size };
	_playerTextDrawSetShadow(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// PlayerTextDrawSetOutline(playerid, PlayerText:text, size)
PyObject *sPlayerTextDrawSetOutline(PyObject *self, PyObject *args)
{
	int pid, txt, size;
	PyArg_ParseTuple(args, "iii", &pid, &txt, &size);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), pid, txt, size };
	_playerTextDrawSetOutline(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// PlayerTextDrawBackgroundColor(playerid, PlayerText:text, color) -- TODO: test
PyObject *sPlayerTextDrawBackgroundColor(PyObject *self, PyObject *args)
{
	int pid, text;
	PyObject *color;
	PyArg_ParseTuple(args, "iiO", &pid, &text, &color);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);

	cell amxargs[4] = { 3 * sizeof(cell), pid, text, colcode };
	_playerTextDrawBackgroundColor(m_AMX, amxargs);

	Py_RETURN_NONE;
}
// PlayerTextDrawFont(playerid, PlayerText:text, font) -- TODO: test
PyObject *sPlayerTextDrawFont(PyObject *self, PyObject *args)
{
	int pid, txt, font;
	PyArg_ParseTuple(args, "iii", &pid, &txt, &font);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), pid, txt, font };
	_playerTextDrawFont(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// PlayerTextDrawSetProportional(playerid, PlayerText:text, set) -- TODO: test
PyObject *sPlayerTextDrawSetProportional(PyObject *self, PyObject *args)
{
	int pid, txt, set;
	PyArg_ParseTuple(args, "iii", &pid, &txt, &set);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), pid, txt, set };
	_playerTextDrawSetProportional(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// PlayerTextDrawSetSelectable(playerid, PlayerText:text, set)
PyObject *sPlayerTextDrawSetSelectable(PyObject *self, PyObject *args)
{
	int pid, txt, set;
	PyArg_ParseTuple(args, "iii", &pid, &txt, &set);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), pid, txt, set };
	_playerTextDrawSetSelectable(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// PlayerTextDrawShow(playerid, PlayerText:text)
PyObject *sPlayerTextDrawShow(PyObject *self, PyObject *args)
{
	int pid, txt;
	PyArg_ParseTuple(args, "ii", &pid, &txt);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, txt};
	_playerTextDrawShow(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// PlayerTextDrawHide(playerid, PlayerText:text)
PyObject *sPlayerTextDrawHide(PyObject *self, PyObject *args)
{
	int pid, txt;
	PyArg_ParseTuple(args, "ii", &pid, &txt);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, txt};
	_playerTextDrawHide(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// PlayerTextDrawSetString(playerid, PlayerText:text, string[])
PyObject *sPlayerTextDrawSetString(PyObject *self, PyObject *args)
{
	int pid, txt;
	char *string = NULL;
	PyArg_ParseTuple(args, "iiO&", &pid, &txt, _stringToCP1252, &string);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), pid, txt, 0 };

	int len = strlen(string) + 1;
	cell *strstring;
	amx_Allot(m_AMX, len, amxargs + 3, &strstring);
	amx_SetString(strstring, string, 0, 0, len);

	_playerTextDrawSetString(m_AMX, amxargs);

	free(string);
	amx_Release(m_AMX, amxargs[3]);
	Py_RETURN_NONE;
}

// Random -- use Python functions
// RemoveBuildingForPlayer(playerid, modelid, Float:fX, Float:fY, Float:fZ, Float:fRadius)
PyObject *sRemoveBuildingForPlayer(PyObject *self, PyObject *args)
{
	int pid, modelid;
	float x, y, z, radius;
	PyArg_ParseTuple(args, "iiffff", &pid, &modelid, &x, &y, &z, &radius);

	if(PyErr_Occurred() != NULL)
		return NULL;
	
	cell amxargs[7] = { 6 * sizeof(cell), pid, modelid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), amx_ftoc(radius) };

	_removeBuildingForPlayer(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// int RemovePlayerAttachedObject(playerid,index) -- TODO: test
PyObject *sRemovePlayerAttachedObject(PyObject *self, PyObject *args)
{
	int pid, idx;
	PyArg_ParseTuple(args, "ii", &pid, &idx);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, idx };
	return Py_BuildValue("i", _removePlayerAttachedObject(m_AMX, amxargs));
}
// RemovePlayerFromVehicle(playerid) -- TODO: test
PyObject *sRemovePlayerFromVehicle(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	_removePlayerFromVehicle(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// RemovePlayerMapIcon(playerid, iconid) -- TODO: test
PyObject *sRemovePlayerMapIcon(PyObject *self, PyObject *args)
{
	int pid, ico;
	PyArg_ParseTuple(args, "ii", &pid, &ico);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, ico };
	_removePlayerMapIcon(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// RemoveVehicleComponent(vehicleid, componentid) -- TODO: test
PyObject *sRemoveVehicleComponent(PyObject *self, PyObject *args)
{
	int pid, comp;
	PyArg_ParseTuple(args, "ii", &pid, &comp);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, comp };
	_removeVehicleComponent(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// RepairVehicle(vehicleid) -- TODO: test
PyObject *sRepairVehicle(PyObject *self, PyObject *args)
{
	int vid;
	PyArg_ParseTuple(args, "i", &vid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), vid };
	_repairVehicle(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// ResetPlayerMoney(playerid)
PyObject *sResetPlayerMoney(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	_resetPlayerMoney(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// ResetPlayerWeapons(playerid)
PyObject *sResetPlayerWeapons(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	_resetPlayerWeapons(m_AMX, amxargs);
	Py_RETURN_NONE;
}

// SelectObject(playerid)
PyObject *sSelectObject(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	_selectObject(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SelectTextDraw(playerid, hovercolor)
PyObject *sSelectTextDraw(PyObject *self, PyObject *args)
{
	int pid;
	PyObject *color;
	PyArg_ParseTuple(args, "iO", &pid, &color);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);

	cell amxargs[3] = { 2 * sizeof(cell), pid, colcode };
	_selectTextDraw(m_AMX, amxargs);

	Py_RETURN_NONE;
}
// void SendClientMessage(playerid, color, const message[])
PyObject *sSendClientMessage(PyObject *self, PyObject *args)
{
	int playerid;
	PyObject *color;
	char *msg = NULL;
	PyArg_ParseTuple(args, "iOO&", &playerid, &color, _stringToCP1252, &msg);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);

	if (msg == NULL) Py_RETURN_NONE;
	cell amxargs[4] = { sizeof(cell) * 3, playerid, colcode, 0 };

	cell *str;
	amx_Allot(m_AMX, strlen(msg) + 1, amxargs + 3, &str);
	amx_SetString(str, msg, 0, 0, strlen(msg) + 1);

	_sendClientMessage(m_AMX, amxargs);

	free(msg);
	amx_Release(m_AMX, amxargs[3]);

	Py_RETURN_NONE;
}
// SendClientMessageToAll(color, const message[])
PyObject *sSendClientMessageToAll(PyObject *self, PyObject *args)
{
	PyObject *color;
	char *msg = NULL;
	PyArg_ParseTuple(args, "OO&", &color, _stringToCP1252, &msg);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);

	if (msg == NULL) Py_RETURN_NONE;
	cell amxargs[3] = { sizeof(cell) * 2, colcode, 0 };

	cell *str;
	amx_Allot(m_AMX, strlen(msg) + 1, amxargs + 2, &str);
	amx_SetString(str, msg, 0, 0, strlen(msg) + 1);

	_sendClientMessageToAll(m_AMX, amxargs);

	free(msg);
	amx_Release(m_AMX, amxargs[2]);

	Py_RETURN_NONE;
}
// SendDeathMessage(killer, victim, reason)
PyObject *sSendDeathMessage(PyObject *self, PyObject *args)
{
	int k, v, reas;
	PyArg_ParseTuple(args, "iii", &k, &v, &reas);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), k, v, reas };
	_sendDeathMessage(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SendPlayerMessageToAll(senderid, const message[])
PyObject *sSendPlayerMessageToAll(PyObject *self, PyObject *args)
{
	int sid;
	char *msg = NULL;
	PyArg_ParseTuple(args, "iO&", &sid, _stringToCP1252, &msg);

	if(PyErr_Occurred() != NULL)
		return NULL;

	if (msg == NULL) Py_RETURN_NONE;
	cell amxargs[3] = { sizeof(cell) * 2, sid, 0 };

	cell *str;
	amx_Allot(m_AMX, strlen(msg) + 1, amxargs + 2, &str);
	amx_SetString(str, msg, 0, 0, strlen(msg) + 1);

	_sendPlayerMessageToAll(m_AMX, amxargs);

	free(msg);
	amx_Release(m_AMX, amxargs[2]);

	Py_RETURN_NONE;
}
// SendPlayerMessageToPlayer(playerid, senderid, const message[])
PyObject *sSendPlayerMessageToPlayer(PyObject *self, PyObject *args)
{
	int pid, sid;
	char *msg = NULL;
	PyArg_ParseTuple(args, "iiO&", &pid, &sid, _stringToCP1252, &msg);

	if(PyErr_Occurred() != NULL)
		return NULL;

	if (msg == NULL) Py_RETURN_NONE;
	cell amxargs[4] = { sizeof(cell) * 3, pid, sid, 0 };

	cell *str;
	amx_Allot(m_AMX, strlen(msg) + 1, amxargs + 3, &str);
	amx_SetString(str, msg, 0, 0, strlen(msg) + 1);

	_sendPlayerMessageToPlayer(m_AMX, amxargs);

	free(msg);
	amx_Release(m_AMX, amxargs[3]);

	Py_RETURN_NONE;
}
// SendRconCommand(command[])
PyObject *sSendRconCommand(PyObject *self, PyObject *args)
{
	char *cmd = NULL;
	PyArg_ParseTuple(args, "O&", _stringToCP1252, &cmd);

	if(PyErr_Occurred() != NULL)
		return NULL;

	if (cmd == NULL) Py_RETURN_NONE;
	cell amxargs[2] = { sizeof(cell), 0 };

	cell *str;
	amx_Allot(m_AMX, strlen(cmd) + 1, amxargs + 1, &str);
	amx_SetString(str, cmd, 0, 0, strlen(cmd) + 1);

	_sendRconCommand(m_AMX, amxargs);

	free(cmd);
	amx_Release(m_AMX, amxargs[1]);

	Py_RETURN_NONE;
}
// SetCameraBehindPlayer(playerid)
PyObject *sSetCameraBehindPlayer(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	_setCameraBehindPlayer(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetDeathDropAmount -- doesn't work
// SetDisabledWeapons -- removed
// SetGameModeText(string[])
PyObject *sSetGameModeText(PyObject *self, PyObject *args)
{
	char *gmtext; int txtlen;
	PyArg_ParseTuple(args, "O&", _stringToCP1252, &gmtext);

	if(PyErr_Occurred() != NULL)
		return NULL;

	txtlen = strlen(gmtext) + 1;
	cell amxargs[2];

	amxargs[0] = sizeof(cell);

	cell *paddr;

	amx_Allot(m_AMX, txtlen, amxargs + 1, &paddr);
	amx_SetString(paddr, gmtext, 0, 0, txtlen);

	_setGameModeText(m_AMX, amxargs);

	free(gmtext);
	Py_RETURN_NONE;
}
// SetGravity(Float:gravity)
PyObject *sSetGravity(PyObject *self, PyObject *args)
{
	float gravity;
	PyArg_ParseTuple(args, "f", &gravity);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), amx_ftoc(gravity) };

	_setGravity(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetMenuColumnHeader(menuid, column, text[]) -- TODO: test
PyObject *sSetMenuColumnHeader(PyObject *self, PyObject *args)
{
	int mid, col;
	char *txt = NULL;
	PyArg_ParseTuple(args, "iiO&", &mid, &col, _stringToCP1252, &txt);

	if(PyErr_Occurred() != NULL)
		return NULL;

	if (txt == NULL) Py_RETURN_NONE;
	cell amxargs[4] = { sizeof(cell) * 3, mid, col, 0 };

	cell *str;
	amx_Allot(m_AMX, strlen(txt) + 1, amxargs + 3, &str);
	amx_SetString(str, txt, 0, 0, strlen(txt) + 1);

	_setMenuColumnHeader(m_AMX, amxargs);

	free(txt);
	amx_Release(m_AMX, amxargs[3]);

	Py_RETURN_NONE;
}
// SetNameTagDrawDistance(Float:distance) -- TODO: test
PyObject *sSetNameTagDrawDistance(PyObject *self, PyObject *args)
{
	float dis;
	PyArg_ParseTuple(args, "f", &dis);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), amx_ftoc(dis) };
	_setNameTagDrawDistance(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetObjectMaterial(objectid, materialindex, modelid, txdname[], texturename[], materialcolor=0)
PyObject *sSetObjectMaterial(PyObject *self, PyObject *args)
{
	int oid, midx, mid;
	unsigned int matcol = 0; // TODO: TEST!
	char *txd = NULL, *texture = NULL;
	// No conversion, should always be ASCII.
	PyArg_ParseTuple(args, "iiissI", &oid, &midx, &mid, &txd, &texture, &matcol);

	if(PyErr_Occurred() != NULL)
		return NULL;

	if (txd == NULL || texture == NULL) Py_RETURN_NONE;
	cell amxargs[7] = { 6 * sizeof(cell), oid, midx, mid, 0, 0, matcol };

	cell *strtxd, *strtexture;
	amx_Allot(m_AMX, strlen(txd) + 1, amxargs + 4, &strtxd);
	amx_SetString(strtxd, txd, 0, 0, strlen(txd) + 1);
	amx_Allot(m_AMX, strlen(texture) + 1, amxargs + 5, &strtexture);
	amx_SetString(strtexture, texture, 0, 0, strlen(texture) + 1);

	_setObjectMaterial(m_AMX, amxargs);

	amx_Release(m_AMX, amxargs[4]); amx_Release(m_AMX, amxargs[5]);

	Py_RETURN_NONE;
}
// SetObjectMaterialText(objectid, text[], materialindex = 0, materialsize = OBJECT_MATERIAL_SIZE_256x128, fontface[] = "Arial", fontsize = 24, bold = 1, fontcolor = 0xFFFFFFFF, backcolor = 0, textalignment = 0)
PyObject *sSetObjectMaterialText(PyObject *self, PyObject *args)
{
	int oid, midx = 0, matsize = OBJECT_MATERIAL_SIZE_256x128, fontsize = 24, bold = 1, txtalig = 0;
	unsigned int fontcol = 0xFFFFFFFF, backcol = 0; // TODO: TEST!
	char *txt = NULL, *fontface = "Arial";
	PyArg_ParseTuple(args, "iO&|iisiiIIi", &oid, _stringToCP1252, &txt, &midx, &matsize, &fontface, &fontsize, &bold, &fontcol, &backcol, &txtalig);

	if(PyErr_Occurred() != NULL)
		return NULL;

	if (txt == NULL) Py_RETURN_NONE;
	cell amxargs[11] = { 10 * sizeof(cell), oid, 0, midx, matsize, 0, fontsize, bold, fontcol, backcol, txtalig };

	cell *strtxt, *strfontface;
	amx_Allot(m_AMX, strlen(txt) + 1, amxargs + 2, &strtxt);
	amx_SetString(strtxt, txt, 0, 0, strlen(txt) + 1);
	amx_Allot(m_AMX, strlen(fontface) + 1, amxargs + 5, &strfontface);
	amx_SetString(strfontface, fontface, 0, 0, strlen(fontface) + 1);

	_setObjectMaterialText(m_AMX, amxargs);

	free(txt);
	amx_Release(m_AMX, amxargs[2]); amx_Release(m_AMX, amxargs[5]);

	Py_RETURN_NONE;
}
// SetObjectPos(objectid, Float:X, Float:Y, Float:Z) -- TODO: test
PyObject *sSetObjectPos(PyObject *self, PyObject *args)
{
	int oid;
	float x, y, z;
	PyArg_ParseTuple(args, "ifff", &oid, &x, &y, &z);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), oid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z) };
	_setObjectPos(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetObjectRot(objectid, Float:RotX, Float:RotY, Float:RotZ) -- TODO: test
PyObject *sSetObjectRot(PyObject *self, PyObject *args)
{
	int oid;
	float x, y, z;
	PyArg_ParseTuple(args, "ifff", &oid, &x, &y, &z);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), oid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z) };
	_setObjectRot(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPVarFloat(playerid, varname[], Float:float_value)
PyObject *sSetPVarFloat(PyObject *self, PyObject *args)
{
	int playerid;
	char *string;
	float value;

	// No conversion, should always be ASCII.
	PyArg_ParseTuple(args, "isf", &playerid, &string, &value);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), playerid, 0, amx_ftoc(value) };

	int len = strlen(string) + 1;
	cell *strstring;
	amx_Allot(m_AMX, len, amxargs + 2, &strstring);
	amx_SetString(strstring, string, 0, 0, len);

	cell ret = _setPVarFloat(m_AMX, amxargs);

	amx_Release(m_AMX, amxargs[2]);

	return Py_BuildValue("i", ret);
}
// SetPVarInt(playerid, varname[], value)
PyObject *sSetPVarInt(PyObject *self, PyObject *args)
{
	int playerid, value;
	char *string;

	// No conversion, should always be ASCII.
	PyArg_ParseTuple(args, "isi", &playerid, &string, &value);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), playerid, 0, value };

	int len = strlen(string) + 1;
	cell *strstring;
	amx_Allot(m_AMX, len, amxargs + 2, &strstring);
	amx_SetString(strstring, string, 0, 0, len);

	cell ret = _setPVarInt(m_AMX, amxargs);

	amx_Release(m_AMX, amxargs[2]);

	return Py_BuildValue("i", ret);
}
// SetPVarString(playerid, varname[], string_value[])
PyObject *sSetPVarString(PyObject *self, PyObject *args)
{
	int playerid;
	char *string, *value;

	PyArg_ParseTuple(args, "isO&", &playerid, &string, _stringToCP1252, &value);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), playerid, 0, 0 };

	int len = strlen(string) + 1;
	cell *strstring;
	amx_Allot(m_AMX, len, amxargs + 2, &strstring);
	amx_SetString(strstring, string, 0, 0, len);

	len = strlen(value) + 1;
	cell *strvalue;
	amx_Allot(m_AMX, len, amxargs + 3, &strvalue);
	amx_SetString(strvalue, value, 0, 0, len);

	_setPVarString(m_AMX, amxargs);

	free(value);
	amx_Release(m_AMX, amxargs[2]);
	amx_Release(m_AMX, amxargs[3]);

	Py_RETURN_NONE;
}
// SetPlayerAmmo(playerid, weapon, ammo)
PyObject *sSetPlayerAmmo(PyObject *self, PyObject *args)
{
	int pid, w, a;
	PyArg_ParseTuple(args, "iii", &pid, &w, &a);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), pid, w, a };
	_setPlayerAmmo(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerArmedWeapon(playerid, weaponid)
PyObject *sSetPlayerArmedWeapon(PyObject *self, PyObject *args)
{
	int pid, wid;
	PyArg_ParseTuple(args, "ii", &pid, &wid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, wid };
	_setPlayerArmedWeapon(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerArmour(playerid, Float:armour)
PyObject *sSetPlayerArmour(PyObject *self, PyObject *args)
{
	int pid;
	float arm;
	PyArg_ParseTuple(args, "if", &pid, &arm);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, amx_ftoc(arm) };
	_setPlayerArmour(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// int SetPlayerAttachedObject(playerid, index, modelid, bone, Float:fOffsetX, Float:fOffsetY, Float:fOffsetZ, Float:fRotX, Float:fRotY, Float:fRotZ, Float:fScaleX, Float:fScaleY, Float:fScaleZ, materialcolor1 = 0, materialcolor2 = 0) -- TODO: test / 0.3c
PyObject *sSetPlayerAttachedObject(PyObject *self, PyObject *args)
{
	int pid, idx, modelid, bone, mat1 = 0, mat2 = 0;
	float ox, oy, oz, rx, ry, rz, sx, sy, sz;
	PyArg_ParseTuple(args, "iiiifffffffffii", &pid, &idx, &modelid, &bone, &ox, &oy, &oz, &rx, &ry, &rz, &sx, &sy, &sz, &mat1, &mat2);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[16] = { 15 * sizeof(cell), pid, idx, modelid, bone, amx_ftoc(ox), amx_ftoc(oy), amx_ftoc(oz), amx_ftoc(rx), amx_ftoc(ry), amx_ftoc(rz), amx_ftoc(sx), amx_ftoc(sy), amx_ftoc(sz), mat1, mat2 };

	cell ret = _setPlayerAttachedObject(m_AMX, amxargs);

	return Py_BuildValue("i", ret);
}
// SetPlayerCameraLookAt(playerid,Float:x,Float:y,Float:z,cut=CAMERA_CUT)
PyObject *sSetPlayerCameraLookAt(PyObject *self, PyObject *args)
{
	int pid, cut = CAMERA_CUT;
	float x, y, z;
	PyArg_ParseTuple(args, "ifff|i", &pid, &x, &y, &z, &cut);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[6] = { 5 * sizeof(cell), pid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), cut };
	_setPlayerCameraLookAt(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerCameraPos(playerid,Float:x,Float:y,Float:z)
PyObject *sSetPlayerCameraPos(PyObject *self, PyObject *args)
{
	int pid;
	float x, y, z;
	PyArg_ParseTuple(args, "ifff", &pid, &x, &y, &z);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), pid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z) };
	_setPlayerCameraPos(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerChatBubble(playerid, text[], color, Float:drawdistance, expiretime)
PyObject *sSetPlayerChatBubble(PyObject *self, PyObject *args)
{
	int pid, exp;
	PyObject *color;
	float drawdist;
	char *txt = NULL;
	PyArg_ParseTuple(args, "iO&Ofi", &pid, _stringToCP1252, &txt, &color, &drawdist, &exp);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);
	
	cell amxargs[6] = { 5 * sizeof(cell), pid, 0, colcode, amx_ftoc(drawdist), exp };

	cell *str;
	amx_Allot(m_AMX, strlen(txt) + 1, amxargs + 2, &str);
	amx_SetString(str, txt, 0, 0, strlen(txt) + 1);

	_setPlayerChatBubble(m_AMX, amxargs);

	free(txt);
	amx_Release(m_AMX, amxargs[2]);
	Py_RETURN_NONE;
}
// SetPlayerCheckpoint(playerid,Float:x,Float:y,Float:z,Float:size)
PyObject *sSetPlayerCheckpoint(PyObject *self, PyObject *args)
{
	int pid;
	float x, y, z, s;
	PyArg_ParseTuple(args, "iffff", &pid, &x, &y, &z, &s);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[6] = { 5 * sizeof(cell), pid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), amx_ftoc(s) };
	_setPlayerCheckpoint(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerColor(playerid,color)
PyObject *sSetPlayerColor(PyObject *self, PyObject *args)
{
	int pid;
	PyObject *color;
	PyArg_ParseTuple(args, "iO", &pid, &color);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);

	cell amxargs[3] = { 2 * sizeof(cell), pid, colcode };
	_setPlayerColor(m_AMX, amxargs);

	//_del(col);
	Py_RETURN_NONE;
}
// SetPlayerDrunkLevel(playerid, level)
PyObject *sSetPlayerDrunkLevel(PyObject *self, PyObject *args)
{
	int pid, lvl;
	PyArg_ParseTuple(args, "ii", &pid, &lvl);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, lvl };
	_setPlayerDrunkLevel(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerFacingAngle(playerid,Float:ang)
PyObject *sSetPlayerFacingAngle(PyObject *self, PyObject *args)
{
	int pid;
	float ang;
	PyArg_ParseTuple(args, "if", &pid, &ang);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, amx_ftoc(ang) };
	_setPlayerFacingAngle(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerFightingStyle(playerid, style)
PyObject *sSetPlayerFightingStyle(PyObject *self, PyObject *args)
{
	int pid, style;
	PyArg_ParseTuple(args, "ii", &pid, &style);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, style };
	_setPlayerFightingStyle(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerHealth(playerid, Float:health)
PyObject *sSetPlayerHealth(PyObject *self, PyObject *args)
{
	int pid;
	float h;
	PyArg_ParseTuple(args, "if", &pid, &h);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, amx_ftoc(h) };
	_setPlayerHealth(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerHoldingObject(playerid, modelid, bone, Float:fOffsetX, Float:fOffsetY, Float:fOffsetZ, Float:fRotX, Float:fRotY, Float:fRotZ) -- will be removed in 0.3c
PyObject *sSetPlayerHoldingObject(PyObject *self, PyObject *args)
{
	Py_RETURN_NONE;
}
// SetPlayerInterior(playerid,interiorid)
PyObject *sSetPlayerInterior(PyObject *self, PyObject *args)
{
	int pid, iid;
	PyArg_ParseTuple(args, "ii", &pid, &iid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, iid };
	_setPlayerInterior(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// int SetPlayerMapIcon(playerid, iconid, Float:x, Float:y, Float:z, markertype, color, style)
PyObject *sSetPlayerMapIcon(PyObject *self, PyObject *args)
{
	int pid, ico, marker, style;
	PyObject *color;
	float x, y, z;
	PyArg_ParseTuple(args, "iifffiOi", &pid, &ico, &x, &y, &z, &marker, &color, &style);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);

	cell amxargs[9] = { 8 * sizeof(cell), pid, ico, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), marker, colcode, style };
	cell ret = _setPlayerMapIcon(m_AMX, amxargs);

	//_del(col);
	Py_RETURN_NONE;
}
// SetPlayerMarkerForPlayer(playerid, showplayerid, color)
PyObject *sSetPlayerMarkerForPlayer(PyObject *self, PyObject *args)
{
	int pid, spid;
	PyObject *color;
	PyArg_ParseTuple(args, "iiO", &pid, &spid, &color);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);

	cell amxargs[4] = { 3 * sizeof(cell), pid, spid, colcode };
	_setPlayerMarkerForPlayer(m_AMX, amxargs);

	//_del(col);
	Py_RETURN_NONE;
}
// int SetPlayerName(playerid, name[])
PyObject *sSetPlayerName(PyObject *self, PyObject *args)
{
	int pid;
	char *name = NULL;
	// No conversion, should always be ASCII.
	PyArg_ParseTuple(args, "is", &pid, &name);

	if(PyErr_Occurred() != NULL)
		return NULL;
	
	cell amxargs[3] = { 2 * sizeof(cell), pid, 0 };

	cell *str;
	amx_Allot(m_AMX, strlen(name) + 1, amxargs + 2, &str);
	amx_SetString(str, name, 0, 0, strlen(name) + 1);

	_setPlayerName(m_AMX, amxargs);

	//PyMem_Free(name);
	amx_Release(m_AMX, amxargs[2]);
	Py_RETURN_NONE;
}
// SetPlayerObjectMaterial(playerid, objectid, materialindex, modelid, txdname[], texturename[], materialcolor=0) -- TODO: test
PyObject *sSetPlayerObjectMaterial(PyObject *self, PyObject *args)
{
	int pid, oid, midx, mid;
	unsigned int matcol = 0; // TODO: TEST!
	char *txd = NULL, *texture = NULL;
	// No conversion, should always be ASCII.
	PyArg_ParseTuple(args, "iiiissI", &pid, &oid, &midx, &mid, &txd, &texture, &matcol);

	if(PyErr_Occurred() != NULL)
		return NULL;

	if (txd == NULL || texture == NULL) Py_RETURN_NONE;
	cell amxargs[8] = { 7 * sizeof(cell), pid, oid, midx, mid, 0, 0, matcol };

	cell *strtxd, *strtexture;
	amx_Allot(m_AMX, strlen(txd) + 1, amxargs + 5, &strtxd);
	amx_SetString(strtxd, txd, 0, 0, strlen(txd) + 1);
	amx_Allot(m_AMX, strlen(texture) + 1, amxargs + 6, &strtexture);
	amx_SetString(strtexture, texture, 0, 0, strlen(texture) + 1);

	_setPlayerObjectMaterial(m_AMX, amxargs);

	amx_Release(m_AMX, amxargs[5]); amx_Release(m_AMX, amxargs[6]);

	Py_RETURN_NONE;
}
// SetPlayerObjectMaterialText(playerid, objectid, text[], materialindex = 0, materialsize = OBJECT_MATERIAL_SIZE_256x128, fontface[] = "Arial", fontsize = 24, bold = 1, fontcolor = 0xFFFFFFFF, backcolor = 0, textalignment = 0) -- TODO: test
PyObject *sSetPlayerObjectMaterialText(PyObject *self, PyObject *args)
{
	int pid, oid, midx = 0, matsize = OBJECT_MATERIAL_SIZE_256x128, fontsize = 24, bold = 1, txtalig = 0;
	unsigned int fontcol = 0xFFFFFFFF, backcol = 0; // TODO: TEST!
	char *txt = NULL, *fontface = "Arial";
	PyArg_ParseTuple(args, "iiO&|iisiiIIi", &pid, &oid, _stringToCP1252, &txt, &midx, &matsize, &fontface, &fontsize, &bold, &fontcol, &backcol, &txtalig);

	if(PyErr_Occurred() != NULL)
		return NULL;

	if (txt == NULL) Py_RETURN_NONE;
	cell amxargs[12] = { 11 * sizeof(cell), pid, oid, 0, midx, matsize, 0, fontsize, bold, fontcol, backcol, txtalig };

	cell *strtxt, *strfontface;
	amx_Allot(m_AMX, strlen(txt) + 1, amxargs + 3, &strtxt);
	amx_SetString(strtxt, txt, 0, 0, strlen(txt) + 1);
	amx_Allot(m_AMX, strlen(fontface) + 1, amxargs + 6, &strfontface);
	amx_SetString(strfontface, fontface, 0, 0, strlen(fontface) + 1);

	_setPlayerObjectMaterialText(m_AMX, amxargs);

	free(txt);
	amx_Release(m_AMX, amxargs[3]); amx_Release(m_AMX, amxargs[6]);

	Py_RETURN_NONE;
}
// SetPlayerObjectPos(playerid, objectid, Float:X, Float:Y, Float:Z) -- TODO: test
PyObject *sSetPlayerObjectPos(PyObject *self, PyObject *args)
{
	int pid, oid;
	float x, y, z;
	PyArg_ParseTuple(args, "iifff", &pid, &oid, &x, &y, &z);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[6] = { 5 * sizeof(cell), pid, oid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z) };
	_setPlayerObjectPos(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerObjectRot(playerid, objectid, Float:RotX, Float:RotY, Float:RotZ) -- TODO: test
PyObject *sSetPlayerObjectRot(PyObject *self, PyObject *args)
{
	int pid, oid;
	float x, y, z;
	PyArg_ParseTuple(args, "iifff", &pid, &oid, &x, &y, &z);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[6] = { 5 * sizeof(cell), pid, oid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z) };
	_setPlayerObjectRot(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerPos(playerid,Float:x,Float:y,Float:z)
PyObject *sSetPlayerPos(PyObject *self, PyObject *args)
{
	int pid;
	float x, y, z;
	PyArg_ParseTuple(args, "ifff", &pid, &x, &y, &z);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), pid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z) };
	_setPlayerPos(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerPosFindZ(playerid, Float:x, Float:y, Float:z)
PyObject *sSetPlayerPosFindZ(PyObject *self, PyObject *args)
{
	int pid;
	float x, y, z;
	PyArg_ParseTuple(args, "ifff", &pid, &x, &y, &z);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), pid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z) };
	_setPlayerPosFindZ(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerRaceCheckpoint(playerid, type, Float:x, Float:y, Float:z, Float:nextx, Float:nexty, Float:nextz, Float:size)
PyObject *sSetPlayerRaceCheckpoint(PyObject *self, PyObject *args)
{
	int pid, type;
	float x, y, z, nx, ny, nz, size;
	PyArg_ParseTuple(args, "iifffffff", &pid, &type, &x, &y, &z, &nx, &ny, &nz, &size);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[10] = { 9 * sizeof(cell), pid, type, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), amx_ftoc(nx), amx_ftoc(ny), amx_ftoc(nz), amx_ftoc(size) };
	_setPlayerRaceCheckpoint(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerScore(playerid, score)
PyObject *sSetPlayerScore(PyObject *self, PyObject *args)
{
	int pid, score;
	PyArg_ParseTuple(args, "ii", &pid, &score);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, score };
	_setPlayerScore(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerShopName(playerid, shopname[])
PyObject *sSetPlayerShopName(PyObject *self, PyObject *args)
{
	int pid;
	char *shop = NULL;
	// No conversion, should always be ASCII.
	PyArg_ParseTuple(args, "is", &pid, &shop);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, 0 };

	cell *strshop;
	amx_Allot(m_AMX, strlen(shop) + 1, amxargs + 2, &strshop);
	amx_SetString(strshop, shop, 0, 0, strlen(shop) + 1);

	_setPlayerShopName(m_AMX, amxargs);

	//PyMem_Free(shop);
	amx_Release(m_AMX, amxargs[2]);
	Py_RETURN_NONE;
}
// SetPlayerSkillLevel(playerid, skill, level)
PyObject *sSetPlayerSkillLevel(PyObject *self, PyObject *args)
{
	int pid, skill, lvl;
	PyArg_ParseTuple(args, "iii", &pid, &skill, &lvl);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), pid, skill, lvl };
	_setPlayerSkillLevel(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerSkin(playerid, skinid)
PyObject *sSetPlayerSkin(PyObject *self, PyObject *args)
{
	int pid, skin;
	PyArg_ParseTuple(args, "ii", &pid, &skin);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, skin };
	_setPlayerSkin(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerSpecialAction(playerid, actionid)
PyObject *sSetPlayerSpecialAction(PyObject *self, PyObject *args)
{
	int pid, act;
	PyArg_ParseTuple(args, "ii", &pid, &act);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, act };
	_setPlayerSpecialAction(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerTeam(playerid, teamid) -- TODO: test
PyObject *sSetPlayerTeam(PyObject *self, PyObject *args)
{
	int pid, tid;
	PyArg_ParseTuple(args, "ii", &pid, &tid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, tid };
	_setPlayerTeam(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerTime(playerid, hour, minute)
PyObject *sSetPlayerTime(PyObject *self, PyObject *args)
{
	int pid, hour, min;
	PyArg_ParseTuple(args, "iii", &pid, &hour, &min);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), pid, hour, min };
	_setPlayerTime(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerVelocity(playerid, Float:x, Float:y, Float:z)
PyObject *sSetPlayerVelocity(PyObject *self, PyObject *args)
{
	int pid;
	float x, y, z;
	PyArg_ParseTuple(args, "ifff", &pid, &x, &y, &z);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), pid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z) };
	_setPlayerVelocity(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerVirtualWorld(playerid,worldid)
PyObject *sSetPlayerVirtualWorld(PyObject *self, PyObject *args)
{
	int pid, wid;
	PyArg_ParseTuple(args, "ii", &pid, &wid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, wid };
	_setPlayerVirtualWorld(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerWantedLevel(playerid, level)
PyObject *sSetPlayerWantedLevel(PyObject *self, PyObject *args)
{
	int pid, lvl;
	PyArg_ParseTuple(args, "ii", &pid, &lvl);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, lvl };
	_setPlayerWantedLevel(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerWeather(playerid, weather)
PyObject *sSetPlayerWeather(PyObject *self, PyObject *args)
{
	int pid, w;
	PyArg_ParseTuple(args, "ii", &pid, &w);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, w };
	_setPlayerWeather(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetPlayerWorldBounds(playerid,Float:x_max,Float:x_min,Float:y_max,Float:y_min)
PyObject *sSetPlayerWorldBounds(PyObject *self, PyObject *args)
{
	int pid;
	float xmax, xmin, ymax, ymin;
	PyArg_ParseTuple(args, "iffff", &pid, &xmax, &xmin, &ymax, &ymin);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[6] = { 5 * sizeof(cell), pid, amx_ftoc(xmax), amx_ftoc(xmin), amx_ftoc(ymax), amx_ftoc(ymin) };
	_setPlayerWorldBounds(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetSpawnInfo(playerid, team, skin, Float:x, Float:y, Float:z, Float:Angle, weapon1, weapon1_ammo, weapon2, weapon2_ammo, weapon3, weapon3_ammo) -- TODO: test
PyObject *sSetSpawnInfo(PyObject *self, PyObject *args)
{
	int pid, team, skin, w1, w1a, w2, w2a, w3, w3a;
	float x, y, z, a;
	PyArg_ParseTuple(args, "iiiffffiiiiii", &pid, &team, &skin, &x, &y, &z, &a, &w1, &w1a, &w2, &w2a, &w3, &w3a);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[14] = { 13 * sizeof(cell), pid, team, skin, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), amx_ftoc(a), w1, w1a, w2, w2a, w3, w3a };
	_setSpawnInfo(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetTeamCount(teams) -- TODO: test
PyObject *sSetTeamCount(PyObject *self, PyObject *args)
{
	int t;
	PyArg_ParseTuple(args, "i", &t);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), t };
	_setTeamCount(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetTimer -- we only use SetTimerEx
// SetTimerEx(funcname[], interval, repeating, format, ...)
PyObject *sSetTimer(PyObject *self, PyObject *args)
{
	static long nextid = 1;

	struct timer_data data;

	char tmp_repeating;

	data.params = NULL; // required for checking for the optional parameter
	PyArg_ParseTuple(args, "Oib|O", &data.func, &data.interval, &tmp_repeating, &data.params);

	if(PyErr_Occurred() != NULL)
		return NULL;

	data.id = nextid++; // use nextid and increment it
	data.repeating = tmp_repeating == 1;

	data.lasttick = GetTickCount(); // initialize last tick
	
	// as we need the function object and the params tuple in OnTimerTick, increase its reference count
	Py_INCREF(data.func);
	Py_XINCREF(data.params);

	m_MainLock->Lock();
	// add to list
	m_TimerList.push_back(data);
	m_MainLock->Unlock();

	return Py_BuildValue("l", data.id);
}
// SetVehicleAngularVelocity(vehicleid, Float:x, Float:y, Float:z)
PyObject *sSetVehicleAngularVelocity(PyObject *self, PyObject *args)
{
	int vid;
	float x, y, z;
	PyArg_ParseTuple(args, "ifff", &vid, &x, &y, &z);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), vid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z) };
	_setVehicleAngularVelocity(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetVehicleHealth(vehicleid, Float:health)
PyObject *sSetVehicleHealth(PyObject *self, PyObject *args)
{
	int vid;
	float h;
	PyArg_ParseTuple(args, "if", &vid, &h);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), vid, amx_ftoc(h) };
	_setVehicleHealth(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetVehicleNumberPlate(vehicleid, numberplate[])
PyObject *sSetVehicleNumberPlate(PyObject *self, PyObject *args)
{
	int vid;
	char *plate = NULL;
	// No conversion, should always be ASCII.
	PyArg_ParseTuple(args, "is", &vid, &plate);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), vid, 0 };

	cell *strplate;
	amx_Allot(m_AMX, strlen(plate) + 1, amxargs + 2, &strplate);
	amx_SetString(strplate, plate, 0, 0, strlen(plate) + 1);

	_setVehicleNumberPlate(m_AMX, amxargs);

	amx_Release(m_AMX, amxargs[2]);
	Py_RETURN_NONE;
}
// SetVehicleParamsEx(vehicleid, engine, lights, alarm, doors, bonnet, boot, objective) -- 0.3c
PyObject *sSetVehicleParamsEx(PyObject *self, PyObject *args)
{
	int vid, eng, lig, alm, doors, bonnet, boot, obj;
	PyArg_ParseTuple(args, "iiiiiiii", &vid, &eng, &lig, &alm, &doors, &bonnet, &boot, &obj);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[9] = { 8 * sizeof(cell), vid, eng, lig, alm, doors, bonnet, boot, obj };
	_setVehicleParamsEx(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetVehicleParamsForPlayer(vehicleid,playerid,objective,doorslocked)
PyObject *sSetVehicleParamsForPlayer(PyObject *self, PyObject *args)
{
	int vid, pid, obj, doors;
	PyArg_ParseTuple(args, "iiii", &vid, &pid, &obj, &doors);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), vid, pid, obj, doors };
	_setVehicleParamsForPlayer(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetVehiclePos(vehicleid, Float:x, Float:y, Float:z)
PyObject *sSetVehiclePos(PyObject *self, PyObject *args)
{
	int vid;
	float x, y, z;
	PyArg_ParseTuple(args, "ifff", &vid, &x, &y, &z);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), vid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z) };
	_setVehiclePos(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetVehicleToRespawn(vehicleid)
PyObject *sSetVehicleToRespawn(PyObject *self, PyObject *args)
{
	int vid;
	PyArg_ParseTuple(args, "i", &vid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), vid };
	_setVehicleToRespawn(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetVehicleVelocity(vehicleid, Float:x, Float:y, Float:z)
PyObject *sSetVehicleVelocity(PyObject *self, PyObject *args)
{
	int vid;
	float x, y, z;
	PyArg_ParseTuple(args, "ifff", &vid, &x, &y, &z);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[5] = { 4 * sizeof(cell), vid, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z) };
	_setVehicleVelocity(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetVehicleVirtualWorld(vehicleid,worldid)
PyObject *sSetVehicleVirtualWorld(PyObject *self, PyObject *args)
{
	int vid, wid;
	PyArg_ParseTuple(args, "ii", &vid, &wid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), vid, wid };
	_setVehicleVirtualWorld(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetVehicleZAngle(vehicleid, Float:z_angl)
PyObject *sSetVehicleZAngle(PyObject *self, PyObject *args)
{
	int vid;
	float zang;
	PyArg_ParseTuple(args, "if", &vid, &zang);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), vid, amx_ftoc(zang) };
	_setVehicleZAngle(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetWeather(weatherid)
PyObject *sSetWeather(PyObject *self, PyObject *args)
{
	int wid;
	PyArg_ParseTuple(args, "i", &wid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), wid };
	_setWeather(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SetWorldTime(hour)
PyObject *sSetWorldTime(PyObject *self, PyObject *args)
{
	int h;
	PyArg_ParseTuple(args, "i", &h);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), h };
	_setWorldTime(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// Setproperty -- no
// ShowMenuForPlayer(menuid, playerid) -- TODO: test
PyObject *sShowMenuForPlayer(PyObject *self, PyObject *args)
{
	int mid, pid;
	PyArg_ParseTuple(args, "ii", &mid, &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), mid, pid };
	_showMenuForPlayer(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// ShowNameTags(enabled) -- TODO: test
PyObject *sShowNameTags(PyObject *self, PyObject *args)
{
	int en;
	PyArg_ParseTuple(args, "i", &en);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), en };
	_showNameTags(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// ShowPlayerDialog(playerid, dialogid, style, caption[], info[], button1[], button2[])
PyObject *sShowPlayerDialog(PyObject *self, PyObject *args)
{
	int pid, did, style;
	char *caption = NULL, *info = NULL, *btn1 = NULL, *btn2 = NULL;
	PyArg_ParseTuple(args, "iiiO&O&O&O&", &pid, &did, &style, _stringToCP1252, &caption, _stringToCP1252, &info, _stringToCP1252, &btn1, _stringToCP1252, &btn2);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[8] = { 7 * sizeof(cell), pid, did, style, 0, 0, 0, 0 };

	cell *strcaption, *strinfo, *strbtn1, *strbtn2;

	amx_Allot(m_AMX, strlen(caption) + 1, amxargs + 4, &strcaption);
	amx_SetString(strcaption, caption, 0, 0, strlen(caption) + 1);
	amx_Allot(m_AMX, strlen(info) + 1, amxargs + 5, &strinfo);
	amx_SetString(strinfo, info, 0, 0, strlen(info) + 1);
	amx_Allot(m_AMX, strlen(btn1) + 1, amxargs + 6, &strbtn1);
	amx_SetString(strbtn1, btn1, 0, 0, strlen(btn1) + 1);
	amx_Allot(m_AMX, strlen(btn2) + 1, amxargs + 7, &strbtn2);
	amx_SetString(strbtn2, btn2, 0, 0, strlen(btn2) + 1);

	_showPlayerDialog(m_AMX, amxargs);

	free(caption);
	free(info);
	free(btn1);
	free(btn2);
	amx_Release(m_AMX, amxargs[4]); amx_Release(m_AMX, amxargs[5]); amx_Release(m_AMX, amxargs[6]); amx_Release(m_AMX, amxargs[7]);
	Py_RETURN_NONE;
}
// ShowPlayerMarkers(mode) -- TODO: test
PyObject *sShowPlayerMarkers(PyObject *self, PyObject *args)
{
	int mode;
	PyArg_ParseTuple(args, "i", &mode);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), mode };
	_showPlayerMarkers(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// ShowPlayerNameTagForPlayer(playerid, showplayerid, show) -- TODO: test
PyObject *sShowPlayerNameTagForPlayer(PyObject *self, PyObject *args)
{
	int pid, spid, show;
	PyArg_ParseTuple(args, "iii", &pid, &spid, &show);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), pid, spid, show };
	_showPlayerNameTagForPlayer(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// SpawnPlayer(playerid)
PyObject *sSpawnPlayer(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	_spawnPlayer(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// StartRecordingPlayerData(playerid, recordtype, recordname[]) -- TODO: test
PyObject *sStartRecordingPlayerData(PyObject *self, PyObject *args)
{
	int playerid, rectype;
	char *recname;
	// No conversion, should always be ASCII.
	PyArg_ParseTuple(args, "iis", &playerid, &rectype, &recname);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), playerid, rectype, 0 };

	int len = strlen(recname) + 1;
	cell *strrecname;
	amx_Allot(m_AMX, len, amxargs + 3, &strrecname);
	amx_SetString(strrecname, recname, 0, 0, len);

	_startRecordingPlayerData(m_AMX, amxargs);

	amx_Release(m_AMX, amxargs[3]);

	Py_RETURN_NONE;
}
// StopAudioStreamForPlayer(playerid)
PyObject *sStopAudioStreamForPlayer(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	_stopAudioStreamForPlayer(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// StopObject(objectid) -- TODO: test
PyObject *sStopObject(PyObject *self, PyObject *args)
{
	int oid;
	PyArg_ParseTuple(args, "i", &oid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), oid };
	_stopObject(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// StopPlayerHoldingObject(playerid) -- will be removed in 0.3c
PyObject *sStopPlayerHoldingObject(PyObject *self, PyObject *args)
{
	Py_RETURN_NONE;
}
// StopPlayerObject(playerid, objectid) -- TODO: test
PyObject *sStopPlayerObject(PyObject *self, PyObject *args)
{
	int pid, oid;
	PyArg_ParseTuple(args, "ii", &pid, &oid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, oid };
	_stopPlayerObject(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// StopRecordingPlayerData(playerid) -- TODO: test
PyObject *sStopRecordingPlayerData(PyObject *self, PyObject *args)
{
	int pid;
	PyArg_ParseTuple(args, "i", &pid);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), pid };
	_stopRecordingPlayerData(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// string functions -- Python has its own string functions

// TextDrawAlignment(Text:text, alignment) -- TODO: test
PyObject *sTextDrawAlignment(PyObject *self, PyObject *args)
{
	int text, alig;
	PyArg_ParseTuple(args, "ii", &text, &alig);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), text, alig };
	_textDrawAlignment(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// TextDrawBackgroundColor(Text:text, color) -- TODO: test
PyObject *sTextDrawBackgroundColor(PyObject *self, PyObject *args)
{
	int text;
	PyObject *color;
	PyArg_ParseTuple(args, "iO", &text, &color);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);

	cell amxargs[3] = { 2 * sizeof(cell), text, colcode };
	_textDrawBackgroundColor(m_AMX, amxargs);

	//_del(col);
	Py_RETURN_NONE;
}
// TextDrawBoxColor(Text:text, color) -- TODO: test
PyObject *sTextDrawBoxColor(PyObject *self, PyObject *args)
{
	int text;
	PyObject *color;
	PyArg_ParseTuple(args, "iO", &text, &color);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);

	cell amxargs[3] = { 2 * sizeof(cell), text, colcode };
	_textDrawBoxColor(m_AMX, amxargs);

	//_del(col);
	Py_RETURN_NONE;
}
// TextDrawColor(Text:text, color) -- TODO: test
PyObject *sTextDrawColor(PyObject *self, PyObject *args)
{
	int text;
	PyObject *color;
	PyArg_ParseTuple(args, "iO", &text, &color);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);

	cell amxargs[3] = { 2 * sizeof(cell), text, colcode };
	_textDrawColor(m_AMX, amxargs);

	//_del(col);
	Py_RETURN_NONE;
}
// int TextDrawCreate(Float:x, Float:y, text[]) -- TODO: test
PyObject *sTextDrawCreate(PyObject *self, PyObject *args)
{
	float x, y;
	char *txt = NULL;
	PyArg_ParseTuple(args, "ffO&", &x, &y, _stringToCP1252, &txt);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), amx_ftoc(x), amx_ftoc(y), 0 };

	cell *strtxt;
	amx_Allot(m_AMX, strlen(txt) + 1, amxargs + 3, &strtxt);
	amx_SetString(strtxt, txt, 0, 0, strlen(txt) + 1);

	cell ret = _textDrawCreate(m_AMX, amxargs);

	free(txt);
	amx_Release(m_AMX, amxargs[3]);
	return Py_BuildValue("i", ret);
}
// TextDrawDestroy(Text:text) -- TODO: test
PyObject *sTextDrawDestroy(PyObject *self, PyObject *args)
{
	int txt;
	PyArg_ParseTuple(args, "i", &txt);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), txt };
	_textDrawDestroy(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// TextDrawFont(Text:text, font) -- TODO: test
PyObject *sTextDrawFont(PyObject *self, PyObject *args)
{
	int txt, font;
	PyArg_ParseTuple(args, "ii", &txt, &font);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), txt, font };
	_textDrawFont(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// TextDrawHideForAll(Text:text) -- TODO: test
PyObject *sTextDrawHideForAll(PyObject *self, PyObject *args)
{
	int txt;
	PyArg_ParseTuple(args, "i", &txt);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), txt };
	_textDrawHideForAll(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// TextDrawHideForPlayer(playerid, Text:text) -- TODO: test
PyObject *sTextDrawHideForPlayer(PyObject *self, PyObject *args)
{
	int pid, txt;
	PyArg_ParseTuple(args, "ii", &pid, &txt);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, txt };
	_textDrawHideForPlayer(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// TextDrawLetterSize(Text:text, Float:x, Float:y) -- TODO: test
PyObject *sTextDrawLetterSize(PyObject *self, PyObject *args)
{
	int txt;
	float x, y;
	PyArg_ParseTuple(args, "iff", &txt, &x, &y);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), txt, amx_ftoc(x), amx_ftoc(y) };
	_textDrawLetterSize(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// TextDrawSetOutline(Text:text, size) -- TODO: test
PyObject *sTextDrawSetOutline(PyObject *self, PyObject *args)
{
	int txt, size;
	PyArg_ParseTuple(args, "ii", &txt, &size);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), txt, size };
	_textDrawSetOutline(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// TextDrawSetPreviewModel(Text:text, modelindex) -- TODO: test
PyObject *sTextDrawSetPreviewModel(PyObject *self, PyObject *args)
{
	int txt, midx;
	PyArg_ParseTuple(args, "ii", &txt, &midx);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), txt, midx };
	_textDrawSetPreviewModel(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// TextDrawSetPreviewRot(Text:text, Float:fRotX, Float:fRotY, Float:fRotZ, Float:fZoom) -- TODO: test
PyObject *sTextDrawSetPreviewRot(PyObject *self, PyObject *args)
{
	int txt;
	float x, y, z, zoom;
	PyArg_ParseTuple(args, "iffff", &txt, &x, &y, &z, &zoom);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[6] = { 5 * sizeof(cell), txt, amx_ftoc(x), amx_ftoc(y), amx_ftoc(z), amx_ftoc(zoom) };
	_textDrawSetPreviewRot(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// TextDrawSetPreviewVehCol(Text:text, color1, color2) -- TODO: test
PyObject *sTextDrawSetPreviewVehCol(PyObject *self, PyObject *args)
{
	int txt, color1, color2;
	PyArg_ParseTuple(args, "iii", &txt, &color1, &color2);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), txt, color1, color2 };
	_textDrawSetPreviewVehCol(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// TextDrawSetProportional(Text:text, set) -- TODO: test
PyObject *sTextDrawSetProportional(PyObject *self, PyObject *args)
{
	int txt, set;
	PyArg_ParseTuple(args, "ii", &txt, &set);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), txt, set };
	_textDrawSetProportional(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// TextDrawSetSelectable(Text:text, set)
PyObject *sTextDrawSetSelectable(PyObject *self, PyObject *args)
{
	int txt, set;
	PyArg_ParseTuple(args, "ii", &txt, &set);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), txt, set };
	_textDrawSetSelectable(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// TextDrawSetShadow(Text:text, size) -- TODO: test
PyObject *sTextDrawSetShadow(PyObject *self, PyObject *args)
{
	int txt, size;
	PyArg_ParseTuple(args, "ii", &txt, &size);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), txt, size };
	_textDrawSetShadow(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// TextDrawSetString(Text:text, string[]) -- TODO: test
PyObject *sTextDrawSetString(PyObject *self, PyObject *args)
{
	int txt;
	char *string = NULL;
	PyArg_ParseTuple(args, "iO&", &txt, _stringToCP1252, &string);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), txt, 0 };

	cell *strstring;
	amx_Allot(m_AMX, strlen(string) + 1, amxargs + 2, &strstring);
	amx_SetString(strstring, string, 0, 0, strlen(string) + 1);

	_textDrawSetString(m_AMX, amxargs);

	free(string);
	amx_Release(m_AMX, amxargs[2]);
	Py_RETURN_NONE;
}
// TextDrawShowForAll(Text:text) -- TODO: test
PyObject *sTextDrawShowForAll(PyObject *self, PyObject *args)
{
	int txt;
	PyArg_ParseTuple(args, "i", &txt);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[2] = { sizeof(cell), txt };
	_textDrawShowForAll(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// TextDrawShowForPlayer(playerid, Text:text) -- TODO: test
PyObject *sTextDrawShowForPlayer(PyObject *self, PyObject *args)
{
	int pid, txt;
	PyArg_ParseTuple(args, "ii", &pid, &txt);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, txt };
	_textDrawShowForPlayer(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// TextDrawTextSize(Text:text, Float:x, Float:y) -- TODO: test
PyObject *sTextDrawTextSize(PyObject *self, PyObject *args)
{
	int txt;
	float x, y;
	PyArg_ParseTuple(args, "iff", &txt, &x, &y);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[4] = { 3 * sizeof(cell), txt, amx_ftoc(x), amx_ftoc(y) };
	_textDrawTextSize(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// TextDrawUseBox(Text:text, use) -- TODO: test
PyObject *sTextDrawUseBox(PyObject *self, PyObject *args)
{
	int txt, use;
	PyArg_ParseTuple(args, "ii", &txt, &use);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), txt, use };
	_textDrawUseBox(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// Tickcount -- no
// TogglePlayerClock(playerid, toggle)
PyObject *sTogglePlayerClock(PyObject *self, PyObject *args)
{
	int pid, tog;
	PyArg_ParseTuple(args, "ii", &pid, &tog);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, tog };
	_togglePlayerClock(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// TogglePlayerControllable(playerid, toggle)
PyObject *sTogglePlayerControllable(PyObject *self, PyObject *args)
{
	int pid, tog;
	PyArg_ParseTuple(args, "ii", &pid, &tog);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, tog };
	_togglePlayerControllable(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// TogglePlayerSpectating(playerid, toggle)
PyObject *sTogglePlayerSpectating(PyObject *self, PyObject *args)
{
	int pid, tog;
	PyArg_ParseTuple(args, "ii", &pid, &tog);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[3] = { 2 * sizeof(cell), pid, tog };
	_togglePlayerSpectating(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// Update3DTextLabelText(Text3D:id, color, text[]) -- TODO: test
PyObject *sUpdate3DTextLabelText(PyObject *self, PyObject *args)
{
	int t3d;
	PyObject *color;
	char *txt = NULL;
	PyArg_ParseTuple(args, "iOO&", &t3d, &color, _stringToCP1252, &txt);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);

	cell amxargs[4] = { 3 * sizeof(cell), t3d, colcode, 0 };

	cell *strtxt;
	amx_Allot(m_AMX, strlen(txt) + 1, amxargs + 3, &strtxt);
	amx_SetString(strtxt, txt, 0, 0, strlen(txt) + 1);

	_update3DTextLabelText(m_AMX, amxargs);

	free(txt);
	amx_Release(m_AMX, amxargs[3]);
	Py_RETURN_NONE;
}
// UpdatePlayer3DTextLabelText(playerid, PlayerText3D:id, color, text[]) -- TODO: test
PyObject *sUpdatePlayer3DTextLabelText(PyObject *self, PyObject *args)
{
	int pid, t3d;
	PyObject *color;
	char *txt = NULL;
	PyArg_ParseTuple(args, "iiOO&", &pid, &t3d, &color, _stringToCP1252, &txt);

	if(PyErr_Occurred() != NULL)
		return NULL;

	_getColor(color);

	cell amxargs[5] = { 4 * sizeof(cell), pid, t3d, colcode, 0 };

	cell *strtxt;
	amx_Allot(m_AMX, strlen(txt) + 1, amxargs + 4, &strtxt);
	amx_SetString(strtxt, txt, 0, 0, strlen(txt) + 1);

	_updatePlayer3DTextLabelText(m_AMX, amxargs);

	free(txt);
	amx_Release(m_AMX, amxargs[4]);
	Py_RETURN_NONE;
}
// UpdateVehicleDamageStatus(vehicleid, panels, doors, lights, tires)
PyObject *sUpdateVehicleDamageStatus(PyObject *self, PyObject *args)
{
	int vid, pan, doors, lig, tires;
	PyArg_ParseTuple(args, "iiiii", &vid, &pan, &doors, &lig, &tires);

	if(PyErr_Occurred() != NULL)
		return NULL;

	cell amxargs[6] = { 5 * sizeof(cell), vid, pan, doors, lig, tires };
	_updateVehicleDamageStatus(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// UsePlayerPedAnims() -- TODO: test
PyObject *sUsePlayerPedAnims(PyObject *self, PyObject *args)
{
	cell amxargs[1] = { 0 };
	_usePlayerPedAnims(m_AMX, amxargs);
	Py_RETURN_NONE;
}
// Uudecode -- ?
// Uuencode -- ?

// InvokeFunction(func, params = NULL)
PyObject *sInvokeFunction(PyObject *self, PyObject *args)
{
	invoke_data tmp = { NULL, NULL };
	PyArg_ParseTuple(args, "O|O", &tmp.func, &tmp.params);

	if(PyErr_Occurred() != NULL)
		return NULL;
	
	Py_INCREF(tmp.func);
	Py_XINCREF(tmp.params);

	m_MainLock->Lock();
	m_InvokeQueue.push(tmp);
	m_MainLock->Unlock();

	Py_RETURN_NONE;
}


//-----------------------------------------
// callbacks -- no check for incorrect parameters!
//-----------------------------------------

/*cell AMX_NATIVE_CALL n_OnTimerTick(AMX *amx, cell *params)
{
	char *timerid = ""; //_getString(m_AMX, params[1]);
	short id = atoi(timerid);
	free(timerid);

	// find tid
	for (std::deque<timer_data>::iterator i = m_TimerList.begin(); i != m_TimerList.end(); i++)
	{
		if (i->id == id)
		{
			// we need to store the pointer to this somewhere else, because timers might be set/killed in here
			timer_data tmp = (timer_data)*i;

			if (!i->repeating)
			{
				// remove it from the list
				m_TimerList.erase(i);
			}

			_pyCallObject(tmp.func, tmp.params);

			if (!tmp.repeating) // here we can free memory used by the timer
			{
				Py_DECREF(tmp.func);
				Py_XDECREF(tmp.params);
			}
			return 0;
		}
	}

	return 0;
}*/

// OnDialogResponse(playerid, dialogid, response, listitem, inputtext[])
cell AMX_NATIVE_CALL n_OnDialogResponse(AMX *amx, cell *params)
{
	int playerid = params[1], dialogid = params[2], response = params[3], listitem = params[4];
	char *str = _getString(amx, params[5]);

	PyEnsureGIL;
	PyObject *o = Py_BuildValue("iiiiN", playerid, dialogid, response, listitem, PyUnicode_Decode(str, strlen(str), "cp1252", "strict"));

	int ret = _pyCallAll("OnDialogResponse", o, 1, 0);
	Py_DECREF(o);
	PyReleaseGIL;

	_del(str);
	return ret;
}
// OnEnterExitModShop(playerid, enterexit, interiorid)
cell AMX_NATIVE_CALL n_OnEnterExitModShop(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("iii", params[1], params[2], params[3]);
	int ret = _pyCallAll("OnEnterExitModShop", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnFilterScriptExit
// OnFilterScriptInit
// OnGameModeExit
// OnGameModeInit
// OnObjectMoved(objectid) -- TODO: test
cell AMX_NATIVE_CALL n_OnObjectMoved(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(i)", params[1]);
	int ret = _pyCallAll("OnObjectMoved", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerClickPlayer(playerid, clickedplayerid, source)
cell AMX_NATIVE_CALL n_OnPlayerClickPlayer(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(iii)", params[1], params[2], params[3]);
	int ret = _pyCallAll("OnPlayerClickPlayer", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerClickTextDraw(playerid, Text:clickedid) -- TODO: test
cell AMX_NATIVE_CALL n_OnPlayerClickTextDraw(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(ii)", params[1], params[2]);
	int ret = _pyCallAll("OnPlayerClickTextDraw", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerClickPlayerTextDraw(playerid, PlayerText:playertextid) -- TODO: test
cell AMX_NATIVE_CALL n_OnPlayerClickPlayerTextDraw(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(ii)", params[1], params[2]);
	int ret = _pyCallAll("OnPlayerClickPlayerTextDraw", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerCommandText(playerid,cmdtext[])
cell AMX_NATIVE_CALL n_OnPlayerCommandText(AMX *amx, cell *params)
{
	int playerid = params[1];
	char *cmd = _getString(m_AMX, params[2]);

	PyEnsureGIL;
	PyObject *o = Py_BuildValue("iN", playerid, PyUnicode_Decode(cmd, strlen(cmd), "cp1252", "strict"));
	int ret = _pyCallAll("OnPlayerCommandText", o, 1, 0);
	Py_DECREF(o);
	PyReleaseGIL;

	_del(cmd);
	
	return ret;
}
// OnPlayerConnect(playerid)
cell AMX_NATIVE_CALL n_OnPlayerConnect(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(i)", params[1]);
	int ret = _pyCallAll("OnPlayerConnect", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerDeath(playerid, killerid, reason)
cell AMX_NATIVE_CALL n_OnPlayerDeath(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(iii)", params[1], params[2], params[3]);
	int ret = _pyCallAll("OnPlayerDeath", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerDisconnect(playerid, reason) -- TODO: test
cell AMX_NATIVE_CALL n_OnPlayerDisconnect(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(ii)", params[1], params[2]);
	int ret = _pyCallAll("OnPlayerDisconnect", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerEditObject(playerid, playerobject, objectid, response, Float:fX, Float:fY, Float:fZ, Float:fRotX, Float:fRotY, Float:fRotZ) -- TODO: test
cell AMX_NATIVE_CALL n_OnPlayerEditObject(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("iiiiffffff", params[1], params[2], params[3], params[4], amx_ctof(params[5]), amx_ctof(params[6]), amx_ctof(params[7]), amx_ctof(params[8]), amx_ctof(params[9]), amx_ctof(params[10]));
	int ret = _pyCallAll("OnPlayerEditObject", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerEditAttachedObject(playerid, response, index, modelid, boneid, Float:fOffsetX, Float:fOffsetY, Float:fOffsetZ, Float:fRotX, Float:fRotY, Float:fRotZ, Float:fScaleX, Float:fScaleY, Float:fScaleZ) -- TODO: test
cell AMX_NATIVE_CALL n_OnPlayerEditAttachedObject(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("iiiiiffffff", params[1], params[2], params[3], params[4], params[5], amx_ctof(params[6]), amx_ctof(params[7]), amx_ctof(params[8]), amx_ctof(params[9]), amx_ctof(params[10]), amx_ctof(params[11]), amx_ctof(params[12]), amx_ctof(params[13]), amx_ctof(params[14]));
	int ret = _pyCallAll("OnPlayerEditAttachedObject", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerEnterCheckpoint(playerid) -- TODO: test
cell AMX_NATIVE_CALL n_OnPlayerEnterCheckpoint(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(i)", params[1]);
	int ret = _pyCallAll("OnPlayerEnterCheckpoint", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerEnterRaceCheckpoint(playerid) -- TODO: test
cell AMX_NATIVE_CALL n_OnPlayerEnterRaceCheckpoint(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(i)", params[1]);
	int ret = _pyCallAll("OnPlayerEnterRaceCheckpoint", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerEnterVehicle(playerid, vehicleid, ispassenger)
cell AMX_NATIVE_CALL n_OnPlayerEnterVehicle(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("iii", params[1], params[2], params[3]);
	int ret = _pyCallAll("OnPlayerEnterVehicle", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerExitVehicle(playerid, vehicleid)
cell AMX_NATIVE_CALL n_OnPlayerExitVehicle(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("ii", params[1], params[2]);
	int ret = _pyCallAll("OnPlayerExitVehicle", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerExitedMenu(playerid) -- TODO: test
cell AMX_NATIVE_CALL n_OnPlayerExitedMenu(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(i)", params[1]);
	int ret = _pyCallAll("OnPlayerExitedMenu", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerInteriorChange(playerid, newinteriorid, oldinteriorid) -- TODO: test
cell AMX_NATIVE_CALL n_OnPlayerInteriorChange(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(iii)", params[1], params[2], params[3]);
	int ret = _pyCallAll("OnPlayerInteriorChange", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerKeyStateChange(playerid, newkeys, oldkeys)
cell AMX_NATIVE_CALL n_OnPlayerKeyStateChange(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(iii)", params[1], params[2], params[3]);
	int ret = _pyCallAll("OnPlayerKeyStateChange", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerLeaveCheckpoint(playerid) -- TODO: test
cell AMX_NATIVE_CALL n_OnPlayerLeaveCheckpoint(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(i)", params[1]);
	int ret = _pyCallAll("OnPlayerLeaveCheckpoint", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerLeaveRaceCheckpoint(playerid) -- TODO: test
cell AMX_NATIVE_CALL n_OnPlayerLeaveRaceCheckpoint(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(i)", params[1]);
	int ret = _pyCallAll("OnPlayerLeaveRaceCheckpoint", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerObjectMoved(playerid, objectid) -- TODO: test
cell AMX_NATIVE_CALL n_OnPlayerObjectMoved(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(ii)", params[1], params[2]);
	int ret = _pyCallAll("OnPlayerObjectMoved", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerPickUpPickup(playerid, pickupid) -- TODO: test
cell AMX_NATIVE_CALL n_OnPlayerPickUpPickup(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(ii)", params[1], params[2]);
	int ret = _pyCallAll("OnPlayerPickUpPickup", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerPrivmsg -- removed
// OnPlayerRequestClass(playerid, classid)
cell AMX_NATIVE_CALL n_OnPlayerRequestClass(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(ii)", params[1], params[2]);
	int ret = _pyCallAll("OnPlayerRequestClass", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerRequestSpawn(playerid)
cell AMX_NATIVE_CALL n_OnPlayerRequestSpawn(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(i)", params[1]);
	int ret = _pyCallAll("OnPlayerRequestSpawn", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerSelectedMenuRow(playerid, row) -- TODO: test
cell AMX_NATIVE_CALL n_OnPlayerSelectedMenuRow(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(ii)", params[1], params[2]);
	int ret = _pyCallAll("OnPlayerSelectedMenuRow", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerSelectObject(playerid, type, objectid, modelid, Float:fX, Float:fY, Float:fZ) -- TODO: test
cell AMX_NATIVE_CALL n_OnPlayerSelectObject(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("iiiifff", params[1], params[2], params[3], params[4], amx_ctof(params[5]), amx_ctof(params[6]), amx_ctof(params[7]));
	int ret = _pyCallAll("OnPlayerSelectObject", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerSpawn(playerid)
cell AMX_NATIVE_CALL n_OnPlayerSpawn(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(i)", params[1]);
	int ret = _pyCallAll("OnPlayerSpawn", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerStateChange(playerid, newstate, oldstate)
cell AMX_NATIVE_CALL n_OnPlayerStateChange(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(iii)", params[1], params[2], params[3]);
	int ret = _pyCallAll("OnPlayerStateChange", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerStreamIn(playerid, forplayerid) -- TODO: test
cell AMX_NATIVE_CALL n_OnPlayerStreamIn(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(ii)", params[1], params[2]);
	int ret = _pyCallAll("OnPlayerStreamIn", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerStreamOut(playerid, forplayerid) -- TODO: test
cell AMX_NATIVE_CALL n_OnPlayerStreamOut(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(ii)", params[1], params[2]);
	int ret = _pyCallAll("OnPlayerStreamOut", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerTeamPrivmsg -- removed
// OnPlayerText(playerid, text[])
cell AMX_NATIVE_CALL n_OnPlayerText(AMX *amx, cell *params)
{
	char *txt = _getString(m_AMX, params[2]);

	PyEnsureGIL;
	PyObject *o = Py_BuildValue("iN", params[1], PyUnicode_Decode(txt, strlen(txt), "cp1252", "strict"));
	int ret = _pyCallAll("OnPlayerText", o);
	Py_DECREF(o);
	PyReleaseGIL;

	_del(txt);
	return ret;
}
// OnPlayerUpdate(playerid)
cell AMX_NATIVE_CALL n_OnPlayerUpdate(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(i)", params[1]);
	int ret = _pyCallAll("OnPlayerUpdate", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnRconCommand(cmd[]) -- TODO: test
cell AMX_NATIVE_CALL n_OnRconCommand(AMX *amx, cell *params)
{
	char *cmd = _getString(m_AMX, params[1]);

	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(N)", PyUnicode_Decode(cmd, strlen(cmd), "cp1252", "strict"));
	int ret = _pyCallAll("OnRconCommand", o);
	Py_DECREF(o);
	PyReleaseGIL;
	
	_del(cmd);
	return ret;
}
// OnRconLoginAttempt(ip[], password[], success) -- TODO: test
cell AMX_NATIVE_CALL n_OnRconLoginAttempt(AMX *amx, cell *params)
{
	char *ip = _getString(m_AMX, params[1]);
	char *pass = _getString(m_AMX, params[2]);

	PyEnsureGIL;
	PyObject *o = Py_BuildValue("ssi", ip, pass, params[3]);

	if(o == NULL)
		return 0;

	int ret = _pyCallAll("OnRconLoginAttempt", o);
	Py_DECREF(o);
	PyReleaseGIL;
	
	_del(ip);
	_del(pass);
	return ret;
}
// OnVehicleDamageStatusUpdate(vehicleid, playerid)
cell AMX_NATIVE_CALL n_OnVehicleDamageStatusUpdate(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("ii", params[1], params[2]);
	int ret = _pyCallAll("OnVehicleDamageStatusUpdate", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnVehicleDeath(vehicleid, killerid)
cell AMX_NATIVE_CALL n_OnVehicleDeath(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("ii", params[1], params[2]);
	int ret = _pyCallAll("OnVehicleDeath", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnVehicleMod(playerid, vehicleid, componentid)
cell AMX_NATIVE_CALL n_OnVehicleMod(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("iii", params[1], params[2], params[3]);
	int ret = _pyCallAll("OnVehicleMod", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnVehiclePaintjob(playerid, vehicleid, paintjobid)
cell AMX_NATIVE_CALL n_OnVehiclePaintjob(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("iii", params[1], params[2], params[3]);
	int ret = _pyCallAll("OnVehiclePaintjob", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnVehicleRespray(playerid, vehicleid, color1, color2)
cell AMX_NATIVE_CALL n_OnVehicleRespray(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("iiii", params[1], params[2], params[3], params[4]);
	int ret = _pyCallAll("OnVehicleRespray", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnVehicleSpawn(vehicleid)
cell AMX_NATIVE_CALL n_OnVehicleSpawn(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("(i)", params[1]);
	int ret = _pyCallAll("OnVehicleSpawn", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnVehicleStreamIn(vehicleid, forplayerid)
cell AMX_NATIVE_CALL n_OnVehicleStreamIn(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("ii", params[1], params[2]);
	int ret = _pyCallAll("OnVehicleStreamIn", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnVehicleStreamOut(vehicleid, forplayerid)
cell AMX_NATIVE_CALL n_OnVehicleStreamOut(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("ii", params[1], params[2]);
	int ret = _pyCallAll("OnVehicleStreamOut", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnUnoccupiedVehicleUpdate(vehicleid, playerid, passenger_seat)
cell AMX_NATIVE_CALL n_OnUnoccupiedVehicleUpdate(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("iii", params[1], params[2], params[3]);
	int ret = _pyCallAll("OnUnoccupiedVehicleUpdate", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerTakeDamage(playerid, issuerid, Float:amount, weaponid);
cell AMX_NATIVE_CALL n_OnPlayerTakeDamage(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("iifi", params[1], params[2], amx_ctof(params[3]), params[4]);
	int ret = _pyCallAll("OnPlayerTakeDamage", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerGiveDamage(playerid, issuerid, Float:amount, weaponid);
cell AMX_NATIVE_CALL n_OnPlayerGiveDamage(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("iifi", params[1], params[2], amx_ctof(params[3]), params[4]);
	int ret = _pyCallAll("OnPlayerGiveDamage", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
// OnPlayerClickMap(playerid, Float:fX, Float:fY, Float:fZ)
cell AMX_NATIVE_CALL n_OnPlayerClickMap(AMX *amx, cell *params)
{
	PyEnsureGIL;
	PyObject *o = Py_BuildValue("ifff", params[1], amx_ctof(params[2]), amx_ctof(params[3]), amx_ctof(params[4]));
	int ret = _pyCallAll("OnPlayerClickMap", o);
	Py_DECREF(o);
	PyReleaseGIL;

	return ret;
}
