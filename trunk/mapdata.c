
/* dist: public */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "asss.h"

/* extra includes */
#include "pathutil.h"
#include "sparse.inc"

/* brick mode constants */
#include "settings/game.h"


#define METADATA_MAGIC 0x6c766c65
#define MAX_CHUNK_SIZE (128*1024)


/* some structs */
typedef struct chunk
{
	u32 type;
	u32 size;
	byte data[1];
} chunk;

struct ELVL;

struct Region
{
	struct ELVL *lvl;
	const char *name;
	HashTable *chunks;
	u32 setmap[8];
};

typedef struct ELVL
{
	sparse_arr tiles;
	sparse_arr rgntiles;
	Region ***rgnsets;
	HashTable *attrs;
	HashTable *regions;
	HashTable *rawchunks;
	int errors, flags;
	pthread_mutex_t mtx;
} ELVL;


/* global data */
local int lvlkey;

/* cached interfaces */
local Imodman *mm;
local Iconfig *cfg;
local Iarenaman *aman;
local Ilogman *lm;
local Iprng *prng;


/* extended lvl mini-library */

local void read_plain_tile_data(ELVL *lvl, const void *vd, int len)
{
	struct tile_data_t
	{
		u32 x : 12;
		u32 y : 12;
		u32 type : 8;
	} const *td = vd;

	while (len >= 4)
	{
		if (td->x < 1024 && td->y < 1024)
		{
			if (td->type == TILE_TURF_FLAG)
				lvl->flags++;
			if (td->type < TILE_BIG_ASTEROID)
				insert_sparse(lvl->tiles, td->x, td->y, td->type);
			else
			{
				int size = 1, x, y;
				if (td->type == TILE_BIG_ASTEROID)
					size = 2;
				else if (td->type == TILE_STATION)
					size = 6;
				else if (td->type == TILE_WORMHOLE)
					size = 5;
				for (x = 0; x < size; x++)
					for (y = 0; y < size; y++)
						insert_sparse(lvl->tiles, td->x+x, td->y+y, td->type);
			}
		}
		else
			lvl->errors++;

		td++;
		len -= 4;
	}
}


local int read_chunks(HashTable *chunks, const byte *d, int len)
{
	u32 buf[2] = { 0, 0 };
	const chunk *tc;
	chunk *c;

	while (len >= 8)
	{
		/* first check chunk header */
		tc = (chunk*)d;

		if (tc->size > MAX_CHUNK_SIZE || (tc->size + 8) > len)
			break;

		/* allocate space for the chunk and copy it in */
		c = amalloc(tc->size + 8);
		memcpy(c, d, tc->size + 8);
		d += tc->size + 8;
		len -= tc->size + 8;

		/* add it to the chunk table */
		buf[0] = c->type;
		HashAdd(chunks, (const char *)buf, c);

		/* skip padding bytes */
		if ((tc->size & 3) != 0)
		{
			int padding = 4 - (tc->size & 3);
			d += padding;
			len -= padding;
		}
	}

	return len == 0;
}


local void free_region(Region *rgn)
{
	if (rgn->chunks)
	{
		HashEnum(rgn->chunks, hash_enum_afree, NULL);
		HashFree(rgn->chunks);
	}
	afree(rgn);
}


local int Contains(Region *rgn, int x, int y)
{
	if (rgn->lvl->rgntiles)
	{
		int i = lookup_sparse(rgn->lvl->rgntiles, x, y);
		return rgn->setmap[i >> 5] & (1 << (i & 31));
	}
	else
		return FALSE;
}


local int rgn_set_len(Region **rgnset)
{
	int c;
	for (c = 0; *rgnset; c++, rgnset++) ;
	return c;
}

local void add_rgn_tile(Region *newrgn, int x, int y)
{
	ELVL *lvl = newrgn->lvl;
	Region **oldset, **newset;
	int i, len;

	oldset = lvl->rgnsets[lookup_sparse(lvl->rgntiles, x, y)];

	len = oldset ? rgn_set_len(oldset) : 0;

	/* try looking up new set */
	for (i = 1; i < 256; i++)
	{
		Region **tryset = lvl->rgnsets[i];
		if (!tryset)
			break;
		if (memcmp(oldset, tryset, len * sizeof(Region *)) == 0 &&
		    tryset[len] == newrgn)
		{
			/* we found the correct set */
			insert_sparse(lvl->rgntiles, x, y, i);
			return;
		}
	}

	/* the correct set doesn't exist yet */
	if (i < 256)
	{
		newset = amalloc((len + 2) * sizeof(Region *));
		memcpy(newset, oldset, len * sizeof(Region *));
		newset[len] = newrgn;
		newset[len+1] = NULL;
		lvl->rgnsets[i] = newset;
		newrgn->setmap[i >> 5] |= (1 << (i & 31));
		insert_sparse(lvl->rgntiles, x, y, i);
	}
	else
		lm->Log(L_WARN, "<mapdata> overlap limit reached. some region data will be lost.");
}

