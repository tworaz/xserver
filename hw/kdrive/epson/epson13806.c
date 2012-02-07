/*
 * Copyright 2004 by Costas Stylianou <costas.stylianou@psion.com> +44(0)7850 394095
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Costas Sylianou not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission. Costas Stylianou makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * COSTAS STYLIANOU DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL COSTAS STYLIANOU BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * epson13806.c - Implementation of hardware accelerated functions for
 *                Epson S1D13806 graphics controller.
 *
 * History:
 * 28-Jan-04  C.Stylianou                     PRJ NBL: Created from fbdev.c.
 * 30-Mar-04  Phil Blundell/Peter Naulls      Integration with XFree 4.3
 *
 */

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include <errno.h>
#include <sys/ioctl.h>

#include "epson13806.h"
#include "epson13806reg.h"

extern int KdTsPhyScreen;

char *fbdevDevicePath = NULL;

Bool
epsonInitialize (KdCardInfo *card, EpsonPriv *priv)
{
    unsigned long off;

    if (fbdevDevicePath == NULL)
        fbdevDevicePath = "/dev/fb0";

    if ((priv->fd = open(fbdevDevicePath, O_RDWR)) < 0) {
        perror("Error opening /dev/fb0\n");
        return FALSE;
    }

    /* quiet valgrind */
    memset (&priv->fix, '\0', sizeof (priv->fix));
    if (ioctl(priv->fd, FBIOGET_FSCREENINFO, &priv->fix) < 0) {
        perror("Error with /dev/fb ioctl FIOGET_FSCREENINFO");
        close (priv->fd);
        return FALSE;
    }
    /* quiet valgrind */
    memset (&priv->var, '\0', sizeof (priv->var));
    if (ioctl(priv->fd, FBIOGET_VSCREENINFO, &priv->var) < 0) {
        perror("Error with /dev/fb ioctl FIOGET_VSCREENINFO");
        close (priv->fd);
        return FALSE;
    }

    priv->fb_base = epsonMapDevice (EPSON13806_PHYSICAL_VMEM_ADDR, EPSON13806_VMEM_SIZE);
    if (priv->fb_base == (char *)-1) {
        perror("ERROR: failed to mmap framebuffer!");
        close (priv->fd);
        return FALSE;
    }

    off = (unsigned long) priv->fix.smem_start % (unsigned long) getpagesize();
    priv->fb = priv->fb_base + off;
    return TRUE;
}

void *
epsonMapDevice (CARD32 addr, CARD32 size)
{
    void    *a;
    int	    fd;

#ifdef __arm__
    fd = open ("/dev/mem", O_RDWR|O_SYNC);
#else
    fd = open ("/dev/mem", O_RDWR);
#endif
    if (fd < 0)
	FatalError ("KdMapDevice: failed to open /dev/mem (%s)\n",
		    strerror (errno));

    a = mmap ((caddr_t) 0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, addr);
    close (fd);
    if ((long) a == -1)
	FatalError ("KdMapDevice: failed to map frame buffer (%s)\n",
		    strerror (errno));
    return a;
}

Bool
epsonCardInit (KdCardInfo *card)
{
    EpsonPriv    *priv;

    priv = (EpsonPriv *) malloc (sizeof (EpsonPriv));
    if (!priv)
        return FALSE;

    if (!epsonInitialize (card, priv))
    {
        free (priv);
        return FALSE;
    }
    card->driver = priv;

    // Call InitEpson to map onto Epson registers
    initEpson13806();

    return TRUE;
}

static Pixel
epsonMakeContig (Pixel orig, Pixel others)
{
    Pixel   low;

    low = lowbit (orig) >> 1;
    while (low && (others & low) == 0)
    {
        orig |= low;
        low >>= 1;
    }
    return orig;
}

