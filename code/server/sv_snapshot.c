/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "server.h"


/*
=============================================================================

Delta encode a client frame onto the network channel

A normal server packet will look like:

4	sequence number (high bit set if an oversize fragment)
<optional reliable commands>
1	svc_snapshot
4	last client reliable command
4	serverTime
1	lastframe for delta compression
1	snapFlags
1	areaBytes
<areabytes>
<playerstate>
<packetentities>

=============================================================================
*/

/*
=============
SV_EmitPacketEntities

Writes a delta update of an entityState_t list to the message.
=============
*/
static void SV_EmitPacketEntities( clientSnapshot_t *from, clientSnapshot_t *to, msg_t *msg ) {
	entityState_t	*oldent, *newent;
	int		oldindex, newindex;
	int		oldnum, newnum;
	int		from_num_entities;

	// generate the delta update
	if ( !from ) {
		from_num_entities = 0;
	} else {
		from_num_entities = from->num_entities;
	}

	newent = NULL;
	oldent = NULL;
	newindex = 0;
	oldindex = 0;
	while ( newindex < to->num_entities || oldindex < from_num_entities ) {
		if ( newindex >= to->num_entities ) {
			newnum = 9999;
		} else {
			newent = &svs.snapshotEntities[(to->first_entity+newindex) % svs.numSnapshotEntities];
			newnum = newent->number;
		}

		if ( oldindex >= from_num_entities ) {
			oldnum = 9999;
		} else {
			oldent = &svs.snapshotEntities[(from->first_entity+oldindex) % svs.numSnapshotEntities];
			oldnum = oldent->number;
		}

		if ( newnum == oldnum ) {
			// delta update from old position
			// because the force parm is qfalse, this will not result
			// in any bytes being emitted if the entity has not changed at all
			MSG_WriteDeltaEntity (msg, oldent, newent, qfalse );
			oldindex++;
			newindex++;
			continue;
		}

		if ( newnum < oldnum ) {
			// this is a new entity, send it from the baseline
			MSG_WriteDeltaEntity (msg, &sv.svEntities[newnum].baseline, newent, qtrue );
			newindex++;
			continue;
		}

		if ( newnum > oldnum ) {
			// the old entity isn't present in the new message
			MSG_WriteDeltaEntity (msg, oldent, NULL, qtrue );
			oldindex++;
			continue;
		}
	}

	MSG_WriteBits( msg, (MAX_GENTITIES-1), GENTITYNUM_BITS );	// end of packetentities
}