local int read_rle_tile_data(Region *rgn, const byte *d, int len)
{
	int cx = 0, cy = 0, i = 0, b, op, d1, n, x, y;

	while (i < len)
	{
		if (cx < 0 || cx > 1023 || cy < 0 || cy > 1023)
			return FALSE;

		b = d[i++];
		op = (b & 192) >> 6;
		d1 = b & 31;
		n = d1 + 1;

		if (b & 32)
			n = (d1 << 8) + d[i++] + 1;

		switch (op)
		{
			case 0:
				/* n empty in a row */
				cx += n;
				break;
			case 1:
				/* n present in a row */
				for (x = cx; x < (cx + n); x++)
					add_rgn_tile(rgn, x, cy);
				cx += n;
				break;
			case 2:
				/* n rows of empty */
				if (cx != 0)
					return FALSE;
				cy += n;
				break;
			case 3:
				/* repeat last row n times */
				if (cx != 0 || cy == 0)
					return FALSE;
				for (y = cy; y < (cy + n); y++)
					for (x = 0; x < 1024; x++)
						if (Contains(rgn, x, cy - 1))
							add_rgn_tile(rgn, x, y);
				cy += n;
				break;
		}

		if (cx == 1024)
		{
			cx = 0;
			cy++;
		}
	}

	if (i != len || cy != 1024)
		return FALSE;

	return TRUE;
}


local int process_region_chunk(const char *key, void *vchunk, void *vrgn)
{
	Region *rgn = vrgn;
	chunk *chunk = vchunk;

	if (chunk->type == MAKE_CHUNK_TYPE(rNAM))
	{
		if (!rgn->name)
		{
			char *name = amalloc(chunk->size + 1);
			memcpy(name, chunk->data, chunk->size);
			rgn->name = name;
		}
		afree(chunk);
		return TRUE;
	}
	else if (chunk->type == MAKE_CHUNK_TYPE(rTIL))
	{
		if (!rgn->lvl->rgntiles)
			rgn->lvl->rgntiles = init_sparse();
		if (!rgn->lvl->rgnsets)
			rgn->lvl->rgnsets = amalloc(256 * sizeof(Region **));
		if (!read_rle_tile_data(rgn, chunk->data, chunk->size))
			lm->Log(L_WARN, "<mapdata> error in lvl file while reading rle tile data");
		afree(chunk);
		return TRUE;
	}
	else
		return FALSE;
}


local int process_map_chunk(const char *key, void *vchunk, void *vlvl)
{
	ELVL *lvl = vlvl;
	chunk *chunk = vchunk;

	if (chunk->type == MAKE_CHUNK_TYPE(ATTR))
	{
		char keybuf[64], *val;
		const byte *t;
		t = delimcpy(keybuf, chunk->data, sizeof(keybuf), '=');
		if (t)
		{
			int vlen = chunk->size - (t - chunk->data);
			val = amalloc(vlen+1);
			memcpy(val, t, vlen);
			if (!lvl->attrs)
				lvl->attrs = HashAlloc();
			HashAdd(lvl->attrs, keybuf, val);
		}
		afree(chunk);
		return TRUE;
	}
	else if (chunk->type == MAKE_CHUNK_TYPE(REGN))
	{
		Region *rgn = amalloc(sizeof(*rgn));
		rgn->lvl = lvl;
		rgn->chunks = HashAlloc();
		if (!read_chunks(rgn->chunks, chunk->data, chunk->size))
			lm->Log(L_WARN, "<mapdata> error in lvl while reading region chunks");
		HashEnum(rgn->chunks, process_region_chunk, rgn);
		if (!lvl->regions)
			lvl->regions = HashAlloc();
		if (rgn->name)
		{
			/* make the region name point to its key in the hash table,
			 * and free the memory used by the temporary name. */
			const char *t = rgn->name;
			rgn->name = HashAdd(lvl->regions, rgn->name, rgn);
			afree(t);
		}
		else
			/* all regions must have a name */
			free_region(rgn);
		afree(chunk);
		return TRUE;
	}
	else if (chunk->type == MAKE_CHUNK_TYPE(TSET))
	{
		/* we don't use this, free it to save some memory. */
		afree(chunk);
		return TRUE;
	}
	else if (chunk->type == MAKE_CHUNK_TYPE(TILE))
	{
		read_plain_tile_data(lvl, chunk->data, chunk->size);
		afree(chunk);
		return TRUE;
	}
	else
		/* keep any other chunks */
		return FALSE;
}


