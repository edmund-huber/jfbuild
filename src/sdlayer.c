// SDL interface layer
// for the Build Engine
// by Jonathon Fowler (jonof@edgenetwk.com)
//
// Use SDL1.2 from http://www.libsdl.org

#include <stdlib.h>
#include <math.h>
#include "SDL.h"
#include "compat.h"
#include "sdlayer.h"
#include "cache1d.h"
#include "pragmas.h"
#include "a.h"
#include "build.h"
#include "osd.h"

#ifdef USE_OPENGL
//#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#endif

#define SURFACE_FLAGS	(SDL_SWSURFACE|SDL_HWPALETTE|SDL_HWACCEL)

// undefine to restrict windowed resolutions to conventional sizes
#define ANY_WINDOWED_SIZE

int   _buildargc = 1;
char *_buildargv[30] = { "buildapp" };
extern long app_main(long argc, char *argv[]);

static char gamma_saved = 0, gamma_supported = 0;
static unsigned short sysgamma[3*256];

char quitevent=0, appactive=1;

// video
static SDL_Surface *sdl_surface;
long xres=-1, yres=-1, bpp=0, fullscreen=0, bytesperline, imageSize;
long frameplace=0, lockcount=0;
char modechange=1;
char offscreenrendering=0;
char videomodereset = 0;
char nofog=0;

#ifdef USE_OPENGL
// OpenGL stuff
struct glinfo glinfo = { "Unknown","Unknown","0.0.0","", 1.0, 0,0,0 };
#endif

// input
char inputdevices=0;
char keystatus[256], keyfifo[KEYFIFOSIZ], keyfifoplc, keyfifoend;
short mousex=0,mousey=0,mouseb=0;
long joyaxis[16], joyb=0;
char joyisgamepad=0, joynumaxes=0, joynumbuttons=0, joynumhats=0;
long joyaxespresent=0;
extern char moustat;


void (*keypresscallback)(long,long) = 0;
void (*mousepresscallback)(long,long) = 0;
void (*joypresscallback)(long,long) = 0;

//static SDL_Surface * loadtarga(const char *fn);		// for loading the icon


//
//
// ---------------------------------------
//
// System
//
// ---------------------------------------
//
//

int main(int argc, char *argv[])
{
	_buildargc = argc;
	memcpy(_buildargv, argv, sizeof(argv[0])*argc);

	baselayer_init();
	return app_main(argc, argv);
}


//
// initsystem() -- init SDL systems
//
int initsystem(void)
{
	const SDL_VideoInfo *vid;
	const SDL_version *linked = SDL_Linked_Version();
	SDL_version compiled;
	char drvname[32];

	SDL_VERSION(&compiled);

	initprintf("Initializing SDL system interface "
		  "(compiled with SDL version %d.%d.%d, DLL version %d.%d.%d)\n",
		linked->major, linked->minor, linked->patch,
		compiled.major, compiled.minor, compiled.patch);

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER
#ifdef NOSDLPARACHUTE
			| SDL_INIT_NOPARACHUTE
#endif
		)) {
		initprintf("Initialization failed! (%s)\n", SDL_GetError());
		return -1;
	}

	atexit(uninitsystem);

	frameplace = 0;
	lockcount = 0;

