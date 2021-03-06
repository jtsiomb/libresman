#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <dirent.h>
#include <imago2.h>
#include "opengl.h"
#include "thumbs.h"
#include "resman.h"

static int load_res_texture(const char *fname, int id, void *cls);
static int done_res_texture(int id, void *cls);
static void free_res_texture(int id, void *cls);

struct resman *texman;
int dbg_load_async = 1;

struct thumbnail *create_thumbs(const char *dirpath)
{
	DIR *dir;
	struct dirent *dent;
	/* allocate dummy head node */
	struct thumbnail *list = calloc(1, sizeof *list);
	char *env;

	if((env = getenv("RESMAN_LOAD_ASYNC"))) {
		dbg_load_async = atoi(env);
	}

	if(!texman) {
		texman = resman_create();
		resman_set_load_func(texman, load_res_texture, 0);
		resman_set_done_func(texman, done_res_texture, 0);
		resman_set_destroy_func(texman, free_res_texture, 0);
	}

	if(!(dir = opendir(dirpath))) {
		fprintf(stderr, "failed to open directory: %s: %s\n", dirpath, strerror(errno));
		return 0;
	}

	while((dent = readdir(dir))) {
		struct img_pixmap img;
		struct thumbnail *node;

		if(!(node = malloc(sizeof *node))) {
			perror("failed to allocate thumbnail list node");
			continue;
		}
		memset(node, 0, sizeof *node);

		if(!(node->fname = malloc(strlen(dirpath) + strlen(dent->d_name) + 2))) {
			free(node);
			continue;
		}
		strcpy(node->fname, dirpath);
		if(dirpath[strlen(dirpath) - 1] != '/') {
			strcat(node->fname, "/");
		}
		strcat(node->fname, dent->d_name);

		node->aspect = 1.0;

		if(dbg_load_async) {
			resman_add(texman, node->fname, node);
		} else {
			img_init(&img);
			if(img_load(&img, node->fname) == -1) {
				img_destroy(&img);
				free(node->fname);
				free(node);
				continue;
			}

			printf("loaded image: %s\n", node->fname);

			node->aspect = (float)img.width / (float)img.height;

			glGenTextures(1, &node->tex);
			glBindTexture(GL_TEXTURE_2D, node->tex);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexImage2D(GL_TEXTURE_2D, 0, img_glintfmt(&img), img.width, img.height, 0, img_glfmt(&img), img_gltype(&img), img.pixels);
			img_destroy(&img);

			node->prev = list;
			node->next = list->next;
			if(list->next) list->next->prev = node;
			list->next = node;
		}
		node->list = list;
		node->load_count = 0;
	}
	closedir(dir);

	return list;
}

void free_thumbs(struct thumbnail *thumbs)
{
	if(!thumbs) return;

	while(thumbs) {
		struct thumbnail *tmp = thumbs;
		thumbs = thumbs->next;

		free(tmp->fname);
		free(tmp);
	}
}


void update_thumbs(void)
{
	resman_poll(texman);
}