local int load_from_file(ELVL *lvl, const char *lvlname)
{
	struct bitmap_file_header_t
	{
		u16 bm;
		u32 fsize;
		u16 res1;
		u16 res2;
		u32 offbits;
	} *bmfh;
	struct metadata_header_t
	{
		u32 magic;
		u32 totalsize;
		u32 res1;
	} *mdhead;

	const byte *d;
	MMapData *map = MapFile(lvlname, FALSE);

	if (!map)
		return FALSE;

	lvl->tiles = init_sparse();

	d = map->data;
	bmfh = (struct bitmap_file_header_t*)d;
	if (map->len >= sizeof(*bmfh) && bmfh->bm == 19778)
	{
		if (bmfh->res1 != 0)
		{
			/* possible metadata, try to read it */
			mdhead = (struct metadata_header_t*)(d + bmfh->res1);
			if (map->len >= (bmfh->res1 + 12) &&
			    mdhead->magic == METADATA_MAGIC &&
			    map->len >= bmfh->res1 + mdhead->totalsize)
			{
				/* looks good. start reading chunks. */
				lvl->rawchunks = HashAlloc();
				if (!read_chunks(lvl->rawchunks, d + bmfh->res1 + 12, mdhead->totalsize - 12))
					lm->Log(L_WARN, "<mapdata> error in lvl while reading metadata chunks");
				/* turn some of them into more useful data. */
				HashEnum(lvl->rawchunks, process_map_chunk, lvl);
			}
		}
		/* get in position for tile data */
		d += bmfh->offbits;
	}

	/* now d points to the tile data. read until eof. */
	read_plain_tile_data(lvl, d, map->len - (d - (const byte *)map->data));

	UnmapFile(map);
	return TRUE;
}


local int hash_enum_free_region(const char *key, void *rgn, void *clos)
{
	free_region(rgn);
	return FALSE;
}

local void clear_level(ELVL *lvl)
{
	if (lvl->tiles)
	{
		delete_sparse(lvl->tiles);
		lvl->tiles = NULL;
	}
	if (lvl->rgntiles)
	{
		delete_sparse(lvl->rgntiles);
		lvl->rgntiles = NULL;
	}
	if (lvl->rgnsets)
	{
		int i;
		for (i = 0; i < 256; i++)
			afree(lvl->rgnsets[i]);
		afree(lvl->rgnsets);
		lvl->rgnsets = NULL;
	}
	if (lvl->attrs)
	{
		HashEnum(lvl->attrs, hash_enum_afree, NULL);
		HashFree(lvl->attrs);
		lvl->attrs = NULL;
	}
	if (lvl->regions)
	{
		HashEnum(lvl->regions, hash_enum_free_region, NULL);
		HashFree(lvl->regions);
		lvl->regions = NULL;
	}
	if (lvl->rawchunks)
	{
		HashEnum(lvl->rawchunks, hash_enum_afree, NULL);
		HashFree(lvl->rawchunks);
		lvl->rawchunks = NULL;
	}
}


/* interface functions */

local int GetMapFilename(Arena *arena, char *buf, int buflen, const char *mapname)
{
	int islvl = 0;
	const char *t;
	struct replace_table repls[2] =
	{
		{'b', arena->basename},
		{'m', NULL},
	};

	if (!buf)
		return FALSE;
	if (!mapname)
		mapname = cfg->GetStr(arena->cfg, "General", "Map");

	if (mapname)
	{
		repls[1].with = mapname;

		t = strrchr(mapname, '.');
		if (t && strcmp(t, ".lvl") == 0)
			islvl = 1;
	}

	return find_file_on_path(
			buf,
			buflen,
			islvl ? CFG_LVL_SEARCH_PATH : CFG_LVZ_SEARCH_PATH,
			repls,
			repls[1].with ? 2 : 1) == 0;
}


