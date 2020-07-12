/* -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 * gui_vesa.cpp
 *  
 *
 * Notes:
 *  - This code assumes the default color bit positions and masks for each color depth.
 *    For example, the 15 bit color depth assumes xrrrrrgggggbbbbb.  However, you should
 *     check for different positions and masks from the Video Mode Information retrieved
 *     from the BIOS.
 *
 *    If the Memory Model field in the Video Information block is returned as 4,
 *     the Mask and Size fields in the Info Block may be and usually are zero.
 *     Therefore, the pixels are assumed as follows:
 *        8 bits per pixel: rrrgggbb
 *       15 bits per pixel: xrrrrrgggggbbbbb
 *       16 bits per pixel: rrrrrggggggbbbbb
 *       24 bits per pixel: rrrrrrrrggggggggbbbbbbbb
 *       32 bits per pixel: xxxxxxxxrrrrrrrrggggggggbbbbbbbb
 *      (however, 24 and 32 shouldn't happen when Memory Model == 4)
 *
 *    If the Memory Model field in the Video Information block is returned as 6,
 *     the Mask and Size fields in the Info Block should be valid.
 *
 */

#include "../include/ctype.h"
#include "gui.h"
#include "grfx.h"
#include "palette.h"
#include "video.h"

#include <dpmi.h>
#include <go32.h>
#include <sys/farptr.h>

// these three items are used from other sources
extern struct S_VIDEO_MODE_INFO *video_mode_info;
extern int base_selector;
extern __dpmi_meminfo base_mi;

// prototype function
void gfx_flush(DRAW_FUNC draw, struct RECT *rect);

/*  vesa__flush()
 *        rect = area to push to screen
 *       array = pixels to push to screen
 *      stride = width of pixel area
 *
 *  push a rect of pixels to the visible screen
 *
 */
void vesa__flush(const struct RECT *rect, PIXEL *array, const int stride) {
  int y = rect->top;
  
  // Push the buffer to the visible screen
  while (y <= rect->bottom) {
    grx_args.screen_out(rect->left, y, rect->right, array);
    array += stride;
    y++;
  }
}

/*  vesa_flush()
 *        rect = area to push to screen
 *
 *   function that gets called by core.cpp to eventually call vesa__flush above   
 */
void vesa_flush(struct RECT *rect) {
  gfx_flush(vesa__flush, rect);
}

/*
 * Linear Addressing
 */
#define VESA_LINEARVAR(factor)                                      \
    const unsigned int y1scanline = top * grx_args.bytes_scanline;  \
    const unsigned int end = y1scanline + (right * factor);         \
    unsigned int start = y1scanline + (left * factor);

/*  vesa_linear8()
 *        left = X1 coordinate relative to left of screen
 *         top = Y coordinate relative to top of screen
 *       right = X2 coordinate relative to top left of screen
 *       color = pixel array of pixels to draw
 *
 *   draws a single scan line to the screen in 8 bit pixels
 */
void vesa_linear8(const int left, const int top, const int right, const PIXEL *color) {
  VESA_LINEARVAR(1);
  
  while (start <= end) {
    _farpokeb(base_selector, start, GUIRGB332(*color));
    color++;
    start++;
  }
}

/*  vesa_linear15()
 *        left = X1 coordinate relative to left of screen
 *         top = Y coordinate relative to top of screen
 *       right = X2 coordinate relative to top left of screen
 *       color = pixel array of pixels to draw
 *
 *   draws a single scan line to the screen in 15 bit pixels
 */
void vesa_linear15(const int left, const int top, const int right, const PIXEL *color) {
  VESA_LINEARVAR(2);
  
  while (start <= end) {
    _farpokew(base_selector, start, GUIRGB555(*color));
    color++;
    start += 2;
  }
}

/*  vesa_linear16()
 *        left = X1 coordinate relative to left of screen
 *         top = Y coordinate relative to top of screen
 *       right = X2 coordinate relative to top left of screen
 *       color = pixel array of pixels to draw
 *
 *   draws a single scan line to the screen in 16 bit pixels
 */
void vesa_linear16(const int left, const int top, const int right, const PIXEL *color) {
  VESA_LINEARVAR(2);
  
  while (start <= end) {
    _farpokew(base_selector, start, GUIRGB565(*color));
    color++;
    start += 2;
  }
}

/*  vesa_linear24()
 *        left = X1 coordinate relative to left of screen
 *         top = Y coordinate relative to top of screen
 *       right = X2 coordinate relative to top left of screen
 *       color = pixel array of pixels to draw
 *
 *   draws a single scan line to the screen in 24 bit pixels
 */
void vesa_linear24(const int left, const int top, const int right, const PIXEL *color) {
  VESA_LINEARVAR(3);
  
  // this is really slow.  It will be up to your driver to make this faster.
  // 24-bit is really not used much because of this.  It is best to use
  //  16- or 32- bit modes
  while (start <= end) {
    _farpokeb(base_selector, start + 0, GUIB(*color));
    _farpokeb(base_selector, start + 1, GUIG(*color));
    _farpokeb(base_selector, start + 2, GUIR(*color));
    start += 3;
    color++;
  }
}

/*  vesa_linear32()
 *        left = X1 coordinate relative to left of screen
 *         top = Y coordinate relative to top of screen
 *       right = X2 coordinate relative to top left of screen
 *       color = pixel array of pixels to draw
 *
 *   draws a single scan line to the screen in 32 bit pixels
 */
void vesa_linear32(const int left, const int top, const int right, const PIXEL *color) {
  VESA_LINEARVAR(4);
  
  while (start <= end) {
    _farpokel(base_selector, start, GUIRGB888(*color));
    color++;
    start += 4;
  }
}


