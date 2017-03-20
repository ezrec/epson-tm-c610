/*
 * Derived form the CUPS rastertoescpx driver, with several liberties taken
 *
 * http://www.cups.org for the original sources.
 *
 * Licensed under the LGPL2, with no additional restrictions, as per the
 * CUPS LICENSE.txt
 */

/*
 * Include necessary headers...
 */

#include <cups/i18n.h>
#include <cupsfilters/driver.h>
#include <signal.h>

#define _(x)    x


/*
 * Globals...
 */

static cups_rgb_t	*RGB;			/* RGB color separation data */
static cups_cmyk_t	*CMYK;			/* CMYK color separation data */
static cups_lut_t *DitherLuts[3];
static cups_dither_t	*DitherStates[7];	/* Dither state tables */
static unsigned PrinterPlanes;
static unsigned int BitPlanes;
static unsigned PrinterLength;
static unsigned PrinterTop;
static unsigned DotRowCount;
static unsigned DotRowMax;
static unsigned DotBufferSize;
static unsigned OutputFeed;
static unsigned Canceled;
static unsigned char	*PixelBuffer,		/* Pixel buffer */
		*CMYKBuffer,		/* CMYK buffer */
		*OutputBuffers[7],	/* Output buffers */
		*DotBuffers[7],		/* Dot buffers */
		*CompBuffer;		/* Compression buffer */
static short		*InputBuffer;		/* Color separation buffer */
static unsigned MicroWeave;

/*
 * Prototypes...
 */

void	Setup(ppd_file_t *);
void	StartPage(ppd_file_t *, cups_page_header2_t *);
void	EndPage(ppd_file_t *, cups_page_header2_t *);
void	Shutdown(ppd_file_t *);

void	CancelJob(int sig);
void	CompressData(ppd_file_t *, const unsigned char *, const int,
	             int, int, const int, const int, const int);
void	ProcessLine(ppd_file_t *, cups_raster_t *,
	            cups_page_header2_t *, const int y);
void EmitDotRows(ppd_file_t *, cups_page_header2_t *);

/*
 * 'Setup()' - Prepare a printer for graphics output.
 */

void
Setup(ppd_file_t *ppd)		/* I - PPD file */
{
 /*
  * Some EPSON printers need an additional command issued at the
  * beginning of each job to exit from USB "packet" mode...
  */

    cupsWritePrintData("\000\000\000\033\001@EJL 1284.4\n@EJL     \n\033@", 29);
}


/*
 * 'StartPage()' - Start a page of graphics.
 */