local const char * GetAttr(Arena *arena, const char *key)
{
	ELVL *lvl = P_ARENA_DATA(arena, lvlkey);
	if (lvl->attrs)
		return HashGetOne(lvl->attrs, key);
	else
		return NULL;
}


local int MapChunk(Arena *arena, u32 ctype, const void **datap, int *sizep)
{
	ELVL *lvl = P_ARENA_DATA(arena, lvlkey);
	if (lvl->rawchunks)
	{
		chunk *c;
		u32 buf[2] = { 0, 0 };
		buf[0] = ctype;
		c = HashGetOne(lvl->rawchunks, (const char *)buf);
		if (c)
		{
			if (datap) *datap = c->data;
			if (sizep) *sizep = c->size;
			return TRUE;
		}
	}
	return FALSE;
}


local int GetFlagCount(Arena *a)
{
	return ((ELVL*)P_ARENA_DATA(a, lvlkey))->flags;
}


local enum map_tile_t GetTile(Arena *a, int x, int y)
{
	int ret;
	ELVL *lvl = P_ARENA_DATA(a, lvlkey);
	pthread_mutex_lock(&lvl->mtx);
	ret = lvl->tiles ? lookup_sparse(lvl->tiles, x, y) : -1;
	pthread_mutex_unlock(&lvl->mtx);
	return ret;
}



local void FindEmptyTileNear(Arena *arena, int *x, int *y)
{
	/* init context. these values are funny because they are one
	 * iteration before where we really want to start from. */
	struct
	{
		enum { up, right, down, left } dir;
		int upto, remaining;
		int x, y;
	} ctx = { left, 0, 1, *x + 1, *y };
	ELVL *lvl;
	sparse_arr arr;

	lvl = P_ARENA_DATA(arena, lvlkey);
	pthread_mutex_lock(&lvl->mtx);

	arr = lvl->tiles;

	if (!arr) goto failed_1;

	/* do it */
	for (;;)
	{
		/* move 1 in current dir */
		switch (ctx.dir)
		{
			case down:  ctx.y++; break;
			case right: ctx.x++; break;
			case up:    ctx.y--; break;
			case left:  ctx.x--; break;
		}
		ctx.remaining--;
		/* if we're at the end of the line */
		if (ctx.remaining == 0)
		{
			ctx.dir = (ctx.dir + 1) % 4;
			if (ctx.dir == 0 || ctx.dir == 2)
				ctx.upto++;
			ctx.remaining = ctx.upto;
		}

		/* check if the tile is empty */
		if (lookup_sparse(arr, ctx.x, ctx.y))
		{
			if (ctx.upto < 35)
				continue;
			else
				goto failed_1;
		}

		/* return values */
		*x = ctx.x; *y = ctx.y;
		break;
	}

failed_1:
	pthread_mutex_unlock(&lvl->mtx);
}