/*
	{
		SDL_Surface *icon;
		icon = loadtarga("icon.tga");
		if (icon) {
			SDL_WM_SetIcon(icon, 0);
			SDL_FreeSurface(icon);
		}
	}
*/
	if (SDL_VideoDriverName(drvname, 32))
		initprintf("Using \"%s\" video driver\n", drvname);

	// dump a quick summary of the graphics hardware
	vid = SDL_GetVideoInfo();
	initprintf("Video device information:\n");
	initprintf("  Can create hardware surfaces?          %s\n", (vid->hw_available)?"Yes":"No");
	initprintf("  Window manager available?              %s\n", (vid->wm_available)?"Yes":"No");
	initprintf("  Accelerated hardware blits?            %s\n", (vid->blit_hw)?"Yes":"No");
	initprintf("  Accelerated hardware colourkey blits?  %s\n", (vid->blit_hw_CC)?"Yes":"No");
	initprintf("  Accelerated hardware alpha blits?      %s\n", (vid->blit_hw_A)?"Yes":"No");
	initprintf("  Accelerated software blits?            %s\n", (vid->blit_sw)?"Yes":"No");
	initprintf("  Accelerated software colourkey blits?  %s\n", (vid->blit_sw_CC)?"Yes":"No");
	initprintf("  Accelerated software alpha blits?      %s\n", (vid->blit_sw_A)?"Yes":"No");
	initprintf("  Accelerated colour fills?              %s\n", (vid->blit_fill)?"Yes":"No");
	initprintf("  Total video memory:                    %dKB\n", vid->video_mem);

	return 0;
}


//
// uninitsystem() -- uninit SDL systems
//
void uninitsystem(void)
{
	uninitinput();
	uninitmouse();
	uninittimer();

	SDL_Quit();
}


//
// initprintf() -- prints a string to the intitialization window
//
void initprintf(const char *f, ...)
{
	va_list va;
	char buf[1024];
	
	va_start(va, f);
	vprintf(f,va);
	va_end(va);

	va_start(va, f);
	Bvsnprintf(buf, 1024, f, va);
	va_end(va);
	OSD_Printf(buf);
}


//
// debugprintf() -- prints a debug string to stderr
//
void debugprintf(const char *f, ...)
{
#ifdef DEBUGGINGAIDS
	va_list va;

	va_start(va,f);
	Bvfprintf(stderr, f, va);
	va_end(va);
#endif
}


//
//
// ---------------------------------------
//
// All things Input
//
// ---------------------------------------
//
//

static char mouseacquired=1;
static long joyblast=0;


//
// initinput() -- init input system
//
int initinput(void)
{
	if (SDL_EnableKeyRepeat(250, 30)) initprintf("Error enabling keyboard repeat.\n");
	inputdevices = 1|2;	// keyboard (1) and mouse (2)
	mouseacquired = 0;

	return 0;
}

//
// uninitinput() -- uninit input system
//
void uninitinput(void)
{
	grabmouse(0);
}

//
// set{key|mouse|joy}presscallback() -- sets a callback which gets notified when keys are pressed
//
void setkeypresscallback(void (*callback)(long, long)) { keypresscallback = callback; }
void setmousepresscallback(void (*callback)(long, long)) { mousepresscallback = callback; }
void setjoypresscallback(void (*callback)(long, long)) { joypresscallback = callback; }

//
// initmouse() -- init mouse input
//
int initmouse(void)
{
	if (moustat) return 0;

	initprintf("Initializing mouse\n");

	// grab input
	grabmouse(1);
	moustat=1;

	return 0;
}

//
// uninitmouse() -- uninit mouse input
//
void uninitmouse(void)
{
	if (!moustat) return;

	grabmouse(0);
	moustat=0;
}


//
// grabmouse() -- show/hide mouse cursor
//
void grabmouse(char a)
{
	SDL_GrabMode g;
	
	if (appactive && moustat) {
		if (a != mouseacquired) {
			g = SDL_WM_GrabInput( a ? SDL_GRAB_ON : SDL_GRAB_OFF );
			mouseacquired = (g == SDL_GRAB_ON);

			SDL_ShowCursor(mouseacquired ? SDL_DISABLE : SDL_ENABLE);
		}
	} else {
		mouseacquired = a;
	}
}


//
// readmousexy() -- return mouse motion information
//
void readmousexy(short *x, short *y)
{
	if (!mouseacquired) { *x = *y = 0; return; }
	*x = mousex;
	*y = mousey;
	mousex = mousey = 0;
}

//
// readmousebstatus() -- return mouse button information
//
void readmousebstatus(short *b)
{
	if (!mouseacquired) *b = 0;
	else *b = mouseb;
}

//
// setjoydeadzone() -- sets the dead and saturation zones for the joystick
//
void setjoydeadzone(int axis, unsigned short dead, unsigned short satur)
{
}