static Bool
epsonMapFramebuffer (KdScreenInfo *screen)
{
    EpsonScrPriv	*scrpriv = screen->driver;
    KdPointerMatrix	m;
    EpsonPriv		*priv = screen->card->driver;

    if (scrpriv->randr != RR_Rotate_0)
        scrpriv->shadow = TRUE;
    else
        scrpriv->shadow = FALSE;

    KdComputePointerMatrix (&m, scrpriv->randr, screen->width, screen->height);

    KdSetPointerMatrix (&m);

    screen->width = priv->var.xres;
    screen->height = priv->var.yres;

    if (scrpriv->shadow)
    {
        if (!KdShadowFbAlloc (screen,
                    scrpriv->randr & (RR_Rotate_90|RR_Rotate_270)))
            return FALSE;
    }
    else
    {
        screen->fb.byteStride = priv->fix.line_length;
        screen->fb.pixelStride = (priv->fix.line_length * 8 /
                                  priv->var.bits_per_pixel);
        screen->fb.frameBuffer = (CARD8 *) (priv->fb);
    }

    return TRUE;
}

static Bool
epsonScreenInitialize (KdScreenInfo *screen, EpsonScrPriv *scrpriv)
{
    EpsonPriv  *priv = screen->card->driver;
    Pixel      allbits;
    int        depth;

    screen->fb.visuals = (1 << TrueColor);
#define Mask(o,l)   (((1 << l) - 1) << o)
    screen->fb.redMask = Mask (priv->var.red.offset, priv->var.red.length);
    screen->fb.greenMask = Mask (priv->var.green.offset, priv->var.green.length);
    screen->fb.blueMask = Mask (priv->var.blue.offset, priv->var.blue.length);

    /*
     * This is a kludge so that Render will work -- fill in the gaps
     * in the pixel
     */
    screen->fb.redMask = epsonMakeContig (screen->fb.redMask,
                                          screen->fb.greenMask|
                                          screen->fb.blueMask);

    screen->fb.greenMask = epsonMakeContig (screen->fb.greenMask,
                                            screen->fb.redMask|
                                            screen->fb.blueMask);

    screen->fb.blueMask = epsonMakeContig (screen->fb.blueMask,
                                           screen->fb.redMask|
                                           screen->fb.greenMask);

    allbits = screen->fb.redMask | screen->fb.greenMask | screen->fb.blueMask;
    depth = 32;
    while (depth && !(allbits & (1 << (depth - 1))))
        depth--;


    screen->fb.depth = depth;
    screen->fb.bitsPerPixel = priv->var.bits_per_pixel;
    screen->rate = 60;

    scrpriv->randr = screen->randr;

    return epsonMapFramebuffer (screen);
}

Bool
epsonScreenInit (KdScreenInfo *screen)
{
    EpsonScrPriv *scrpriv;

    scrpriv = calloc (1, sizeof (EpsonScrPriv));
    if (!scrpriv)
        return FALSE;
    screen->driver = scrpriv;
    if (!epsonScreenInitialize (screen, scrpriv))
    {
        screen->driver = 0;
        free (scrpriv);
        return FALSE;
    }
    return TRUE;
}

static void *
epsonWindowLinear (ScreenPtr    pScreen,
                   CARD32       row,
                   CARD32       offset,
                   int          mode,
                   CARD32       *size,
                   void         *closure)
{
    KdScreenPriv(pScreen);
    EpsonPriv *priv = pScreenPriv->card->driver;

    if (!pScreenPriv->enabled)
        return 0;
    *size = priv->fix.line_length;
    return (CARD8 *) priv->fb + row * priv->fix.line_length + offset;
}

static void
epsonSetScreenSizes (ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo	*screen = pScreenPriv->screen;
    EpsonScrPriv	*scrpriv = screen->driver;
    EpsonPriv		*priv = screen->card->driver;

    if (scrpriv->randr & (RR_Rotate_0|RR_Rotate_180))
    {
        pScreen->width = priv->var.xres;
        pScreen->height = priv->var.yres;
        pScreen->mmWidth = screen->width_mm;
        pScreen->mmHeight = screen->height_mm;
    }
    else
    {
        pScreen->width = priv->var.yres;
        pScreen->height = priv->var.xres;
        pScreen->mmWidth = screen->height_mm;
        pScreen->mmHeight = screen->width_mm;
    }
}