/*
 * Bank Switching
 */


/*  vesa_bankswitch_init()
 *      no parameters
 *
 *   initializes the address and current bank number variables
 */
void vesa_bankswitch_init(void) {
  // set up the address and current bank variables
  base_mi.address = (video_mode_info->wina_segment << 4);
  base_mi.size = 0xFFFFFFFF;  // we use the base_mi.size value for our current bank
}

/*  vesa_switch_bank()
 *      bank = current bank to switch to
 *
 *   if not already on that bank, we call the BIOS to switch to specified bank
 */
void vesa_switch_bank(const int bank) {
  __dpmi_regs r;
  
  if (bank != base_mi.size) {
    r.x.ax = 0x4F05;
    r.x.bx = 0;        // sub func 0 (BH = 0), window A (BL = 0)
    r.x.dx = bank;     // in grad units (0 = 0x00000, 1 = 0x00000 + (1 * grad), 2 = 0x0000 + (2 * grad), etc)
    __dpmi_int(0x10, &r);

    base_mi.size = bank;
  }
}

// must use video_mode_info->bytes_scanline for Bank Switching modes
#define VESA_BANKVAR(factor)                                                  \
    const unsigned int y1scanline = top * video_mode_info->bytes_scanline;    \
    const unsigned int end = y1scanline + (right * factor);                   \
    unsigned int start = y1scanline + (left * factor);

/*  vesa_bankswitch8()
 *        left = X1 coordinate relative to left of screen
 *         top = Y coordinate relative to top of screen
 *       right = X2 coordinate relative to top left of screen
 *       color = pixel array of pixels to draw
 *
 *   draws a single scan line to the screen in 8 bit pixels
 */
void vesa_bankswitch8(const int left, const int top, const int right, const PIXEL *color) {
  VESA_BANKVAR(1);
  
  while (start <= end) {
    int bank_number = start / (video_mode_info->win_granularity * 1024);
    int bank_offset = start % (video_mode_info->win_granularity * 1024);
    vesa_switch_bank(bank_number);
    
    _farpokeb(_dos_ds, base_mi.address + bank_offset, GUIRGB332(*color));
    color++;
    start++;
  }
}

/*  vesa_bankswitch15()
 *        left = X1 coordinate relative to left of screen
 *         top = Y coordinate relative to top of screen
 *       right = X2 coordinate relative to top left of screen
 *       color = pixel array of pixels to draw
 *
 *   draws a single scan line to the screen in 15 bit pixels
 */
void vesa_bankswitch15(const int left, const int top, const int right, const PIXEL *color) {
  VESA_BANKVAR(2);
  
  while (start <= end) {
    int bank_number = start / (video_mode_info->win_granularity * 1024);
    int bank_offset = start % (video_mode_info->win_granularity * 1024);
    vesa_switch_bank(bank_number);
    
    _farpokew(_dos_ds, base_mi.address + bank_offset, GUIRGB555(*color));
    color++;
    start += 2;
  }
}

/*  vesa_bankswitch16()
 *        left = X1 coordinate relative to left of screen
 *         top = Y coordinate relative to top of screen
 *       right = X2 coordinate relative to top left of screen
 *       color = pixel array of pixels to draw
 *
 *   draws a single scan line to the screen in 16 bit pixels
 */
void vesa_bankswitch16(const int left, const int top, const int right, const PIXEL *color) {
  VESA_BANKVAR(2);
  
  while (start <= end) {
    int bank_number = start / (video_mode_info->win_granularity * 1024);
    int bank_offset = start % (video_mode_info->win_granularity * 1024);
    vesa_switch_bank(bank_number);
    
    _farpokew(_dos_ds, base_mi.address + bank_offset, GUIRGB565(*color));
    color++;
    start += 2;
  }
}

/*  vesa_bankswitch24()
 *        left = X1 coordinate relative to left of screen
 *         top = Y coordinate relative to top of screen
 *       right = X2 coordinate relative to top left of screen
 *       color = pixel array of pixels to draw
 *
 *   draws a single scan line to the screen in 24 bit pixels
 */
void vesa_bankswitch24(const int left, const int top, const int right, const PIXEL *color) {
  VESA_BANKVAR(3);
  
  while (start <= end) {
    int bank_number = start / (video_mode_info->win_granularity * 1024);
    int bank_offset = start % (video_mode_info->win_granularity * 1024);
    vesa_switch_bank(bank_number);
    
    _farpokeb(_dos_ds, base_mi.address + bank_offset + 0, GUIB(*color));
    _farpokeb(_dos_ds, base_mi.address + bank_offset + 1, GUIG(*color));
    _farpokeb(_dos_ds, base_mi.address + bank_offset + 2, GUIR(*color));
    color++;
    start += 3;
  }
}

/*  vesa_bankswitch32()
 *        left = X1 coordinate relative to left of screen
 *         top = Y coordinate relative to top of screen
 *       right = X2 coordinate relative to top left of screen
 *       color = pixel array of pixels to draw
 *
 *   draws a single scan line to the screen in 32 bit pixels
 */
void vesa_bankswitch32(const int left, const int top, const int right, const PIXEL *color) {
  VESA_BANKVAR(4);
  
  while (start <= end) {
    int bank_number = start / (video_mode_info->win_granularity * 1024);
    int bank_offset = start % (video_mode_info->win_granularity * 1024);
    vesa_switch_bank(bank_number);
    
    _farpokel(_dos_ds, base_mi.address + bank_offset, GUIRGB888(*color));
    color++;
    start += 4;
  }
}
