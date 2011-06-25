/*!
   \file Gp3.c

   \brief OGSF library - loading point sets (lower level functions)

   GRASS OpenGL gsurf OGSF Library 

   (C) 1999-2008 by the GRASS Development Team

   This program is free software under the 
   GNU General Public License (>=v2). 
   Read the file COPYING that comes with GRASS
   for details.

   \author Bill Brown USACERL, GMSL/University of Illinois (January 1994)
   \author Doxygenized by Martin Landa <landa.martin gmail.com> (May 2008)
 */

#include <stdlib.h>

#include <grass/gis.h>
#include <grass/colors.h>
#include <grass/raster.h>
#include <grass/vector.h>
#include <grass/dbmi.h>
#include <grass/glocale.h>
#include <grass/gstypes.h>

/*!
   \brief Load to points to memory

   The other alternative may be to load to a tmp file.

   \param name name of vector map to be loaded
   \param[out] nsites number of loaded points
   \param[out] has_z 2D or 3D points data loaded?

   \return pointer to geopoint struct (array)
   \return NULL on failure
 */
geopoint *Gp_load_sites(const char *name, int *nsites, int *has_z)
{
    struct Map_info map;
    static struct line_pnts *Points = NULL;
    struct line_cats *Cats = NULL;
    geopoint *top, *gpt, *prev;
    int np, ltype, eof;
    struct Cell_head wind;
    int ndim;
    const char *mapset;

    np = 0;
    eof = 0;
    
    mapset = G_find_vector2(name, "");
    if (!mapset) {
	G_warning(_("Vector map <%s> not found"), name);
	return NULL;
    }
    
    Vect_set_open_level(1);
    if (Vect_open_old(&map, name, "") == -1) {
	G_fatal_error(_("Unable to open vector map <%s>"),
		      G_fully_qualified_name(name, mapset));
    }
    
    Points = Vect_new_line_struct();
    Cats = Vect_new_cats_struct();
    
    top = gpt = (geopoint *) G_malloc(sizeof(geopoint));
    G_zero(gpt, sizeof(geopoint));
    if (!top) {
	return NULL;
    }

    G_get_set_window(&wind);

    /* get ndim */
    *has_z = 0;
    ndim = 2;
    if (Vect_is_3d(&map)) {
	*has_z = 1;
	ndim = 3;
    }

    while (eof == 0) {
	ltype = Vect_read_next_line(&map, Points, Cats);
	switch (ltype) {
	case -1:
	    {
		G_warning(_("Unable to read vector map <%s>"),
			  G_fully_qualified_name(name, mapset));
		return (NULL);
	    }
	case -2:		/* EOF */
	    {
		eof = 1;
		continue;
	    }
	}
	if ((ltype & GV_POINTS)) {
	    np++;
	    gpt->p3[X] = Points->x[0];
	    gpt->p3[Y] = Points->y[0];

	    if (ndim > 2) {
		gpt->dims = 3;
		gpt->p3[Z] = Points->z[0];
	    }
	    else {
		gpt->dims = 2;
	    }

	    /* Store category info for thematic display */
	    if (Cats->n_cats > 0) {
		gpt->cats = Cats;
		Cats = Vect_new_cats_struct();
	    }
	    else {
		Vect_reset_cats(Cats);
	    }
	    
	    G_debug(5, "loading vector point %d x=%f y=%f ncats=%d",
		    np, Points->x[0], Points->y[0], Cats->n_cats);

	    gpt->next = (geopoint *) G_malloc(sizeof(geopoint));	/* G_fatal_error */
	    G_zero(gpt->next, sizeof(geopoint));
	    if (!gpt->next) {
		return NULL;
	    }

	    prev = gpt;
	    gpt = gpt->next;
	}

    }
    if (np > 0) {
	prev->next = NULL;
	G_free(gpt);
    }

    Vect_close(&map);

    if (!np) {
	G_warning(_("No points from vector map <%s> fall within current region"),
		  G_fully_qualified_name(name, mapset));
	return (NULL);
    }
    else {
	G_message(_("Vector map <%s> loaded (%d points)"),
		  G_fully_qualified_name(name, mapset), np);
    }

    *nsites = np;

    return top;
}

/*!
  \brief Load styles for geopoints based on thematic mapping

  \todo Currently only supports color settings
  
  \param gp pointer to geosite structure

  \return number of points defined by thematic mapping
  \return -1 on error
*/
int Gp_load_sites_thematic(geosite *gp)
{
    geopoint *gpt;

    struct Map_info Map;
    struct field_info *Fi;
    
    int nvals, cat, npts;
    int red, blu, grn;
    const char *str;
    const char *mapset;

    dbDriver *driver;
    dbValue value;
    
    if(!gp || !gp->tstyle || !gp->filename)
	return -1;

    mapset = G_find_vector2(gp->filename, "");
    if (!mapset) {
	G_fatal_error(_("Vector map <%s> not found"), gp->filename);
    }
    
    Vect_set_open_level(1);
    if (Vect_open_old(&Map, gp->filename, "") == -1) {
	G_fatal_error(_("Unable to open vector map <%s>"),
		      G_fully_qualified_name(gp->filename, mapset));
    }
    
    Fi = Vect_get_field(&Map, gp->tstyle->layer);
    if (!Fi)
	G_fatal_error(_("Database connection not defined for layer %d"),
		      gp->tstyle->layer);
    
    driver = db_start_driver_open_database(Fi->driver, Fi->database);
    if (!driver)
	G_fatal_error(_("Unable to open database <%s> by driver <%s>"),
		      Fi->database, Fi->driver);
    
    gpt = gp->points;
    npts = 0;
    G_message(_("Loading thematic points layer <%s>..."),
	      G_fully_qualified_name(gp->filename, mapset));
    for(;gpt; gpt = gpt->next) {
	gpt->style = (gvstyle *) G_malloc(sizeof(gvstyle));
	G_zero(gpt->style, sizeof(gvstyle));
	/* use default style */
	gpt->style->color  = gp->style->color;
	gpt->style->symbol = gp->style->symbol;
	gpt->style->size   = gp->style->size;
	gpt->style->width  = gp->style->width;
	
	Vect_cat_get(gpt->cats, gp->tstyle->layer, &cat);
	if (cat < 0)
	    continue;

	/* color */
	nvals = db_select_value(driver, Fi->table, Fi->key, cat, gp->tstyle->color_column, &value);
	if (nvals < 1)
	    continue;
	str = db_get_value_string(&value);
	if (G_str_to_color(str, &red, &grn, &blu) != 1) {
	    G_warning(_("Invalid color definition (%s)"),
		      str);
	    gpt->style->color = gp->style->color;
	}
	else {
	    gpt->style->color = (red & RED_MASK) + ((int)((grn) << 8) & GRN_MASK) +
		((int)((blu) << 16) & BLU_MASK);
	}

	npts++;
    }
    
    return npts;
}