local int FindBrickEndpoints(
		Arena *arena,
		int brickmode,
		int dropx,
		int dropy,
		int direction,
		int length,
		int *x1, int *y1, int *x2, int *y2)
{
	ELVL *lvl;
	sparse_arr arr;
	enum { up, right, down, left } dir;
	int bestcount, bestdir, x, y, destx = dropx, desty = dropy;

	lvl = P_ARENA_DATA(arena, lvlkey);
	pthread_mutex_lock(&lvl->mtx);
	arr = lvl->tiles;

	if (lookup_sparse(arr, dropx, dropy))
	{
		pthread_mutex_unlock(&lvl->mtx);
		return FALSE;
	}

	/* in the worst case, we can always just drop a single block */

	if (brickmode == BRICK_VIE)
	{
		/* find closest wall and the point next to it */
		bestcount = 3000;
		bestdir = -1;
		for (dir = 0; dir < 4; dir++)
		{
			int count = 0, oldx = dropx, oldy = dropy;
			x = dropx; y = dropy;

			while (x >= 0 && x < 1024 &&
			       y >= 0 && y < 1024 &&
			       lookup_sparse(arr, x, y) == 0 &&
			       count < length)
			{
				switch (dir)
				{
					case down:  oldy = y++; break;
					case right: oldx = x++; break;
					case up:    oldy = y--; break;
					case left:  oldx = x--; break;
				}
				count++;
			}

			if (count < bestcount)
			{
				bestcount = count;
				bestdir = dir;
				destx = oldx; desty = oldy;
			}
		}

		if (bestdir == -1)
		{
			/* shouldn't happen */
			*x1 = *x2 = dropx;
			*y1 = *y2 = dropy;
			goto failed_2;
		}

		if (bestcount == length)
		{
			/* no closest wall */
			if (prng->Get32() & 1)
			{
				destx = dropx - length / 2;
				desty = dropy;
				bestdir = left;
			}
			else
			{
				destx = dropx;
				desty = dropy - length / 2;
				bestdir = up;
			}
		}
	}
	else if (brickmode == BRICK_AHEAD)
	{
		int count = 0, oldx = dropx, oldy = dropy;

		x = dropx; y = dropy;

		bestdir = ((direction + 5) % 40) / 10;

		while (x >= 0 && x < 1024 &&
		       y >= 0 && y < 1024 &&
		       lookup_sparse(arr, x, y) == 0 &&
		       count < length)
		{
			switch (bestdir)
			{
				case down:  oldy = y++; break;
				case right: oldx = x++; break;
				case up:    oldy = y--; break;
				case left:  oldx = x--; break;
			}
			count++;
		}

		destx = oldx; desty = oldy;
	}
	else if (brickmode == BRICK_LATERAL)
	{
		int count = 0, oldx = dropx, oldy = dropy;

		x = dropx; y = dropy;

		bestdir = ((direction + 15) % 40) / 10;

		while (x >= 0 && x < 1024 &&
		       y >= 0 && y < 1024 &&
		       lookup_sparse(arr, x, y) == 0 &&
		       count <= length / 2)
		{
			switch (bestdir)
			{
				case down:  oldy = y++; break;
				case right: oldx = x++; break;
				case up:    oldy = y--; break;
				case left:  oldx = x--; break;
			}
			count++;
		}

		destx = oldx; desty = oldy;
	}
	else if (brickmode == BRICK_CAGE)
	{
		/* generate drop coords inside map */
		int sx = dropx - (length) / 2,
			sy = dropy - (length) / 2,
			ex = dropx + (length + 1) / 2,
			ey = dropy + (length + 1) / 2;

		if (sx < 0) sx = 0;
		if (sy < 0) sy = 0;
		if (ex > 1023) ex = 1023;
		if (ey > 1023) ey = 1023;

		/* top */
		x = sx;
		y = sy;

		while (x <= ex && lookup_sparse(arr, x, y))
			++x;

		if (x > ex)
		{
			x1[0] = dropx;
			x2[0] = dropx;
			y1[0] = dropy;
			y2[0] = dropy;
		}
		else
		{
			x1[0] = x++;
			while (x <= ex && !lookup_sparse(arr, x, y))
				++x;
			x2[0] = x-1;
			y1[0] = y;
			y2[0] = y;
		}

		/* bottom */
		x = sx;
		y = ey;

		while (x <= ex && lookup_sparse(arr, x, y))
			++x;

		if (x > ex)
		{
			x1[1] = dropx;
			x2[1] = dropx;
			y1[1] = dropy;
			y2[1] = dropy;
		}
		else
		{
			x1[1] = x++;
			while (x <= ex && !lookup_sparse(arr, x, y))
				++x;
			x2[1] = x-1;
			y1[1] = y;
			y2[1] = y;
		}

		/* left */
		x = sx;
		y = sy;

		while (y <= ey && lookup_sparse(arr, x, y))
			++y;

		if (y > ey)
		{
			x1[2] = dropx;
			x2[2] = dropx;
			y1[2] = dropy;
			y2[2] = dropy;
		}
		else
		{
			y1[2] = y++;
			while (y <= ey && !lookup_sparse(arr, x, y))
				++y;
			y2[2] = y-1;
			x1[2] = x;
			x2[2] = x;
		}

		/* right */
		x = ex;
		y = sy;

		while (y <= ey && lookup_sparse(arr, x, y))
			++y;

		if (y > ey)
		{
			x1[3] = dropx;
			x2[3] = dropx;
			y1[3] = dropy;
			y2[3] = dropy;
		}
		else
		{
			y1[3] = y++;
			while (y <= ey && !lookup_sparse(arr, x, y))
				++y;
			y2[3] = y-1;
			x1[3] = x;
			x2[3] = x;
		}

		goto failed_2;
	}
	else
		goto failed_2;

	/* enter first coordinate */
	dropx = x = *x1 = destx; dropy = y = *y1 = desty;

	/* go from closest point */
	switch (bestdir)
	{
		case down:
			while (lookup_sparse(arr, x, y) == 0 &&
			       (dropy - y) < length &&
			       y >= 0)
				desty = y--;
			break;

		case right:
			while (lookup_sparse(arr, x, y) == 0 &&
			       (dropx - x) < length &&
			       x >= 0)
				destx = x--;
			break;

		case up:
			while (lookup_sparse(arr, x, y) == 0 &&
			       (y - dropy) < length &&
			       y < 1024)
				desty = y++;
			break;

		case left:
			while (lookup_sparse(arr, x, y) == 0 &&
			       (x - dropx) < length &&
			       x < 1024)
				destx = x++;
			break;
	}

	/* enter second coordinate */
	*x2 = destx; *y2 = desty;

	/* swap if necessary */
	if (*x1 > *x2)
	{
		x = *x1;
		*x1 = *x2;
		*x2 = x;
	}
	if (*y1 > *y2)
	{
		y = *y1;
		*y1 = *y2;
		*y2 = y;
	}

failed_2:
	pthread_mutex_unlock(&lvl->mtx);
	return TRUE;
}