void
StartPage(ppd_file_t         *ppd,	/* I - PPD file */
          cups_page_header2_t *header)	/* I - Page header */
{
  int		i, y;			/* Looping vars */
  int		subrow,			/* Current subrow */
		modrow,			/* Subrow modulus */
		plane;			/* Current color plane */
  unsigned char	*ptr;			/* Pointer into dot buffer */
  int		bands;			/* Number of bands to allocate */
  int		units;			/* Units for resolution */
  const char	*colormodel;		/* Color model string */
  char		resolution[PPD_MAX_NAME],
					/* Resolution string */
		spec[PPD_MAX_NAME];	/* PPD attribute name */
  ppd_attr_t	*attr;			/* Attribute from PPD file */
  const float	default_lut[] =	/* Default dithering lookup table */
		{
		  0.0,
		  0.25,
		  0.5,
		  0.75,
		};


  fprintf(stderr, "DEBUG: StartPage...\n");
  fprintf(stderr, "DEBUG: MediaClass = \"%s\"\n", header->MediaClass);
  fprintf(stderr, "DEBUG: MediaColor = \"%s\"\n", header->MediaColor);
  fprintf(stderr, "DEBUG: MediaType = \"%s\"\n", header->MediaType);
  fprintf(stderr, "DEBUG: OutputType = \"%s\"\n", header->OutputType);

  fprintf(stderr, "DEBUG: AdvanceDistance = %d\n", header->AdvanceDistance);
  fprintf(stderr, "DEBUG: AdvanceMedia = %d\n", header->AdvanceMedia);
  fprintf(stderr, "DEBUG: Collate = %d\n", header->Collate);
  fprintf(stderr, "DEBUG: CutMedia = %d\n", header->CutMedia);
  fprintf(stderr, "DEBUG: Duplex = %d\n", header->Duplex);
  fprintf(stderr, "DEBUG: HWResolution = [ %d %d ]\n", header->HWResolution[0],
          header->HWResolution[1]);
  fprintf(stderr, "DEBUG: ImagingBoundingBox = [ %d %d %d %d ]\n",
          header->ImagingBoundingBox[0], header->ImagingBoundingBox[1],
          header->ImagingBoundingBox[2], header->ImagingBoundingBox[3]);
  fprintf(stderr, "DEBUG: InsertSheet = %d\n", header->InsertSheet);
  fprintf(stderr, "DEBUG: Jog = %d\n", header->Jog);
  fprintf(stderr, "DEBUG: LeadingEdge = %d\n", header->LeadingEdge);
  fprintf(stderr, "DEBUG: Margins = [ %d %d ]\n", header->Margins[0],
          header->Margins[1]);
  fprintf(stderr, "DEBUG: ManualFeed = %d\n", header->ManualFeed);
  fprintf(stderr, "DEBUG: MediaPosition = %d\n", header->MediaPosition);
  fprintf(stderr, "DEBUG: MediaWeight = %d\n", header->MediaWeight);
  fprintf(stderr, "DEBUG: MirrorPrint = %d\n", header->MirrorPrint);
  fprintf(stderr, "DEBUG: NegativePrint = %d\n", header->NegativePrint);
  fprintf(stderr, "DEBUG: NumCopies = %d\n", header->NumCopies);
  fprintf(stderr, "DEBUG: Orientation = %d\n", header->Orientation);
  fprintf(stderr, "DEBUG: OutputFaceUp = %d\n", header->OutputFaceUp);
  fprintf(stderr, "DEBUG: PageSize = [ %d %d ]\n", header->PageSize[0],
          header->PageSize[1]);
  fprintf(stderr, "DEBUG: Separations = %d\n", header->Separations);
  fprintf(stderr, "DEBUG: TraySwitch = %d\n", header->TraySwitch);
  fprintf(stderr, "DEBUG: Tumble = %d\n", header->Tumble);
  fprintf(stderr, "DEBUG: cupsWidth = %d\n", header->cupsWidth);
  fprintf(stderr, "DEBUG: cupsHeight = %d\n", header->cupsHeight);
  fprintf(stderr, "DEBUG: cupsMediaType = %d\n", header->cupsMediaType);
  fprintf(stderr, "DEBUG: cupsBitsPerColor = %d\n", header->cupsBitsPerColor);
  fprintf(stderr, "DEBUG: cupsBitsPerPixel = %d\n", header->cupsBitsPerPixel);
  fprintf(stderr, "DEBUG: cupsBytesPerLine = %d\n", header->cupsBytesPerLine);
  fprintf(stderr, "DEBUG: cupsColorOrder = %d\n", header->cupsColorOrder);
  fprintf(stderr, "DEBUG: cupsColorSpace = %d\n", header->cupsColorSpace);
  fprintf(stderr, "DEBUG: cupsCompression = %d\n", header->cupsCompression);
  fprintf(stderr, "DEBUG: cupsRowCount = %d\n", header->cupsRowCount);
  fprintf(stderr, "DEBUG: cupsRowFeed = %d\n", header->cupsRowFeed);
  fprintf(stderr, "DEBUG: cupsRowStep = %d\n", header->cupsRowStep);

 /*
  * Figure out the color model and spec strings...
  */

  switch (header->cupsColorSpace)
  {
    case CUPS_CSPACE_K :
        colormodel = "Black";
	break;
    case CUPS_CSPACE_W :
        colormodel = "Gray";
	break;
    default :
    case CUPS_CSPACE_RGB :
        colormodel = "RGB";
	break;
    case CUPS_CSPACE_CMYK :
        colormodel = "CMYK";
	break;
  }

  if (header->HWResolution[0] != header->HWResolution[1])
    snprintf(resolution, sizeof(resolution), "%dx%ddpi",
             header->HWResolution[0], header->HWResolution[1]);
  else
    snprintf(resolution, sizeof(resolution), "%ddpi",
             header->HWResolution[0]);

  if (!header->MediaType[0])
    strcpy(header->MediaType, "Plain");

 /*
  * Load the appropriate color profiles...
  */

  RGB  = NULL;
  CMYK = NULL;

  fputs("DEBUG: Attempting to load color profiles using the following values:\n", stderr);
  fprintf(stderr, "DEBUG: ColorModel = %s\n", colormodel);
  fprintf(stderr, "DEBUG: MediaType = %s\n", header->MediaType);
  fprintf(stderr, "DEBUG: Resolution = %s\n", resolution);

  if (header->cupsColorSpace == CUPS_CSPACE_RGB ||
      header->cupsColorSpace == CUPS_CSPACE_W)
    RGB = cupsRGBLoad(ppd, colormodel, header->MediaType, resolution);
  else
    RGB = NULL;

  CMYK = cupsCMYKLoad(ppd, colormodel, header->MediaType, resolution);

  if (RGB)
    fputs("DEBUG: Loaded RGB separation from PPD.\n", stderr);

  if (CMYK)
    fputs("DEBUG: Loaded CMYK separation from PPD.\n", stderr);
  else
  {
    fputs("DEBUG: Loading default CMY separation.\n", stderr);
    CMYK = cupsCMYKNew(3);
  }

  PrinterPlanes = CMYK->num_channels;

  fprintf(stderr, "DEBUG: PrinterPlanes = %d\n", PrinterPlanes);

 /*
  * Get the dithering parameters...
  */

  switch (PrinterPlanes)
  {
    case 1 : /* K */
        DitherLuts[0] = cupsLutLoad(ppd, colormodel, header->MediaType,
	                            resolution, "Black");
        break;

    case 3 : /* CMY */
        DitherLuts[0] = cupsLutLoad(ppd, colormodel, header->MediaType,
	                            resolution, "Cyan");
        DitherLuts[1] = cupsLutLoad(ppd, colormodel, header->MediaType,
	                            resolution, "Magenta");
        DitherLuts[2] = cupsLutLoad(ppd, colormodel, header->MediaType,
	                            resolution, "Yellow");
        break;
  }

  for (plane = 0; plane < PrinterPlanes; plane ++)
  {
    DitherStates[plane] = cupsDitherNew(header->cupsWidth);

    if (!DitherLuts[plane])
      DitherLuts[plane] = cupsLutNew(sizeof(default_lut)/sizeof(default_lut[0]), default_lut);
  }

  BitPlanes = 2;

 /*
  * Initialize the printer...
  */

  printf("\033@");

   /*
    * Go into remote mode...
    */

    cupsWritePrintData("\033(R\010\000\000REMOTE1", 13);

	cupsWritePrintData("EX\006\000\000\000\000\000\005\000", 10);

    if ((attr = ppdFindAttr(ppd, "cupsESCPAC", spec)) != NULL && attr->value)
    {
     /*
      * Enable/disable cutter.
      */

      cupsWritePrintData("AC\002\000\000", 5);
      putchar(header->CutMedia ? '\001' : '\000');
    }

   /*
    * Exit remote mode...
    */

    cupsWritePrintData("\033\000\000\000", 4);

    /*
     * Idle spacing
     */
    for (int i = 0; i < 2; i++)
    {
        cupsWritePrintData("\033(d\xff\x7f", 5);
        for (int j = 0; j < 32767; j++)
            cupsWritePrintData("\000", 1);
    }

 /*
  * Enter graphics mode...
  */

  cupsWritePrintData("\033(G\001\000\001", 6);

 /*
  * Set the line feed increment...
  */

  /* TODO: get this from the PPD file... */
  for (units = 1440; units < header->HWResolution[0]; units *= 2);

  cupsWritePrintData("\033(U\005\000", 5);
  putchar(units / header->HWResolution[1]);
  putchar(units / header->HWResolution[1]);
  putchar(units / header->HWResolution[0]);
  putchar(units);
  putchar(units >> 8);

 /*
  * Set the page length...
  */

  PrinterLength = header->PageSize[1] * header->HWResolution[1] / 72;

  cupsWritePrintData("\033(C\004\000", 5);
  putchar(PrinterLength & 255);
  putchar((PrinterLength >> 8) & 255);
  putchar((PrinterLength >> 16) & 255);
  putchar((PrinterLength >> 24) & 255);

 /*
  * Set the top and bottom margins...
  */

  PrinterTop = (int)((ppd->sizes[1].length - ppd->sizes[1].top) *
                     header->HWResolution[1] / 72.0);

  cupsWritePrintData("\033(c\010\000", 5);

  putchar(PrinterTop);
  putchar(PrinterTop >> 8);
  putchar(PrinterTop >> 16);
  putchar(PrinterTop >> 24);

  putchar(PrinterLength);
  putchar(PrinterLength >> 8);
  putchar(PrinterLength >> 16);
  putchar(PrinterLength >> 24);

 /*
  * Setup softweave parameters...
  */

  DotRowCount = 0;
  DotRowMax     = 180;
  DotBufferSize = (header->cupsWidth * BitPlanes + 7) / 8;

  fprintf(stderr, "DEBUG: DotBufferSize = %d\n", DotBufferSize);
  fprintf(stderr, "DEBUG: DotRowMax = %d\n", DotRowMax);
  fprintf(stderr, "DEBUG: DotRowCount = %d\n", DotRowCount);

  fprintf(stderr, "DEBUG: model_number = %x\n", ppd->model_number);

 /*
  * Allocate memory for a single line of graphics...
  */

  ptr = calloc(PrinterPlanes, DotBufferSize * DotRowMax);

  for (plane = 0; plane < PrinterPlanes; plane ++, ptr += DotBufferSize * DotRowMax)
    DotBuffers[plane] = ptr;

 /*
  * Set the output resolution...
  */

  // Paper load/ejecting
  cupsWritePrintData("\033\x19\x01", 3);

 /*
  * Set the top of form...
  */

  OutputFeed = 0;

 /*
  * Allocate buffers as needed...
  */

  PixelBuffer      = calloc(1, header->cupsBytesPerLine);
  InputBuffer      = calloc(PrinterPlanes, header->cupsWidth * sizeof(InputBuffer[0]));
  OutputBuffers[0] = calloc(PrinterPlanes, header->cupsWidth * DotRowMax);

  for (i = 1; i < PrinterPlanes + 1; i ++)
    OutputBuffers[i] = OutputBuffers[0] + i * header->cupsWidth * DotRowMax;

  if (RGB)
    CMYKBuffer = calloc(PrinterPlanes + 1, header->cupsWidth);

  CompBuffer = malloc(10 * DotBufferSize * DotRowMax);
}


