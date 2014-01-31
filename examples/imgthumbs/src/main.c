#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "opengl.h"
#include "resman.h"
#include "thumbs.h"

static int init(void);
static void cleanup(void);
static void display(void);
/*static void idle(void);*/
static void reshape(int x, int y);
static void keyb(unsigned char key, int x, int y);
static void mouse(int bn, int st, int x, int y);
static void motion(int x, int y);
static void sball_motion(int x, int y, int z);
static struct thumbnail *find_thumb(int x, int y);

const char *path = ".";
struct resman *texman;
int win_width, win_height;
float win_aspect;
float pan_x, pan_y;
float show_pan_x, show_pan_y;
float show_zoom = 1.0;
float thumbs_size = 0.25;

struct thumbnail *thumbs, *show_thumb;

int main(int argc, char **argv)
{
	glutInitWindowSize(1024, 768);
	glutInit(&argc, argv);

	if(argv[1]) {
		path = argv[1];
	}

	glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
	glutCreateWindow("imgthumbs");

	glutDisplayFunc(display);
	glutReshapeFunc(reshape);
	glutKeyboardFunc(keyb);
	glutMouseFunc(mouse);
	glutMotionFunc(motion);
	glutSpaceballMotionFunc(sball_motion);

	if(init() == -1) {
		return 1;
	}
	atexit(cleanup);

	glutMainLoop();
	return 0;
}

static int init(void)
{
	glewInit();

	thumbs = create_thumbs(path);
	return 0;
}

static void cleanup(void)
{
	free_thumbs(thumbs);
}

static void display(void)
{
	glClear(GL_COLOR_BUFFER_BIT);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	if(show_thumb) {
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, show_thumb->tex);

		glScalef(show_zoom, show_zoom, 1);
		glTranslatef(2.0 * show_pan_x, 2.0 * show_pan_y, 0);
		if(show_thumb->aspect >= win_aspect) {
			glScalef(1, 1.0 / show_thumb->aspect, 1);
		} else {
			glScalef(show_thumb->aspect / win_aspect, 1.0 / win_aspect, 1);
		}

		glBegin(GL_QUADS);
		glColor3f(1, 1, 1);
		glTexCoord2f(0, 0); glVertex2f(-1, -1);
		glTexCoord2f(1, 0); glVertex2f(1, -1);
		glTexCoord2f(1, 1); glVertex2f(1, 1);
		glTexCoord2f(0, 1); glVertex2f(-1, 1);
		glEnd();

		glDisable(GL_TEXTURE_2D);
	} else {
		draw_thumbs(thumbs, thumbs_size, pan_y);
	}

	glutSwapBuffers();
	assert(glGetError() == GL_NO_ERROR);
}

/*
static void idle(void)
{
	glutPostRedisplay();
}
*/

static void reshape(int x, int y)
{
	win_aspect = (float)x / (float)y;

	glViewport(0, 0, x, y);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(-1, 1, 1.0 / win_aspect, -1.0 / win_aspect, -1, 1);

	win_width = x;
	win_height = y;
}

static void keyb(unsigned char key, int x, int y)
{
	switch(key) {
	case 27:
		if(show_thumb) {
			show_thumb = 0;
			glutPostRedisplay();
		} else {
			exit(0);
		}
		break;

	case ' ':
		show_zoom = 1.0;
		thumbs_size = 0.25;
		pan_x = pan_y = show_pan_x = show_pan_y = 0;
		glutPostRedisplay();
		break;
	}
}

static int bnstate[32];
static int prev_x, prev_y;
static int click_x[32], click_y[32];

static void mouse(int bn, int st, int x, int y)
{
	int bidx = bn - GLUT_LEFT_BUTTON;
	int state = st == GLUT_DOWN ? 1 : 0;

	bnstate[bidx] = state;

	prev_x = x;
	prev_y = y;

	if(state) {
		click_x[bidx] = x;
		click_y[bidx] = y;
	} else {
		int is_drag = abs(x - click_x[bidx]) > 3 || abs(y - click_y[bidx]) > 3;

		if(bidx == 0) {
			if(!show_thumb) {
				if(!is_drag) {
					struct thumbnail *sel = find_thumb(x, y);
					if(sel) {
						show_thumb = sel;
						show_pan_x = show_pan_y = 0;
						glutPostRedisplay();
					}
				}
			}
		} else {
			if(!is_drag) {
				show_thumb = 0;
				glutPostRedisplay();
			}
		}
	}
}

static void motion(int x, int y)
{
	int dx = x - prev_x;
	int dy = y - prev_y;
	prev_x = x;
	prev_y = y;

	if(!dx && !dy) return;

	if(bnstate[0]) {
		float fdx = dx / (float)win_width;
		float fdy = dy / (float)win_height / win_aspect;

		if(show_thumb) {
			show_pan_x += fdx / show_zoom;
			show_pan_y += fdy / show_zoom;
		} else {
			pan_x += fdx;
			pan_y += fdy;
		}
		glutPostRedisplay();
	}

	if(bnstate[2]) {
		if(show_thumb) {
			show_zoom -= dy * 0.0075;
			if(show_zoom <= 0) show_zoom = 0;
		} else {
			thumbs_size -= dy * 0.005;
			if(thumbs_size <= 0.01) thumbs_size = 0.01;
		}
		glutPostRedisplay();
	}
}

static void sball_motion(int x, int y, int z)
{
	float fx = -x * 0.0004;
	float fy = z * 0.0004;
	float fz = -y * 0.0005;

	if(show_thumb) {
		show_pan_x += fx / show_zoom;
		show_pan_y += fy / show_zoom;
		show_zoom += fz;
		if(show_zoom <= 0) show_zoom = 0;
	} else {
		pan_x += fx;
		pan_y += fy;
		thumbs_size += fz;
		if(thumbs_size <= 0.01) thumbs_size = 0.01;
	}
	glutPostRedisplay();
}

static struct thumbnail *find_thumb(int x, int y)
{
	float fx = (float)x / (float)win_width;
	float fy = (float)y / (float)win_height / win_aspect;
	struct thumbnail *node;

	node = thumbs;
	while(node) {
		float nx = node->layout_pos[0];
		float ny = node->layout_pos[1];

		if(fx >= nx && fx < nx + node->layout_size[0] &&
				fy >= ny && fy < ny + node->layout_size[1]) {
			return node;
		}
		node = node->next;
	}
	return 0;
}