local u32 GetChecksum(Arena *arena, u32 key)
{
	int x, y, savekey = (int)key;
	ELVL *lvl;
	sparse_arr arr;

	lvl = P_ARENA_DATA(arena, lvlkey);
	pthread_mutex_lock(&lvl->mtx);
	arr = lvl->tiles;

	if (!arr) goto failed_3;

	for (y = savekey % 32; y < 1024; y += 32)
		for (x = savekey % 31; x < 1024; x += 31)
		{
			byte tile = lookup_sparse(arr, x, y);
			if ((tile >= TILE_START && tile <= TILE_END) || tile == TILE_SAFE)
				key += savekey ^ tile;
		}

failed_3:
	pthread_mutex_unlock(&lvl->mtx);
	return key;
}


local void DoBrick(Arena *arena, int drop, int x1, int y1, int x2, int y2)
{
	unsigned char tile = drop ? TILE_BRICK : 0;
	sparse_arr arr;
	ELVL *lvl = P_ARENA_DATA(arena, lvlkey);

	pthread_mutex_lock(&lvl->mtx);
	arr = lvl->tiles;

	if (x1 == x2)
	{
		int y;
		for (y = y1; y <= y2; y++)
			insert_sparse(arr, x1, y, tile);
	}
	else if (y1 == y2)
	{
		int x;
		for (x = x1; x <= x2; x++)
			insert_sparse(arr, x, y1, tile);
	}

	/* try to keep memory down */
	if (tile == 0)
		cleanup_sparse(arr);

	pthread_mutex_unlock(&lvl->mtx);
}


local void GetMemoryStats(Arena *arena, struct mapdata_memory_stats_t *stats)
{
	int bts, blks, i;
	ELVL *lvl = P_ARENA_DATA(arena, lvlkey);

	memset(stats, 0, sizeof(*stats));

	/* get data for lvl itself */
	bts = blks = 0;
	pthread_mutex_lock(&lvl->mtx);
	sparse_allocated(lvl->tiles, &bts, &blks);
	pthread_mutex_unlock(&lvl->mtx);
	stats->lvlbytes = bts;
	stats->lvlblocks = blks;

	/* now data for regions */
	bts = blks = 0;
	if (lvl->rgntiles)
		sparse_allocated(lvl->rgntiles, &bts, &blks);
	if (lvl->rgnsets)
		for (i = 0; i < 256; i++)
		{
			Region **set = lvl->rgnsets[i];
			if (set)
			{
				bts += (rgn_set_len(set) + 1) * sizeof(Region *);
				blks += 1;
			}
		}
	stats->rgnbytes = bts;
	stats->rgnblocks = blks;
}


local Region * FindRegionByName(Arena *arena, const char *name)
{
	ELVL *lvl = P_ARENA_DATA(arena, lvlkey);

	if (lvl->regions)
		return HashGetOne(lvl->regions, name);
	else
		return NULL;
}


local const char * RegionName(Region *rgn)
{
	return rgn->name;
}