/*
==================
SV_WriteSnapshotToClient
==================
*/
static void SV_WriteSnapshotToClient( client_t *client, msg_t *msg ) {
	clientSnapshot_t	*frame, *oldframe;
	int					lastframe;
	int					i;
	int					snapFlags;

	// this is the snapshot we are creating
	frame = &client->frames[ client->netchan.outgoingSequence & PACKET_MASK ];

	// try to use a previous frame as the source for delta compressing the snapshot
	if ( client->deltaMessage <= 0 || client->state != CS_ACTIVE ) {
		// client is asking for a retransmit
		oldframe = NULL;
		lastframe = 0;
	} else if ( client->netchan.outgoingSequence - client->deltaMessage 
		>= (PACKET_BACKUP - 3) ) {
		// client hasn't gotten a good message through in a long time
		Com_DPrintf ("%s: Delta request from out of date packet.\n", client->name);
		oldframe = NULL;
		lastframe = 0;
	} else {
		// we have a valid snapshot to delta from
		oldframe = &client->frames[ client->deltaMessage & PACKET_MASK ];
		lastframe = client->netchan.outgoingSequence - client->deltaMessage;

		// the snapshot's entities may still have rolled off the buffer, though
		if ( oldframe->first_entity <= svs.nextSnapshotEntities - svs.numSnapshotEntities ) {
			Com_DPrintf ("%s: Delta request from out of date entities.\n", client->name);
			oldframe = NULL;
			lastframe = 0;
		}
	}

	MSG_WriteByte (msg, svc_snapshot);

	// NOTE, MRE: now sent at the start of every message from server to client
	// let the client know which reliable clientCommands we have received
	//MSG_WriteLong( msg, client->lastClientCommand );

	// send over the current server time so the client can drift
	// its view of time to try to match
	if( client->oldServerTime ) {
		// The server has not yet got an acknowledgement of the
		// new gamestate from this client, so continue to send it
		// a time as if the server has not restarted. Note from
		// the client's perspective this time is strictly speaking
		// incorrect, but since it'll be busy loading a map at
		// the time it doesn't really matter.
		MSG_WriteLong (msg, sv.time + client->oldServerTime);
	} else {
		MSG_WriteLong (msg, sv.time);
	}

	// what we are delta'ing from
	MSG_WriteByte (msg, lastframe);

	snapFlags = svs.snapFlagServerBit;
	if ( client->rateDelayed ) {
		snapFlags |= SNAPFLAG_RATE_DELAYED;
	}
	if ( client->state != CS_ACTIVE ) {
		snapFlags |= SNAPFLAG_NOT_ACTIVE;
	}

	MSG_WriteByte (msg, snapFlags);

	// send over the areabits
	MSG_WriteByte (msg, frame->areabytes);
	MSG_WriteData (msg, frame->areabits, frame->areabytes);

	// delta encode the playerstate
	if ( oldframe ) {
		MSG_WriteDeltaPlayerstate( msg, &oldframe->ps, &frame->ps );
	} else {
		MSG_WriteDeltaPlayerstate( msg, NULL, &frame->ps );
	}

	// delta encode the entities
	SV_EmitPacketEntities (oldframe, frame, msg);

	// padding for rate debugging
	if ( sv_padPackets->integer ) {
		for ( i = 0 ; i < sv_padPackets->integer ; i++ ) {
			MSG_WriteByte (msg, svc_nop);
		}
	}
}


/*
==================
SV_UpdateServerCommandsToClient

(re)send all server commands the client hasn't acknowledged yet
==================
*/
void SV_UpdateServerCommandsToClient( client_t *client, msg_t *msg ) {
	int		i;

	// write any unacknowledged serverCommands
	for ( i = client->reliableAcknowledge + 1 ; i <= client->reliableSequence ; i++ ) {
		MSG_WriteByte( msg, svc_serverCommand );
		MSG_WriteLong( msg, i );
		MSG_WriteString( msg, client->reliableCommands[ i & (MAX_RELIABLE_COMMANDS-1) ] );
	}
	client->reliableSent = client->reliableSequence;
}

/*
=============================================================================

Build a client snapshot structure

=============================================================================
*/

typedef struct {
	int		numSnapshotEntities;
	int		snapshotEntities[MAX_SNAPSHOT_ENTITIES];	
} snapshotEntityNumbers_t;

/*
=======================
SV_QsortEntityNumbers
=======================
*/
static int QDECL SV_QsortEntityNumbers( const void *a, const void *b ) {
	int	*ea, *eb;

	ea = (int *)a;
	eb = (int *)b;

	if ( *ea == *eb ) {
		Com_Error( ERR_DROP, "SV_QsortEntityStates: duplicated entity" );
	}

	if ( *ea < *eb ) {
		return -1;
	}

	return 1;
}


/*
===============
SV_AddEntToSnapshot

Follows fakk2.exe (0x448000). An entity reached a second time in the same
snapshot is one seen both directly and through a portal.
===============
*/
static void SV_AddEntToSnapshot( svEntity_t *svEnt, sharedEntity_t *gEnt, snapshotEntityNumbers_t *eNums,
								 qboolean portal, qboolean sky ) {
	if ( svEnt->snapshotCounter == sv.snapshotCounter ) {
		gEnt->s.renderfx = ( gEnt->s.renderfx & ~RF_PORTALENTITY ) | RF_DUALENTITY;
		return;
	}
	svEnt->snapshotCounter = sv.snapshotCounter;

	// if we are full, silently discard entities
	if ( eNums->numSnapshotEntities == MAX_SNAPSHOT_ENTITIES ) {
		return;
	}

	gEnt->s.renderfx &= ~( RF_SKYENTITY | RF_PORTALENTITY | RF_DUALENTITY );
	if ( portal ) {
		gEnt->s.renderfx |= RF_PORTALENTITY;
	} else if ( sky ) {
		gEnt->s.renderfx |= RF_SKYENTITY;
	}

	eNums->snapshotEntities[ eNums->numSnapshotEntities ] = gEnt->s.number;
	eNums->numSnapshotEntities++;
}

