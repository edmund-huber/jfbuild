#import <Cocoa/Cocoa.h>
#include <stdlib.h>

#include "baselayer.h"

static struct {
	int fullscreen;
	int xdim3d, ydim3d, bpp3d;
	int forcesetup;
} settings;

@interface VideoModeFormatter : NSFormatter
@end

@implementation VideoModeFormatter
- (NSString *)stringForObjectValue:(id)object
{
	return [NSString stringWithFormat:@"%ld x %ld %ldbpp",
		object.xdim, object.ydim, object.bpp];
}
@end

@interface StartupWinController : NSWindowController
{
	IBOutlet NSButton *alwaysShowButton;
	IBOutlet NSButton *fullscreenButton;
	IBOutlet NSTextView *messagesView;
	IBOutlet NSTabView *tabView;
	IBOutlet NSComboBox *videoModeCbox;
	
	IBOutlet NSButton *cancelButton;
	IBOutlet NSButton *startButton;
}

- (IBAction)alwaysShowClicked:(id)sender;
- (IBAction)fullscreenClicked:(id)sender;

- (IBAction)cancel:(id)sender;
- (IBAction)start:(id)sender;

- (void)setupRunMode;
- (void)populateForm;
- (void)setupMessagesMode;
- (void)putsMessage:(NSString *)str;
- (void)setTitle:(NSString *)str;

// NSComboBoxDataSource informal protocol
- (id)comboBox:(NSComboBox *)comboBox objectValueForItemAtIndex:(int)index;
- (int)numberOfItemsInComboBox:(NSComboBox *)comboBox;
@end

@implementation StartupWinController

- (IBAction)alwaysShowClicked:(id)sender
{
	settings.forcesetup = [sender state] == NSOnState;
}

- (IBAction)fullscreenClicked:(id)sender
{
	settings.fullscreen = [sender state] == NSOnState;
	// XXX: recalculate the video modes list to take into account the fullscreen status
}

- (IBAction)cancel:(id)sender
{
	[NSApp abortModal];
	[self clearVideoModeMap];
}

- (IBAction)start:(id)sender
{
	[NSApp stopModal];
}

- (void)setupRunMode
{
	// XXX: populate the lists and set everything up to represent the current options
	[self populateForm];

	// enable all the controls on the Configuration page
	NSEnumerator *enumerator = [[[[tabView tabViewItemAtIndex:0] view] subviews] objectEnumerator];
	NSControl *control;
	while (control = [enumerator nextObject])
		[control setEnabled:true];
	
	[cancelButton setEnabled:true];
	[startButton setEnabled:true];

	[tabView selectTabViewItemAtIndex:0];
}

- (void)populateForm
{
	int mode3d, i;
	
	mode3d = checkvideomode(&settings.xdim3d, &settings.ydim3d, settings.bpp3d, settings.fullscreen, 1);
	if (mode3d < 0) {
		int i, cd[] = { 32, 24, 16, 15, 8, 0 };
		for (i=0; cd[i]; ) { if (cd[i] >= settings.bpp3d) i++; else break; }
		for ( ; cd[i]; i++) {
			mode3d = checkvideomode(&settings.xdim3d, &settings.ydim3d, cd[i], settings.fullscreen, 1);
			if (mode3d < 0) continue;
			settings.bpp3d = cd[i];
			break;
		}
	}
	
	[fullscreenButton setState:(settings.fullscreen ? NSOnState : NSOffState)];
	[alwaysShowButton setState:(settings.forcesetup ? NSOnState : NSOffState)];
	
	[self clearVideoModeMap];
	for (i=0; i<validmodecnt; i++) {
		if (validmode[i].fs != settings.fullscreen) continue;
		
		NSString *buf = [[NSString alloc] initWithFormat:
					@"%ld x %ld %ldbpp",
					validmode[i].xdim,
					validmode[i].ydim,
					validmode[i].bpp];
	}

	[videoModeCbox removeAllItems];
	[videoModeCbox setFormatter:[[VideoModeFormatter alloc] init]];
	[videoModeCbox addItemsWithObjectValues:[NSArray arrayWithObjects:validmode count:validmodecnt]];
}

