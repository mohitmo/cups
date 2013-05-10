/*
 * "$Id$"
 *
 *   Device scanning routines for the Common UNIX Printing System (CUPS).
 *
 *   Copyright 1997-2000 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Easy Software Products and are protected by Federal
 *   copyright law.  Distribution and use rights are outlined in the file
 *   "LICENSE.txt" which should have been included with this file.  If this
 *   file is missing or damaged please contact Easy Software Products
 *   at:
 *
 *       Attn: CUPS Licensing Information
 *       Easy Software Products
 *       44141 Airport View Drive, Suite 204
 *       Hollywood, Maryland 20636-3111 USA
 *
 *       Voice: (301) 373-9603
 *       EMail: cups-info@cups.org
 *         WWW: http://www.cups.org
 *
 * Contents:
 *
 *   LoadDevices() - Load all available devices.
 */

/*
 * Include necessary headers...
 */

#include "cupsd.h"


/*
 * Device information structure...
 */

typedef struct
{
  char	device_class[128],		/* Device class */
	device_make_and_model[128],	/* Make and model, if known */
	device_info[128],		/* Device info/description */
	device_uri[1024];		/* Device URI */
} dev_info_t;


/*
 * Local globals...
 */

static int		num_devs,	/* Number of devices */
			alloc_devs;	/* Number of allocated entries */
static dev_info_t	*devs;		/* Device info */


/*
 * Local functions...
 */

static int	compare_devs(const dev_info_t *p0, const dev_info_t *p1);


/*
 * 'LoadDevices()' - Load all available devices.
 */

void
LoadDevices(const char *d)	/* I - Directory to scan */
{
  int		i;		/* Looping var */
  FILE		*fp;		/* Pipe to device backend */
  DIR		*dir;		/* Directory pointer */
  DIRENT	*dent;		/* Directory entry */
  char		filename[1024],	/* Name of backend */
		line[2048],	/* Line from backend */
		dclass[64],	/* Device class */
		uri[1024],	/* Device URI */
		info[128],	/* Device info */
		make_model[256];/* Make and model */
  dev_info_t	*dev;		/* Current device */


 /*
  * We always support the "file" device...
  */

  Devices = ippNew();

  ippAddString(Devices, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
               "device-class", NULL, "file");
  ippAddString(Devices, IPP_TAG_PRINTER, IPP_TAG_TEXT,
               "device-info", NULL, "Disk File");
  ippAddString(Devices, IPP_TAG_PRINTER, IPP_TAG_TEXT,
               "device-make-and-model", NULL, "Unknown");
  ippAddString(Devices, IPP_TAG_PRINTER, IPP_TAG_URI,
               "device-uri", NULL, "file");

 /*
  * Try opening the backend directory...
  */

  if ((dir = opendir(d)) == NULL)
  {
    LogMessage(L_ERROR, "LoadDevices: Unable to open backend directory \"%s\": %s",
               d, strerror(errno));
    return;
  }

 /*
  * Setup the devices array...
  */

  alloc_devs = 0;
  num_devs   = 0;
  devs       = (dev_info_t *)0;

 /*
  * Loop through all of the device backends...
  */

  while ((dent = readdir(dir)) != NULL)
  {
   /*
    * Skip "." and ".."...
    */

    if (dent->d_name[0] == '.')
      continue;

   /*
    * Run the backend with no arguments and collect the output...
    */

    snprintf(filename, sizeof(filename), "%s/%s", d, dent->d_name);
    if ((fp = popen(filename, "r")) != NULL)
    {
      while (fgets(line, sizeof(line), fp) != NULL)
      {
       /*
        * Each line is of the form:
	*
	*   class URI "make model" "name"
	*/

        if (sscanf(line, "%63s%1023s%*[ \t]\"%127[^\"]\"%*[ \t]\"%255[^\"]",
	           dclass, uri, make_model, info) != 4)
        {
	 /*
	  * Bad format; strip trailing newline and write an error message.
	  */

	  line[strlen(line) - 1] = '\0';
	  LogMessage(L_ERROR, "LoadDevices: Bad line from \"%s\": %s",
	             dent->d_name, line);
        }
	else
	{
	 /*
	  * Add the device to the array of available devices...
	  */

	  if (num_devs >= alloc_devs)
	  {
	   /*
	    * Allocate (more) memory for the PPD files...
	    */

	    if (alloc_devs == 0)
              dev = malloc(sizeof(dev_info_t) * 16);
	    else
              dev = realloc(devs, sizeof(dev_info_t) * (alloc_devs + 16));

	    if (dev == NULL)
	    {
              LogMessage(L_ERROR, "load_devs: Ran out of memory for %d devices!",
	        	 alloc_devs + 16);
              closedir(dir);
	      return;
	    }

	    devs = dev;
	    alloc_devs += 16;
	  }

	  dev = devs + num_devs;
	  num_devs ++;

	  memset(dev, 0, sizeof(dev_info_t));
	  strncpy(dev->device_class, dclass, sizeof(dev->device_class) - 1);
	  strncpy(dev->device_info, info, sizeof(dev->device_info) - 1);
	  strncpy(dev->device_make_and_model, make_model,
        	  sizeof(dev->device_make_and_model) - 1);
	  strncpy(dev->device_uri, uri, sizeof(dev->device_uri) - 1);

          LogMessage(L_DEBUG, "LoadDevices: Added device \"%s\"...", uri);
	}
      }

      pclose(fp);
    }
    else
      LogMessage(L_WARN, "LoadDevices: Unable to execute \"%s\" backend: %s",
                 dent->d_name, strerror(errno));
  }

  closedir(dir);

 /*
  * Sort the available devices...
  */

  if (num_devs > 1)
    qsort(devs, num_devs, sizeof(dev_info_t),
          (int (*)(const void *, const void *))compare_devs);

 /*
  * Create the list of devices...
  */

  for (i = num_devs, dev = devs; i > 0; i --, dev ++)
  {
   /*
    * Add strings to attributes...
    */

    ippAddSeparator(Devices);
    ippAddString(Devices, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
                 "device-class", NULL, dev->device_class);
    ippAddString(Devices, IPP_TAG_PRINTER, IPP_TAG_TEXT,
                 "device-info", NULL, dev->device_info);
    ippAddString(Devices, IPP_TAG_PRINTER, IPP_TAG_TEXT,
                 "device-make-and-model", NULL, dev->device_make_and_model);
    ippAddString(Devices, IPP_TAG_PRINTER, IPP_TAG_URI,
                 "device-uri", NULL, dev->device_uri);
  }

 /*
  * Free the devices array...
  */

  if (alloc_devs)
    free(devs);
}


