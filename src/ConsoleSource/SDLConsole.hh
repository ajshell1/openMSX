// $Id$

#ifndef __SDLCONSOLE_HH__
#define __SDLCONSOLE_HH__

#include "OSDConsoleRenderer.hh"

class SDL_Surface;


class SDLConsole : public OSDConsoleRenderer
{
	public:
		SDLConsole(SDL_Surface *screen);
		virtual ~SDLConsole();

		virtual bool loadFont(const std::string &filename);
		virtual bool loadBackground(const std::string &filename);
		virtual void drawConsole();
		virtual void updateConsole();

	private:
		/** This is the font for the console.
		  */
		class Font *font;

		/** Surface of the console.
		  */
		SDL_Surface *consoleSurface;

		/** This is the screen to draw the console to.
		  */
		SDL_Surface *outputScreen;

		/** Background image for the console.
		  */
		SDL_Surface *backgroundImage;

		/** Dirty rectangle to draw over behind the users background.
		  */
		SDL_Surface *inputBackground;

		/** The top-left x coordinate of the console on the display screen.
		  */
		int dispX;

		/** The top-left y coordinate of the console on the display screen.
		  */
		int dispY;

		/** The consoles alpha level.
		  */
		unsigned char consoleAlpha;

		/** Is the cursor currently blinking
		  */
		bool blink;

		/** Last time the consoles cursor blinked 
		  */
		Uint32 lastBlinkTime;
	
		BackgroundSetting* backgroundSetting;
		FontSetting *fontSetting;

		class Console* console;
		
		void alpha(unsigned char alpha);
		void loadBackground();
		void position(int x, int y);
		void resize(SDL_Rect rect);
		void reloadBackground();
		void drawCursor();

};

#endif
