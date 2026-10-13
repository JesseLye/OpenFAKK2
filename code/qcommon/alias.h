/*
===========================================================================
Alias lists for Heavy Metal: F.A.K.K. 2.

An alias maps a short name to a real resource name, usually a sound. Several
aliases may share a prefix (snd_pain1, snd_pain2) so that a lookup by the
prefix picks one at random. There is one global list and one list per model.

The game module walks these structures directly when it saves a game, so
their layout is shared with it and must not change.
===========================================================================
*/

#ifndef __ALIAS_H__
#define __ALIAS_H__

#include "q_shared.h"

#define	MAX_ALIAS_NAME_LENGTH		32
#define	MAX_REAL_NAME_LENGTH		128
#define	MAX_ALIASLIST_NAME_LENGTH	32

typedef struct AliasActorNode_s {
	int		actor_number;

	int		number_of_times_played;
	byte	been_played_this_loop;
	int		last_time_played;

	struct AliasActorNode_s	*next;
} AliasActorNode_t;

typedef struct AliasListNode_s {
	char	alias_name[MAX_ALIAS_NAME_LENGTH];
	char	real_name[MAX_REAL_NAME_LENGTH];
	float	weight;

	// dialog only
	byte	global_flag;
	byte	stop_flag;
	float	timeout;
	int		maximum_use;

	// global dialog use
	int		number_of_times_played;
	byte	been_played_this_loop;
	int		last_time_played;

	// per-actor dialog use
	AliasActorNode_t	*actor_list;

	struct AliasListNode_s	*next;
} AliasListNode_t;

typedef struct AliasList_s {
	char	name[MAX_ALIASLIST_NAME_LENGTH];
	qboolean	dirty;
	int		num_in_list;
	AliasListNode_t	**sorted_list;
	AliasListNode_t	*data_list;
} AliasList_t;

AliasList_t	*AliasList_New( const char *name );
void		Alias_ListDelete( AliasList_t *list );
void		Alias_ListClear( AliasList_t *list );

qboolean	Alias_ListAdd( AliasList_t *list, const char *alias, const char *name, const char *parameters );
const char	*Alias_ListFind( AliasList_t *list, const char *alias );
const char	*Alias_ListFindRandom( AliasList_t *list, const char *alias );
void		Alias_ListDump( AliasList_t *list );

const char	*Alias_ListFindDialog( AliasList_t *list, const char *alias, int random, int actor_number );
void		Alias_ListUpdateDialog( AliasList_t *list, const char *alias, int number_of_times_played, byte been_played_this_loop, int last_time_played );
void		Alias_ListAddActorDialog( AliasList_t *list, const char *alias, int actor_number, int number_of_times_played, byte been_played_this_loop, int last_time_played );

// the global list
qboolean	Alias_Add( const char *alias, const char *name, const char *parameters );
const char	*Alias_FindRandom( const char *alias );
void		Alias_Dump( void );
void		Alias_Clear( void );

#endif // __ALIAS_H__
