/*
 * xselectionrun.c
 *
 * on pressing F1, run a command on the selection and show the result
 */

/*
 * todo:
 * - document that the selection is currently limited to 200 bytes
 * - read result until the end, not just the first line
 * - display multiple lines
 * - wrap text in the window
 * - adapt window size to the program output
 * - find a better name?
 * - option -e to send an 'esc' key press to the focus window to make it
 *   remove its context menu
 * - allow multiple command, choose which to run with keys 1,2,3... or by
 *   a dropdown menu
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>

/*
 * font
 */
#define FONT "-*-*-medium-r-*-*-18-*-*-*-m-*-iso10646-1"

/*
 * concatenate two strings
 */
char *strdupcat(char *a, char *b) {
	char *res;

	res = malloc(strlen(a) + strlen(b) + 1);
	strcpy(res, a);
	strcat(res, b);

	return res;
}

/*
 * retrieve the selection
 */
char *GetSelection(Display *dsp, Window win, XEvent ev) {
	Bool res;
	int format;
	unsigned long i, nitems, after;
	unsigned char *string;
	Atom actualtype;
	char *r;

	res = XGetWindowProperty(dsp, win, ev.xselection.selection,
			0, 200, True, ev.xselection.target,
			&actualtype, &format, &nitems, &after, &string);
	if (res != Success)
		return NULL;
	if (actualtype != XA_STRING)
		return NULL;

	printf("bytes left: %lu\n", after);
	for (i = 0; i < nitems; i++)
		printf("%c", string[i]);
	printf("\n");

	r = strdup((char *) string);
	XFree(string);
	return r;
}

/*
 * run the command, return the first line of the result
 */
char *RunCommand(char *template, char *selection) {
	FILE *fifo;
	char *command;
	char *result;
	int ret;

	command = malloc(strlen(template) + strlen(selection) + 1);
	sprintf(command, template, selection);
	printf("executing %s\n", command);

	fifo = popen(command, "r");
	if (fifo == NULL)
		return strdupcat(">> pipe creation error: ", strerror(errno));

	result = malloc(100);
	if (NULL != fgets(result, 99, fifo))
		result[strlen(result) - 1] = '\0';
	else {
		free(result);
		result = errno == EAGAIN ?
			strdupcat(">> no output from command: ", command) :
			strdupcat(">> reading error: ", strerror(errno));
	}

	ret = pclose(fifo);
	printf("result: %d %d %d\n", ret, WIFEXITED(ret), WEXITSTATUS(ret));
	if (WIFEXITED(ret) && WEXITSTATUS(ret) == 0)
		return result;
	free(result);
	if (! WIFEXITED(ret))
		return strdupcat(">> abnormal command termination: ", command);
	if (WEXITSTATUS(ret) == 127)
		return strdupcat(">> command not found: ", command);
	if (WEXITSTATUS(ret) == 126)
		return strdupcat(">> command not executable: ", command);
	return strdup(strerror(WEXITSTATUS(ret)));
}

/*
 * position of the pointer
 */
void PointerPosition(Display *d, Window r, int *x, int *y) {
	Window child;
	int wx, wy;
	unsigned int mask;

	XQueryPointer(d, r, &r, &child, x, y, &wx, &wy, &mask);
}

/*
 * place the window at cursor
 */
void WindowAtPointer(Display *d, Window w) {
	int x, y;
	unsigned int width, height, border, depth, rwidth, rheight, rborder;
	Window root, r;

	XGetGeometry(d, w, &root, &x, &y, &width, &height, &border, &depth);
	XGetGeometry(d, root, &r, &x, &y, &rwidth, &rheight, &rborder, &depth);

	PointerPosition(d, root, &x, &y);
	x =  x - (int) width / 2;
	if (x < 0)
		x = border;
	if (x + width >= rwidth)
		x = rwidth - width - 2 * border;
	y = y + 10 + height + 2 * border < rheight ?
		y + 10 :
		y - 10 - (int) height;

	XMoveWindow(d, w, x, y);
}