/*
===============
SV_EntityParent

The entity this one is attached or bound to, if any.
===============
*/
static sharedEntity_t *SV_EntityParent( const sharedEntity_t *ent ) {
	int		parent = ent->s.parent;

	if ( parent == ENTITYNUM_NONE ) {
		parent = ent->s.bindparent;
		if ( parent == ENTITYNUM_NONE ) {
			return NULL;
		}
	}
	return SV_GentityNum( parent );
}

/*
===============
SV_AddEntitiesVisibleFromPoint

Follows fakk2.exe (0x448090). portalEnt is the portal being looked through, if
any; sky is set for the pass made from the sky portal's origin.
===============
*/
static void SV_AddEntitiesVisibleFromPoint( vec3_t origin, clientSnapshot_t *frame,
									snapshotEntityNumbers_t *eNums, svEntity_t *portalEnt, qboolean sky ) {
	int		e, i;
	sharedEntity_t *ent, *parent, *skyOrigin;
	svEntity_t	*svEnt, *visEnt;
	int		l;
	int		clientarea, clientcluster;
	int		leafnum;
	byte	*clientpvs;
	vec3_t	delta;
	float	radius;

	// during an error shutdown message we may need to transmit
	// the shutdown message after the server has shutdown, so
	// specfically check for it
	if ( !sv.state ) {
		return;
	}

	leafnum = CM_PointLeafnum (origin);
	clientarea = CM_LeafArea (leafnum);
	clientcluster = CM_LeafCluster (leafnum);

	// calculate the visible areas
	frame->areabytes = CM_WriteAreaBits( frame->areabits, clientarea );

	clientpvs = CM_ClusterPVS (clientcluster);
	skyOrigin = NULL;

	for ( e = 0 ; e < sv.num_entities ; e++ ) {
		ent = SV_GentityNum(e);

		if ( !ent->inuse ) {
			continue;
		}

		if (ent->s.number != e) {
			Com_DPrintf ("FIXING ENT->S.NUMBER!!!\n");
			ent->s.number = e;
		}

		// set for every entity in use whenever a snapshot is built, whether or
		// not the entity ends up in it
		ent->svFlags |= SVF_SENT;

		// never send entities that aren't linked in, and
		// entities can be flagged to explicitly not be sent to the client
		if ( !ent->linked || ( ent->svFlags & SVF_NOCLIENT ) ) {
			continue;
		}

		// an attached entity is not sent when its parent is hidden
		parent = SV_EntityParent( ent );
		if ( parent && ( ( parent->svFlags & SVF_NOCLIENT ) || ( parent->s.renderfx & RF_DONTDRAW ) ) ) {
			continue;
		}

		svEnt = SV_SvEntityForGentity( ent );

		if ( ent->s.renderfx & RF_SKYORIGIN ) {
			// the sky portal's camera: sent when the level uses a sky portal,
			// and everything visible from it is added afterwards
			if ( sv_skyPortal && !sky && !portalEnt ) {
				if ( skyOrigin ) {
					Com_Error( ERR_DROP, "SV_AddEntitiesVisibleFromPoint: duplicate sky origins" );
				}
				skyOrigin = ent;
				SV_AddEntToSnapshot( svEnt, ent, eNums, qfalse, qfalse );
			}
			continue;
		}

		// broadcast entities are always sent, as are ones not yet sent once
		if ( ent->svFlags & ( SVF_BROADCAST | SVF_SENDONCE ) ) {
			SV_AddEntToSnapshot( svEnt, ent, eNums, portalEnt != NULL, sky );
			continue;
		}

		// an attached entity goes wherever its parent goes, and is otherwise
		// tested for visibility at its parent's position
		visEnt = svEnt;
		if ( parent ) {
			visEnt = SV_SvEntityForGentity( parent );
			if ( visEnt->snapshotCounter == sv.snapshotCounter ) {
				SV_AddEntToSnapshot( svEnt, ent, eNums, portalEnt != NULL, sky );
				continue;
			}
		}

		// nothing to draw or hear
		if ( !( ent->svFlags & SVF_SENDPVS ) && !ent->s.modelindex && !ent->s.loopSound ) {
			continue;
		}

		// a looping sound with this minimum distance is heard everywhere
		if ( ent->s.loopSound && ent->s.loopSoundMinDist == 10000.0f ) {
			SV_AddEntToSnapshot( svEnt, ent, eNums, portalEnt != NULL, sky );
			continue;
		}

		// ignore if not touching a PV leaf
		// check area
		if ( !CM_AreasConnected( clientarea, visEnt->areanum ) ) {
			// doors can legally straddle two areas, so
			// we may need to check another one
			if ( !CM_AreasConnected( clientarea, visEnt->areanum2 ) ) {
				continue;		// blocked by a door
			}
		}

		// beyond the level's far plane
		if ( sv_farPlaneSquared ) {
			if ( parent ) {
				VectorSubtract( parent->centroid, origin, delta );
				radius = parent->radius;
			} else {
				VectorSubtract( ent->centroid, origin, delta );
				radius = ent->radius;
			}
			if ( (float)sv_farPlaneSquared < DotProduct( delta, delta ) - radius * radius ) {
				continue;
			}
		}

		// check individual leafs
		l = 0;
		for ( i=0 ; i < visEnt->numClusters ; i++ ) {
			l = visEnt->clusternums[i];
			if ( clientpvs[l >> 3] & (1 << (l&7) ) ) {
				break;
			}
		}

		// if we haven't found it to be visible,
		// check overflow clusters that coudln't be stored
		if ( i == visEnt->numClusters ) {
			if ( !visEnt->lastCluster ) {
				continue;
			}
			// (the retail loop is bounded by the entity's own last cluster even
			// when its parent's clusters are the ones being tested)
			for ( ; l <= svEnt->lastCluster ; l++ ) {
				if ( clientpvs[l >> 3] & (1 << (l&7) ) ) {
					break;
				}
			}
			if ( l == visEnt->lastCluster ) {
				continue;	// not visible
			}
		}

		// add it; one already present was also seen from another viewpoint
		if ( svEnt->snapshotCounter == sv.snapshotCounter ) {
			SV_AddEntToSnapshot( svEnt, ent, eNums, portalEnt != NULL, sky );
			continue;
		}
		SV_AddEntToSnapshot( svEnt, ent, eNums, portalEnt != NULL, sky );

		// if it's a portal entity, add everything visible from its camera position
		if ( ( ent->svFlags & SVF_PORTAL ) && svEnt != portalEnt ) {
			SV_AddEntitiesVisibleFromPoint( ent->s.origin2, frame, eNums, svEnt, qfalse );
		}

	}

	if ( skyOrigin && !sky && !portalEnt ) {
		SV_AddEntitiesVisibleFromPoint( skyOrigin->s.origin, frame, eNums, NULL, qtrue );
	}
}

