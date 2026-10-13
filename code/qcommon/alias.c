/*
===========================================================================
Alias lists for Heavy Metal: F.A.K.K. 2. See alias.h.

Adding an alias, its parameters and the printed messages follow fakk2.exe.
Range lookup and the random pick follow OpenMoHAA's implementation of the
same Ritual code and have not been checked against fakk2.exe. Dialog
selection (Alias_ListFindDialog) is provisional: it does not yet apply the
timeout, maximum-use and per-actor rules.
===========================================================================
*/

#include "alias.h"
#include "qcommon.h"

#define	MAX_RANDOM_ALIASES	64

static AliasList_t	*globalAliases;

static void *Alias_Alloc( size_t size ) {
	void	*p = calloc( 1, size ? size : 1 );

	if ( !p ) {
		Com_Error( ERR_FATAL, "Alias_ListAdd : Out of memory" );
	}
	return p;
}

AliasList_t *AliasList_New( const char *name ) {
	AliasList_t	*list = Alias_Alloc( sizeof( *list ) );

	Q_strncpyz( list->name, name, sizeof( list->name ) );
	return list;
}

void Alias_ListClear( AliasList_t *list ) {
	AliasListNode_t	*node, *nextNode;

	if ( !list ) {
		return;
	}
	for ( node = list->data_list ; node ; node = nextNode ) {
		AliasActorNode_t	*actor, *nextActor;

		nextNode = node->next;
		for ( actor = node->actor_list ; actor ; actor = nextActor ) {
			nextActor = actor->next;
			free( actor );
		}
		free( node );
	}
	free( list->sorted_list );

	list->sorted_list = NULL;
	list->data_list = NULL;
	list->num_in_list = 0;
	list->dirty = qfalse;
}

void Alias_ListDelete( AliasList_t *list ) {
	if ( !list ) {
		return;
	}
	Alias_ListClear( list );
	free( list );
}

static int Alias_ListCompareNodes( const void *a, const void *b ) {
	const AliasListNode_t	*one = *(const AliasListNode_t * const *)a;
	const AliasListNode_t	*two = *(const AliasListNode_t * const *)b;

	return strcmp( one->alias_name, two->alias_name );
}

static void Alias_ListSort( AliasList_t *list ) {
	AliasListNode_t	*node;
	int				i;

	if ( !list->dirty ) {
		return;
	}
	free( list->sorted_list );
	list->sorted_list = Alias_Alloc( list->num_in_list * sizeof( *list->sorted_list ) );
	for ( i = 0, node = list->data_list ; i < list->num_in_list && node ; i++, node = node->next ) {
		list->sorted_list[i] = node;
	}
	qsort( list->sorted_list, list->num_in_list, sizeof( *list->sorted_list ), Alias_ListCompareNodes );
	list->dirty = qfalse;
}

// lower-cases an alias for lookup; qfalse when it cannot be a stored name
static qboolean Alias_Normalize( const char *alias, char *out, int outSize ) {
	int		i;

	for ( i = 0 ; alias[i] ; i++ ) {
		if ( i >= outSize - 1 ) {
			return qfalse;
		}
		out[i] = tolower( (unsigned char)alias[i] );
	}
	out[i] = 0;
	return qtrue;
}

static AliasListNode_t *Alias_ListFindNode( AliasList_t *list, const char *alias ) {
	char	name[MAX_ALIAS_NAME_LENGTH];
	int		low, high;

	if ( !list || !alias || !Alias_Normalize( alias, name, sizeof( name ) ) ) {
		return NULL;
	}
	Alias_ListSort( list );

	low = 0;
	high = list->num_in_list - 1;
	while ( low <= high ) {
		int		mid = ( low + high ) >> 1;
		int		diff = strcmp( name, list->sorted_list[mid]->alias_name );

		if ( diff == 0 ) {
			return list->sorted_list[mid];
		}
		if ( diff > 0 ) {
			low = mid + 1;
		} else {
			high = mid - 1;
		}
	}
	return NULL;
}