//
// getjoydeadzone() -- gets the dead and saturation zones for the joystick
//
void getjoydeadzone(int axis, unsigned short *dead, unsigned short *satur)
{
	*dead = *satur = 0;
}


//
// releaseallbuttons()
//
void releaseallbuttons(void)
{
}


//
//
// ---------------------------------------
//
// All things Timer
// Ken did this
//
// ---------------------------------------
//
//

static Uint32 timerfreq=0;
static Uint32 timerlastsample=0;
static Uint32 timerticspersec=0;
static void (*usertimercallback)(void) = NULL;

//
// inittimer() -- initialize timer
//
int inittimer(int tickspersecond)
{
	if (timerfreq) return 0;	// already installed

	initprintf("Initializing timer\n");

	timerfreq = 1000;
	timerticspersec = tickspersecond;
	timerlastsample = SDL_GetTicks() * timerticspersec / timerfreq;

	usertimercallback = NULL;

	return 0;
}

//
// uninittimer() -- shut down timer
//
void uninittimer(void)
{
	if (!timerfreq) return;

	timerfreq=0;
}

//
// sampletimer() -- update totalclock
//
void sampletimer(void)
{
	Uint32 i;
	long n;
	
	if (!timerfreq) return;
	
	i = SDL_GetTicks();
	n = (long)(i * timerticspersec / timerfreq) - timerlastsample;
	if (n>0) {
		totalclock += n;
		timerlastsample += n;
	}

	if (usertimercallback) for (; n>0; n--) usertimercallback();
}

//
// getticks() -- returns the sdl ticks count
//
unsigned long getticks(void)
{
	return (unsigned long)SDL_GetTicks();
}


//
// gettimerfreq() -- returns the number of ticks per second the timer is configured to generate
//
int gettimerfreq(void)
{
	return timerticspersec;
}


//
// installusertimercallback() -- set up a callback function to be called when the timer is fired
//
void (*installusertimercallback(void (*callback)(void)))(void)
{
	void (*oldtimercallback)(void);

	oldtimercallback = usertimercallback;
	usertimercallback = callback;

	return oldtimercallback;
}



//
//
// ---------------------------------------
//
// All things Video
//
// ---------------------------------------
//
// 


//
// getvalidmodes() -- figure out what video modes are available
//
static char modeschecked=0;
void getvalidmodes(void)
{
	static int cdepths[] = {
		8,
#ifdef USE_OPENGL
		16,24,32,
#endif
		0 };
	static int defaultres[][2] = {
		{1280,1024},{1280,960},{1152,864},{1024,768},{800,600},{640,480},
		{640,400},{512,384},{480,360},{400,300},{320,240},{320,200},{0,0}
	};
	SDL_Rect **modes;
	SDL_PixelFormat pf = { NULL, 8, 1, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0 };
	int i, j, maxx=0, maxy=0;

	if (modeschecked) return;

	validmodecnt=0;

#define ADDMODE(x,y,c,f) if (validmodecnt<MAXVALIDMODES) { \
	validmodexdim[validmodecnt]=x; \
	validmodeydim[validmodecnt]=y; \
	validmodebpp[validmodecnt]=c; \
	validmodefs[validmodecnt]=f; \
	validmodecnt++; \
	initprintf("Adding mode %dx%d (%d-bit %s)\n", x, y, c, (f&1)?"fullscreen":"windowed"); \
	}	