/*
 * 'EndPage()' - Finish a page of graphics.
 */

void
EndPage(ppd_file_t         *ppd,	/* I - PPD file */
        cups_page_header2_t *header)	/* I - Page header */
{
  int		i;			/* Looping var */
  int		plane;			/* Current plane */
  int		subrow;			/* Current subrow */
  int		subrows;		/* Number of subrows */

  EmitDotRows(ppd, header);

 /*
  * Output the last bands of print data as necessary...
  */
   free(DotBuffers[0]);

 /*
  * Output a page eject sequence...
  */

  putchar(12);

 /*
  * Free memory for the page...
  */

  for (i = 0; i < PrinterPlanes; i ++)
  {
    cupsDitherDelete(DitherStates[i]);
    cupsLutDelete(DitherLuts[i]);
  }

  free(OutputBuffers[0]);

  free(PixelBuffer);
  free(InputBuffer);
  free(CompBuffer);

  cupsCMYKDelete(CMYK);

  if (RGB)
  {
    cupsRGBDelete(RGB);
    free(CMYKBuffer);
  }
}


/*
 * 'Shutdown()' - Shutdown a printer.
 */

void
Shutdown(ppd_file_t *ppd)		/* I - PPD file */
{
 /*
  * Reset the printer...
  */

  printf("\033@");
  printf("\033@");

 /*
  * Go into remote mode...
  */

  cupsWritePrintData("\033(R\010\000\000REMOTE1", 13);

 /*
  * Load defaults...
  */

  cupsWritePrintData("LD\000\000", 4);

 /*
  * Exit remote mode...
  */
}