/*
=============
SV_BuildClientSnapshot

Decides which entities are going to be visible to the client, and
copies off the playerstate and areabits.

This properly handles multiple recursive portals, but the render
currently doesn't.

For viewing through other player's eyes, clent can be something other than client->gentity
=============
*/
static void SV_BuildClientSnapshot( client_t *client ) {
	vec3_t						org;
	clientSnapshot_t			*frame;
	snapshotEntityNumbers_t		entityNumbers;
	int							i;
	sharedEntity_t				*ent;
	entityState_t				*state;
	sharedEntity_t				*clent;
	int							clientNum;
	playerState_t				*ps;

	// bump the counter used to prevent double adding
	sv.snapshotCounter++;

	// this is the frame we are creating
	frame = &client->frames[ client->netchan.outgoingSequence & PACKET_MASK ];

	// clear everything in this snapshot
	entityNumbers.numSnapshotEntities = 0;
	Com_Memset( frame->areabits, 0, sizeof( frame->areabits ) );

  // https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=62
	frame->num_entities = 0;
	
	clent = client->gentity;
	if ( !clent || client->state == CS_ZOMBIE ) {
		return;
	}

	// grab the current playerState_t
	ps = SV_GameClientNum( client - svs.clients );
	frame->ps = *ps;

	clientNum = frame->ps.clientNum;
	if ( clientNum < 0 || clientNum >= MAX_GENTITIES ) {
		Com_Error( ERR_DROP, "SV_SvEntityForGentity: bad gEnt" );
	}

	// find the client's viewpoint
	if ( ps->pm_flags & PMF_CAMERA_VIEW ) {
		VectorCopy( ps->camera_origin, org );
	} else {
		VectorCopy( ps->origin, org );
		org[2] += ps->viewheight;
	}

	// Quake III never sends a client its own entity. FAKK2 is third person and
	// the client draws the player from it, so it goes in first (fakk2.exe 0x447dd0).
	SV_AddEntToSnapshot( SV_SvEntityForGentity( clent ), clent, &entityNumbers, qfalse, qfalse );

	// add all the entities directly visible to the eye, which
	// may include portal entities that merge other viewpoints
	SV_AddEntitiesVisibleFromPoint( org, frame, &entityNumbers, NULL, qfalse );

	// if there were portals visible, there may be out of order entities
	// in the list which will need to be resorted for the delta compression
	// to work correctly.  This also catches the error condition
	// of an entity being included twice.
	qsort( entityNumbers.snapshotEntities, entityNumbers.numSnapshotEntities, 
		sizeof( entityNumbers.snapshotEntities[0] ), SV_QsortEntityNumbers );

	// now that all viewpoint's areabits have been OR'd together, invert
	// all of them to make it a mask vector, which is what the renderer wants
	for ( i = 0 ; i < MAX_MAP_AREA_BYTES/4 ; i++ ) {
		((int *)frame->areabits)[i] = ((int *)frame->areabits)[i] ^ -1;
	}

	// copy the entity states out
	frame->num_entities = 0;
	frame->first_entity = svs.nextSnapshotEntities;
	for ( i = 0 ; i < entityNumbers.numSnapshotEntities ; i++ ) {
		ent = SV_GentityNum(entityNumbers.snapshotEntities[i]);
		state = &svs.snapshotEntities[svs.nextSnapshotEntities % svs.numSnapshotEntities];
		*state = ent->s;
		svs.nextSnapshotEntities++;
		// this should never hit, map should always be restarted first in SV_Frame
		if ( svs.nextSnapshotEntities >= 0x7FFFFFFE ) {
			Com_Error(ERR_FATAL, "svs.nextSnapshotEntities wrapped");
		}
		frame->num_entities++;
	}
}