#define CHECK(w,h) if ((w < maxx) && (h < maxy))

	// do fullscreen modes first
	for (j=0; cdepths[j]; j++) {
		pf.BitsPerPixel = cdepths[j];
		pf.BytesPerPixel = cdepths[j] >> 3;

		modes = SDL_ListModes(&pf, SURFACE_FLAGS | SDL_FULLSCREEN);
		if (modes == (SDL_Rect **)0) {
			if (cdepths[j] > 8) cdepths[j] = -1;
			continue;
		}

		if (modes == (SDL_Rect **)-1) {
			for (i=0; defaultres[i][0]; i++)
				ADDMODE(defaultres[i][0],defaultres[i][1],cdepths[j],1)
		} else {
			for (i=0; modes[i]; i++) {
				if ((modes[i]->w > MAXXDIM) || (modes[i]->h > MAXYDIM)) continue;

				ADDMODE(modes[i]->w, modes[i]->h, cdepths[j], 1)

				if ((modes[i]->w > maxx) && (modes[i]->h > maxy)) {
					maxx = modes[i]->w;
					maxy = modes[i]->h;
				}
			}
		}
	}

	if (maxx == 0 && maxy == 0) {
		initprintf("No fullscreen modes available!\n");
		maxx = MAXXDIM; maxy = MAXYDIM;
	}

	// add windowed modes next
	for (j=0; cdepths[j]; j++) {
		if (cdepths[j] < 0) continue;
		for (i=0; defaultres[i][0]; i++)
			CHECK(defaultres[i][0],defaultres[i][1])
				ADDMODE(defaultres[i][0],defaultres[i][1],cdepths[j],0)
	}

#undef CHECK
#undef ADDMODE

	modeschecked=1;
}


//
// checkvideomode() -- makes sure the video mode passed is legal
//
int checkvideomode(long *x, long *y, int c, int fs)
{
	int i, nearest=-1, dx, dy, odx=9999, ody=9999;

	getvalidmodes();

	// fix up the passed resolution values to be multiples of 8
	// and at least 320x200 or at most MAXXDIMxMAXYDIM
	if (*x < 320) *x = 320;
	if (*y < 200) *y = 200;
	if (*x > MAXXDIM) *x = MAXXDIM;
	if (*y > MAXYDIM) *y = MAXYDIM;
	*x &= 0xfffffff8l;

	for (i=0; i<validmodecnt; i++) {
		if (validmodebpp[i] != c) continue;
		if (validmodefs[i] != fs) continue;
		dx = klabs(validmodexdim[i] - *x);
		dy = klabs(validmodeydim[i] - *y);
		if (!(dx | dy)) { 	// perfect match
			nearest = i;
			break;
		}
		if ((dx <= odx) && (dy <= ody)) {
			nearest = i;
			odx = dx; ody = dy;
		}
	}
		
#ifdef ANY_WINDOWED_SIZE
	if ((fs&1) == 0 && (nearest < 0 || (validmodexdim[nearest]!=*x || validmodeydim[nearest]!=*y)))
		return 0x7fffffffl;
#endif

	if (nearest < 0) {
		// no mode that will match (eg. if no fullscreen modes)
		return -1;
	}

	*x = validmodexdim[nearest];
	*y = validmodeydim[nearest];

	return nearest;		// JBF 20031206: Returns the mode number
}