/*
 * 'CancelJob()' - Cancel the current job...
 */

void
CancelJob(int sig)			/* I - Signal */
{
  (void)sig;

  Canceled = 1;
}


/*
 * 'CompressData()' - Compress a line of graphics.
 */

void
CompressData(ppd_file_t          *ppd,	/* I - PPD file information */
             const unsigned char *line,	/* I - Data to compress */
             const int           length,/* I - Number of bytes */
	     int                 plane,	/* I - Color plane */
	     int                 type,	/* I - Type of compression */
	     const int           rows,	/* I - Number of lines to write */
	     const int           offset, /* I - Head offset */
	     const int           microweave)
{
  register const unsigned char *line_ptr,
					/* Current byte pointer */
        	*line_end,		/* End-of-line byte pointer */
        	*start;			/* Start of compression sequence */
  register unsigned char *comp_ptr;	/* Pointer into compression buffer */
  register int  count;			/* Count of bytes for output */
  register int	bytes;			/* Number of bytes per row */
  static int	ctable[7][7] =		/* Colors */
		{
		  {  0,  0,  0,  0,  0,  0,  0 },	/* K */
		  {  0, 16,  0,  0,  0,  0,  0 },	/* Kk */
		  {  2,  1,  4,  0,  0,  0,  0 },	/* CMY */
		  {  2,  1,  4,  0,  0,  0,  0 },	/* CMYK */
		  {  0,  0,  0,  0,  0,  0,  0 },
		  {  2, 18,  1, 17,  4,  0,  0 },	/* CcMmYK */
		  {  2, 18,  1, 17,  4,  0, 16 },	/* CcMmYKk */
		};


  switch (type)
  {
    case 0 :
       /*
	* Do no compression...
	*/

	line_ptr = (const unsigned char *)line;
	line_end = (const unsigned char *)line + length;
	break;

    default :
       /*
        * Do TIFF pack-bits encoding...
        */

	line_ptr = (const unsigned char *)line;
	line_end = (const unsigned char *)line + length;
	comp_ptr = CompBuffer;

	while (line_ptr < line_end && (comp_ptr - CompBuffer) < length)
	{
	  if ((line_ptr + 1) >= line_end)
	  {
	   /*
	    * Single byte on the end...
	    */

	    *comp_ptr++ = 0x00;
	    *comp_ptr++ = *line_ptr++;
	  }
	  else if (line_ptr[0] == line_ptr[1])
	  {
	   /*
	    * Repeated sequence...
	    */

	    line_ptr ++;
	    count = 2;

	    while (line_ptr < (line_end - 1) &&
        	   line_ptr[0] == line_ptr[1] &&
        	   count < 127)
	    {
              line_ptr ++;
              count ++;
	    }

	    *comp_ptr++ = 257 - count;
	    *comp_ptr++ = *line_ptr++;
	  }
	  else
	  {
	   /*
	    * Non-repeated sequence...
	    */

	    start    = line_ptr;
	    line_ptr ++;
	    count    = 1;

	    while (line_ptr < (line_end - 1) &&
        	   line_ptr[0] != line_ptr[1] &&
        	   count < 127)
	    {
              line_ptr ++;
              count ++;
	    }

	    *comp_ptr++ = count - 1;

	    memcpy(comp_ptr, start, count);
	    comp_ptr += count;
	  }
	}

        if ((comp_ptr - CompBuffer) < length)
	{
          line_ptr = (const unsigned char *)CompBuffer;
          line_end = (const unsigned char *)comp_ptr;
	}
	else
	{
	  type     = 0;
	  line_ptr = (const unsigned char *)line;
	  line_end = (const unsigned char *)line + length;
	}
	break;
  }

  if (microweave || offset)
  {
    cupsWritePrintData("\033($\004\000", 5);
    putchar(offset & 255);
    putchar((offset >> 8) & 255);
    putchar((offset >> 16) & 255);
    putchar((offset >> 24) & 255);
  }

 /*
  * Send the graphics...
  */

  bytes = length / rows;

   /*
    * Send graphics with ESC i command.
    */

    printf("\033i");
    putchar(ctable[PrinterPlanes - 1][plane] | (microweave ? 64 : 0));
    putchar(type != 0);
    putchar(BitPlanes);
    putchar(bytes & 255);
    putchar(bytes >> 8);
    putchar(rows & 255);
    putchar(rows >> 8);

  cupsWritePrintData(line_ptr, line_end - line_ptr);

 /*
  * Position the print head...
  */
  if (microweave)
    putchar(0x0d);

}