static Bool
epsonUnmapFramebuffer (KdScreenInfo *screen)
{
    KdShadowFbFree (screen);
    return TRUE;
}

static Bool
epsonSetShadow (ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo	*screen = pScreenPriv->screen;
    EpsonScrPriv	*scrpriv = screen->driver;
    EpsonPriv		*priv = screen->card->driver;
    ShadowUpdateProc	update;
    ShadowWindowProc	window;
    int                 useYX = 0;

#ifdef __arm__
    /* Use variant copy routines that always read left to right in the
       shadow framebuffer.  Reading vertical strips is exceptionally
       slow on XScale due to cache effects.  */
    useYX = 1;
#endif

    window = epsonWindowLinear;
    update = 0;
    if (scrpriv->randr)
    {
        if (priv->var.bits_per_pixel == 16) {
            switch (scrpriv->randr) {
                case RR_Rotate_90:
                    if (useYX)
                        update = shadowUpdateRotate16_90YX;
                    else
                        update =  shadowUpdateRotate16_90;
                    break;
                case RR_Rotate_180:
                    update = shadowUpdateRotate16_180;
                    break;
                case RR_Rotate_270:
                    if (useYX)
                        update = shadowUpdateRotate16_270YX;
                    else
                        update =  shadowUpdateRotate16_270;
                    break;
                default:
                    update = shadowUpdateRotate16;
                    break;
            }
        } else
            update = shadowUpdateRotatePacked;
    }
    else
    {
        update = shadowUpdatePacked;
    }

    return KdShadowSet (pScreen, scrpriv->randr, update, window);
}

#ifdef RANDR
static Bool
epsonRandRGetInfo (ScreenPtr pScreen, Rotation *rotations)
{
    KdScreenPriv(pScreen);
    KdScreenInfo        *screen = pScreenPriv->screen;
    EpsonScrPriv        *scrpriv = screen->driver;
    RRScreenSizePtr     pSize;
    Rotation            randr;
    int                 n;

    *rotations = RR_Rotate_All|RR_Reflect_All;

    for (n = 0; n < pScreen->numDepths; n++)
        if (pScreen->allowedDepths[n].numVids)
            break;
    if (n == pScreen->numDepths)
        return FALSE;

    pSize = RRRegisterSize (pScreen,
                            screen->width,
                            screen->height,
                            screen->width_mm,
                            screen->height_mm);

    randr = KdSubRotation (scrpriv->randr, screen->randr);

    RRSetCurrentConfig (pScreen, randr, RR_Rotate_0, pSize);

    return TRUE;
}

static Bool
epsonRandRSetConfig (ScreenPtr       pScreen,
		             Rotation        randr,
		             int             rate,
		             RRScreenSizePtr pSize)
{
    KdScreenPriv(pScreen);
    KdScreenInfo	*screen = pScreenPriv->screen;
    EpsonScrPriv	*scrpriv = screen->driver;
    Bool            wasEnabled = pScreenPriv->enabled;
    EpsonScrPriv    oldscr;
    int             oldwidth;
    int             oldheight;
    int             oldmmwidth;
    int             oldmmheight;
    int             newwidth, newheight;

    if (screen->randr & (RR_Rotate_0|RR_Rotate_180))
    {
        newwidth = pSize->width;
        newheight = pSize->height;
    }
    else
    {
        newwidth = pSize->height;
        newheight = pSize->width;
    }

    if (wasEnabled)
        KdDisableScreen (pScreen);

    oldscr = *scrpriv;

    oldwidth = screen->width;
    oldheight = screen->height;
    oldmmwidth = pScreen->mmWidth;
    oldmmheight = pScreen->mmHeight;

    /*
     * Set new configuration
     */

    scrpriv->randr = KdAddRotation (screen->randr, randr);

    epsonUnmapFramebuffer (screen);

    if (!epsonMapFramebuffer (screen))
        goto bail4;

    KdShadowUnset (screen->pScreen);

    if (!epsonSetShadow (screen->pScreen))
        goto bail4;

    epsonSetScreenSizes (screen->pScreen);

    /*
     * Set frame buffer mapping
     */
    (*pScreen->ModifyPixmapHeader) (fbGetScreenPixmap (pScreen),
                                    pScreen->width,
                                    pScreen->height,
                                    screen->fb.depth,
                                    screen->fb.bitsPerPixel,
                                    screen->fb.byteStride,
                                    screen->fb.frameBuffer);

    /* set the subpixel order */

    KdSetSubpixelOrder (pScreen, scrpriv->randr);
    if (wasEnabled)
        KdEnableScreen (pScreen);

    return TRUE;

bail4:
    epsonUnmapFramebuffer (screen);
    *scrpriv = oldscr;
    (void) epsonMapFramebuffer (screen);
    pScreen->width = oldwidth;
    pScreen->height = oldheight;
    pScreen->mmWidth = oldmmwidth;
    pScreen->mmHeight = oldmmheight;

    if (wasEnabled)
        KdEnableScreen (pScreen);
    return FALSE;
}