/*
===============
Alias_ListAddParms

parameters: global | stop | timeout <seconds> | maxuse <count> | weight <w>
===============
*/
static void Alias_ListAddParms( AliasListNode_t *node, const char *parameters ) {
	char	*data = (char *)parameters;
	char	*token;

	if ( !parameters ) {
		return;
	}

	while ( 1 ) {
		token = COM_ParseExt( &data, qtrue );
		if ( !token[0] ) {
			return;
		}

		if ( !Q_stricmp( token, "global" ) ) {
			node->global_flag = 1;
		} else if ( !Q_stricmp( token, "stop" ) ) {
			node->stop_flag = 1;
		} else if ( !Q_stricmp( token, "timeout" ) ) {
			float	value;

			token = COM_ParseExt( &data, qtrue );
			if ( !token[0] ) {
				return;
			}
			value = atof( token );
			if ( value > 0 ) {
				node->timeout = value;
			} else {
				Com_Printf( "Timeout value out of range in %s alias\n", node->alias_name );
			}
		} else if ( !Q_stricmp( token, "maxuse" ) ) {
			int		value;

			token = COM_ParseExt( &data, qtrue );
			if ( !token[0] ) {
				return;
			}
			value = atoi( token );
			if ( value >= 1 ) {
				node->maximum_use = value;
			} else {
				Com_Printf( "Maximum use value out of range in %s alias\n", node->alias_name );
			}
		} else if ( !Q_stricmp( token, "weight" ) ) {
			float	value;

			token = COM_ParseExt( &data, qtrue );
			if ( !token[0] ) {
				return;
			}
			value = atof( token );
			if ( value > 0 ) {
				node->weight = value;
			} else {
				Com_Printf( "Weight value out of range in %s alias\n", node->alias_name );
			}
		} else {
			Com_Printf( "Unknown parameter for %s alias\n", node->alias_name );
		}
	}
}

qboolean Alias_ListAdd( AliasList_t *list, const char *alias, const char *name, const char *parameters ) {
	AliasListNode_t	*node;
	int				i;

	if ( !list ) {
		return qfalse;
	}

	if ( !list->dirty ) {
		node = Alias_ListFindNode( list, alias );
		if ( node ) {
			if ( strcmp( name, node->real_name ) ) {
				Com_DPrintf( "Duplicate Aliases for %s in list %s.\n", alias, list->name );
			}
			return qtrue;
		}
	} else {
		// While additions are pending the retail engine does not re-sort: it
		// compares the new alias, as written, against the stored length of
		// each existing name. That is kept as is, so the same aliases are
		// accepted and rejected.
		for ( node = list->data_list ; node ; node = node->next ) {
			if ( !strncmp( node->alias_name, alias, strlen( node->alias_name ) ) ) {
				return qtrue;
			}
		}
	}

	node = Alias_Alloc( sizeof( *node ) );
	strncpy( node->alias_name, alias, MAX_ALIAS_NAME_LENGTH - 1 );
	for ( i = 0 ; i < MAX_ALIAS_NAME_LENGTH ; i++ ) {
		node->alias_name[i] = tolower( (unsigned char)node->alias_name[i] );
	}
	strncpy( node->real_name, name, MAX_REAL_NAME_LENGTH - 1 );
	node->weight = 1.0f;
	node->maximum_use = -1;

	Alias_ListAddParms( node, parameters );

	list->num_in_list++;
	list->dirty = qtrue;
	node->next = list->data_list;
	list->data_list = node;

	return qtrue;
}

const char *Alias_ListFind( AliasList_t *list, const char *alias ) {
	AliasListNode_t	*node = Alias_ListFindNode( list, alias );

	return node ? node->real_name : NULL;
}

/*
===============
Alias_ListFindRandomRange

Finds the run of sorted aliases that start with the given name. An alias
that continues with an underscore is a different alias, not a variant.
===============
*/
static int Alias_ListFindRandomRange( AliasList_t *list, const char *alias, AliasListNode_t **found, float *totalWeight ) {
	char	name[MAX_ALIAS_NAME_LENGTH];
	int		length, low, high, mid = 0, first, i;
	int		numFound = 0;

	*totalWeight = 0;
	if ( !list || !alias || !alias[0] || !Alias_Normalize( alias, name, sizeof( name ) ) ) {
		return 0;
	}
	Alias_ListSort( list );
	length = strlen( name );

	low = 0;
	high = list->num_in_list - 1;
	while ( low <= high ) {
		int		diff;

		mid = ( low + high ) >> 1;
		diff = strncmp( name, list->sorted_list[mid]->alias_name, length );
		if ( diff == 0 ) {
			break;
		}
		if ( diff > 0 ) {
			low = mid + 1;
		} else {
			high = mid - 1;
		}
	}
	if ( low > high ) {
		return 0;
	}

	first = mid;
	while ( first > 0 && !strncmp( name, list->sorted_list[first - 1]->alias_name, length ) ) {
		first--;
	}
	for ( i = first ; i < list->num_in_list && numFound < MAX_RANDOM_ALIASES ; i++ ) {
		AliasListNode_t	*node = list->sorted_list[i];

		if ( strncmp( name, node->alias_name, length ) ) {
			break;
		}
		if ( node->alias_name[length] == '_' ) {
			continue;
		}
		found[numFound++] = node;
		*totalWeight += node->weight;
	}
	return numFound;
}

