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
#include <unistd.h>
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
int RunCommand(char *template, char *selection, char **result) {
	FILE *fifo;
	char *command;
	int ret;

	command = malloc(strlen(template) + strlen(selection) + 1);
	sprintf(command, template, selection);
	printf("executing: %s\n", command);

	fifo = popen(command, "r");
	if (fifo == NULL) {
		*result =
			strdupcat(">> pipe creation error: ", strerror(errno));
		return -1;
	}

	*result = malloc(100);
	if (NULL != fgets(*result, 99, fifo))
		(*result)[strlen(*result) - 1] = '\0';
	else {
		free(*result);
		printf("errno: %d\n", errno);
		*result = errno == EAGAIN ?
			strdupcat(">> no output from command: ", command) :
			strdupcat(">> reading error: ", strerror(errno));
	}

	ret = pclose(fifo);
	printf("result: %d %d %d\n", ret, WIFEXITED(ret), WEXITSTATUS(ret));
	if (WIFEXITED(ret) && WEXITSTATUS(ret) == 0)
		return 0;
	if (WIFEXITED(ret) && WEXITSTATUS(ret) == 1)
		return -1;
	free(*result);
	if (! WIFEXITED(ret))
		*result = strdupcat(">> abnormal command termination: ",
		                    command);
	else if (WEXITSTATUS(ret) == 127)
		*result = strdupcat(">> command not found: ", command);
	else if (WEXITSTATUS(ret) == 126)
		*result = strdupcat(">> command not executable: ", command);
	else
		*result = strdupcat(">> error: ", strerror(WEXITSTATUS(ret)));
	return -1;
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
void DrawWindow(Display *dsp, Window win, GC gc, int ret, char *result) {
	XSetWindowBackground(dsp, win,
		ret ? BlackPixel(dsp, 0) : WhitePixel(dsp, 0));
	XSetForeground(dsp, gc, ret ? WhitePixel(dsp, 0) : BlackPixel(dsp, 0));
	XClearWindow(dsp, win);
	XDrawString(dsp, win, gc, 4, 20, result, strlen(result));
}

/*
 * get a timestamp for "now" (see ICCCM)
 */
Time GetTimestampForNow(Display *d, Window w) {
	XEvent e;

	XChangeProperty(d, w, XA_CURSOR, XA_STRING, 8, PropModeAppend, NULL, 0);
	XWindowEvent(d, w, PropertyChangeMask, &e);
	return e.xproperty.time;
}

/*
 * acquire ownership of the primary selection
 */
Bool AcquirePrimarySelection(Display *d, Window root, Window w, Time *t) {
	Window o;

	XSetSelectionOwner(d, XA_PRIMARY, w, CurrentTime);
	o = XGetSelectionOwner(d, XA_PRIMARY);
	if (o != w) {
		printf("Cannot get selection ownership\n");
		return True;
	}
	if (t != NULL)
		*t = GetTimestampForNow(d, w);
	XDeleteProperty(d, root, XInternAtom(d, "CUT_BUFFER0", True));
	return False;
}

/*
 * check whether target of selection is supported
 */
Bool UnsupportedSelection(Display *d, Atom type, int stringonly) {
	if (type == XA_STRING)
		return False;
	if (type == XInternAtom(d, "TARGETS", False))
		return False;
	if (! stringonly && type == XInternAtom(d, "UTF8_STRING", False))
		return False;
	return True;
}

/*
 * refuse to send selection
 */
void RefuseSelection(Display *d, XSelectionRequestEvent *re) {
	XEvent ne;

	printf("refusing to send selection\n");

	ne.type = SelectionNotify;
	ne.xselection.requestor = re->requestor;
	ne.xselection.selection = re->selection;
	ne.xselection.target = re->target;
	ne.xselection.property = None;
	ne.xselection.time = re->time;

	XSendEvent(d, re->requestor, True, NoEventMask, &ne);
}

/*
 * send the selection to answer a selection request event
 *
 * - it the requested target is not a string (or utf8), do not send it
 * - if the property is none, use the target as the property
 * - check the timestamp: send the selection only if the timestamp is after the
 *   selection ownership assignment (note: CurrentTime may be implemented as 0)
 * - change the property of the requestor
 * - notify the requestor by a PropertyNotify event
 */
Bool SendSelection(Display *d, Time t, XSelectionRequestEvent *re,
		char *chars, int nchars, int stringonly) {
	XEvent ne;
	Atom property;
	int targetlen;
	Atom targetlist[2];

				/* check type of selection requested */

	if (UnsupportedSelection(d, re->target, stringonly)) {
		printf("request for an unsupported type\n");
		RefuseSelection(d, re);
		return True;
	}

				/* check property (obsolete clients) */

	if (re->property != None)
		property = re->property;
	else {
		printf("note: property is None\n");
		property = re->target;
	}

				/* request precedes time of ownership */

	if (re->time < t && re->time != CurrentTime) {
		printf("request precedes selection ownership: ");
		printf("%ld < %ld\n", re->time, t);
		RefuseSelection(d, re);
		return True;
	}

				/* store the selection or the targets */

	if (re->target == XInternAtom(d, "TARGETS", True)) {
		targetlen = 0;
		targetlist[targetlen++] = XInternAtom(d, "STRING", True);
		if (! stringonly)
			targetlist[targetlen++] =
				XInternAtom(d, "UTF8_STRING", True);
		printf("storing selection TARGETS\n");
		XChangeProperty(d, re->requestor, re->property, // re->target,
			XInternAtom(d, "ATOM", True), 32,
			PropModeReplace,
			(unsigned char *) &targetlist, targetlen);
	}
	else {
		printf("storing selection: %s\n", chars);
		XChangeProperty(d, re->requestor, re->property, re->target, 8,
			PropModeReplace,
			(unsigned char *) chars, nchars);
	}

				/* send notification */

	ne.type = SelectionNotify;
	ne.xselection.requestor = re->requestor;
	ne.xselection.selection = re->selection;
	ne.xselection.target = re->target;
	ne.xselection.property = property;
	ne.xselection.time = re->time;

	XSendEvent(d, re->requestor, True, NoEventMask, &ne);

	printf("selection sent and notified\n");

	return False;
}

/*
 * main
 */
int main(int argc, char *argv[]) {
	int opt, usage = False, exitval = EXIT_FAILURE;
	int show = True, flash = False, select = False;
	Display *dsp;
        XSetWindowAttributes swa;
	Window root, win;
	XFontStruct *fs;
	GC gc;
	Bool showing = False;
	int res, ret;
	XEvent ev;
	XKeyEvent *k;
	Time t;
	char *selection = NULL, *result = NULL;
	char *template;

					/* arguments */

	while (-1 != (opt = getopt(argc, argv, "qfsh"))) {
		switch (opt) {
		case 'q':
			show = False;
			break;
		case 'f':
			flash = True;
			break;
		case 's':
			select = True;
			break;
		case 'h':
			exitval = EXIT_SUCCESS;
			// fallthrough
		default:
			usage = True;
			break;
		}
	}
	if (argc - optind < 1 && ! usage) {
		printf("argument required: command\n");
		usage = True;
	}
	if (usage) {
		printf("on F1, run a command on the selection and ");
		printf("show its output\n");
		printf("usage:\n\txselectionrun ");
		printf("[-q] [-f] [-s] [-h] command\n");
		printf("\t\t-q\texecute command but do not show its output\n");
		printf("\t\t-f\tonly flash output for half a second\n");
		printf("\t\t-s\tcommand output becomes the new selection\n");
		printf("\t\t-h\tthis help\n");
		printf("example:\n");
		printf("\txselectionrun \"grep '%%s' data.txt\"\n");
		printf("\tselect some text\n");
		printf("\tpress F1 ");
		printf("(shows the result of running grep with the ");
		printf("selection)\n");
		printf("\tpress F1 ");
		printf("(removes the result from the screen)\n");
		exit(exitval);
	}

	if (strstr(argv[optind], "%s"))
		template = argv[optind];
	else
		template = strdupcat(argv[optind], " '%s'");

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

	XSelectInput(dsp, win,
		KeyPressMask | ExposureMask | PropertyChangeMask);

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
				if (showing) {
					XUnmapWindow(dsp, win);
					showing = False;
					if (! select) {
						free(result);
						result = NULL;
					}
					break;
				}
				showing = True;
				if (XGetSelectionOwner(dsp, XA_PRIMARY) ==
				    win) {
					if (show)
						XMapWindow(dsp, win);
					break;
				}
				XConvertSelection(dsp, XA_PRIMARY, XA_STRING,
						XA_PRIMARY, win, CurrentTime);
				// -> SelectionNotify
			}
			break;
		case SelectionNotify:
			selection = GetSelection(dsp, win, ev);
			if (selection == NULL) {
				printf("cannot retrieve the selection\n");
				showing = False;
				break;
			}
			ret = RunCommand(template, selection, &result);
			free(selection);
			if (ret) {
				printf("failed running command\n");
				showing = False;
				break;
			}
			printf("command execution: %d \"%s\"\n", ret, result);
			WindowAtPointer(dsp, win);
			if (show)
				XMapWindow(dsp, win);
			else
				showing = False;
			if (select)
				AcquirePrimarySelection(dsp, root, win, &t);
			break;
		case SelectionRequest:
			if (result == NULL)
				break;
			SendSelection(dsp, t, &ev.xselectionrequest,
				result, strlen(result), 1);
			break;
		case KeyRelease:
			printf("key release\n");
			break;
		case Expose:
			printf("expose\n");
			if (result == NULL)
				break;
			DrawWindow(dsp, win, gc, ret, result);
			if (flash) {
				XFlush(dsp);
				usleep(500000);
				XUnmapWindow(dsp, win);
				showing = False;
			}
			break;
		case PropertyNotify:
			printf("property notify\n");
			break;
		case SelectionClear:
			printf("selection clear\n");
			if (flash && select) {
				XUnmapWindow(dsp, win);
				showing = False;
				free(result);
				result = NULL;
			}
			break;
		default:
			printf("unexpected event of type %d\n", ev.type);
		}
	}

					/* finish */

	XCloseDisplay(dsp);
	return EXIT_SUCCESS;
}