void EmitDotRows(ppd_file_t *ppd, cups_page_header2_t *header)
{
    unsigned plane;
    unsigned input_width = DotRowMax * header->cupsWidth;
  unsigned rows = DotRowCount / 2;

    if (!DotRowCount)
    {
        return;
    }

    for (plane = 0; plane < PrinterPlanes; plane++)
    {
        unsigned microweave;

         /*
          * Handle microweaved output...
          */

        // Anything to print?
        if (cupsCheckBytes(OutputBuffers[plane], input_width))
            continue;

        for (microweave = 0; microweave < 2; microweave++)
        {
            int row;

            for (row = 0; row < rows; row++)
            {
                cupsPackHorizontal2(&OutputBuffers[plane][input_width / 2 * microweave + row * header->cupsWidth], &DotBuffers[plane][row * DotBufferSize], header->cupsWidth, 1);
            }

            if (OutputFeed > 0)
            {
                cupsWritePrintData("\033(v\004\000", 5);
                putchar(OutputFeed & 255);
                putchar((OutputFeed >> 8) & 255);
                putchar((OutputFeed >> 16) & 255);
                putchar((OutputFeed >> 24) & 255);
                OutputFeed = 0;
             }

             CompressData(ppd, DotBuffers[plane], DotBufferSize * rows, plane, header->cupsCompression, rows, 0, microweave);
        }

        fflush(stdout);
    }

    OutputFeed += DotRowCount;
    DotRowCount = 0;
}