#ifdef USE_VOIP
/*
==================
SV_WriteVoipToClient

Check to see if there is any VoIP queued for a client, and send if there is.
==================
*/
static void SV_WriteVoipToClient(client_t *cl, msg_t *msg)
{
	int totalbytes = 0;
	int i;
	voipServerPacket_t *packet;

	if(cl->queuedVoipPackets)
	{
		// Write as many VoIP packets as we reasonably can...
		for(i = 0; i < cl->queuedVoipPackets; i++)
		{
			packet = cl->voipPacket[(i + cl->queuedVoipIndex) % ARRAY_LEN(cl->voipPacket)];

			if(!*cl->downloadName)
			{
        			totalbytes += packet->len;
	        		if (totalbytes > (msg->maxsize - msg->cursize) / 2)
		        		break;

        			MSG_WriteByte(msg, svc_voipOpus);
        			MSG_WriteShort(msg, packet->sender);
	        		MSG_WriteByte(msg, (byte) packet->generation);
		        	MSG_WriteLong(msg, packet->sequence);
		        	MSG_WriteByte(msg, packet->frames);
        			MSG_WriteShort(msg, packet->len);
        			MSG_WriteBits(msg, packet->flags, VOIP_FLAGCNT);
	        		MSG_WriteData(msg, packet->data, packet->len);
                        }

			Z_Free(packet);
		}

		cl->queuedVoipPackets -= i;
		cl->queuedVoipIndex += i;
		cl->queuedVoipIndex %= ARRAY_LEN(cl->voipPacket);
	}
}
#endif