- (void)setupMessagesMode
{
	[tabView selectTabViewItemAtIndex:1];

	// disable all the controls on the Configuration page except "always show", so the
	// user can enable it if they want to while waiting for something else to happen
	NSEnumerator *enumerator = [[[[tabView tabViewItemAtIndex:0] view] subviews] objectEnumerator];
	NSControl *control;
	while (control = [enumerator nextObject]) {
		if (control == alwaysShowButton) continue;
		[control setEnabled:false];
	}
	
	[enumerator release];
	[cancelButton setEnabled:false];
	[startButton setEnabled:false];
}

- (void)putsMessage:(NSString *)str
{
	NSRange end;
	NSTextStorage *text = [messagesView textStorage];
	BOOL shouldAutoScroll;

	shouldAutoScroll = ((int)NSMaxY([messagesView bounds]) == (int)NSMaxY([messagesView visibleRect]));

	end.location = [text length];
	end.length = 0;

	[text beginEditing];
	[messagesView replaceCharactersInRange:end withString:str];
	[text endEditing];
	
	if (shouldAutoScroll) {
		end.location = [text length];
		end.length = 0;
		[messagesView scrollRangeToVisible:end];
	}
}

- (void)setTitle:(NSString *)str
{
	[[self window] setTitle:str];
}

- (int)numberOfItemsInComboBox:(NSComboBox *)comboBox
{
	if (comboBox == videoModeCbox) {
	}
	return 0;
}

- (id)comboBox:(NSComboBox *)comboBox objectValueForItemAtIndex:(int)value
{
	if (comboBox == videoModeCbox) {
	}
	return 0;
}

@end	// implementation StartupWinController

static StartupWinController *startwin = nil;

int startwin_open(void)
{
	if (startwin != nil) return 1;
	
	startwin = [[StartupWinController alloc] initWithWindowNibName:@"startwin.game"];
	if (startwin == nil) return -1;

	[startwin showWindow:nil];
	[startwin setupMessagesMode];
	
	// so that SDL's chicanery doesn't interfere with our UI
	setenv("SDL_ENABLEAPPEVENTS","1",1);

	return 0;
}

int startwin_close(void)
{
	if (startwin == nil) return 1;

	[startwin close];
	startwin = nil;
	
	unsetenv("SDL_ENABLEAPPEVENTS");

	return 0;
}

int startwin_puts(const char *s)
{
	NSString *ns;

	if (!s) return -1;
	if (startwin == nil) return 1;

	ns = [[NSString alloc] initWithCString:s];
	[startwin putsMessage:ns];
	[ns release];

	return 0;
}

int startwin_settitle(const char *s)
{
	NSString *ns;
	
	if (!s) return -1;
	if (startwin == nil) return 1;
	
	ns = [[NSString alloc] initWithCString:s];
	[startwin setTitle:ns];
	[ns release];

	return 0;
}

int startwin_idle(void *v)
{
	if (startwin) [[startwin window] displayIfNeeded];
	return 0;
}

extern int xdimgame, ydimgame, bppgame, forcesetup;

int startwin_run(void)
{
	int retval;
	
	if (startwin == nil) return 0;

	settings.fullscreen = fullscreen;
	settings.xdim3d = xdimgame;
	settings.ydim3d = ydimgame;
	settings.bpp3d = bppgame;
	settings.forcesetup = forcesetup;
	
	[startwin setupRunMode];
	
	switch ([NSApp runModalForWindow:[startwin window]]) {
		case NSRunStoppedResponse: retval = 1; break;
		case NSRunAbortedResponse: retval = 0; break;
		default: retval = -1;
	}

	if (retval > 0) {
		fullscreen = settings.fullscreen;
		xdimgame = settings.xdim3d;
		ydimgame = settings.ydim3d;
		bppgame = settings.bpp3d;
		forcesetup = settings.forcesetup;
	}
	
	[startwin setupMessagesMode];
	
	return retval;
}