/*
 * draw window
 */
void DrawWindow(Display *dsp, Window win, GC gc, char *result) {
	XClearWindow(dsp, win);
	XDrawString(dsp, win, gc, 4, 20, result, strlen(result));
}

/*
 * main
 */
int main(int argn, char *argv[]) {
	Display *dsp;
        XSetWindowAttributes swa;
	Window root, win;
	XFontStruct *fs;
	GC gc;
	Bool serving = False;
	int res;
	XEvent ev;
	XKeyEvent *k;
	char *selection = NULL, *result = NULL;
	char *template;

					/* arguments */

	if (argn - 1 < 1) {
		printf("argument required: see -h for details\n");
		exit(EXIT_FAILURE);
	}
	else if (! strcmp(argv[1], "-h")) {
		printf("on F1, run a command on the selection and ");
		printf("show its output\n");
		printf("usage example:\n");
		printf("\txselectionrun \"grep '%%s' data.txt\"\n");
		printf("\tselect some text\n");
		printf("\tpress F1 ");
		printf("(shows the result of running grep with the ");
		printf("selection)\n");
		printf("\tpress F1 ");
		printf("(removes the result from the screen)\n");
		exit(EXIT_SUCCESS);
	}

	if (strstr(argv[1], "%s"))
		template = argv[1];
	else
		template = strdupcat(argv[1], " '%s'");

					/* open display */

	dsp = XOpenDisplay(getenv("DISPLAY"));
	if (dsp == NULL) {
		printf("cannot open display\n");
		exit(EXIT_FAILURE);
	}
	root = DefaultRootWindow(dsp);

					/* create window */

	swa.background_pixel = WhitePixelOfScreen(DefaultScreenOfDisplay(dsp));
	swa.override_redirect = True;
	win = XCreateWindow(dsp, root, 200, 100, 640, 30, 1,
			CopyFromParent, CopyFromParent, CopyFromParent,
			CWBackPixel | CWOverrideRedirect, &swa);

	fs = XLoadQueryFont(dsp, FONT);
	gc = XCreateGC(dsp, win, 0, NULL);
	XSetFont(dsp, gc, fs->fid);

					/* select events and grab */

	XSelectInput(dsp, win, KeyPressMask | ExposureMask);

	res = XGrabKey(dsp, XKeysymToKeycode(dsp, XK_F1), 0,
			root, False, GrabModeAsync, GrabModeAsync);
	if (res != 1) {
		printf("cannot grab F1\n");
		XCloseDisplay(dsp);
		exit(EXIT_FAILURE);
	}

					/* main loop */

	while (1) {
		XNextEvent(dsp, &ev);
		switch (ev.type) {
		case KeyPress:
			k = &ev.xkey;
			if (XLookupKeysym(k, 0) == XK_F1 && k->state == 0) {
				printf("F1 press\n");
				if (serving) {
					serving = False;
					XUnmapWindow(dsp, win);
					free(result);
					result = NULL;
					break;
				}
				serving = True;
				XConvertSelection(dsp, XA_PRIMARY, XA_STRING,
						XA_PRIMARY, win, CurrentTime);
				// -> SelectionNotify
			}
			break;
		case SelectionNotify:
			selection = GetSelection(dsp, win, ev);
			if (selection == NULL) {
				printf("cannot retrieve the selection\n");
				serving = False;
				break;
			}
			result = RunCommand(template, selection);
			free(selection);
			if (result == NULL) {
				printf("failed running command\n");
				serving = False;
				break;
			}
			printf("%s\n", result);
			WindowAtPointer(dsp, win);
			XMapWindow(dsp, win);
			break;
		case KeyRelease:
			printf("key release\n");
			break;
		case Expose:
			if (result == NULL)
				break;
			DrawWindow(dsp, win, gc, result);
			break;
		default:
			printf("unexpected event of type %d\n", ev.type);
		}
	}

					/* finish */

	XCloseDisplay(dsp);
	return EXIT_SUCCESS;
}