void draw_thumbs(struct thumbnail *thumbs, float thumb_sz, float start_y)
{
	int vp[4];
	float gap = thumb_sz / 4.0;
	float x = gap;
	float y = gap + start_y;
	float view_aspect;

	glGetIntegerv(GL_VIEWPORT, vp);
	view_aspect = (float)(vp[2] - vp[0]) / (float)(vp[3] - vp[1]);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, 1, 1.0 / view_aspect, 0, -1, 1);

	glMatrixMode(GL_MODELVIEW);

	thumbs = thumbs->next;	/* skip dummy node */
	while(thumbs) {
		glPushMatrix();
		glTranslatef(x, y, 0);

		glScalef(thumb_sz, thumb_sz, 1);

		glBegin(GL_QUADS);
		glColor3f(0.25, 0.25, 0.25);
		glTexCoord2f(0, 0); glVertex2f(0, 0);
		glTexCoord2f(1, 0); glVertex2f(1, 0);
		glTexCoord2f(1, 1); glVertex2f(1, 1);
		glTexCoord2f(0, 1); glVertex2f(0, 1);
		glEnd();

		if(thumbs->tex) {
			if(thumbs->aspect >= 1.0) {
				glTranslatef(0, 0.5 - 0.5 / thumbs->aspect, 0);
				glScalef(1, 1.0 / thumbs->aspect, 1);
			} else {
				glTranslatef(0.5 - thumbs->aspect / 2.0, 0, 0);
				glScalef(thumbs->aspect, 1, 1);
			}

			if(glIsTexture(thumbs->tex)) {
				glEnable(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, thumbs->tex);

				glBegin(GL_QUADS);
				glColor3f(1, 1, 1);
				glTexCoord2f(0, 0); glVertex2f(0, 0);
				glTexCoord2f(1, 0); glVertex2f(1, 0);
				glTexCoord2f(1, 1); glVertex2f(1, 1);
				glTexCoord2f(0, 1); glVertex2f(0, 1);
				glEnd();
			} else {
				fprintf(stderr, "invalid texture: %u\n", thumbs->tex);
			}

			glPopMatrix();
			glDisable(GL_TEXTURE_2D);
		}

		thumbs->layout_pos[0] = x;
		thumbs->layout_pos[1] = y;
		thumbs->layout_size[0] = thumb_sz;
		thumbs->layout_size[1] = thumb_sz;

		x += thumb_sz + gap;
		if(x >= 1.0 - thumb_sz) {
			x = gap;
			y += thumb_sz + gap;
		}

		thumbs = thumbs->next;
	}

	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
}

static int load_res_texture(const char *fname, int id, void *cls)
{
	struct thumbnail *rdata = resman_get_res_data(texman, id);

	assert(rdata);
	if(!rdata->img) {
		if(!(rdata->img = img_create())) {
			return -1;
		}
	}

	if(img_load(rdata->img, fname) == -1) {
		img_free(rdata->img);
		rdata->img = 0;
		return -1;
	}
	rdata->aspect = (float)rdata->img->width / (float)rdata->img->height;

	/* set the resource's data to the loaded image, so that we can use
	 * it in the done callback */
	resman_set_res_data(texman, id, rdata);
	return 0;
}

static int done_res_texture(int id, void *cls)
{
	struct thumbnail *thumb = resman_get_res_data(texman, id);
	int load_result = resman_get_res_result(texman, id);

	if(load_result == -1) {
		/* returning -1 will remove this resource, the free_res_texture
		 * destroy handler will be called, which will remove the node
		 * from the list
		 */
		return -1;
	}

	if(resman_get_res_result(texman, id) != 0 || !thumb) {
		fprintf(stderr, "failed to load resource %d (%s)\n", id, resman_get_res_name(texman, id));
	} else {
		printf("done loading resource %d (%s)\n", id, resman_get_res_name(texman, id));
	}

	if(!thumb->tex) {
		glGenTextures(1, &thumb->tex);
	}
	glBindTexture(GL_TEXTURE_2D, thumb->tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, img_glintfmt(thumb->img),
			thumb->img->width, thumb->img->height, 0, img_glfmt(thumb->img),
			img_gltype(thumb->img), thumb->img->pixels);

	/* and add it to the list of thumbnails (if it's the first loading) */
	if(resman_get_res_load_count(texman, id) == 0) {
		thumb->prev = thumb->list;
		thumb->next = thumb->list->next;
		if(thumb->list->next) thumb->list->next->prev = thumb;
		thumb->list->next = thumb;
	}
	return 0;
}

static void free_res_texture(int id, void *cls)
{
	struct thumbnail *thumb = resman_get_res_data(texman, id);

	printf("deleting thumb %d: %s\n", id, thumb->fname);

	if(thumb) {
		if(thumb->tex) {
			glDeleteTextures(1, &thumb->tex);
		}
		if(thumb->img) {
			img_free(thumb->img);
		}
	}

	/* remove from the list */
	if(thumb->prev) {
		thumb->prev->next = thumb->next;
	}
	if(thumb->next) {
		thumb->next->prev = thumb->prev;
	}
	free(thumb);
}