/*
 * 'ProcessLine()' - Read graphics from the page stream and output as needed.
 */

void
ProcessLine(ppd_file_t         *ppd,	/* I - PPD file */
            cups_raster_t      *ras,	/* I - Raster stream */
            cups_page_header2_t *header,	/* I - Page header */
            const int          y)	/* I - Current scanline */
{
  int		plane,			/* Current color plane */
		width,			/* Width of line */
		offset,			/* Offset to current line */
		pass;			/* Pass number */


 /*
  * Read a row of graphics...
  */

  if (!cupsRasterReadPixels(ras, PixelBuffer, header->cupsBytesPerLine))
    return;

 /*
  * Perform the color separation...
  */

  width    = header->cupsWidth;

  switch (header->cupsColorSpace)
  {
    case CUPS_CSPACE_W :
        if (RGB)
	{
	  cupsRGBDoGray(RGB, PixelBuffer, CMYKBuffer, width);
	  cupsCMYKDoCMYK(CMYK, CMYKBuffer, InputBuffer, width);
	}
	else
          cupsCMYKDoGray(CMYK, PixelBuffer, InputBuffer, width);
	break;

    case CUPS_CSPACE_K :
        cupsCMYKDoBlack(CMYK, PixelBuffer, InputBuffer, width);
	break;

    default :
    case CUPS_CSPACE_RGB :
        if (RGB)
	{
	  cupsRGBDoRGB(RGB, PixelBuffer, CMYKBuffer, width);
	  cupsCMYKDoCMYK(CMYK, CMYKBuffer, InputBuffer, width);
	}
	else
          cupsCMYKDoRGB(CMYK, PixelBuffer, InputBuffer, width);
	break;

    case CUPS_CSPACE_CMYK :
        cupsCMYKDoCMYK(CMYK, PixelBuffer, InputBuffer, width);
	break;
  }

 /*
  * Dither the pixels...
  */

  unsigned int base = (DotRowCount & 1) * DotRowMax / 2;
  unsigned int index = DotRowCount / 2;
  for (plane = 0; plane < PrinterPlanes; plane ++)
  {
    cupsDitherLine(DitherStates[plane], DitherLuts[plane], InputBuffer + plane,
                   PrinterPlanes, &OutputBuffers[plane][(base + index) * header->cupsWidth]);
  }

  DotRowCount++;

  if (DotRowCount == DotRowMax)
  {
    EmitDotRows(ppd, header);
  }

}