local int RegionChunk(Region *rgn, u32 ctype, const void **datap, int *sizep)
{
	if (rgn->chunks)
	{
		chunk *c;

		u32 buf[2] = { 0, 0 };
		buf[0] = ctype;
		c = HashGetOne(rgn->chunks, (const char *)buf);
		if (c)
		{
			if (datap) *datap = c->data;
			if (sizep) *sizep = c->size;
			return TRUE;
		}
	}
	return FALSE;
}


struct enum_all_clos
{
	void *clos;
	void (*cb)(void *clos, Region *rgn);
};

local int enum_all_work(const char *rname, void *rgn, void *clos)
{
	struct enum_all_clos *eac = clos;
	eac->cb(eac->clos, rgn);
	return FALSE;
}

local void EnumContaining(Arena *arena, int x, int y,
		void (*cb)(void *clos, Region *rgn), void *clos)
{
	ELVL *lvl = P_ARENA_DATA(arena, lvlkey);

	if (!lvl->regions || !lvl->rgntiles || !lvl->rgnsets)
		return;

	if (x >= 0 && y >= 0)
	{
		int i = lookup_sparse(lvl->rgntiles, x, y);
		Region **set = lvl->rgnsets[i];
		if (set)
			for (; *set; set++)
				cb(clos, *set);
	}
	else
	{
		/* special case: enum all regions */
		struct enum_all_clos eac = { clos, cb };
		HashEnum(lvl->regions, enum_all_work, &eac);
	}
}


local Region * GetOneContaining(Arena *arena, int x, int y)
{
	ELVL *lvl = P_ARENA_DATA(arena, lvlkey);
	Region **set;

	if (!lvl->regions || !lvl->rgntiles || !lvl->rgnsets)
		return NULL;

	set = lvl->rgnsets[lookup_sparse(lvl->rgntiles, x, y)];
	return set ? *set : NULL;
}


local void md_aaction(Arena *arena, int action)
{
	ELVL *lvl = P_ARENA_DATA(arena, lvlkey);

	if (action == AA_CREATE)
		pthread_mutex_init(&lvl->mtx, NULL);
	else if (action == AA_DESTROY)
		pthread_mutex_destroy(&lvl->mtx);

	/* no matter what is happening, destroy old mapdata */
	if (action == AA_CREATE || action == AA_DESTROY)
		clear_level(lvl);

	/* now, if we're creating, do it */
	if (action == AA_CREATE)
	{
		char mapname[256];
		pthread_mutex_lock(&lvl->mtx);
		if (GetMapFilename(arena, mapname, sizeof(mapname), NULL) &&
		    load_from_file(lvl, mapname))
		{
			lm->LogA(L_INFO, "mapdata", arena, "successfully processed map file '%s'",
					mapname);
		}
		else
		{
			/* fall back to emergency. this matches the compressed map
			 * in mapnewsdl.c. */
			lm->LogA(L_WARN, "mapdata", arena, "error finding or reading level file");
			lvl->tiles = init_sparse();
			insert_sparse(lvl->tiles, 0, 0, 1);
			lvl->flags = lvl->errors = 0;
		}
		pthread_mutex_unlock(&lvl->mtx);
	}
}


/* this module's interface */
local Imapdata mapdataint =
{
	INTERFACE_HEAD_INIT(I_MAPDATA, "mapdata")
	GetMapFilename,
	GetAttr, MapChunk,
	GetFlagCount, GetTile,
	FindEmptyTileNear, FindBrickEndpoints,
	GetChecksum, DoBrick,
	GetMemoryStats,
	FindRegionByName, RegionName,
	RegionChunk, Contains,
	EnumContaining, GetOneContaining
};


EXPORT int MM_mapdata(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		prng = mm->GetInterface(I_PRNG, ALLARENAS);
		if (!cfg || !aman || !lm || !prng) return MM_FAIL;

		lvlkey = aman->AllocateArenaData(sizeof(ELVL));
		if (lvlkey == -1) return MM_FAIL;

		mm->RegCallback(CB_ARENAACTION, md_aaction, ALLARENAS);

		mm->RegInterface(&mapdataint, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&mapdataint, ALLARENAS))
			return MM_FAIL;

		mm->UnregCallback(CB_ARENAACTION, md_aaction, ALLARENAS);

		aman->FreeArenaData(lvlkey);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(prng);

		return MM_OK;
	}
	return MM_FAIL;
}