static Bool
epsonRandRInit (ScreenPtr pScreen)
{
    rrScrPrivPtr    pScrPriv;

    if (!RRScreenInit (pScreen))
        return FALSE;

    pScrPriv = rrGetScrPriv(pScreen);
    pScrPriv->rrGetInfo = epsonRandRGetInfo;
    pScrPriv->rrSetConfig = epsonRandRSetConfig;
    return TRUE;
}
#endif

static Bool
epsonCreateColormap (ColormapPtr pmap)
{
    ScreenPtr        pScreen = pmap->pScreen;
    KdScreenPriv(pScreen);
    EpsonPriv        *priv = pScreenPriv->card->driver;
    VisualPtr        pVisual;
    int              i;
    int              nent;
    xColorItem       *pdefs;

    switch (priv->fix.visual) {
    case FB_VISUAL_STATIC_PSEUDOCOLOR:
        pVisual = pmap->pVisual;
        nent = pVisual->ColormapEntries;
        pdefs = malloc (nent * sizeof (xColorItem));
        if (!pdefs)
            return FALSE;
        for (i = 0; i < nent; i++)
            pdefs[i].pixel = i;
        epsonGetColors (pScreen, nent, pdefs);
        for (i = 0; i < nent; i++)
        {
            pmap->red[i].co.local.red = pdefs[i].red;
            pmap->red[i].co.local.green = pdefs[i].green;
            pmap->red[i].co.local.blue = pdefs[i].blue;
        }
        free (pdefs);
        return TRUE;

    default:
        return fbInitializeColormap (pmap);
    }
}

Bool
epsonInitScreen (ScreenPtr pScreen)
{
#ifdef TOUCHSCREEN
    KdTsPhyScreen = pScreen->myNum;
#endif

    pScreen->CreateColormap = epsonCreateColormap;
    return TRUE;
}

Bool
epsonFinishInitScreen (ScreenPtr pScreen)
{
    if (!shadowSetup (pScreen))
        return FALSE;

#ifdef RANDR
    if (!epsonRandRInit (pScreen))
        return FALSE;
#endif

    return TRUE;
}

Bool
epsonCreateResources (ScreenPtr pScreen)
{
    return epsonSetShadow (pScreen);
}

void
epsonPreserve (KdCardInfo *card)
{
}

static int
epsonUpdateFbColormap(EpsonPriv *priv, int minidx, int maxidx)
{
    struct fb_cmap cmap;

    cmap.start = minidx;
    cmap.len = maxidx - minidx + 1;
    cmap.red = &priv->red[minidx];
    cmap.green = &priv->green[minidx];
    cmap.blue = &priv->blue[minidx];
    cmap.transp = 0;

    return ioctl(priv->fd, FBIOPUTCMAP, &cmap);
}