//
// setvideomode() -- set SDL video mode
//
int setvideomode(int x, int y, int c, int fs)
{
	if ((fs == fullscreen) && (x == xres) && (y == yres) && (c == bpp) &&
	    !videomodereset) return 0;

	if (mouseacquired) {
		SDL_WM_GrabInput(SDL_GRAB_OFF);
		SDL_ShowCursor(SDL_ENABLE);
	}

	if (lockcount) while (lockcount) enddrawing();

#if defined(USE_OPENGL)
	if (bpp > 8 && sdl_surface) {
		polymost_glreset();
	}

	if (c > 8) {
		int i;
		struct {
			SDL_GLattr attr;
			int value;
		} attributes[] = {
#if 0
			{ SDL_GL_RED_SIZE, 8 },
			{ SDL_GL_GREEN_SIZE, 8 },
			{ SDL_GL_BLUE_SIZE, 8 },
			{ SDL_GL_ALPHA_SIZE, 8 },
			{ SDL_GL_BUFFER_SIZE, c },
			{ SDL_GL_STENCIL_SIZE, 0 },
			{ SDL_GL_ACCUM_RED_SIZE, 0 },
			{ SDL_GL_ACCUM_GREEN_SIZE, 0 },
			{ SDL_GL_ACCUM_BLUE_SIZE, 0 },
			{ SDL_GL_ACCUM_ALPHA_SIZE, 0 },
			{ SDL_GL_DEPTH_SIZE, 32 },
#endif
			{ SDL_GL_DOUBLEBUFFER, 1 }
		};
		
		for (i=0; i < sizeof(attributes)/sizeof(attributes[0]); i++)
			SDL_GL_SetAttribute(attributes[i].attr, attributes[i].value);
	}
#endif

	initprintf("Setting video mode %dx%d (%d-bpp %s)\n",
			x,y,c, ((fs&1) ? "fullscreen" : "windowed"));
	sdl_surface = SDL_SetVideoMode(x, y, c, (c>8?SDL_OPENGL:SURFACE_FLAGS) | ((fs&1)?SDL_FULLSCREEN:0));
	if (!sdl_surface) {
		initprintf("Unable to set video mode!\n");
		return -1;
	}

	if (mouseacquired) {
		SDL_WM_GrabInput(SDL_GRAB_ON);
		SDL_ShowCursor(SDL_DISABLE);
	}

#if 0
	{
	char flags[512] = "";
#define FLAG(x,y) if ((sdl_surface->flags & x) == x) { strcat(flags, y); strcat(flags, " "); }
	FLAG(SDL_SWSURFACE, "SWSURFACE")
	FLAG(SDL_HWSURFACE, "HWSURFACE")
	FLAG(SDL_ASYNCBLIT, "ASYNCBLIT")
	FLAG(SDL_ANYFORMAT, "ANYFORMAT")
	FLAG(SDL_HWPALETTE, "HWPALETTE")
	FLAG(SDL_DOUBLEBUF, "DOUBLEBUF")
	FLAG(SDL_FULLSCREEN, "FULLSCREEN")
	FLAG(SDL_OPENGL, "OPENGL")
	FLAG(SDL_OPENGLBLIT, "OPENGLBLIT")
	FLAG(SDL_RESIZABLE, "RESIZABLE")
	FLAG(SDL_HWACCEL, "HWACCEL")
	FLAG(SDL_SRCCOLORKEY, "SRCCOLORKEY")
	FLAG(SDL_RLEACCEL, "RLEACCEL")
	FLAG(SDL_SRCALPHA, "SRCALPHA")
	FLAG(SDL_PREALLOC, "PREALLOC")
#undef FLAG
	printOSD("SDL Surface flags: %s\n", flags);
	}
#endif

	{
		//static char t[384];
		//sprintf(t, "%s (%dx%d %s)", apptitle, x, y, ((fs) ? "fullscreen" : "windowed"));
		SDL_WM_SetCaption(apptitle, 0);
	}

#ifdef USE_OPENGL
	if (c > 8) {
		GLubyte *p,*p2,*p3;
		int i;

		polymost_glreset();
		
		glEnable(GL_TEXTURE_2D);
		glShadeModel(GL_SMOOTH); //GL_FLAT
		glClearColor(0,0,0,0.5); //Black Background
		glHint(GL_PERSPECTIVE_CORRECTION_HINT,GL_NICEST); //Use FASTEST for ortho!
		glHint(GL_LINE_SMOOTH_HINT,GL_NICEST);
		glDisable(GL_DITHER);
	
		glinfo.vendor     = glGetString(GL_VENDOR);
		glinfo.renderer   = glGetString(GL_RENDERER);
		glinfo.version    = glGetString(GL_VERSION);
		glinfo.extensions = glGetString(GL_EXTENSIONS);
		
		glinfo.maxanisotropy = 1.0;
		glinfo.bgra = 0;
		glinfo.texcompr = 0;

		// 3Dfx's hate fog (bleh)
		//if (Bstrstr(glinfo.renderer, "3Dfx") != NULL)
		//	nofog = 1;

		// process the extensions string and flag stuff we recognize
		p = Bstrdup(glinfo.extensions);
		p3 = p;
		while ((p2 = Bstrtoken(p3==p?p:NULL, " ", &p3, 1)) != NULL) {
			if (!Bstrcmp(p2, "GL_EXT_texture_filter_anisotropic")) {
				// supports anisotropy. get the maximum anisotropy level
				glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &glinfo.maxanisotropy);
			} else if (!Bstrcmp(p2, "GL_EXT_texture_edge_clamp") ||
			           !Bstrcmp(p2, "GL_SGIS_texture_edge_clamp")) {
				// supports GL_CLAMP_TO_EDGE or GL_CLAMP_TO_EDGE_SGIS
				glinfo.clamptoedge = 1;
			} else if (!Bstrcmp(p2, "GL_EXT_bgra")) {
				// support bgra textures
				glinfo.bgra = 1;
			} else if (!Bstrcmp(p2, "GL_ARB_texture_compression")) {
				// support texture compression
				glinfo.texcompr = 1;
			}
		}
		Bfree(p);
	}
