#ifndef THUMBS_H_
#define THUMBS_H_

struct img_pixmap;

struct thumbnail {
	char *fname;
	unsigned int tex;
	float aspect;

	float layout_pos[2];
	float layout_size[2];

	struct img_pixmap *img;

	struct thumbnail *next, *prev;
};

struct thumbnail *create_thumbs(const char *dirpath);
void free_thumbs(struct thumbnail *thumbs);

void update_thumbs(void);
void draw_thumbs(struct thumbnail *thumbs, float thumb_sz, float start_y);

#endif	/* THUMBS_H_ */
