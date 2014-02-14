/*
libresman - a multithreaded resource data file manager.
Copyright (C) 2014  John Tsiombikas <nuclear@member.fsf.org>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dynarr.h"

/* The array descriptor keeps auxilliary information needed to manipulate
 * the dynamic array. It's allocated adjacent to the array buffer.
 */
struct arrdesc {
	int nelem, szelem;
	int max_elem;
	int bufsz;	/* not including the descriptor */
};

#define DESC(x)		((struct arrdesc*)((char*)(x) - sizeof(struct arrdesc)))

void *dynarr_alloc(int elem, int szelem)
{
	struct arrdesc *desc;

	if(!(desc = malloc(elem * szelem + sizeof *desc))) {
		return 0;
	}
	desc->nelem = desc->max_elem = elem;
	desc->szelem = szelem;
	desc->bufsz = elem * szelem;
	return (char*)desc + sizeof *desc;
}

void dynarr_free(void *da)
{
	if(da) {
		free(DESC(da));
	}
}

void *dynarr_resize(void *da, int elem)
{
	int newsz;
	void *tmp;
	struct arrdesc *desc;

	if(!da) return 0;
	desc = DESC(da);

	newsz = desc->szelem * elem;

	if(!(tmp = realloc(desc, newsz + sizeof *desc))) {
		return 0;
	}
	desc = tmp;

	desc->nelem = desc->max_elem = elem;
	desc->bufsz = newsz;
	return (char*)desc + sizeof *desc;
}

int dynarr_empty(void *da)
{
	return DESC(da)->nelem ? 0 : 1;
}

int dynarr_size(void *da)
{
	return DESC(da)->nelem;
}


/* stack semantics */
void *dynarr_push(void *da, void *item)
{
	struct arrdesc *desc;
	int nelem;

	desc = DESC(da);
	nelem = desc->nelem;

	if(nelem >= desc->max_elem) {
		/* need to resize */
		struct arrdesc *tmp;
		int newsz = desc->max_elem ? desc->max_elem * 2 : 1;

		if(!(tmp = dynarr_resize(da, newsz))) {
			fprintf(stderr, "failed to resize\n");
			return da;
		}
		da = tmp;
		desc = DESC(da);
		desc->nelem = nelem;
	}

	if(item) {
		memcpy((char*)da + desc->nelem++ * desc->szelem, item, desc->szelem);
	}
	return da;
}

void *dynarr_pop(void *da)
{
	struct arrdesc *desc;
	int nelem;

	desc = DESC(da);
	nelem = desc->nelem;

	if(!nelem) return da;

	if(nelem <= desc->max_elem / 3) {
		/* reclaim space */
		struct arrdesc *tmp;
		int newsz = desc->max_elem / 2;

		if(!(tmp = dynarr_resize(da, newsz))) {
			fprintf(stderr, "failed to resize\n");
			return da;
		}
		da = tmp;
		desc = DESC(da);
		desc->nelem = nelem;
	}
	desc->nelem--;

	return da;
}