Bool
epsonEnable (ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    EpsonPriv        *priv = pScreenPriv->card->driver;
    int              k;

    priv->var.activate = FB_ACTIVATE_NOW|FB_CHANGE_CMAP_VBL;

    /* display it on the LCD */
    k = ioctl (priv->fd, FBIOPUT_VSCREENINFO, &priv->var);
    if (k < 0)
    {
        perror ("FBIOPUT_VSCREENINFO");
        return FALSE;
    }

    if (priv->fix.visual == FB_VISUAL_DIRECTCOLOR)
    {
        int        i;

        for (i = 0;
             i < (1 << priv->var.red.length) ||
             i < (1 << priv->var.green.length) ||
             i < (1 << priv->var.blue.length); i++)
        {
            priv->red[i] = i * 65535 / ((1 << priv->var.red.length) - 1);
            priv->green[i] = i * 65535 / ((1 << priv->var.green.length) - 1);
            priv->blue[i] = i * 65535 / ((1 << priv->var.blue.length) - 1);
        }

	    epsonUpdateFbColormap(priv, 0, i);
    }
    return TRUE;
}

Bool
epsonDPMS (ScreenPtr pScreen, int mode)
{
    KdScreenPriv(pScreen);
    EpsonPriv    *priv = pScreenPriv->card->driver;
    static int oldmode = -1;

    if (mode == oldmode)
        return TRUE;
#ifdef FBIOPUT_POWERMODE
    if (ioctl (priv->fd, FBIOPUT_POWERMODE, &mode) >= 0)
    {
        oldmode = mode;
        return TRUE;
    }
#endif
#ifdef FBIOBLANK
    if (ioctl (priv->fd, FBIOBLANK, mode ? mode + 1 : 0) >= 0)
    {
        oldmode = mode;
        return TRUE;
    }
#endif
    return FALSE;
}

void
epsonDisable (ScreenPtr pScreen)
{
}

void
epsonRestore (KdCardInfo *card)
{
}

void
epsonScreenFini (KdScreenInfo *screen)
{
}

void
epsonCardFini (KdCardInfo *card)
{
    EpsonPriv    *priv = card->driver;

    munmap (priv->fb_base, priv->fix.smem_len);
    close (priv->fd);
    free (priv);
}

void
epsonGetColors (ScreenPtr pScreen, int n, xColorItem *pdefs)
{
    KdScreenPriv(pScreen);
    EpsonPriv        *priv = pScreenPriv->card->driver;
    struct fb_cmap  cmap;
    int            p;
    int            k;
    int            min, max;

    min = 256;
    max = 0;
    for (k = 0; k < n; k++)
    {
        if (pdefs[k].pixel < min)
            min = pdefs[k].pixel;
        if (pdefs[k].pixel > max)
            max = pdefs[k].pixel;
    }
    cmap.start = min;
    cmap.len = max - min + 1;
    cmap.red = &priv->red[min];
    cmap.green = &priv->green[min];;
    cmap.blue = &priv->blue[min];
    cmap.transp = 0;
    k = ioctl (priv->fd, FBIOGETCMAP, &cmap);
    if (k < 0)
    {
        perror ("can't get colormap");
        return;
    }
    while (n--)
    {
        p = pdefs->pixel;
        pdefs->red = priv->red[p];
        pdefs->green = priv->green[p];
        pdefs->blue = priv->blue[p];
        pdefs++;
    }
}

void
epsonPutColors (ScreenPtr pScreen, int n, xColorItem *pdefs)
{
    KdScreenPriv(pScreen);
    EpsonPriv    *priv = pScreenPriv->card->driver;
    int            p;
    int            min, max;

    min = 256;
    max = 0;
    while (n--)
    {
        p = pdefs->pixel;
        priv->red[p] = pdefs->red;
        priv->green[p] = pdefs->green;
        priv->blue[p] = pdefs->blue;
        if (p < min)
            min = p;
        if (p > max)
            max = p;
        pdefs++;
    }

    epsonUpdateFbColormap(priv, min, max);
}