#endif
	
	xres = x;
	yres = y;
	bpp = c;
	fullscreen = fs;
	//bytesperline = sdl_surface->pitch;
	//imageSize = bytesperline*yres;
	numpages = c>8?2:1;
	frameplace = 0;
	lockcount = 0;
	modechange=1;
	videomodereset = 0;
	OSD_ResizeDisplay(xres,yres);

	if (c==8) setpalette(0,256,0);
	baselayer_onvideomodechange(c>8);
	
	return 0;
}


//
// resetvideomode() -- resets the video system
//
void resetvideomode(void)
{
	videomodereset = 1;
	modeschecked = 0;
}


//
// begindrawing() -- locks the framebuffer for drawing
//
void begindrawing(void)
{
	long i,j;

	if (bpp > 8) {
		if (offscreenrendering) return;
		frameplace = 0;
		bytesperline = 0;
		imageSize = 0;
		modechange = 0;
		return;
	}

	// lock the frame
	if (lockcount++ > 0)
		return;

	if (offscreenrendering) return;
	
	if (SDL_MUSTLOCK(sdl_surface)) SDL_LockSurface(sdl_surface);
	frameplace = sdl_surface->pixels;
	
	if (sdl_surface->pitch != bytesperline || modechange) {
		bytesperline = sdl_surface->pitch;
		imageSize = bytesperline*yres;
		setvlinebpl(bytesperline);

		j = 0;
		for(i=0;i<=ydim;i++) ylookup[i] = j, j += bytesperline;
		modechange=0;
	}
}


//
// enddrawing() -- unlocks the framebuffer
//
void enddrawing(void)
{
	if (bpp > 8) {
		if (!offscreenrendering) frameplace = 0;
		return;
	}

	if (!frameplace) return;
	if (lockcount > 1) { lockcount--; return; }
	if (!offscreenrendering) frameplace = 0;
	if (lockcount == 0) return;
	lockcount = 0;

	if (offscreenrendering) return;

	if (SDL_MUSTLOCK(sdl_surface)) SDL_UnlockSurface(sdl_surface);
}


//
// showframe() -- update the display
//
void showframe(int w)
{
	long i,j;

#ifdef USE_OPENGL
	if (bpp > 8) {
		if (palfadedelta || palfadeclamp.f) {
			glMatrixMode(GL_PROJECTION);
			glPushMatrix();
			glLoadIdentity();
			glOrtho(0,xres,yres,0,-1,1);
			glMatrixMode(GL_MODELVIEW);
			glPushMatrix();
			glLoadIdentity();

			glDisable(GL_DEPTH_TEST);
			glDisable(GL_TEXTURE_2D);

			glEnable(GL_BLEND);
			glColor4ub(
				max(palfadeclamp.r,palfadergb.r),
				max(palfadeclamp.g,palfadergb.g),
				max(palfadeclamp.b,palfadergb.b),
				max(palfadeclamp.f,palfadedelta));

			glBegin(GL_QUADS);
			glVertex2i(0, 0);
			glVertex2i(xres, 0);
			glVertex2i(xres, yres);
			glVertex2i(0, yres);
			glEnd();
			
			glMatrixMode(GL_MODELVIEW);
			glPopMatrix();
			glMatrixMode(GL_PROJECTION);
			glPopMatrix();
		}

		SDL_GL_SwapBuffers();
		return;
	}
#endif
	
	if (offscreenrendering) return;

	if (lockcount) {
		printf("Frame still locked %d times when showframe() called.\n", lockcount);
		while (lockcount) enddrawing();
	}

	SDL_Flip(sdl_surface);
}