/*
 * 'main()' - Main entry and processing of driver.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line arguments */
     char *argv[])			/* I - Command-line arguments */
{
  int			fd;		/* File descriptor */
  cups_raster_t		*ras;		/* Raster stream for printing */
  cups_page_header2_t	header;		/* Page header from file */
  int			page;		/* Current page */
  int			y;		/* Current line */
  ppd_file_t		*ppd;		/* PPD file */
  int			num_options;	/* Number of options */
  cups_option_t		*options;	/* Options */
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action;		/* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */


 /*
  * Make sure status messages are not buffered...
  */

  setbuf(stderr, NULL);

 /*
  * Check command-line...
  */

  if (argc < 6 || argc > 7)
  {
    _cupsLangPrintFilter(stderr, "ERROR",
                         _("%s job-id user title copies options [file]"),
			 "rastertoescpx");
    return (1);
  }

  num_options = cupsParseOptions(argv[5], 0, &options);

 /*
  * Open the PPD file...
  */

  ppd = ppdOpenFile(getenv("PPD"));

  if (!ppd)
  {
    ppd_status_t	status;		/* PPD error */
    int			linenum;	/* Line number */

    _cupsLangPrintFilter(stderr, "ERROR",
                         _("The PPD file could not be opened."));

    status = ppdLastError(&linenum);

    fprintf(stderr, "DEBUG: %s on line %d.\n", ppdErrorString(status), linenum);

    return (1);
  }

  ppdMarkDefaults(ppd);
  cupsMarkOptions(ppd, num_options, options);

 /*
  * Open the page stream...
  */

  if (argc == 7)
  {
    if ((fd = open(argv[6], O_RDONLY)) == -1)
    {
      _cupsLangPrintError("ERROR", _("Unable to open raster file"));
      return (1);
    }
  }
  else
    fd = 0;

  ras = cupsRasterOpen(fd, CUPS_RASTER_READ);

 /*
  * Register a signal handler to eject the current page if the
  * job is cancelled.
  */

  Canceled = 0;

#ifdef HAVE_SIGSET /* Use System V signals over POSIX to avoid bugs */
  sigset(SIGTERM, CancelJob);
#elif defined(HAVE_SIGACTION)
  memset(&action, 0, sizeof(action));

  sigemptyset(&action.sa_mask);
  action.sa_handler = CancelJob;
  sigaction(SIGTERM, &action, NULL);
#else
  signal(SIGTERM, CancelJob);
#endif /* HAVE_SIGSET */

 /*
  * Initialize the print device...
  */

  Setup(ppd);

 /*
  * Process pages as needed...
  */

  page = 0;

  while (cupsRasterReadHeader2(ras, &header))
  {
   /*
    * Write a status message with the page number and number of copies.
    */

    if (Canceled)
      break;

    page ++;

    fprintf(stderr, "PAGE: %d 1\n", page);
    _cupsLangPrintFilter(stderr, "INFO", _("Starting page %d."), page);

    StartPage(ppd, &header);

    for (y = 0; y < header.cupsHeight; y ++)
    {
     /*
      * Let the user know how far we have progressed...
      */

      if (Canceled)
	break;

      if ((y & 127) == 0)
      {
        _cupsLangPrintFilter(stderr, "INFO",
	                     _("Printing page %d, %d%% complete."),
			     page, 100 * y / header.cupsHeight);
        fprintf(stderr, "ATTR: job-media-progress=%d\n",
		100 * y / header.cupsHeight);
      }

     /*
      * Read and write a line of graphics or whitespace...
      */

      ProcessLine(ppd, ras, &header, y);
    }

   /*
    * Eject the page...
    */

    _cupsLangPrintFilter(stderr, "INFO", _("Finished page %d."), page);

    EndPage(ppd, &header);

    if (Canceled)
      break;
  }

  Shutdown(ppd);

  cupsFreeOptions(num_options, options);

  cupsRasterClose(ras);

  if (fd != 0)
    close(fd);

  if (page == 0)
  {
    _cupsLangPrintFilter(stderr, "ERROR", _("No pages were found."));
    return (1);
  }
  else
  {
    _cupsLangPrintFilter(stderr, "INFO", _("Ready to print."));
    return (0);
  }
}


/*
 * End of "$Id$".
 */