static AliasListNode_t *Alias_ListPickRandom( AliasList_t *list, const char *alias ) {
	AliasListNode_t	*found[MAX_RANDOM_ALIASES];
	float			totalWeight, pick, sum = 0;
	int				numFound, i;

	numFound = Alias_ListFindRandomRange( list, alias, found, &totalWeight );
	if ( !numFound ) {
		return NULL;
	}

	pick = ( (float)Com_UnseededRand() / 32768.0f ) * totalWeight;
	for ( i = 0 ; i < numFound ; i++ ) {
		sum += found[i]->weight;
		if ( sum > pick ) {
			return found[i];
		}
	}
	return NULL;
}

const char *Alias_ListFindRandom( AliasList_t *list, const char *alias ) {
	AliasListNode_t	*node = Alias_ListPickRandom( list, alias );

	return node ? node->real_name : NULL;
}

void Alias_ListDump( AliasList_t *list ) {
	int		i;

	if ( !list ) {
		return;
	}
	Alias_ListSort( list );

	Com_Printf( "Alias List: %s\n", list->name );
	Com_Printf( "Alias Name / Weight / Real Name\n" );
	for ( i = 0 ; i < list->num_in_list ; i++ ) {
		AliasListNode_t	*node = list->sorted_list[i];

		Com_Printf( "%32s %3.2f %s\n", node->alias_name, node->weight, node->real_name );
	}
	Com_Printf( "%d total aliases\n", list->num_in_list );
}

// provisional: see the note at the top of the file
const char *Alias_ListFindDialog( AliasList_t *list, const char *alias, int random, int actor_number ) {
	(void)actor_number;

	if ( random ) {
		return Alias_ListFindRandom( list, alias );
	}
	return Alias_ListFind( list, alias );
}

void Alias_ListUpdateDialog( AliasList_t *list, const char *alias, int number_of_times_played, byte been_played_this_loop, int last_time_played ) {
	AliasListNode_t	*node = Alias_ListFindNode( list, alias );

	if ( !node ) {
		return;
	}
	node->number_of_times_played = number_of_times_played;
	node->been_played_this_loop = been_played_this_loop;
	node->last_time_played = last_time_played;
}

void Alias_ListAddActorDialog( AliasList_t *list, const char *alias, int actor_number, int number_of_times_played, byte been_played_this_loop, int last_time_played ) {
	AliasListNode_t		*node = Alias_ListFindNode( list, alias );
	AliasActorNode_t	*actor;

	if ( !node ) {
		return;
	}
	for ( actor = node->actor_list ; actor ; actor = actor->next ) {
		if ( actor->actor_number == actor_number ) {
			Com_Printf( "Actor %d already added to this %s alias\n", actor_number, alias );
			return;
		}
	}

	actor = Alias_Alloc( sizeof( *actor ) );
	actor->actor_number = actor_number;
	actor->number_of_times_played = number_of_times_played;
	actor->been_played_this_loop = been_played_this_loop;
	actor->last_time_played = last_time_played;
	actor->next = node->actor_list;
	node->actor_list = actor;
}

/*
===========================================================================

The global list

===========================================================================
*/

static AliasList_t *Alias_GlobalList( void ) {
	if ( !globalAliases ) {
		globalAliases = AliasList_New( "Global" );
	}
	return globalAliases;
}

qboolean Alias_Add( const char *alias, const char *name, const char *parameters ) {
	return Alias_ListAdd( Alias_GlobalList(), alias, name, parameters );
}

const char *Alias_FindRandom( const char *alias ) {
	return Alias_ListFindRandom( Alias_GlobalList(), alias );
}

void Alias_Dump( void ) {
	Alias_ListDump( Alias_GlobalList() );
}

void Alias_Clear( void ) {
	Alias_ListClear( Alias_GlobalList() );
}