//
// setpalette() -- set palette values
//
int setpalette(int start, int num, char *dapal)
{
	SDL_Color pal[256];
	int i,n;

	if (bpp > 8) return 0;	// no palette in opengl

	copybuf(curpalettefaded, pal, 256);
	
	for (i=start, n=num; n>0; i++, n--) {
		/*
		pal[i].b = dapal[0] << 2;
		pal[i].g = dapal[1] << 2;
		pal[i].r = dapal[2] << 2;
		*/
		curpalettefaded[i].f = pal[i].unused = 0;
		dapal += 4;
	}

	//return SDL_SetPalette(sdl_surface, SDL_LOGPAL|SDL_PHYSPAL, pal, 0, 256);
	return SDL_SetColors(sdl_surface, pal, 0, 256);
}

//
// getpalette() -- get palette values
//
/*
int getpalette(int start, int num, char *dapal)
{
	int i;
	SDL_Palette *pal;

	// we shouldn't need to lock the surface to get the palette
	pal = sdl_surface->format->palette;

	for (i=num; i>0; i--, start++) {
		dapal[0] = pal->colors[start].b >> 2;
		dapal[1] = pal->colors[start].g >> 2;
		dapal[2] = pal->colors[start].r >> 2;
		dapal += 4;
	}

	return 1;
}
*/

//
// setgamma
//
int setgamma(float ro, float go, float bo)
{
	int i;
	unsigned short gt[3*256];
	
	return -1;

	if (!gamma_saved) {
		gamma_saved = 1;
	}

	for (i=0;i<256;i++) {
		gt[i]     = min(255,(unsigned short)floor(255.0 * pow((float)i / 255.0, ro))) << 8;
		gt[i+256] = min(255,(unsigned short)floor(255.0 * pow((float)i / 255.0, go))) << 8;
		gt[i+512] = min(255,(unsigned short)floor(255.0 * pow((float)i / 255.0, bo))) << 8;
	}

	return 0;
}

/*
static SDL_Surface * loadtarga(const char *fn)
{
	int i, width, height, palsize;
	char head[18];
	SDL_Color palette[256];
	SDL_Surface *surf=0;
	long fil;

	clearbufbyte(palette, sizeof(palette), 0);

	if ((fil = kopen4load(fn,0)) == -1) return NULL;

	kread(fil, head, 18);
	if (head[0] > 0) klseek(fil, head[0], SEEK_CUR);

	if ((head[1] != 1) ||		// colormap type
		(head[2] != 1) ||	// image type
		(head[7] != 24)	||	// colormap entry size
		(head[16] != 8)) {	// image descriptor
		printOSD("%s in unsuitable format for icon\n", fn);
		goto nogo;
	}

	width = head[12] | ((int)head[13] << 8);
	height = head[14] | ((int)head[15] << 8);

	if (width != 32 && height != 32) goto nogo;

	surf = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 8, 0,0,0,0);
	if (!surf) goto nogo;

	// palette first
	palsize = head[5] | ((int)head[6] << 8);
	for (i=(head[3]|((int)head[4]<<8)); palsize; --palsize, ++i) {
		kread(fil, &palette[i].b, 1);		// b
		kread(fil, &palette[i].g, 1);		// g
		kread(fil, &palette[i].r, 1);		// r
	}

	// targa renders bottom to top, from left to right
	for (i=height-1; i>=0; i--) {
		kread(fil, surf->pixels+i*width, width);
	}
	SDL_SetPalette(surf, SDL_PHYSPAL|SDL_LOGPAL, palette, 0, 256);

nogo:
	kclose(fil);
	return(surf);
}
*/