/*
 * 'compare_devs()' - Compare PPD file make and model names for sorting.
 */

static int				/* O - Result of comparison */
compare_devs(const dev_info_t *d0,	/* I - First PPD file */
             const dev_info_t *d1)	/* I - Second PPD file */
{
  const char	*s,			/* First name */
		*t;			/* Second name */
  int		diff,			/* Difference between digits */
		digits;			/* Number of digits */


 /* 
  * First compare names...
  */

  s = d0->device_info;
  t = d1->device_info;

 /*
  * Loop through both nicknames, returning only when a difference is
  * seen.  Also, compare whole numbers rather than just characters, too!
  */

  while (*s && *t)
  {
    if (isdigit(*s) && isdigit(*t))
    {
     /*
      * Got a number; start by skipping leading 0's...
      */

      while (*s == '0')
        s ++;
      while (*t == '0')
        t ++;

     /*
      * Skip equal digits...
      */

      while (isdigit(*s) && *s == *t)
      {
        s ++;
	t ++;
      }

     /*
      * Bounce out if *s and *t aren't both digits...
      */

      if (isdigit(*s) && !isdigit(*t))
        return (1);
      else if (!isdigit(*s) && isdigit(*t))
        return (-1);
      else if (!isdigit(*s) || !isdigit(*t))
        continue;     

      if (*s < *t)
        diff = -1;
      else
        diff = 1;

     /*
      * Figure out how many more digits there are...
      */

      digits = 0;
      s ++;
      t ++;

      while (isdigit(*s))
      {
        digits ++;
	s ++;
      }

      while (isdigit(*t))
      {
        digits --;
	t ++;
      }

     /*
      * Return if the number or value of the digits is different...
      */

      if (digits < 0)
        return (-1);
      else if (digits > 0)
        return (1);
      else if (diff)
        return (diff);
    }
    else if (tolower(*s) < tolower(*t))
      return (-1);
    else if (tolower(*s) > tolower(*t))
      return (1);
    else
    {
      s ++;
      t ++;
    }
  }

 /*
  * Return the results of the final comparison...
  */

  if (*s)
    return (1);
  else if (*t)
    return (-1);
  else if ((diff = strcasecmp(d0->device_class, d1->device_class)) != 0)
    return (diff);
  else
    return (strcasecmp(d0->device_uri, d1->device_uri));
}


/*
 * End of "$Id$".
 */