/*
=======================
SV_SendMessageToClient

Called by SV_SendClientSnapshot and SV_SendClientGameState
=======================
*/
void SV_SendMessageToClient(msg_t *msg, client_t *client)
{
	// record information about the message
	client->frames[client->netchan.outgoingSequence & PACKET_MASK].messageSize = msg->cursize;
	client->frames[client->netchan.outgoingSequence & PACKET_MASK].messageSent = svs.time;
	client->frames[client->netchan.outgoingSequence & PACKET_MASK].messageAcked = -1;

	// send the datagram
	SV_Netchan_Transmit(client, msg);
}


/*
=======================
SV_SendClientSnapshot

Also called by SV_FinalMessage

=======================
*/
void SV_SendClientSnapshot( client_t *client ) {
	byte		msg_buf[MAX_MSGLEN];
	msg_t		msg;

	// build the snapshot
	SV_BuildClientSnapshot( client );

	// bots need to have their snapshots build, but
	// the query them directly without needing to be sent
	if ( client->gentity && client->gentity->svFlags & SVF_BOT ) {
		return;
	}

	MSG_Init (&msg, msg_buf, sizeof(msg_buf));
	msg.allowoverflow = qtrue;

	// NOTE, MRE: all server->client messages now acknowledge
	// let the client know which reliable clientCommands we have received
	MSG_WriteLong( &msg, client->lastClientCommand );

	// (re)send any reliable server commands
	SV_UpdateServerCommandsToClient( client, &msg );

	// send over all the relevant entityState_t
	// and the playerState_t
	SV_WriteSnapshotToClient( client, &msg );

#ifdef USE_VOIP
	SV_WriteVoipToClient( client, &msg );
#endif

	// check for overflow
	if ( msg.overflowed ) {
		Com_Printf ("WARNING: msg overflowed for %s\n", client->name);
		MSG_Clear (&msg);
	}

	SV_SendMessageToClient( &msg, client );
}


/*
=======================
SV_SendClientMessages
=======================
*/
void SV_SendClientMessages(void)
{
	int		i;
	client_t	*c;

	// send a message to each connected client
	for(i=0; i < sv_maxclients->integer; i++)
	{
		c = &svs.clients[i];
		
		if(!c->state)
			continue;		// not connected

		if(SV_IsTestPlayer(c) && SV_TestPlayer_Stalled(c))
			continue;		// imitating a client that is still loading

		if(svs.time - c->lastSnapshotTime < c->snapshotMsec * com_timescale->value)
			continue;		// It's not time yet

		if(*c->downloadName)
			continue;		// Client is downloading, don't send snapshots

		if(c->netchan.unsentFragments || c->netchan_start_queue)
		{
			c->rateDelayed = qtrue;
			continue;		// Drop this snapshot if the packet queue is still full or delta compression will break
		}

		if(!(c->netchan.remoteAddress.type == NA_LOOPBACK ||
		     (sv_lanForceRate->integer && Sys_IsLANAddress(c->netchan.remoteAddress))))
		{
			// rate control for clients not on LAN 
			if(SV_RateMsec(c) > 0)
			{
				// Not enough time since last packet passed through the line
				c->rateDelayed = qtrue;
				continue;
			}
		}

		// generate and send a new message
		SV_SendClientSnapshot(c);
		c->lastSnapshotTime = svs.time;
		c->rateDelayed = qfalse;
	}
}