//
//
// ---------------------------------------
//
// Miscellany
//
// ---------------------------------------
//
// 

// generated using misc/makesdlkeytrans.c
static char keytranslation[SDLK_LAST] = {
	0, 0, 0, 0, 0, 0, 0, 0, 14, 15, 0, 0, 0, 28, 0, 0, 0, 0, 0, 89, 0, 0, 0, 
	0, 0, 0, 0, 1, 0, 0, 0, 0, 57, 2, 40, 4, 5, 6, 8, 40, 10, 11, 9, 13, 51,
	12, 52, 53, 11, 2, 3, 4, 5, 6, 7, 8, 9, 10, 39, 39, 51, 13, 52, 53, 3, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	26, 43, 27, 7, 12, 41, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
	49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44, 0, 0, 0, 0, 211, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 82, 79, 80, 81, 75, 76, 77, 71, 72, 73, 83, 181, 55, 74, 78, 156, 0,
	200, 208, 205, 203, 210, 199, 207, 201, 209, 59, 60, 61, 62, 63, 64, 65,
	66, 67, 68, 87, 88, 0, 0, 0, 0, 0, 0, 69, 58, 70, 54, 42, 157, 29, 184, 56,
	0, 0, 219, 220, 0, 0, 0, /*-2*/0, 84, 183, 221, 0, 0, 0
};


//
// handleevents() -- process the SDL message queue
//   returns !0 if there was an important event worth checking (like quitting)
//
int handleevents(void)
{
	int code, rv=0, j;
	SDL_Event ev;

#define SetKey(key,state) { \
	keystatus[key] = state; \
		if (state) { \
	keyfifo[keyfifoend] = key; \
	keyfifo[(keyfifoend+1)&(KEYFIFOSIZ-1)] = state; \
	keyfifoend = ((keyfifoend+2)&(KEYFIFOSIZ-1)); \
		} \
}

	while (SDL_PollEvent(&ev)) {
		switch (ev.type) {
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				code = keytranslation[ev.key.keysym.sym];

				// hook in the osd
				if (OSD_HandleKey(code, (ev.key.type == SDL_KEYDOWN)) == 0)
					break;

				if (ev.key.type == SDL_KEYDOWN) {
					if (!keystatus[code]) {
						SetKey(code, 1);
						if (keypresscallback)
							keypresscallback(code, 1);
					}
				} else {
					SetKey(code, 0);
					if (keypresscallback)
						keypresscallback(code, 0);
				}
				break;

			case SDL_ACTIVEEVENT:
				if (ev.active.state & SDL_APPINPUTFOCUS) {
					appactive = ev.active.gain;
					if (mouseacquired) {
						if (appactive) {
							SDL_WM_GrabInput(SDL_GRAB_ON);
							SDL_ShowCursor(SDL_DISABLE);
						} else {
							SDL_WM_GrabInput(SDL_GRAB_OFF);
							SDL_ShowCursor(SDL_ENABLE);
						}
					}
					rv=-1;
				}
				break;

			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				switch (ev.button.button) {
					case SDL_BUTTON_LEFT: j = 0; break;
					case SDL_BUTTON_RIGHT: j = 1; break;
					case SDL_BUTTON_MIDDLE: j = 2; break;
					default: j = -1; break;
				}
				if (j<0) break;
				
				if (ev.button.state == SDL_PRESSED)
					mouseb |= (1<<j);
				else
					mouseb &= ~(1<<j);

				if (mousepresscallback)
					mousepresscallback(j+1, ev.button.state == SDL_PRESSED);
				break;
				
			case SDL_MOUSEMOTION:
				if (appactive) {
					mousex += ev.motion.xrel;
					mousey += ev.motion.yrel;
				}
				break;

			case SDL_QUIT:
				quitevent = 1;
				rv=-1;
				break;

			default:
				//printOSD("Got event (%d)\n", ev.type);
				break;
		}
	}

	sampletimer();

#undef SetKey

	return rv;
}
