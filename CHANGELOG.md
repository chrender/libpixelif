


   **Version 0.8.5 — Febuary 21, 2019**

 - Replaced Fira Sans with FiraGO.
 - Fix includes to avoid configure-error when “disable-x11” was active.
 - Fixes underscores in markdown documentation.

---


   **Version 0.8.4 — September 3, 2017**

 - Fix superfluous libraries and includes during install when using $DESTDIR, addressing github issue #21.
 - Fix possible buffer overflow when writing score and turn data into status line.
 - Made screen size functions use 16-bit instead of 8-bit values, allowing version 5+ games to work with screen dimensions > 255.
 - Add missing “clean-dev” target.
 - Added missing contributor phrasing to BSD-3 clause. The resulting license now exactly matches the wording used on Github and so also makes the license detection work.

---


   **Version 0.8.3 — April 9, 2017**

 - Adapted to replacement of en\_US locale with en\_GB from libfizmo.
 - Fixed missing opening screen and disappearing prompt during timed input in “eliza.z5”. Thanks to Stephen Gutknecht for reporting the problem.
 - Fixed overlong reverse chars. This corrects several ASCII art problems as in Photopia, Nameless and ZChess, as well as in several opening screens. Thanks to Stephen Gutknecht.
 - Fixed incomplete flush function in true\_type\_wordwrapper.c. This corrects the incorrect display of the startup-screens in “AtWork.z5” and “Photopia”. Thanks to Stephen Gutknecht for reporting the problem.
 - Raised version number into 0.8 range to comply with general version scheme for new modules.

---


   **Version 0.7.3**

 - Fixed missing display of upper screen output on game start, like, for example, the title display of “Alpha“ or “Anchorhead”.
 - Fixed missing evaluation of events on start-up. This fixes a bug that made the interpreter crash when your initial screen size was less than the default size. This fix does now also allow resizing the window during frontispiece display.
 - Fixed build error which occured when "--disable-x11" was set for drilbo.
 - Fixed drawing the scrollbar at invalid positions.

---


   **Version 0.7.2 — October 9, 2016**

 - Improved build system for separate library and interface builds.

---


   **Version 0.7.1 — August 31, 2016**

 - Use tiny-xml-doc-tools for documentation.

---


   **Version 0.7.0 — July 28, 2016**

 - The “libpixelif” interface translates output from the fizmo Z-Machine to a pixel-based interface by using the freetype2 engine, providing proportional font display, antialiasing, and subpixel-rendering along with HiDPI support.
 - It uses the “Fira Sans” and “Fira Mono” font faces which were made by Erik Spiekermann and Ralph du Carrois.


