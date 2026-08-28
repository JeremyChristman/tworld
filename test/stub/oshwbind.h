/* test/stub/oshwbind.h: a substitute for the OS/hardware binding, so that
 * generic/in.c can be compiled and driven by a test with no Qt and no SDL.
 *
 * MOD (Jeremy, jc-43). generic/generic.h includes "oshwbind.h" by quoted name;
 * putting this directory first on the include path substitutes this file for
 * oshw-qt/oshwbind.h. Nothing here is used by the game -- only by test/.
 *
 * TWO THINGS HERE ARE LOAD BEARING, and both of them silently corrupt results
 * rather than failing to build if you get them wrong:
 *
 *  1. The key codes must not collide with ASCII. The real Qt header fudges
 *     every key into 0x100..0x1FF (TWK_FUDGE(k) = ((k) & 0xFF) | 0x100), and
 *     the keycmds tables in in.c key ordinary letters by their character code
 *     ('p', 'r', 'n', ...). Numbering these from 1 would alias a modifier onto
 *     ' ' and let 'a'..'z' index keystates[TWK_LAST] out of bounds. So this
 *     enum starts at 0x100, exactly as the real one does.
 *
 *  2. TWK_LAST must be the largest value, since it sizes keystates[] and
 *     keypressorder[] and bounds the scancode guard in _keyeventcallback().
 */

#ifndef	HEADER_test_oshwbind_h_
#define	HEADER_test_oshwbind_h_

#include	<stdint.h>

#ifdef __cplusplus
	#define OSHW_EXTERN extern "C"
#else
	#define OSHW_EXTERN extern
#endif

enum {
	TW_ALPHA_TRANSPARENT = 0,
	TW_ALPHA_OPAQUE      = 255
};

/* Mouse buttons, referenced by in.c's retrievemousecommand().
 */
enum {
	TW_BUTTON_LEFT = 1,
	TW_BUTTON_MIDDLE,
	TW_BUTTON_RIGHT,
	TW_BUTTON_WHEELUP,
	TW_BUTTON_WHEELDOWN
};

/* Key codes. Values need not match Qt's -- nothing here crosses a process
 * boundary -- but they must be distinct, above ASCII, and below TWK_LAST.
 */
enum {
	TWK_dummy = 0x100,

	TWK_BACKSPACE,
	TWK_TAB,
	TWK_RETURN,
	TWK_KP_ENTER,
	TWK_ESCAPE,

	/* Ordered West < North < East < South to mirror the real Qt codes
	 * (Key_Left < Key_Up < Key_Right < Key_Down). restartkeystates() replays
	 * held keys in scancode order, so this ordering is observable and a test
	 * pins it. */
	TWK_LEFT,
	TWK_UP,
	TWK_RIGHT,
	TWK_DOWN,

	TWK_INSERT,
	TWK_DELETE,
	TWK_HOME,
	TWK_END,
	TWK_PAGEUP,
	TWK_PAGEDOWN,

	TWK_F1, TWK_F2, TWK_F3, TWK_F4, TWK_F5,
	TWK_F6, TWK_F7, TWK_F8, TWK_F9, TWK_F10,

	TWK_KP2, TWK_KP4, TWK_KP6, TWK_KP8,

	TWK_LSHIFT, TWK_RSHIFT,
	TWK_LCTRL, TWK_RCTRL,
	TWK_LALT, TWK_RALT,
	TWK_LMETA, TWK_RMETA,
	TWK_NUMLOCK, TWK_CAPSLOCK, TWK_MODE,

	TWK_CTRL_C,

	/* "Virtual" keys: menu and toolbar commands, delivered as a press
	 * immediately followed by a release (see PulseKey in the Qt layer). */
	TWC_SEESCORES,
	TWC_SEESOLUTIONFILES,
	TWC_TIMESCLIPBOARD,
	TWC_QUITLEVEL,
	TWC_QUIT,
	TWC_PROCEED,
	TWC_PAUSEGAME,
	TWC_SAMELEVEL,
	TWC_NEXTLEVEL,
	TWC_PREVLEVEL,
	TWC_GOTOLEVEL,
	TWC_PLAYBACK,
	TWC_CHECKSOLUTION,
	TWC_REPLSOLUTION,
	TWC_KILLSOLUTION,
	TWC_SEEK,
	TWC_HELP,
	TWC_KEYS,

	TWK_LAST
};

typedef struct TW_Rect {
	int x, y;
	int w, h;
} TW_Rect;

typedef struct TW_Surface {
	int w, h;
	int pitch;
	void* pixels;
	int bytesPerPixel;
} TW_Surface;

OSHW_EXTERN uint8_t* TW_GetKeyState(int* pNumKeys);

#endif
