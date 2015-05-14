/***************************************************************************
 *            ncm_mset_catalog.c
 *
 *  Tue February 18 10:49:26 2014
 *  Copyright  2014  Sandro Dias Pinto Vitenti
 *  <sandro@isoftware.com.br>
 ****************************************************************************/
/*
 * ncm_mset_catalog.c
 * Copyright (C) 2014 Sandro Dias Pinto Vitenti <sandro@isoftware.com.br>
 *
 * numcosmo is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * numcosmo is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:ncm_mset_catalog
 * @title: NcmMSetCatalog
 * @short_description: Ordered catalog of different NcmMSet parameter values.
 *
 * This class defines a catalog type object. This object can automatically synchronize
 * with a fits file (thought cfitsio). 
 * 
 * For Mote Carlo studies, like resampling from a fiducial model or bootstrap, it is used
 * to save the best-fitting values of each realization. Since the order of the
 * resampling is important, due to the fact that we use the same pseudo-random number 
 * generator for all resamplings, this object also guarantees the order of the samples
 * added.
 * 
 * For Markov Chain Monte Carlo (MCMC) this object saves the value of the same likelihood in
 * different points of the parameter space.
 * 
 * For both applications this object keeps an interactive mean and variance of the
 * parameters added, this allows a sample by sample analyses of the convergence. 
 * Some MCMC convergence diagnostic functions are also implemented here.
 * 
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */
#include "build_cfg.h"

#include "math/ncm_mset_catalog.h"
#include "math/ncm_cfg.h"
#include "math/ncm_func_eval.h"
#include "ncm_enum_types.h"

#include <gsl/gsl_statistics_double.h>
#include <gsl/gsl_sort.h>

G_DEFINE_TYPE (NcmMSetCatalog, ncm_mset_catalog, G_TYPE_OBJECT);

enum
{
  PROP_0,
  PROP_MSET,
  PROP_NADD_VALS,
  PROP_WEIGHTED,
  PROP_NCHAINS,
  PROP_RNG,
  PROP_FILE,
  PROP_RUN_TYPE_STR,
  PROP_FLUSH_MODE,
  PROP_FLUSH_INTERVAL,
};

static void
ncm_mset_catalog_init (NcmMSetCatalog *mcat)
{
  mcat->mset           = NULL;
  mcat->nadd_vals      = 0;
  mcat->add_vals_names = g_ptr_array_new_with_free_func (g_free);
  mcat->pstats         = NULL;
  mcat->fmode          = NCM_MSET_CATALOG_FLUSH_LEN;
  mcat->rng            = NULL;
  mcat->weighted       = FALSE;
  mcat->first_flush    = FALSE;
  mcat->nchains        = 0;
  mcat->chain_pstats   = g_ptr_array_new ();
  g_ptr_array_set_free_func (mcat->chain_pstats, (GDestroyNotify) &ncm_stats_vec_free);
  mcat->mean_pstats    = NULL;
  mcat->chain_means    = NULL;
  mcat->chain_vars     = NULL;
  mcat->chain_cov      = NULL;
  mcat->chain_sM       = NULL;
  mcat->chain_sM_ws    = NULL;
  mcat->chain_sM_ev    = NULL;
  mcat->tau            = NULL;

  mcat->rng_inis       = NULL;
  mcat->rng_stat       = NULL;
  mcat->flush_timer    = g_timer_new ();
  mcat->cur_id         = -1; /* Represents that no elements in the catalog. */
  mcat->first_id       = 0;  /* The element to be in the catalog will be the one with index == 0 */
  mcat->file_cur_id    = -1; /* Represents that no elements in the catalog file. */
  mcat->file_first_id  = 0;  /* The element to be in the catalog file will be the one with index == 0 */
  mcat->file           = NULL;
  mcat->mset_file      = NULL;
  mcat->rtype_str      = NULL;
  mcat->porder         = g_array_new (FALSE, FALSE, sizeof (gint));
  mcat->quantile_ws    = NULL;
#ifdef NUMCOSMO_HAVE_CFITSIO
  mcat->fptr           = NULL;
#endif /* NUMCOSMO_HAVE_CFITSIO */
  mcat->pdf_i          = -1;
  mcat->h              = NULL;
  mcat->h_pdf          = NULL;
  mcat->params_max     = NULL;
  mcat->params_min     = NULL;
}

#ifdef NUMCOSMO_HAVE_CFITSIO
static void _ncm_mset_catalog_open_create_file (NcmMSetCatalog *mcat, gboolean load_from_cat);
static void _ncm_mset_catalog_flush_file (NcmMSetCatalog *mcat);
#endif /* NUMCOSMO_HAVE_CFITSIO */

static void 
_ncm_mset_catalog_constructed_reset_alloc (NcmMSetCatalog *mcat)
{
  g_ptr_array_set_size (mcat->add_vals_names, 0);
}

static void 
_ncm_mset_catalog_constructed_alloc_chains (NcmMSetCatalog *mcat)
{
  guint free_params_len = ncm_mset_fparams_len (mcat->mset);
  guint total = free_params_len + mcat->nadd_vals + (mcat->weighted ? 1 : 0);
  guint i;

  mcat->pstats     = ncm_stats_vec_new (total, NCM_STATS_VEC_COV, TRUE);
  mcat->params_max = ncm_vector_new (total);
  mcat->params_min = ncm_vector_new (total);

  ncm_vector_set_all (mcat->params_max, GSL_NEGINF);
  ncm_vector_set_all (mcat->params_min, GSL_POSINF);

  if (mcat->nchains > 1)
  {
    for (i = 0; i < mcat->nchains; i++)
    {
      NcmStatsVec *pstats = ncm_stats_vec_new (total, NCM_STATS_VEC_COV, FALSE);
      g_ptr_array_add (mcat->chain_pstats, pstats);
    }
    mcat->mean_pstats   = ncm_stats_vec_new (free_params_len, NCM_STATS_VEC_COV, FALSE);
    mcat->chain_means   = ncm_vector_new (mcat->nchains);
    mcat->chain_vars    = ncm_vector_new (mcat->nchains);
    mcat->chain_cov     = ncm_matrix_new (free_params_len, free_params_len);
    mcat->chain_sM      = ncm_matrix_new (free_params_len, free_params_len);
    mcat->chain_sM_ws   = gsl_eigen_nonsymm_alloc (free_params_len);
    mcat->chain_sM_ev   = gsl_vector_complex_alloc (free_params_len);
  }
  mcat->tau = ncm_vector_new (free_params_len);
  ncm_vector_set_all (mcat->tau, 1.0);
}

static void
_ncm_mset_catalog_constructed (GObject *object)
{
  /* Chain up : start */
  G_OBJECT_CLASS (ncm_mset_catalog_parent_class)->constructed (object);
  {
    NcmMSetCatalog *mcat = NCM_MSET_CATALOG (object);

    if (mcat->mset == NULL)
    {
#ifdef NUMCOSMO_HAVE_CFITSIO
      gint status = 0;
      if (mcat->mset_file == NULL)
      {
        g_error ("_ncm_mset_catalog_constructed: cannot create catalog without mset.");
      }

      if (!g_file_test (mcat->file, G_FILE_TEST_EXISTS))
      {
        g_error ("_ncm_mset_catalog_constructed: cannot create catalog file `%s' not found.",
                 mcat->file);
      }
      if (!g_file_test (mcat->mset_file, G_FILE_TEST_EXISTS))
      {
        g_error ("_ncm_mset_catalog_constructed: cannot create catalog file `%s' not found.",
                 mcat->mset_file);
      }
      
      {
        NcmSerialize *ser = ncm_serialize_global ();
        mcat->mset = ncm_mset_load (mcat->mset_file, ser);
        ncm_serialize_free (ser);
      }
      _ncm_mset_catalog_constructed_reset_alloc (mcat);
      _ncm_mset_catalog_open_create_file (mcat, TRUE);
      _ncm_mset_catalog_constructed_alloc_chains (mcat);

      ncm_mset_catalog_sync (mcat, TRUE);

      _ncm_mset_catalog_flush_file (mcat);

      fits_flush_file (mcat->fptr, &status);
      NCM_FITS_ERROR (status);
#else
      g_error ("_ncm_mset_catalog_constructed: cannot create catalog without mset.");
#endif /* NUMCOSMO_HAVE_CFITSIO */
    }
    else
    {
      guint free_params_len = ncm_mset_fparams_len (mcat->mset);
      guint total = free_params_len + mcat->nadd_vals + (mcat->weighted ? 1 : 0);
      guint i;

      g_array_set_size (mcat->porder, total);
      _ncm_mset_catalog_constructed_reset_alloc (mcat);
      _ncm_mset_catalog_constructed_alloc_chains (mcat);
      
      if (mcat->add_vals_names->len != 0 && (mcat->add_vals_names->len != mcat->nadd_vals))
        g_error ("_ncm_mset_catalog_constructed: number of additional fields do not match %u vs %u.", mcat->add_vals_names->len, mcat->nadd_vals);
      
      for (i = 0; i < mcat->nadd_vals; i++)
      {
        g_ptr_array_add (mcat->add_vals_names, g_strdup_printf ("NcmMSetCatalog:additional-param-%u", i + 1));
      }

      if (mcat->weighted)
      {
        g_ptr_array_add (mcat->add_vals_names, g_strdup ("NcmMSetCatalog:Row-weights"));
        mcat->nadd_vals++;
      }
    }
  }
}

static void
_ncm_mset_catalog_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  NcmMSetCatalog *mcat = NCM_MSET_CATALOG (object);
  g_return_if_fail (NCM_IS_MSET_CATALOG (object));

  switch (prop_id)
  {
    case PROP_MSET:
      mcat->mset = g_value_dup_object (value);
      break;
    case PROP_NADD_VALS:
      mcat->nadd_vals = g_value_get_uint (value);
      break;
    case PROP_WEIGHTED:
      mcat->weighted = g_value_get_boolean (value);
      break;
    case PROP_NCHAINS:
      mcat->nchains = g_value_get_uint (value);
      break;
    case PROP_RNG:
      ncm_mset_catalog_set_rng (mcat, g_value_get_object (value));
      break;
    case PROP_FILE:
      ncm_mset_catalog_set_file (mcat, g_value_get_string (value));
      break;    
    case PROP_RUN_TYPE_STR:
      ncm_mset_catalog_set_run_type (mcat, g_value_get_string (value));
      break;    
    case PROP_FLUSH_MODE:
      ncm_mset_catalog_set_flush_mode (mcat, g_value_get_enum (value));
      break;
    case PROP_FLUSH_INTERVAL:
      ncm_mset_catalog_set_flush_interval (mcat, g_value_get_double (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
_ncm_mset_catalog_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  NcmMSetCatalog *mcat = NCM_MSET_CATALOG (object);
  g_return_if_fail (NCM_IS_MSET_CATALOG (object));

  switch (prop_id)
  {
    case PROP_MSET:
      g_value_set_object (value, mcat->mset);
      break;
    case PROP_NADD_VALS:
      g_value_set_uint (value, mcat->nadd_vals);
      break;
    case PROP_WEIGHTED:
      g_value_set_boolean (value, mcat->weighted);
      break;
    case PROP_NCHAINS:
      g_value_set_uint (value, mcat->nchains);
      break;
    case PROP_RNG:
      g_value_set_object (value, mcat->rng);
      break;
    case PROP_FILE:
      g_value_set_string (value, mcat->file);
      break;
    case PROP_RUN_TYPE_STR:
      g_value_set_string (value, mcat->rtype_str);
      break;
    case PROP_FLUSH_MODE:
      g_value_set_enum (value, mcat->fmode);
      break;
    case PROP_FLUSH_INTERVAL:
      g_value_set_double (value, mcat->flush_interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
static void
_ncm_mset_catalog_dispose (GObject *object)
{
  NcmMSetCatalog *mcat = NCM_MSET_CATALOG (object);

  if (mcat->mset != NULL && mcat->mset_file != NULL)
  {
    NcmSerialize *ser = ncm_serialize_new (NCM_SERIALIZE_OPT_NONE);
    ncm_mset_save (mcat->mset, ser, mcat->mset_file, TRUE);
    ncm_serialize_free (ser);
  }

  ncm_mset_clear (&mcat->mset);
  ncm_rng_clear (&mcat->rng);
  ncm_stats_vec_clear (&mcat->pstats);
  ncm_vector_clear (&mcat->params_max);
  ncm_vector_clear (&mcat->params_min);
  
  g_clear_pointer (&mcat->chain_pstats, g_ptr_array_unref);
  ncm_stats_vec_clear (&mcat->mean_pstats);
  ncm_vector_clear (&mcat->chain_means);
  ncm_vector_clear (&mcat->chain_vars);
  ncm_matrix_clear (&mcat->chain_cov);
  ncm_matrix_clear (&mcat->chain_sM);
  g_clear_pointer (&mcat->chain_sM_ws, gsl_eigen_nonsymm_free);
  g_clear_pointer (&mcat->chain_sM_ev, gsl_vector_complex_free);
  ncm_vector_clear (&mcat->tau);
  ncm_vector_clear (&mcat->quantile_ws);
  
  /* Chain up : end */
  G_OBJECT_CLASS (ncm_mset_catalog_parent_class)->dispose (object);
}

#ifdef NUMCOSMO_HAVE_CFITSIO
static void _ncm_mset_catalog_close_file (NcmMSetCatalog *mcat);
#endif /* NUMCOSMO_HAVE_CFITSIO */

static void
_ncm_mset_catalog_finalize (GObject *object)
{
  NcmMSetCatalog *mcat = NCM_MSET_CATALOG (object);

  if (mcat->h != NULL)
    gsl_histogram_free (mcat->h);
  if (mcat->h_pdf != NULL)
    gsl_histogram_pdf_free (mcat->h_pdf);

#ifdef NUMCOSMO_HAVE_CFITSIO
  _ncm_mset_catalog_close_file (mcat);
#endif /* NUMCOSMO_HAVE_CFITSIO */

  g_clear_pointer (&mcat->rtype_str, g_free);
  
  g_array_unref (mcat->porder);
  g_timer_destroy (mcat->flush_timer);

  g_ptr_array_unref (mcat->add_vals_names);

  g_clear_pointer (&mcat->rng_inis, g_free);
  g_clear_pointer (&mcat->rng_stat, g_free);

  g_clear_pointer (&mcat->file, g_free);
  g_clear_pointer (&mcat->mset_file, g_free);

  /* Chain up : end */
  G_OBJECT_CLASS (ncm_mset_catalog_parent_class)->finalize (object);
}

static void
ncm_mset_catalog_class_init (NcmMSetCatalogClass *klass)
{
  GObjectClass* object_class = G_OBJECT_CLASS (klass);

  object_class->constructed  = &_ncm_mset_catalog_constructed;
  object_class->set_property = &_ncm_mset_catalog_set_property;
  object_class->get_property = &_ncm_mset_catalog_get_property;
  object_class->dispose      = &_ncm_mset_catalog_dispose;
  object_class->finalize     = &_ncm_mset_catalog_finalize;

  g_object_class_install_property (object_class,
                                   PROP_MSET,
                                   g_param_spec_object ("mset",
                                                        NULL,
                                                        "NcmMSet object",
                                                        NCM_TYPE_MSET,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_NADD_VALS,
                                   g_param_spec_uint ("nadd-vals",
                                                      NULL,
                                                      "Number of additional values",
                                                      1, G_MAXUINT32, 1,
                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB));
  
  g_object_class_install_property (object_class,
                                   PROP_WEIGHTED,
                                   g_param_spec_boolean ("weighted",
                                                         NULL,
                                                         "Catalog with weighted rows",
                                                         FALSE,
                                                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_NCHAINS,
                                   g_param_spec_uint ("nchains",
                                                      NULL,
                                                      "Number of different chains in the catalog",
                                                      1, G_MAXUINT32, 1,
                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB));
  
  g_object_class_install_property (object_class,
                                   PROP_RNG,
                                   g_param_spec_object ("rng",
                                                        NULL,
                                                        "Random number generator object",
                                                        NCM_TYPE_RNG,
                                                        G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_FLUSH_MODE,
                                   g_param_spec_enum ("fmode",
                                                      NULL,
                                                      "Catalog flush mode",
                                                      NCM_TYPE_MSET_CATALOG_FLUSH, NCM_MSET_CATALOG_FLUSH_AUTO,
                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB));
  g_object_class_install_property (object_class,
                                   PROP_FILE,
                                   g_param_spec_string ("filename",
                                                        NULL,
                                                        "Catalog filename",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB));
  g_object_class_install_property (object_class,
                                   PROP_RUN_TYPE_STR,
                                   g_param_spec_string ("run-type-string",
                                                        NULL,
                                                        "Run type string",
                                                        NCM_MSET_CATALOG_RTYPE_UNDEFINED,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB));
  g_object_class_install_property (object_class,
                                   PROP_FLUSH_INTERVAL,
                                   g_param_spec_double ("flush-interval",
                                                        NULL,
                                                        "Data flush interval",
                                                        0.0, 1.0e3, 10.0,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB));

}

/**
 * ncm_mset_catalog_new:
 * @mset: a #NcmMSet
 * @nadd_vals: number of additional values
 * @nchains: number of different chains in the catalog (>=1)
 * @weighted: set to TRUE whenever the catalog is weighted
 * @...: additional values names
 *
 * Creates a new #NcmMSetCatalog based on the #NcmFit object @fit. The catalog assumes that
 * the @fit object will remain with the same set of free parameters during its whole lifetime.
 * 
 * If @nchains is larger than one, the catalog will keep track of the statistics of each chain
 * separately.
 *
 * Returns: (transfer full): a new #NcmMSetCatalog
 */
NcmMSetCatalog *
ncm_mset_catalog_new (NcmMSet *mset, guint nadd_vals, guint nchains, gboolean weighted, ...)
{
  va_list ap;  
  NcmMSetCatalog *mcat = g_object_new (NCM_TYPE_MSET_CATALOG, 
                                       "mset", mset,
                                       "nadd-vals", nadd_vals,
                                       "nchains", nchains,
                                       "weighted", weighted,
                                       NULL);
  guint i;

  va_start (ap, weighted);
  
  for (i = 0; i < nadd_vals; i++)
  {
    gchar *name = va_arg (ap, gchar *);
    g_assert (name != NULL);
    ncm_mset_catalog_set_add_val_name (mcat, i, name);
  }
  
  va_end (ap);

  return mcat;
}

/**
 * ncm_mset_catalog_new_from_file:
 * @filename: filename of the catalog fits
 *
 * Creates a new #NcmMSetCatalog from the catalog in the file @file.
 * It will use also the mset file (same name but with .mset extension).
 * 
 *
 * Returns: (transfer full): a new #NcmMSetCatalog
 */
NcmMSetCatalog *
ncm_mset_catalog_new_from_file (const gchar *filename)
{
  NcmMSetCatalog *mcat = g_object_new (NCM_TYPE_MSET_CATALOG, 
                                       "filename", filename,
                                       NULL);
  return mcat;
}


/**
 * ncm_mset_catalog_ref:
 * @mcat: a #NcmMSetCatalog
 *
 * Increases the reference count of @mcat atomically.
 *
 * Returns: (transfer full): @mcat.
 */
NcmMSetCatalog *
ncm_mset_catalog_ref (NcmMSetCatalog *mcat)
{
  return g_object_ref (mcat);
}

/**
 * ncm_mset_catalog_free:
 * @mcat: a #NcmMSetCatalog
 *
 * Decreases the reference count of @mcat atomically.
 *
 */
void 
ncm_mset_catalog_free (NcmMSetCatalog *mcat)
{
  ncm_mset_catalog_sync (mcat, TRUE);  
  g_object_unref (mcat);
}

/**
 * ncm_mset_catalog_clear:
 * @mcat: a #NcmMSetCatalog
 *
 * Decrese the reference count of *@mcat atomically and sets the pointer *@mcat to null.
 *
 */
void 
ncm_mset_catalog_clear (NcmMSetCatalog **mcat)
{
  if (*mcat != NULL)
    ncm_mset_catalog_sync (*mcat, TRUE);  
  g_clear_object (mcat);
}

#ifdef NUMCOSMO_HAVE_CFITSIO
static void
_ncm_mset_catalog_open_create_file (NcmMSetCatalog *mcat, gboolean load_from_cat)
{
  guint fparam_len = ncm_mset_fparam_len (mcat->mset);
  gchar key_text[FLEN_VALUE];
  gint status = 0;
  guint i;
  
  g_assert (mcat->file != NULL);
  g_assert (mcat->fptr == NULL);

  if (g_file_test (mcat->file, G_FILE_TEST_EXISTS))
  {
    gboolean weighted = FALSE;
    gint nchains   = 0;
    gint nadd_vals = 0;
    glong nrows;

    fits_open_file (&mcat->fptr, mcat->file, READWRITE, &status);
    NCM_FITS_ERROR (status);

    fits_movnam_hdu (mcat->fptr, BINARY_TBL, NCM_MSET_CATALOG_EXTNAME, 0, &status);
    NCM_FITS_ERROR (status);

    fits_read_key (mcat->fptr, TINT, NCM_MSET_CATALOG_FIRST_ID_LABEL,
                   &mcat->file_first_id, NULL, &status);
    NCM_FITS_ERROR (status);

    fits_read_key (mcat->fptr, TSTRING, NCM_MSET_CATALOG_RTYPE_LABEL,
                   key_text, NULL, &status);
    NCM_FITS_ERROR (status);

    if (load_from_cat)
    {
      ncm_mset_catalog_set_run_type (mcat, key_text);
    } 
    else if (strcmp (mcat->rtype_str, key_text) != 0)
      g_error ("_ncm_mset_catalog_open_create_file: incompatible run type strings from catalog and file, catalog: `%s' file: `%s'.",
               mcat->rtype_str, key_text);

    fits_read_key (mcat->fptr, TINT, NCM_MSET_CATALOG_NCHAINS_LABEL,
                   &nchains, NULL, &status);
    NCM_FITS_ERROR (status);
    g_assert_cmpint (nchains, >, 0);
    
    if (load_from_cat)
    {
      mcat->nchains = nchains;
    }
    else if (nchains != mcat->nchains)
      g_error ("_ncm_mset_catalog_open_create_file: catalog has %d chains and file contains %d.", mcat->nchains, nchains);

    fits_read_key (mcat->fptr, TINT, NCM_MSET_CATALOG_NADDVAL_LABEL,
                   &nadd_vals, NULL, &status);
    NCM_FITS_ERROR (status);
    
    if (load_from_cat)
    {
      mcat->nadd_vals = nadd_vals;
    }
    else if (nadd_vals != mcat->nadd_vals)
      g_error ("_ncm_mset_catalog_open_create_file: catalog has %d additional values and file contains %d.", mcat->nadd_vals, nadd_vals);

    fits_read_key (mcat->fptr, TLOGICAL, NCM_MSET_CATALOG_WEIGHTED_LABEL,
                   &weighted, NULL, &status);
    NCM_FITS_ERROR (status);

    if (load_from_cat)
    {
      mcat->weighted = weighted ? TRUE : FALSE;
    }
    else if ((weighted && !mcat->weighted) || (!weighted && mcat->weighted))
      g_error ("_ncm_mset_catalog_open_create_file: catalog %s weighted and file %s.", 
               mcat->weighted ? "is" : "is not",
               weighted ? "is" : "is not");
    
    fits_get_num_rows (mcat->fptr, &nrows, &status);
    NCM_FITS_ERROR (status);

    if (mcat->file_first_id != mcat->first_id)
    {
      if (nrows == 0)
      {
        if (mcat->file_first_id != 0)
          g_warning ("_ncm_mset_catalog_open_create_file: Empty data file with "NCM_MSET_CATALOG_FIRST_ID_LABEL" different from first_id: %d != %d. Setting to first_id.\n", 
                     mcat->file_first_id, mcat->first_id);
        mcat->file_first_id = mcat->first_id;
      } 
      else if (mcat->cur_id < mcat->first_id)
      {
        if (mcat->first_id != 0)
          g_warning ("_ncm_mset_catalog_open_create_file: Empty memory catalog with first_id different from "NCM_MSET_CATALOG_FIRST_ID_LABEL": %d != %d. Setting to "NCM_MSET_CATALOG_FIRST_ID_LABEL".\n", 
                     mcat->first_id, mcat->file_first_id);
        mcat->first_id = mcat->file_first_id;
        mcat->cur_id = mcat->file_first_id - 1;
      }
    }
    mcat->file_cur_id = mcat->file_first_id + nrows - 1; 

    if (load_from_cat)
    {
      gchar colname[FLEN_VALUE];
      gint cindex = 0;
      guint total = fparam_len + mcat->nadd_vals + (mcat->weighted ? 1 : 0);

      g_array_set_size (mcat->porder, total);

      i = 0;
      while (fits_get_colname (mcat->fptr, CASESEN, "*", colname, &cindex, &status) == COL_NOT_UNIQUE)
      {
        gchar *d_colname = g_strdup (colname);
        g_assert_cmpint (i + 1, ==, cindex);

        g_ptr_array_add (mcat->add_vals_names, d_colname);
        g_array_index (mcat->porder, gint, i) = cindex;

        i++;
        if (i >= mcat->nadd_vals)
          break;
      }
      status = 0;
    }
    else
    {
      for (i = 0; i < mcat->nadd_vals; i++)
      {
        const gchar *cname = g_ptr_array_index (mcat->add_vals_names, i);
        gint cindex = 0;

        if (fits_get_colnum (mcat->fptr, CASESEN, (gchar *)cname, &cindex, &status))
          g_error ("_ncm_mset_catalog_open_create_file: Additional column %s not found, invalid fits file.", cname);

        if (cindex != i + 1)
        {
          g_error ("_ncm_mset_catalog_open_create_file: Additional column %s is not the %d-th column [%d], invalid fits file.", 
                   cname, i + 1, cindex);
        }
        g_array_index (mcat->porder, gint, i) = cindex;  
      }
    }
    
    for (i = 0; i < fparam_len; i++)
    {
      const gchar *fparam_fullname = ncm_mset_fparam_full_name (mcat->mset, i);
      if (fits_get_colnum (mcat->fptr, CASESEN, (gchar *)fparam_fullname, &g_array_index (mcat->porder, gint, i + mcat->nadd_vals), &status))
      {                /* I don't like this too ^^^^^^^^^  */
        g_error ("_ncm_mset_catalog_open_create_file: Column %s not found, invalid fits file.", fparam_fullname);
      }
    }
  }
  else
  {
    GPtrArray *ttype_array = g_ptr_array_sized_new (10);
    GPtrArray *tform_array = g_ptr_array_sized_new (10);
    fits_create_file (&mcat->fptr, mcat->file, &status);
    NCM_FITS_ERROR (status);

    for (i = 0; i < mcat->nadd_vals; i++)
    {
      const gchar *cname = g_ptr_array_index (mcat->add_vals_names, i);
      g_ptr_array_add (ttype_array, (gchar *)cname);
      g_ptr_array_add (tform_array, "1D");
      g_array_index (mcat->porder, gint, i) = tform_array->len;
    }

    for (i = 0; i < fparam_len; i++)
    {
      const gchar *fparam_fullname = ncm_mset_fparam_full_name (mcat->mset, i);
      g_ptr_array_add (ttype_array, (gchar *)fparam_fullname);
      /* I don't like this too ^^^^^^^^^  */
      g_ptr_array_add (tform_array, "1D");
      g_array_index (mcat->porder, gint, i + mcat->nadd_vals) = tform_array->len;
    }

    /* append a new empty binary table onto the FITS file */
    fits_create_tbl (mcat->fptr, BINARY_TBL, 0, fparam_len + mcat->nadd_vals, (gchar **)ttype_array->pdata, (gchar **)tform_array->pdata,
                     NULL, NCM_MSET_CATALOG_EXTNAME, &status);
    NCM_FITS_ERROR (status);

    fits_update_key (mcat->fptr, TSTRING, NCM_MSET_CATALOG_RTYPE_LABEL, mcat->rtype_str, "Run type string.", &status);
    NCM_FITS_ERROR (status);  

    fits_update_key (mcat->fptr, TINT, NCM_MSET_CATALOG_NCHAINS_LABEL, &mcat->nchains, "Number of chains.", &status);
    NCM_FITS_ERROR (status);
    
    fits_update_key (mcat->fptr, TINT, NCM_MSET_CATALOG_NADDVAL_LABEL, &mcat->nadd_vals, "Number of additional values.", &status);
    NCM_FITS_ERROR (status);
    
    fits_update_key (mcat->fptr, TLOGICAL, NCM_MSET_CATALOG_WEIGHTED_LABEL, &mcat->weighted, "Whether the catalog is weighted.", &status);
    NCM_FITS_ERROR (status);
    
    for (i = 0; i < fparam_len; i++)
    {
      gchar *fsymbi = g_strdup_printf ("%s%d", NCM_MSET_CATALOG_FSYMB_LABEL, i + 1);
      gchar *fsymb_desc = g_strdup_printf ("Symbol for parameter %s[%d]", 
                                           ncm_mset_fparam_name (mcat->mset, i), 
                                           i + 1);
      const gchar *fsymb  = ncm_mset_fparam_symbol (mcat->mset, i);
      
      fits_update_key (mcat->fptr, TSTRING, fsymbi, (gchar *)fsymb, fsymb_desc, &status);
      NCM_FITS_ERROR (status);  
      g_free (fsymbi);
      g_free (fsymb_desc);
    }
    
    mcat->file_first_id = 0;
    mcat->file_cur_id   = -1;
  }

  fits_read_key (mcat->fptr, TSTRING, NCM_MSET_CATALOG_RNG_ALGO_LABEL, 
                 key_text, NULL, &status);
  if (status == 0)
  {
    gulong seed = 0;
    gchar *inis = NULL;
    fits_read_key (mcat->fptr, TULONG, NCM_MSET_CATALOG_RNG_SEED_LABEL, 
                   &seed, NULL, &status);
    NCM_FITS_ERROR (status);

    fits_read_key_longstr (mcat->fptr, NCM_MSET_CATALOG_RNG_INIS_LABEL, &inis, NULL, &status);
    NCM_FITS_ERROR (status);
        
    if (mcat->rng != NULL)
    {
      const gchar *cat_algo = ncm_rng_get_algo (mcat->rng);
      g_assert_cmpstr (cat_algo, ==, key_text);
      g_assert_cmpstr (inis, ==, mcat->rng_inis);
    }
    else
    {
      NcmRNG *rng = ncm_rng_new (key_text);
      ncm_rng_set_state (rng, inis);
      ncm_mset_catalog_set_rng (mcat, rng);
      ncm_rng_free (rng);
    }

    fits_free_memory (inis, &status);
    NCM_FITS_ERROR (status);
  }
  else if (status == KEY_NO_EXIST)
  {
    status = 0;
    if (mcat->rng != NULL)
    {
      gulong seed = ncm_rng_get_seed (mcat->rng);

      fits_update_key (mcat->fptr, TSTRING, NCM_MSET_CATALOG_RNG_ALGO_LABEL, (gchar *)ncm_rng_get_algo (mcat->rng), "RNG Algorithm name.", &status);
      NCM_FITS_ERROR (status);  

      fits_update_key (mcat->fptr, TULONG, NCM_MSET_CATALOG_RNG_SEED_LABEL, &seed, "RNG Algorithm seed.", &status);
      NCM_FITS_ERROR (status);  

      fits_update_key_longstr (mcat->fptr, NCM_MSET_CATALOG_RNG_INIS_LABEL, mcat->rng_inis, NULL, &status);
      NCM_FITS_ERROR (status);
    }
  }
  else
    NCM_FITS_ERROR (status);

  fits_update_key (mcat->fptr, TINT, NCM_MSET_CATALOG_FIRST_ID_LABEL, &mcat->file_first_id, "Id of the first element.", &status);
  NCM_FITS_ERROR (status);

  fits_flush_file (mcat->fptr, &status);
  NCM_FITS_ERROR (status);

  {
    NcmSerialize *ser = ncm_serialize_new (NCM_SERIALIZE_OPT_NONE);
    ncm_mset_save (mcat->mset, ser, mcat->mset_file, TRUE);
    ncm_serialize_free (ser);
  }
}

#endif /* NUMCOSMO_HAVE_CFITSIO */

/**
 * ncm_mset_catalog_set_add_val_name:
 * @mcat: a #NcmMSetCatalog
 * @i: index of the additional value
 * @name: additional value name
 *
 * Sets the @i-th additional value name
 *
 */
void 
ncm_mset_catalog_set_add_val_name (NcmMSetCatalog *mcat, guint i, const gchar *name)
{
  g_assert_cmpuint (i, <, mcat->nadd_vals);
  g_free (g_ptr_array_index (mcat->add_vals_names, i));
  g_ptr_array_index (mcat->add_vals_names, i) = g_strdup (name);
}

/**
 * ncm_mset_catalog_set_file:
 * @mcat: a #NcmMSetCatalog
 * @filename: a filename
 *
 * Sets the data filename to be used to sync/save data.
 *
 */
void 
ncm_mset_catalog_set_file (NcmMSetCatalog *mcat, const gchar *filename)
{
#ifdef NUMCOSMO_HAVE_CFITSIO
  gint status = 0;
  if (mcat->file != NULL && strcmp (mcat->file, filename) == 0)
    return;
  
  _ncm_mset_catalog_close_file (mcat);

  g_clear_pointer (&mcat->file, g_free);
  g_clear_pointer (&mcat->mset_file, g_free);

  if (filename == NULL)
    return;

  mcat->file = g_strdup (filename);
  {
    GError *error = NULL;
    GMatchInfo *match_info = NULL;
    GRegex *fits_ext = g_regex_new ("(.*)\\.[fF][iI][tT][sS]?$", 0, 0, &error);
    gchar *base_name = NULL;

    if (g_regex_match (fits_ext, mcat->file, 0, &match_info))
    {
      base_name = g_match_info_fetch (match_info, 1);
      g_match_info_free (match_info);
    }
    else
      base_name = g_strdup (filename);
    g_regex_unref (fits_ext);

    mcat->mset_file = g_strdup_printf ("%s.mset", base_name);
    g_free (base_name);
  }

  if (mcat->mset != NULL)
  {
    _ncm_mset_catalog_open_create_file (mcat, FALSE);
    ncm_mset_catalog_sync (mcat, TRUE);

    _ncm_mset_catalog_flush_file (mcat);

    fits_flush_file (mcat->fptr, &status);
    NCM_FITS_ERROR (status);
  }
#else
  g_error ("ncm_mset_catalog_set_file: cannot set file without cfitsio.");
#endif /* NUMCOSMO_HAVE_CFITSIO */

  mcat->first_flush = TRUE;
}

/**
 * ncm_mset_catalog_set_flush_mode:
 * @mcat: a #NcmMSetCatalog
 * @fmode: flush mode
 *
 * Sets the flush mode to @fmode.
 *
 */
void 
ncm_mset_catalog_set_flush_mode (NcmMSetCatalog *mcat, NcmMSetCatalogFlush fmode)
{
  mcat->fmode = fmode;
}

/**
 * ncm_mset_catalog_set_flush_interval:
 * @mcat: a #NcmMSetCatalog
 * @interval: Minimum time interval between flushs
 *
 * Sets the minimum time interval between flushs.
 *
 */
void 
ncm_mset_catalog_set_flush_interval (NcmMSetCatalog *mcat, gdouble interval)
{
  mcat->flush_interval = interval;
}

/**
 * ncm_mset_catalog_set_first_id:
 * @mcat: a #NcmMSetCatalog
 * @first_id: the id of the first item in the sample
 * 
 * Sets the first id of the catalog, mainly used to inform in which realization the catalog starts. 
 * 
 */
void 
ncm_mset_catalog_set_first_id (NcmMSetCatalog *mcat, gint first_id)
{
  if (first_id == mcat->first_id)
    return;

  g_assert_cmpint (mcat->file_first_id, ==, mcat->first_id);
  g_assert_cmpint (mcat->file_cur_id, ==, mcat->cur_id);

  if (mcat->cur_id != mcat->first_id - 1)
    g_error ("ncm_mset_catalog_set_first_id: cannot modify first_id to %d in a non-empty catalog, catalog first id: %d, catalog current id: %d.", 
             first_id, mcat->first_id, mcat->cur_id);

  mcat->first_id = first_id;
  mcat->cur_id   = first_id - 1;

  mcat->file_first_id = first_id;
  mcat->file_cur_id   = first_id - 1;
#ifdef NUMCOSMO_HAVE_CFITSIO
  if (mcat->fptr != NULL)
  {
    gint status = 0;
    fits_update_key (mcat->fptr, TINT, NCM_MSET_CATALOG_FIRST_ID_LABEL, &mcat->file_first_id, "Id of the first element.", &status);
    NCM_FITS_ERROR (status);
    ncm_mset_catalog_sync (mcat, TRUE);
  }
#endif /* NUMCOSMO_HAVE_CFITSIO */
}

/**
 * ncm_mset_catalog_set_run_type:
 * @mcat: a #NcmMSetCatalog
 * @rtype_str: the run type string
 * 
 * Sets the run type string. 
 * 
 */
void 
ncm_mset_catalog_set_run_type (NcmMSetCatalog *mcat, const gchar *rtype_str)
{
  g_assert (rtype_str != NULL);
  
  if (mcat->rtype_str != NULL)
  {
    if (strcmp (mcat->rtype_str, rtype_str) != 0)
    {
      if (mcat->cur_id + 1 != mcat->first_id)
        g_error ("ncm_mset_catalog_set_run_type: cannot change run type string in a non-empty catalog, actual: `%s' new: `%s'.",
                 mcat->rtype_str, rtype_str);
      else
      {
        g_clear_pointer (&mcat->rtype_str, g_free);
      }
    }
    else
      return;
  }

  mcat->rtype_str = g_strdup (rtype_str);
#ifdef NUMCOSMO_HAVE_CFITSIO
  if (mcat->fptr != NULL)
  {
    gint status = 0;
    fits_update_key (mcat->fptr, TSTRING, NCM_MSET_CATALOG_RTYPE_LABEL, mcat->rtype_str, NULL, &status);
    NCM_FITS_ERROR (status);
  }
#endif /* NUMCOSMO_HAVE_CFITSIO */
}

/**
 * ncm_mset_catalog_set_rng:
 * @mcat: a #NcmMSetCatalog
 * @rng: a #NcmRNG
 * 
 * Sets the random number generator. 
 * 
 */
void 
ncm_mset_catalog_set_rng (NcmMSetCatalog *mcat, NcmRNG *rng)
{
  g_assert (rng != NULL);
  
  if (mcat->rng != NULL)
  {
    g_error ("ncm_mset_catalog_set_rng: random number generator alread set.");
  }

  if (mcat->cur_id + 1 != mcat->first_id)
    g_warning ("ncm_mset_catalog_set_rng: setting RNG in a non-empty catalog, catalog first id: %d, catalog current id: %d.",
             mcat->first_id, mcat->cur_id);

  mcat->rng = ncm_rng_ref (rng);

  mcat->rng_inis = ncm_rng_get_state (rng);
  mcat->rng_stat = g_strdup (mcat->rng_inis);
#ifdef NUMCOSMO_HAVE_CFITSIO
  if (mcat->file != NULL)
  {
    gint status = 0;
    gulong seed = ncm_rng_get_seed (rng);

    fits_update_key (mcat->fptr, TSTRING, NCM_MSET_CATALOG_RNG_ALGO_LABEL, (gchar *)ncm_rng_get_algo (rng), "RNG Algorithm name.", &status);
    NCM_FITS_ERROR (status);  

    fits_update_key (mcat->fptr, TULONG, NCM_MSET_CATALOG_RNG_SEED_LABEL, &seed, "RNG Algorithm seed.", &status);
    NCM_FITS_ERROR (status);  

    fits_update_key (mcat->fptr, TINT, NCM_MSET_CATALOG_FIRST_ID_LABEL, &mcat->file_first_id, "Id of the first element.", &status);
    NCM_FITS_ERROR (status);

    fits_update_key_longstr (mcat->fptr, NCM_MSET_CATALOG_RNG_INIS_LABEL, mcat->rng_inis, NULL, &status);
    NCM_FITS_ERROR (status);

    fits_flush_file (mcat->fptr, &status);
    NCM_FITS_ERROR (status);
  }
#endif /* NUMCOSMO_HAVE_CFITSIO */
}
#ifdef NUMCOSMO_HAVE_CFITSIO
static void
_ncm_mset_catalog_flush_file (NcmMSetCatalog *mcat)
{
  gint status = 0;
  gint64 nrows = mcat->file_cur_id - mcat->file_first_id + 1;

  fits_update_key (mcat->fptr, TLONGLONG, NCM_MSET_CATALOG_NROWS_LABEL, &nrows, NULL, &status);
  NCM_FITS_ERROR (status);

  if (mcat->rng != NULL)
  {
    g_clear_pointer (&mcat->rng_stat, g_free);
    mcat->rng_stat = ncm_rng_get_state (mcat->rng);

    fits_update_key_longstr (mcat->fptr, NCM_MSET_CATALOG_RNG_STAT_LABEL, mcat->rng_stat, NULL, &status);
    NCM_FITS_ERROR (status);
  }

  if (G_UNLIKELY (mcat->first_flush))
  {
    fits_flush_file (mcat->fptr, &status);
    NCM_FITS_ERROR (status);
    mcat->first_flush = FALSE;
  }
  else
  {
    fits_flush_buffer (mcat->fptr, 0, &status);
    NCM_FITS_ERROR (status);
  }
}

static void
_ncm_mset_catalog_close_file (NcmMSetCatalog *mcat)
{
  gint status = 0;
  if (mcat->fptr != NULL)
  {
    _ncm_mset_catalog_flush_file (mcat);
    fits_close_file (mcat->fptr, &status);
    NCM_FITS_ERROR (status);
    mcat->fptr = NULL;
  }
}

static void 
_ncm_mset_catalog_write_row (NcmMSetCatalog *mcat, NcmVector *row, guint row_index)
{
  guint i;
  gint status = 0;
  
  for (i = 0; i < ncm_vector_len (row); i++)
  {
    fits_write_col (mcat->fptr, TDOUBLE, g_array_index (mcat->porder, gint, i), row_index, 
                    1, 1, ncm_vector_ptr (row, i), &status);
    NCM_FITS_ERROR (status);
  }
}

static void 
_ncm_mset_catalog_read_row (NcmMSetCatalog *mcat, NcmVector *row, guint row_index)
{
  guint i;
  gint status = 0;
    
  for (i = 0; i < ncm_vector_len (row); i++)
  {
    fits_read_col (mcat->fptr, TDOUBLE, g_array_index (mcat->porder, gint, i), row_index, 
                   1, 1, NULL, ncm_vector_ptr (row, i), NULL, &status);
    NCM_FITS_ERROR (status);
  }
}
#endif /* NUMCOSMO_HAVE_CFITSIO */

/**
 * ncm_mset_catalog_sync:
 * @mcat: a #NcmMSetCatalog
 * @check: whether to check consistence between file and memory data
 * 
 * Synchronize memory and data file. If no file was defined, it simply returns.
 * 
 */
void
ncm_mset_catalog_sync (NcmMSetCatalog *mcat, gboolean check)
{
#ifdef NUMCOSMO_HAVE_CFITSIO
  gint status = 0;
  guint i;

  if (mcat->file == NULL)
    return;

  g_assert (mcat->fptr != NULL);
  
  if (check)
  {
    gchar fptr_filename[FLEN_FILENAME];
  
    fits_file_name (mcat->fptr, fptr_filename, &status);
    NCM_FITS_ERROR (status);

    g_assert_cmpstr (fptr_filename, ==, mcat->file);

    if ((mcat->file_cur_id < mcat->first_id - 1) || (mcat->cur_id < mcat->file_first_id - 1))
      g_error ("ncm_mset_catalog_sync: file data & catalog mismatch, they do not intersect each other: file data [%d, %d] catalog [%d, %d]",
               mcat->file_first_id, mcat->file_cur_id,
               mcat->first_id, mcat->cur_id);
  }

  if (mcat->file_first_id != mcat->first_id)
  {
    if (mcat->file_first_id > mcat->first_id)
    {
      guint rows_to_add = mcat->file_first_id - mcat->first_id;
      fits_insert_rows (mcat->fptr, 0, rows_to_add, &status);
      NCM_FITS_ERROR (status);

      for (i = 0; i < rows_to_add; i++)
      {
        NcmVector *row = ncm_stats_vec_peek_row (mcat->pstats, i);
        _ncm_mset_catalog_write_row (mcat, row, i + 1);
      }
      mcat->file_first_id = mcat->first_id;

      if (mcat->rng != NULL)
      {
        fits_update_key_longstr (mcat->fptr, NCM_MSET_CATALOG_RNG_INIS_LABEL, mcat->rng_inis, NULL, &status);
        NCM_FITS_ERROR (status);
      }
      
      fits_update_key (mcat->fptr, TINT, NCM_MSET_CATALOG_FIRST_ID_LABEL, &mcat->file_first_id, "Id of the first element.", &status);
      NCM_FITS_ERROR (status);
    }
    else if (mcat->file_first_id < mcat->first_id)
    {
      guint rows_to_add = mcat->first_id - mcat->file_first_id;
      GPtrArray *rows = g_ptr_array_new ();
      gchar *inis = NULL;
      
      g_ptr_array_set_size (rows, rows_to_add);
      for (i = 0; i < rows_to_add; i++)
      {
        NcmVector *row = ncm_vector_dup (ncm_stats_vec_peek_x (mcat->pstats));
        _ncm_mset_catalog_read_row (mcat, row, i + 1);
        g_ptr_array_index (rows, i) = row;
      }
      ncm_stats_vec_prepend_data (mcat->pstats, rows, FALSE);
      if (mcat->nchains > 1)
      {
        for (i = 0; i < rows->len; i++)
        {
          NcmVector *x = g_ptr_array_index (rows, i);
          guint chain_id = (mcat->file_first_id + i) % mcat->nchains;
          NcmStatsVec *pstats = g_ptr_array_index (mcat->chain_pstats, chain_id);
          ncm_stats_vec_prepend (pstats, x, FALSE);
        }
      }
      
      g_ptr_array_unref (rows);
      mcat->first_id = mcat->file_first_id;

      if (mcat->rng != NULL)
      {
        fits_read_key_longstr (mcat->fptr, NCM_MSET_CATALOG_RNG_INIS_LABEL, &inis, NULL, &status);
        NCM_FITS_ERROR (status);

        g_clear_pointer (&mcat->rng_inis, g_free);
        mcat->rng_inis = g_strdup (inis);

        fits_free_memory (inis, &status);
        NCM_FITS_ERROR (status);
      }
    }
    g_assert_cmpint (mcat->file_first_id, ==, mcat->first_id);
  }

  if (mcat->file_cur_id != mcat->cur_id)
  {
    if (mcat->file_cur_id < mcat->cur_id)
    {
      guint rows_to_add = mcat->cur_id - mcat->file_cur_id;
      guint offset = mcat->file_cur_id + 1 - mcat->file_first_id;

      for (i = 0; i < rows_to_add; i++)
      {
        NcmVector *row = ncm_stats_vec_peek_row (mcat->pstats, offset + i);
        _ncm_mset_catalog_write_row (mcat, row, offset + i + 1);
      }
      mcat->file_cur_id = mcat->cur_id;

      if (mcat->rng != NULL)
      {
        g_clear_pointer (&mcat->rng_stat, g_free);
        mcat->rng_stat = ncm_rng_get_state (mcat->rng);

        fits_update_key_longstr (mcat->fptr, NCM_MSET_CATALOG_RNG_STAT_LABEL, mcat->rng_stat, NULL, &status);
        NCM_FITS_ERROR (status);
      }
    }
    else if (mcat->file_cur_id > mcat->cur_id)
    {
      guint rows_to_add = mcat->file_cur_id - mcat->cur_id;
      guint offset = mcat->cur_id + 1 - mcat->first_id;
      GPtrArray *rows = g_ptr_array_new ();
      gchar *stat = NULL;
      
      g_ptr_array_set_size (rows, rows_to_add);

      for (i = 0; i < rows_to_add; i++)
      {
        NcmVector *row = ncm_vector_dup (ncm_stats_vec_peek_x (mcat->pstats));
        _ncm_mset_catalog_read_row (mcat, row, offset + i + 1);
        g_ptr_array_index (rows, i) = row;
      }
      ncm_stats_vec_append_data (mcat->pstats, rows, FALSE);
      if (mcat->nchains > 1)
      {
        for (i = 0; i < rows->len; i++)
        {
          NcmVector *x = g_ptr_array_index (rows, i);
          guint chain_id = (mcat->cur_id + 1 + i) % mcat->nchains;
          NcmStatsVec *pstats = g_ptr_array_index (mcat->chain_pstats, chain_id);
          ncm_stats_vec_append (pstats, x, FALSE);
        }
      }

      mcat->cur_id = mcat->file_cur_id;
      
      g_ptr_array_unref (rows);
      
      if (mcat->rng != NULL)
      {
        fits_read_key_longstr (mcat->fptr, NCM_MSET_CATALOG_RNG_STAT_LABEL, &stat, NULL, &status);
        NCM_FITS_ERROR (status);

        g_clear_pointer (&mcat->rng_stat, g_free);
        mcat->rng_stat = g_strdup (stat);

        fits_free_memory (stat, &status);
        NCM_FITS_ERROR (status);

        ncm_rng_set_state (mcat->rng, mcat->rng_stat);
      }
    } 
  }

  switch (mcat->fmode)
  {
    case NCM_MSET_CATALOG_FLUSH_DISABLE:
      break;
    case NCM_MSET_CATALOG_FLUSH_AUTO:
      _ncm_mset_catalog_flush_file (mcat);
      break;
    case NCM_MSET_CATALOG_FLUSH_TIMED:
    {
      if (g_timer_elapsed (mcat->flush_timer, NULL) > mcat->flush_interval)
      {
        g_timer_start (mcat->flush_timer);
        _ncm_mset_catalog_flush_file (mcat);        
      }
      break;
    }
    default:
      g_assert_not_reached ();
      break;
  }
#endif /* NUMCOSMO_HAVE_CFITSIO */
}

/**
 * ncm_mset_catalog_reset_stats:
 * @mcat: a #NcmMSetCatalog
 * 
 * Reset catalog statistical quantities.
 * 
 */
void
ncm_mset_catalog_reset_stats (NcmMSetCatalog *mcat)
{  
  ncm_stats_vec_reset (mcat->pstats, FALSE);
  if (mcat->nchains > 1)
  {
    guint i;
    for (i = 0; i < mcat->nchains; i++)
    {
      NcmStatsVec *pstats = g_ptr_array_index (mcat->chain_pstats, i);
      ncm_stats_vec_reset (pstats, FALSE);
    }
    ncm_stats_vec_reset (mcat->mean_pstats, FALSE);
  }
  ncm_vector_set_all (mcat->params_max, GSL_NEGINF);
  ncm_vector_set_all (mcat->params_min, GSL_POSINF);
}


/**
 * ncm_mset_catalog_reset:
 * @mcat: a #NcmMSetCatalog
 * 
 * Clean all catalog data from memory and file. Otherwise it does 
 * not change any object's parameter.
 * 
 */
void
ncm_mset_catalog_reset (NcmMSetCatalog *mcat)
{  
  ncm_mset_catalog_erase_data (mcat);
  
  ncm_stats_vec_reset (mcat->pstats, TRUE);
  if (mcat->nchains > 1)
  {
    guint i;
    for (i = 0; i < mcat->nchains; i++)
    {
      NcmStatsVec *pstats = g_ptr_array_index (mcat->chain_pstats, i);
      ncm_stats_vec_reset (pstats, TRUE);
    }
    ncm_stats_vec_reset (mcat->mean_pstats, TRUE);
  }
  
  ncm_vector_set_all (mcat->params_max, GSL_NEGINF);
  ncm_vector_set_all (mcat->params_min, GSL_POSINF);

  mcat->cur_id    = mcat->first_id - 1;
#ifdef NUMCOSMO_HAVE_CFITSIO
  _ncm_mset_catalog_close_file (mcat);
#endif /* NUMCOSMO_HAVE_CFITSIO */
}

/**
 * ncm_mset_catalog_erase_data:
 * @mcat: a #NcmMSetCatalog
 * 
 * Erases all data from the fits file associated with the
 * catalog.
 * 
 */
void
ncm_mset_catalog_erase_data (NcmMSetCatalog *mcat)
{
#ifdef NUMCOSMO_HAVE_CFITSIO  
  if (mcat->fptr != NULL)
  {
    gint status = 0;
    gint nrows = mcat->file_cur_id - mcat->file_first_id + 1; 

    if (nrows > 0)
    {    
      fits_delete_rows (mcat->fptr, 1, nrows, &status);
      NCM_FITS_ERROR (status);

      mcat->file_cur_id = mcat->file_first_id - 1;
      _ncm_mset_catalog_flush_file (mcat);
    }
  }
#endif /* NUMCOSMO_HAVE_CFITSIO */
}

/**
 * ncm_mset_catalog_peek_filename:
 * @mcat: a #NcmMSetCatalog
 * 
 * Gets the filename associated with @mcat.
 * 
 * Returns: (transfer none): filename or NULL.
 */
const gchar *
ncm_mset_catalog_peek_filename (NcmMSetCatalog *mcat)
{
  return mcat->file;
}

/**
 * ncm_mset_catalog_get_rng:
 * @mcat: a #NcmMSetCatalog
 * 
 * This function checks if any pseudo random number generator (RNG) is registred in the catalog.
 * If so, it returns it or NULL.
 * 
 * Returns: (transfer full) (allow-none): the registered #NcmRNG in the catalog or NULL.
 */
NcmRNG *
ncm_mset_catalog_get_rng (NcmMSetCatalog *mcat)
{
  if (mcat->rng != NULL)
    return ncm_rng_ref (mcat->rng);
  else
    return NULL;
}

/**
 * ncm_mset_catalog_is_empty:
 * @mcat: a #NcmMSetCatalog
 * 
 * Returns: TRUE when the catalog is empty.
 */
gboolean 
ncm_mset_catalog_is_empty (NcmMSetCatalog *mcat)
{
  return (mcat->cur_id < mcat->first_id) && 
    (mcat->file_cur_id < mcat->file_first_id);
}

/**
 * ncm_mset_catalog_largest_error:
 * @mcat: a #NcmMSetCatalog
 * 
 * This function calculates the largest proportional error of the parameters included, i.e., $\text{lre} = \sigma_{\hat{p}}/(|\hat{p}|\sqrt{n})$ 
 * where $n$ represents the number of samples in the catalog, $\hat{p}$ is the estimated mean of the parameter $p$
 * and $\sigma_{\hat{p}}$ its standard deviation. 
 * 
 * It tries to guess when $p = 0$. In this case $\sigma_{\hat{p}} \approx |\hat{p}|\sqrt{n}$. Therefore, for $n > 10$, it tests
 * if $\text{lre} \approx 1$ and, if it is the case, it returns $\text{lre} = \sigma_{\hat{p}}/\sqrt{n}$ instead.
 * 
 * Returns: the largest proportional error $\text{lre}$.
 */
gdouble
ncm_mset_catalog_largest_error (NcmMSetCatalog *mcat)
{
  guint i;
  guint free_params_len = ncm_mset_fparams_len (mcat->mset);
  const gdouble n = ncm_stats_vec_get_weight (mcat->pstats);
  const gdouble sqrt_n = sqrt (n);
  const gdouble fpi = mcat->nadd_vals;
  const gdouble fpf = free_params_len + mcat->nadd_vals;
  gdouble lerror = 0.0;
  
  if (n < 10)
  {
    for (i = fpi; i < fpf; i++)
    {
      const gdouble mu = ncm_stats_vec_get_mean (mcat->pstats, i);
      const gdouble sd = ncm_stats_vec_get_sd (mcat->pstats, i);
      gdouble lerror_i = fabs (sd / (mu * sqrt_n));
      lerror_i *= sqrt (ncm_vector_get (mcat->tau, i - fpi));

      lerror = GSL_MAX (lerror, lerror_i);
    }
  }
  else
  {
    for (i = fpi; i < fpf; i++)
    {
      const gdouble mu = ncm_stats_vec_get_mean (mcat->pstats, i);
      const gdouble sd = ncm_stats_vec_get_sd (mcat->pstats, i);
      gdouble lerror_i = fabs (sd / (mu * sqrt_n));
      guint lerror_i_truc = lerror_i; 
      if (lerror_i_truc == 1)
        lerror_i = fabs (sd / sqrt_n);

      lerror_i *= sqrt (ncm_vector_get (mcat->tau, i - fpi));

      lerror = GSL_MAX (lerror, lerror_i);
    }    
  }
  return lerror;
}

/**
 * ncm_mset_catalog_len:
 * @mcat: a #NcmMSetCatalog
 * 
 * Number of itens in the catalog.
 * 
 * Returns: number of itens in the catalog.
 */
guint
ncm_mset_catalog_len (NcmMSetCatalog *mcat)
{
  return mcat->pstats->nitens;
}

static void
_ncm_mset_catalog_post_update (NcmMSetCatalog *mcat)
{
  guint i;
  guint len = mcat->pstats->len;
  NcmVector *x = ncm_stats_vec_peek_x (mcat->pstats);

  for (i = 0; i < len; i++)
  {
    gdouble p_i = ncm_vector_get (x, i);
    gdouble cur_max_p_i = ncm_vector_get (mcat->params_max, i);
    gdouble cur_min_p_i = ncm_vector_get (mcat->params_min, i);

    ncm_vector_set (mcat->params_max, i, GSL_MAX (p_i, cur_max_p_i));
    ncm_vector_set (mcat->params_min, i, GSL_MIN (p_i, cur_min_p_i));
  }

  if (mcat->weighted)
  {
    if (mcat->nchains > 1)
    {
      guint chain_id = (mcat->cur_id + 1) % mcat->nchains;
      NcmStatsVec *pstats = g_ptr_array_index (mcat->chain_pstats, chain_id);
      ncm_vector_memcpy (ncm_stats_vec_peek_x (pstats), x);
      ncm_stats_vec_update_weight (pstats, ncm_vector_get (x, mcat->nadd_vals - 1));
    }
    ncm_stats_vec_update_weight (mcat->pstats, ncm_vector_get (x, mcat->nadd_vals - 1));
  }
  else
  {
    if (mcat->nchains > 1)
    {
      guint chain_id = (mcat->cur_id + 1) % mcat->nchains;
      NcmStatsVec *pstats = g_ptr_array_index (mcat->chain_pstats, chain_id);
      ncm_vector_memcpy (ncm_stats_vec_peek_x (pstats), x);
      ncm_stats_vec_update (pstats);
    }
    ncm_stats_vec_update (mcat->pstats);
  }

  mcat->cur_id++;

  ncm_mset_catalog_sync (mcat, FALSE);
}

/**
 * ncm_mset_catalog_add_from_mset:
 * @mcat: a #NcmMSetCatalog
 * @mset: a #NcmMSet
 * @...: additional values
 * 
 * This function adds a new element to the catalog using the parameters from @mset. 
 * It assumes that @mset is compatible with the catalog and expect the
 * right number of additional values.
 * 
 */
void
ncm_mset_catalog_add_from_mset (NcmMSetCatalog *mcat, NcmMSet *mset, ...)
{
  NcmVector *row_i = ncm_stats_vec_peek_x (mcat->pstats);
  va_list ap;
  guint i;

  va_start (ap, mset);
  
  for (i = 0; i < mcat->nadd_vals; i++)
  {
    gdouble val = va_arg (ap, gdouble);
    ncm_vector_set (row_i, i, val);
  }
  
  va_end (ap);

  ncm_mset_fparams_get_vector_offset (mset, row_i, mcat->nadd_vals);

  _ncm_mset_catalog_post_update (mcat);
}

/**
 * ncm_mset_catalog_add_from_mset_array:
 * @mcat: a #NcmMSetCatalog
 * @mset: a #NcmMSet
 * @ax: (array) (element-type double): additional values array
 * 
 * This funtion adds a new element to the catalog using the parameters from @mset. 
 * It assumes that @mset is compatible with the catalog and expect the
 * right number of additional values in the array @ax.
 * 
 */
void
ncm_mset_catalog_add_from_mset_array (NcmMSetCatalog *mcat, NcmMSet *mset, gdouble *ax)
{
  NcmVector *row_i = ncm_stats_vec_peek_x (mcat->pstats);
  guint i;
  
  for (i = 0; i < mcat->nadd_vals; i++)
    ncm_vector_set (row_i, i, ax[i]);

  ncm_mset_fparams_get_vector_offset (mset, row_i, mcat->nadd_vals);

  _ncm_mset_catalog_post_update (mcat);
}

/**
 * ncm_mset_catalog_add_from_vector:
 * @mcat: a #NcmMSetCatalog
 * @vals: a #NcmVector
 * 
 * Adds a new element to the catalog using the values from the vector
 * @vals.
 * 
 */
void
ncm_mset_catalog_add_from_vector (NcmMSetCatalog *mcat, NcmVector *vals)
{
  NcmVector *row_i = ncm_stats_vec_peek_x (mcat->pstats);
  ncm_vector_memcpy (row_i, vals);
  _ncm_mset_catalog_post_update (mcat);
}

static gdouble 
_fvar (gdouble v_i, guint i, gpointer user_data)
{
  NcmStatsVec *pstats = NCM_STATS_VEC (user_data);
  return sqrt (v_i * pstats->bias_wt);
}

static gdouble 
_fmeanvar (gdouble v_i, guint i, gpointer user_data)
{
  NcmMSetCatalog *mcat = NCM_MSET_CATALOG (user_data);
  if (i < mcat->nadd_vals)
    return sqrt (v_i * mcat->pstats->bias_wt / mcat->pstats->nitens);
  else
    return sqrt (v_i * mcat->pstats->bias_wt * ncm_vector_get (mcat->tau, i - mcat->nadd_vals) / mcat->pstats->nitens);
}

static gdouble 
_ftau (gdouble v_i, guint i, gpointer user_data)
{
  NcmMSetCatalog *mcat = NCM_MSET_CATALOG (user_data);
  if (i < mcat->nadd_vals)
    return 1.0;
  else
    return ncm_vector_get (mcat->tau, i - mcat->nadd_vals);
}


/**
 * ncm_mset_catalog_log_current_stats:
 * @mcat: a #NcmMSetCatalog
 * 
 * Logs the current means and standard deviations of the catalog's parameters.
 * 
 */
void
ncm_mset_catalog_log_current_stats (NcmMSetCatalog *mcat)
{
  ncm_vector_log_vals (mcat->pstats->mean,     "# NcmMSetCatalog: Current mean:  ", "% -12.5g");
  ncm_vector_log_vals_func (mcat->pstats->var, "# NcmMSetCatalog: Current msd:   ", "% -12.5g", &_fmeanvar, mcat);
  ncm_vector_log_vals_func (mcat->pstats->var, "# NcmMSetCatalog: Current sd:    ", "% -12.5g", &_fvar, mcat->pstats);
  ncm_vector_log_vals_avpb (mcat->pstats->var, "# NcmMSetCatalog: Current var:   ", "% -12.5g", mcat->pstats->bias_wt, 0.0);
  ncm_vector_log_vals_func (mcat->pstats->var, "# NcmMSetCatalog: Current tau:   ", "% -12.5g", &_ftau, mcat);
}

/**
 * ncm_mset_catalog_get_mset:
 * @mcat: a #NcmMSetCatalog
 * 
 * Gets the #NcmMSet catalog from @mcat.
 * 
 * Returns: (transfer full): a reference to the used #NcmMSet object.
 */
NcmMSet *
ncm_mset_catalog_get_mset (NcmMSetCatalog *mcat)
{
  return ncm_mset_ref (mcat->mset);
}

/**
 * ncm_mset_catalog_get_run_type:
 * @mcat: a #NcmMSetCatalog
 * 
 * Gets the run type string from @mcat.
 * 
 * Returns: (transfer none): the run type string.
 */
const gchar *
ncm_mset_catalog_get_run_type (NcmMSetCatalog *mcat)
{
  return mcat->rtype_str;
}

/**
 * ncm_mset_catalog_log_current_chain_stats:
 * @mcat: a #NcmMSetCatalog
 * 
 * Logs the current means and standard deviations of the catalog's parameters
 * for each chain.
 * 
 */
void 
ncm_mset_catalog_log_current_chain_stats (NcmMSetCatalog *mcat)
{
  if (mcat->nchains > 1)
  {
    gdouble shrink_factor = ncm_mset_catalog_get_shrink_factor (mcat);
    g_message ("# NcmMSetCatalog: Maximal  Shrink factor = % 20.15g\n", shrink_factor);
  }
}

/**
 * ncm_mset_catalog_peek_row:
 * @mcat: a #NcmMSetCatalog
 * @i: the row index
 * 
 * Gets the @i-th row.
 * 
 * Returns: (transfer none): the row with index @i or NULL if not available. 
 */
NcmVector *
ncm_mset_catalog_peek_row (NcmMSetCatalog *mcat, guint i)
{
  guint nrows = ncm_stats_vec_nrows (mcat->pstats);
  if (i >= nrows)
    return NULL;
  else
    return ncm_stats_vec_peek_row (mcat->pstats, i);
}

/**
 * ncm_mset_catalog_peek_current_row:
 * @mcat: a #NcmMSetCatalog
 * 
 * Gets the last added row.
 * 
 * Returns: (transfer none): last added row or NULL if empty. 
 */
NcmVector *
ncm_mset_catalog_peek_current_row (NcmMSetCatalog *mcat)
{
  if (mcat->pstats->nitens == 0)
    return NULL;
  else
    return ncm_stats_vec_peek_row (mcat->pstats, mcat->pstats->nitens - 1);
}

/**
 * ncm_mset_catalog_get_mean:
 * @mcat: a #NcmMSetCatalog
 * @mean: (inout) (allow-none) (transfer full): a #NcmVector
 *
 * Gets the current mean vector.
 * 
 */
void
ncm_mset_catalog_get_mean (NcmMSetCatalog *mcat, NcmVector **mean)
{
  if (*mean == NULL)
    *mean = ncm_vector_new (mcat->pstats->len - mcat->nadd_vals);
  ncm_stats_vec_get_mean_vector (mcat->pstats, *mean, mcat->nadd_vals);
}

/**
 * ncm_mset_catalog_get_covar:
 * @mcat: a #NcmMSetCatalog
 * @cov: (inout) (allow-none) (transfer full): a #NcmMatrix
 *
 * Gets the current covariance matrix.
 * 
 */
void
ncm_mset_catalog_get_covar (NcmMSetCatalog *mcat, NcmMatrix **cov)
{
  if (*cov == NULL)
    *cov = ncm_matrix_new (mcat->pstats->len - mcat->nadd_vals, mcat->pstats->len - mcat->nadd_vals);
  ncm_stats_vec_get_cov_matrix (mcat->pstats, *cov, mcat->nadd_vals);
}

/**
 * ncm_mset_catalog_estimate_autocorrelation_tau:
 * @mcat: a #NcmMSetCatalog
 *
 * Updates the internal estimates of the integrate autocorrelation time.
 * 
 */
void 
ncm_mset_catalog_estimate_autocorrelation_tau (NcmMSetCatalog *mcat)
{
  guint fparams_len = ncm_mset_fparams_len (mcat->mset);
  guint p;

  if (mcat->nchains == 1)
  {
    for (p = 0; p < fparams_len; p++)
    {
      gdouble tau = ncm_stats_vec_get_autocorr_tau (mcat->pstats, p + mcat->nadd_vals, 0, 0.0);
      ncm_vector_set (mcat->tau, p, tau);
    }
  }
  else
  {
    for (p = 0; p < fparams_len; p++)
    {
      gdouble tau = ncm_stats_vec_get_subsample_autocorr_tau (mcat->pstats, p + mcat->nadd_vals, mcat->nchains, 0, 0.0);
      ncm_vector_set (mcat->tau, p, tau);
    }
  }
}

/**
 * ncm_mset_catalog_peek_autocorrelation_tau:
 * @mcat: a #NcmMSetCatalog
 *
 * Gets the last estimate of the autocorrelation tau calculated
 * in the last call of ncm_mset_catalog_estimate_autocorrelation_tau().
 * 
 * Returns: (transfer none): the last estimate of the autocorrelation tau.
 */
NcmVector * 
ncm_mset_catalog_peek_autocorrelation_tau (NcmMSetCatalog *mcat)
{
  return mcat->tau;
}

/**
 * ncm_mset_catalog_get_param_shrink_factor:
 * @mcat: a #NcmMSetCatalog
 * @p: parameter id.
 *
 * Gets the current shrink factor of parameter @p.
 * 
 * Returns: the shrink factor of @p.
 */
gdouble 
ncm_mset_catalog_get_param_shrink_factor (NcmMSetCatalog *mcat, guint p)
{
  guint i;
  gdouble W, B_n, shrink_factor;
  guint n = mcat->pstats->nitens;

  if (mcat->nchains == 1)
    return 1.0;

  if (n % mcat->nchains != 0)
    g_warning ("ncm_mset_catalog_get_param_shrink_factor: not all chains have the same size [%u %u] %u.", n, mcat->nchains, (n % mcat->nchains));

  n = n / mcat->nchains;
  
  for (i = 0; i < mcat->nchains; i++)
  {
    NcmStatsVec *pstats = g_ptr_array_index (mcat->chain_pstats, i);
    ncm_vector_set (mcat->chain_means, i, ncm_stats_vec_get_mean (pstats, p));
    ncm_vector_set (mcat->chain_vars, i, ncm_stats_vec_get_var (pstats, p));
  }
  W   = gsl_stats_mean (ncm_vector_ptr (mcat->chain_vars, 0), ncm_vector_stride (mcat->chain_vars), ncm_vector_len (mcat->chain_vars));
  B_n = gsl_stats_variance (ncm_vector_ptr (mcat->chain_means, 0), ncm_vector_stride (mcat->chain_means), ncm_vector_len (mcat->chain_means));

  shrink_factor = sqrt ((n - 1.0) /  (1.0 * n) + B_n / W);

  return shrink_factor;
}

/**
 * ncm_mset_catalog_get_shrink_factor:
 * @mcat: a #NcmMSetCatalog
 *
 * Gets the current shrink factor which is  the multivatiate potential scale reduction factor (MPSRF), namely,
 * $$\hat{R}^p = \sqrt{\frac{n - 1}{n} + \left( \frac{m + 1}{m} \right) \lambda_1},$$
 * where $n$ is the number of points of one chain, $m$ is the number of chains and $\lambda_1$ is the largest 
 * eigenvalue of the positive definite matrix $W^{-1}B/n$.
 * 
 * $W$ is the within-chain variance: $$W = $$ FIXME
 * 
 * $B$ is the between-chain variance: $$B = $$ FIXME
 * 
 * Refined version:
 * $$\hat{R}^p = \sqrt{\frac{\hat{d} + 3}{\hat{d} + 1} \left(\frac{n - 1}{n} + \left( \frac{m + 1}{m} \right) \lambda_1\right)},$$
 * where $\hat{d} = 2 \hat{V}^2 / \widehat{Var}(\hat{V})$, $$\hat{V} = \frac{n -1}{n}W + \frac{m + 1}{m} \frac{B}{n}.$$
 * 
 * Some references for this MCMC convergence diagnostic: <link linkend="XBrooks1998">Brooks and Gelman (1998)</link>,
 * <link linkend="XGelman1992">Gelman and Rubin (1992)</link>,
 * <ulink url="http://support.sas.com/documentation/cdl/en/statug/63033/HTML/default/viewer.htm#statug_introbayes_sect008.htm">SAS/STAT</ulink>
 * 
 * Returns: the shrink factor $\hat{R}^p$
 */
gdouble 
ncm_mset_catalog_get_shrink_factor (NcmMSetCatalog *mcat)
{
  gint ret;
  guint i;
  guint n = mcat->pstats->nitens;
  const guint free_params_len = mcat->pstats->len - mcat->nadd_vals;
  gdouble shrink_factor = 1.0e10;
  
  if (mcat->nchains == 1)
    return 1.0;

  if (n % mcat->nchains != 0)
    g_warning ("ncm_mset_catalog_get_shrink_factor: not all chains have the same size [%u %u] %u.", n, mcat->nchains, (n % mcat->nchains));

  ncm_stats_vec_reset (mcat->mean_pstats, TRUE);
  ncm_matrix_set_zero (mcat->chain_cov);

  for (i = 0; i < mcat->nchains; i++)
  {
    NcmStatsVec *pstats = g_ptr_array_index (mcat->chain_pstats, i);
    NcmMatrix *cov = ncm_stats_vec_peek_cov_matrix (pstats, mcat->nadd_vals);
    guint p;
    
    for (p = 0; p < free_params_len; p++)
      ncm_stats_vec_set (mcat->mean_pstats, p, ncm_stats_vec_get_mean (pstats, p + mcat->nadd_vals));

    ncm_stats_vec_update (mcat->mean_pstats);
    
    ncm_matrix_add_mul (mcat->chain_cov, 1.0, cov);
  }
  ncm_matrix_scale (mcat->chain_cov, 1.0 / (1.0 * mcat->nchains));
  
  {
    NcmMatrix *cov = ncm_stats_vec_peek_cov_matrix (mcat->mean_pstats, 0);
/*
    ncm_matrix_log_vals (mcat->chain_cov, "# mean cov", "% 10.5g");
    ncm_matrix_log_vals (cov, "# cov mean", "% 10.5g");
*/    
    if (gsl_finite (ncm_matrix_get (mcat->chain_cov, 0, 0)))
    {
      gdouble lev = 0.0;

      ret = ncm_matrix_cholesky_decomp (mcat->chain_cov, 'U');
      if (ret != 0)
        g_error ("ncm_mset_catalog_get_shrink_factor[ncm_matrix_cholesky_decomp]: %d.", ret);
      ret = ncm_matrix_cholesky_inverse (mcat->chain_cov, 'U');
      if (ret != 0)
        g_error ("ncm_mset_catalog_get_shrink_factor[ncm_matrix_cholesky_inverse]: %d.", ret);
      //ncm_matrix_copy_triangle (mcat->chain_cov, 'U');

      ncm_matrix_dsymm (mcat->chain_cov, 'U', 1.0, cov, 0.0, mcat->chain_sM);
/*      
      ncm_matrix_log_vals (mcat->chain_cov, "# mean inv_cov", "% 10.5g");
      ncm_matrix_log_vals (mcat->chain_sM, "# sM ", "% 10.5g");
*/
      gsl_eigen_nonsymm_params (0, 0, mcat->chain_sM_ws);
      gsl_eigen_nonsymm (ncm_matrix_gsl (mcat->chain_sM), mcat->chain_sM_ev, mcat->chain_sM_ws);

      for (i = 0; i < free_params_len; i++)
      {
        lev = GSL_MAX (lev, GSL_VECTOR_REAL (mcat->chain_sM_ev, i));

        if (GSL_VECTOR_IMAG (mcat->chain_sM_ev, i) != 0.0)
          g_warning ("ncm_mset_catalog_get_shrink_factor: complex eigenvalue in SM matrix, unreliable shrink factor, try using more chains.");
        
        /*printf ("(% 20.15g, % 20.15g)\n", GSL_VECTOR_REAL (mcat->chain_sM_ev, i), GSL_VECTOR_IMAG (mcat->chain_sM_ev, i));*/
      }
      shrink_factor = sqrt ((n - 1.0) / (1.0 * n) + (mcat->nchains + 1.0) * lev / (mcat->nchains * 1.0));
    }
  }
  /*printf ("# shrink_factor % 20.15g\n", shrink_factor);*/
  return shrink_factor;
}



/**
 * ncm_mset_catalog_param_pdf:
 * @mcat: a #NcmMSetCatalog
 * @i: parameter index.
 * 
 * Bins and calculates the pdf associated with the parameter @i.
 * (not ready yet FIXME)
 * 
 */
void
ncm_mset_catalog_param_pdf (NcmMSetCatalog *mcat, guint i)
{
  const guint n = mcat->pstats->nitens;
  const guint nbins = n / 10 >= 10 ? n / 10 : 10;
  const gdouble p_max = ncm_vector_get (mcat->params_max, i);
  const gdouble p_min = ncm_vector_get (mcat->params_min, i);
  guint k;

  mcat->pdf_i = i;
	
  if (mcat->h != NULL && mcat->h->n != nbins)
  {
    gsl_histogram_free (mcat->h);
    mcat->h = NULL;
  }
  if (mcat->h == NULL)
    mcat->h = gsl_histogram_alloc (nbins);

  if (mcat->h_pdf != NULL && mcat->h_pdf->n != nbins)
  {
    gsl_histogram_pdf_free (mcat->h_pdf);
    mcat->h_pdf = NULL;
  }
  if (mcat->h_pdf == NULL)
    mcat->h_pdf = gsl_histogram_pdf_alloc (nbins);

  gsl_histogram_set_ranges_uniform (mcat->h, p_min, p_max);

  for (k = 0; k < mcat->pstats->nitens; k++)
  {
    NcmVector *row = ncm_stats_vec_peek_row (mcat->pstats, k);
    gsl_histogram_increment (mcat->h, ncm_vector_get (row, i));
  }

  gsl_histogram_pdf_init (mcat->h_pdf, mcat->h);
}

/**
 * ncm_mset_catalog_param_pdf_pvalue:
 * @mcat: a #NcmMSetCatalog
 * @pval: parameter value
 * @both: one or both sides p-value
 *
 * Calculates the p-value associated with the parameter value @pval.
 * 
 * Returns: the p-value.
 */
gdouble 
ncm_mset_catalog_param_pdf_pvalue (NcmMSetCatalog *mcat, gdouble pval, gboolean both)
{
  g_assert_cmpint (mcat->pdf_i, >=, 0);
  g_assert (mcat->h_pdf != NULL);

  {
    const gdouble p_max = ncm_vector_get (mcat->params_max, mcat->pdf_i);
    const gdouble p_min = ncm_vector_get (mcat->params_min, mcat->pdf_i);
    gsize i = 0;

    NCM_UNUSED (both);
    if (pval < p_min || pval > p_max)
    {
      g_warning ("ncm_mset_catalog_param_pdf_pvalue: value % 20.15g outside mc obtained interval [% 20.15g % 20.15g]. Assuming 0 pvalue.",
                 pval, p_min, p_max);
      return 0.0;
    }
    gsl_histogram_find (mcat->h, pval, &i);
    g_assert_cmpint (i, <=, mcat->h_pdf->n);
    if (i == 0)
      return 1.0;
    else
      return (1.0 - mcat->h_pdf->sum[i - 1]);
  }
}

/**
 * ncm_mset_catalog_calc_ci_direct:
 * @mcat: a #NcmMSetCatalog
 * @burnin: burn-in size
 * @func: a #NcmMSetFunc of type n-n
 * @x: (array) (element-type double): the argument of @func
 * @p_val: (element-type double): p-values for the confidence intervals
 *
 * Calculates the mean and the confidence interval (CI) for the value of @func 
 * for each p-value in @p_val. It stores the results in a #NcmVector, where the 
 * first element contains the mean and the following contain the lower and 
 * upper bounds for each p-value in @p_val.
 * 
 * This function calculates the quantiles directly using:
 * gsl_stats_quantile_from_sorted_data for this reason it must allocates the
 * catalog size times the number of elements in @x, for a less memory intensive
 * version use ncm_mset_catalog_calc_ci_interp().
 * 
 * The #NcmMSetFunc @func must be of dimension one.
 * 
 * # Example: #
 * 
 * If @p_val contains two values ($1\sigma$) 0.6827 and ($\sigma$) 0.9545,
 * the first element will contain the mean, the second and third, the lower 
 * and upper bounds, respectively. Then, the fourth and fifth elements the 
 * lower and upper bounds of $2\sigma$ CI.
 * 
 * Returns: (transfer full): a #NcmVector containing the mean and lower/upper bound of the confidence interval for @func.
 */
NcmMatrix * 
ncm_mset_catalog_calc_ci_direct (NcmMSetCatalog *mcat, guint burnin, NcmMSetFunc *func, gdouble *x, GArray *p_val)
{
  guint dim = ncm_mset_func_get_dim (func);
  g_assert_cmpuint (p_val->len, >, 1);
  g_assert_cmpuint (burnin, <, ncm_mset_catalog_len (mcat));
  {
    const guint nelem = p_val->len * 2 + 1;
    NcmMatrix *res = ncm_matrix_new (dim, nelem);
    NcmVector *save_params = ncm_vector_new (ncm_mset_fparams_len (mcat->mset));
    const guint cat_len = ncm_mset_catalog_len (mcat);
    const guint acat_len = cat_len - burnin;
    guint i, j;

    ncm_mset_fparams_get_vector (mcat->mset, save_params);

    ncm_vector_clear (&mcat->quantile_ws);
    mcat->quantile_ws = ncm_vector_new (acat_len * dim);

    for (i = burnin; i < cat_len; i++)
    {
      NcmVector *row = ncm_mset_catalog_peek_row (mcat, i);
      ncm_mset_fparams_set_vector_offset (mcat->mset, row, mcat->nadd_vals);

      ncm_mset_func_eval_direct (func, mcat->mset, x, ncm_vector_ptr (mcat->quantile_ws, i * dim));
    }

    for (i = 0; i < dim; i++)
    {
      gdouble *ret_i = ncm_vector_ptr (mcat->quantile_ws, i);
      gsl_sort (ret_i, dim, acat_len);
      ncm_matrix_set (res, i, 0, gsl_stats_mean (ret_i, dim, acat_len));
    }

    for (j = 0; j < p_val->len; j++)
    {
      const gdouble p = g_array_index (p_val, gdouble, j);
      g_assert_cmpfloat (p, >, 0.0);
      g_assert_cmpfloat (p, <, 1.0);
      for (i = 0; i < dim; i++)
      {
        gdouble *ret_i = ncm_vector_ptr (mcat->quantile_ws, i);
        const gdouble lb_prob = (1.0 - p) / 2.0;
        const gdouble ub_prob = (1.0 + p) / 2.0;
        const gdouble lb = gsl_stats_quantile_from_sorted_data (ret_i, dim, acat_len, lb_prob);
        const gdouble ub = gsl_stats_quantile_from_sorted_data (ret_i, dim, acat_len, ub_prob);
        ncm_matrix_set (res, i, 1 + j * 2 + 0, lb);
        ncm_matrix_set (res, i, 1 + j * 2 + 1, ub);
      }
    }

    ncm_vector_clear (&mcat->quantile_ws);
    ncm_mset_fparams_set_vector (mcat->mset, save_params);
    ncm_vector_free (save_params);
    return res;
  }
}

/**
 * ncm_mset_catalog_calc_ci_interp:
 * @mcat: a #NcmMSetCatalog
 * @burnin: burn-in size
 * @func: a #NcmMSetFunc of type n-n
 * @x: (array) (element-type double): the argument of @func
 * @p_val: (element-type double): p-values for the confidence intervals
 * @nodes: number of nodes in the distribution approximations
 * @mtype: #NcmFitRunMsgs log level
 *
 * Calculates the mean and the confidence interval (CI) for the value of @func 
 * for each p-value in @p_val. It stores the results in a #NcmVector, where the 
 * first element contains the mean and the following contain the lower and 
 * upper bounds for each p-value in @p_val.
 * 
 * This function creates an approximation of the distribution for each value of 
 * the function @func and calculates the quantiles from this approximation.
 * 
 * The #NcmMSetFunc @func must be of dimension one.
 * 
 * # Example: #
 * 
 * If @p_val contains two values ($1\sigma$) 0.6827 and ($\sigma$) 0.9545,
 * the first element will contain the mean, the second and third, the lower 
 * and upper bounds, respectively. Then, the fourth and fifth elements the 
 * lower and upper bounds of $2\sigma$ CI.
 * 
 * Returns: (transfer full): a #NcmVector containing the mean and lower/upper bound of the confidence interval for @func.
 */
NcmMatrix * 
ncm_mset_catalog_calc_ci_interp (NcmMSetCatalog *mcat, guint burnin, NcmMSetFunc *func, gdouble *x, GArray *p_val, guint nodes, NcmFitRunMsgs mtype)
{
  guint dim = ncm_mset_func_get_dim (func);
  g_assert_cmpuint (p_val->len, >, 1);
  g_assert_cmpuint (burnin, <, ncm_mset_catalog_len (mcat));
  {
    const guint nelem = p_val->len * 2 + 1;
    NcmMatrix *res = ncm_matrix_new (dim, nelem);
    NcmVector *save_params = ncm_vector_new (ncm_mset_fparams_len (mcat->mset));
    const guint cat_len = ncm_mset_catalog_len (mcat);
    GPtrArray *epdf_a = g_ptr_array_sized_new (dim);
    guint i, j;
    
    ncm_mset_fparams_get_vector (mcat->mset, save_params);

    if (mcat->quantile_ws == NULL)
    {
      mcat->quantile_ws = ncm_vector_new (dim);
    }
    else if (ncm_vector_len (mcat->quantile_ws) != dim)
    {
      ncm_vector_clear (&mcat->quantile_ws);
      mcat->quantile_ws = ncm_vector_new (dim);
    }

    g_ptr_array_set_free_func (epdf_a, (GDestroyNotify)ncm_stats_dist1d_free);
    for (i = 0; i < dim; i++)
    {
      NcmStatsDist1dEPDF *epdf = ncm_stats_dist1d_epdf_new (1000, 0.1, 0.01);
      g_ptr_array_add (epdf_a, epdf);
    }

    if (mtype > NCM_FIT_RUN_MSGS_NONE)
    {
      ncm_message ("# Calculating %u models in catalog: \n# - |", cat_len);
      for (i = 0; i < 100; i++)
        ncm_message ("-");
      ncm_message ("|\n# - |");
    }

    if (mtype > NCM_FIT_RUN_MSGS_NONE)
    {
      for (i = 0; i < burnin; i++)
      {
        if (i % (cat_len / 100) == 0)
        {
          ncm_message ("x");
        }
      }
    }
    for (i = burnin; i < cat_len; i++)
    {
      NcmVector *row = ncm_mset_catalog_peek_row (mcat, i);
      ncm_mset_fparams_set_vector_offset (mcat->mset, row, mcat->nadd_vals);
      ncm_mset_func_eval_direct (func, mcat->mset, x, ncm_vector_data (mcat->quantile_ws));

      for (j = 0; j < dim; j++)
      {
        NcmStatsDist1dEPDF *epdf = g_ptr_array_index (epdf_a, j);
        ncm_stats_dist1d_epdf_add_obs (epdf, ncm_vector_get (mcat->quantile_ws, j));
      }
      if (i % (cat_len / 100) == 0)
      {
        if (mtype > NCM_FIT_RUN_MSGS_NONE)
        {
          ncm_message ("=");
        }
      }
    }
    if (mtype > NCM_FIT_RUN_MSGS_NONE)
    {
      if (i % (cat_len / 100) != 0)
        ncm_message ("=");
      ncm_message ("|\n", cat_len);
      ncm_message ("# - |", cat_len);
      for (i = 0; i < 100; i++)
        ncm_message ("-");
      ncm_message ("|\n");
    }
    
    for (i = 0; i < dim; i++)
    {
      NcmStatsDist1dEPDF *epdf = g_ptr_array_index (epdf_a, i);
      const gdouble mean = ncm_stats_dist1d_epdf_get_obs_mean (epdf);
      ncm_stats_dist1d_prepare (NCM_STATS_DIST1D (epdf));

      ncm_matrix_set (res, i, 0, mean);
      for (j = 0; j < p_val->len; j++)
      {
        const gdouble p = g_array_index (p_val, gdouble, j);
        const gdouble lb_prob = (1.0 - p) / 2.0;
        const gdouble ub_prob = (1.0 + p) / 2.0;
        const gdouble lb = ncm_stats_dist1d_eval_inv_pdf (NCM_STATS_DIST1D (epdf), lb_prob);
        const gdouble ub = ncm_stats_dist1d_eval_inv_pdf (NCM_STATS_DIST1D (epdf), ub_prob);

        g_assert_cmpfloat (p, >, 0.0);
        g_assert_cmpfloat (p, <, 1.0);

        ncm_matrix_set (res, i, 1 + j * 2 + 0, lb);
        ncm_matrix_set (res, i, 1 + j * 2 + 1, ub);        
      }
    }

    g_ptr_array_unref (epdf_a);
    ncm_mset_fparams_set_vector (mcat->mset, save_params);
    ncm_vector_free (save_params);
    return res;
  }
}

/**
 * ncm_mset_catalog_calc_distrib:
 * @mcat: a #NcmMSetCatalog
 * @burnin: burn-in size
 * @func: a #NcmMSetFunc of type 0-1
 * @mtype: #NcmFitRunMsgs log level
 *
 * Calculates the distribution of @func.
 * 
 * This function creates an approximation of the distribution for each value of 
 * the function @func calculated in each model in @mcat.
 * 
 * Returns: (transfer full): a #NcmStatsDist1d describing the distribution.
 */
NcmStatsDist1d *
ncm_mset_catalog_calc_distrib (NcmMSetCatalog *mcat, guint burnin, NcmMSetFunc *func, NcmFitRunMsgs mtype)
{
  guint dim = ncm_mset_func_get_dim (func);
  g_assert_cmpuint (dim, ==, 1);
  g_assert_cmpuint (burnin, <, ncm_mset_catalog_len (mcat));
  {
    NcmStatsDist1dEPDF *epdf1d = ncm_stats_dist1d_epdf_new (1000, 0.1, 0.01);
    NcmVector *save_params = ncm_vector_new (ncm_mset_fparams_len (mcat->mset));
    const guint cat_len = ncm_mset_catalog_len (mcat);
    guint i;
    
    ncm_mset_fparams_get_vector (mcat->mset, save_params);

    if (mtype > NCM_FIT_RUN_MSGS_NONE)
    {
      ncm_message ("# Calculating %u models in catalog: \n# - |", cat_len);
      for (i = 0; i < 100; i++)
        ncm_message ("-");
      ncm_message ("|\n# - |");
    }

    if (mtype > NCM_FIT_RUN_MSGS_NONE)
    {
      for (i = 0; i < burnin; i++)
      {
        if (i % (cat_len / 100) == 0)
        {
          ncm_message ("x");
        }
      }
    }
    for (i = burnin; i < cat_len; i++)
    {
      NcmVector *row = ncm_mset_catalog_peek_row (mcat, i);
      gdouble x;
      
      ncm_mset_fparams_set_vector_offset (mcat->mset, row, mcat->nadd_vals);
      x = ncm_mset_func_eval0 (func, mcat->mset);
      ncm_stats_dist1d_epdf_add_obs (epdf1d, x);

      if (i % (cat_len / 100) == 0)
      {
        if (mtype > NCM_FIT_RUN_MSGS_NONE)
        {
          ncm_message ("=");
        }
      }
    }
    if (mtype > NCM_FIT_RUN_MSGS_NONE)
    {
      if (i % (cat_len / 100) != 0)
        ncm_message ("=");
      ncm_message ("|\n", cat_len);
      ncm_message ("# - |", cat_len);
      for (i = 0; i < 100; i++)
        ncm_message ("-");
      ncm_message ("|\n");
    }

    ncm_stats_dist1d_prepare (NCM_STATS_DIST1D (epdf1d));

    ncm_mset_fparams_set_vector (mcat->mset, save_params);
    ncm_vector_free (save_params);
    return NCM_STATS_DIST1D (epdf1d);
  }
}

/**
 * ncm_mset_catalog_calc_param_distrib:
 * @mcat: a #NcmMSetCatalog
 * @burnin: burn-in size
 * @pi: a #NcmMSetPIndex
 * @mtype: #NcmFitRunMsgs log level
 *
 * Calculates the distribution of parameter @pi.
 * 
 * This function creates an approximation of the distribution for each value of 
 * the parameter @pi in @mcat.
 * 
 * Returns: (transfer full): a #NcmStatsDist1d describing the distribution.
 */
NcmStatsDist1d *
ncm_mset_catalog_calc_param_distrib (NcmMSetCatalog *mcat, guint burnin, NcmMSetPIndex *pi, NcmFitRunMsgs mtype)
{
  NcmStatsDist1dEPDF *epdf1d = ncm_stats_dist1d_epdf_new (1000, 0.1, 0.01);
  NcmVector *save_params = ncm_vector_new (ncm_mset_fparams_len (mcat->mset));
  const guint cat_len = ncm_mset_catalog_len (mcat);
  guint i;

  g_assert_cmpuint (burnin, <, cat_len);
  
  ncm_mset_fparams_get_vector (mcat->mset, save_params);

  g_assert (ncm_mset_param_get_ftype (mcat->mset, pi->mid, pi->pid) == NCM_PARAM_TYPE_FREE);

  if (mtype > NCM_FIT_RUN_MSGS_NONE)
  {
    ncm_message ("# Calculating %u models in catalog: \n# - |", cat_len);
    for (i = 0; i < 100; i++)
      ncm_message ("-");
    ncm_message ("|\n# - |");
  }

  if (mtype > NCM_FIT_RUN_MSGS_NONE)
  {
    for (i = 0; i < burnin; i++)
    {
      if (i % (cat_len / 100) == 0)
      {
        ncm_message ("x");
      }
    }
  }
  for (i = burnin; i < cat_len; i++)
  {
    NcmVector *row = ncm_mset_catalog_peek_row (mcat, i);
    guint fpi = ncm_mset_fparam_get_fpi (mcat->mset, pi->mid, pi->pid);
    gdouble x = ncm_vector_get (row, mcat->nadd_vals + fpi);

    ncm_stats_dist1d_epdf_add_obs (epdf1d, x);

    if (i % (cat_len / 100) == 0)
    {
      if (mtype > NCM_FIT_RUN_MSGS_NONE)
      {
        ncm_message ("=");
      }
    }
  }
  if (mtype > NCM_FIT_RUN_MSGS_NONE)
  {
    if (i % (cat_len / 100) != 0)
      ncm_message ("=");
    ncm_message ("|\n", cat_len);
    ncm_message ("# - |", cat_len);
    for (i = 0; i < 100; i++)
      ncm_message ("-");
    ncm_message ("|\n");
  }

  ncm_stats_dist1d_prepare (NCM_STATS_DIST1D (epdf1d));

  ncm_mset_fparams_set_vector (mcat->mset, save_params);
  ncm_vector_free (save_params);
  return NCM_STATS_DIST1D (epdf1d);
}

/**
 * ncm_mset_catalog_calc_add_param_distrib:
 * @mcat: a #NcmMSetCatalog
 * @burnin: burn-in size
 * @add_param: additional parameter index
 * @mtype: #NcmFitRunMsgs log level
 *
 * Calculates the distribution of parameter @pi.
 * 
 * This function creates an approximation of the distribution for each value of 
 * the parameter @pi in @mcat.
 * 
 * Returns: (transfer full): a #NcmStatsDist1d describing the distribution.
 */
NcmStatsDist1d *
ncm_mset_catalog_calc_add_param_distrib (NcmMSetCatalog *mcat, guint burnin, guint add_param, NcmFitRunMsgs mtype)
{
  NcmStatsDist1dEPDF *epdf1d = ncm_stats_dist1d_epdf_new (1000, 0.1, 0.01);
  NcmVector *save_params = ncm_vector_new (ncm_mset_fparams_len (mcat->mset));
  const guint cat_len = ncm_mset_catalog_len (mcat);
  guint i;

  g_assert_cmpuint (burnin, <, cat_len);
  
  ncm_mset_fparams_get_vector (mcat->mset, save_params);

  g_assert_cmpuint (add_param, <, mcat->nadd_vals);

  if (mtype > NCM_FIT_RUN_MSGS_NONE)
  {
    ncm_message ("# Calculating %u models in catalog: \n# - |", cat_len);
    for (i = 0; i < 100; i++)
      ncm_message ("-");
    ncm_message ("|\n# - |");
  }

  if (mtype > NCM_FIT_RUN_MSGS_NONE)
  {
    for (i = 0; i < burnin; i++)
    {
      if (i % (cat_len / 100) == 0)
      {
        ncm_message ("x");
      }
    }
  }
  
  for (i = burnin; i < cat_len; i++)
  {
    NcmVector *row = ncm_mset_catalog_peek_row (mcat, i);
    gdouble x = ncm_vector_get (row, add_param);
    ncm_stats_dist1d_epdf_add_obs (epdf1d, x);

    if (i % (cat_len / 100) == 0)
    {
      if (mtype > NCM_FIT_RUN_MSGS_NONE)
      {
        ncm_message ("=");
      }
    }
  }
  if (mtype > NCM_FIT_RUN_MSGS_NONE)
  {
    if (i % (cat_len / 100) != 0)
      ncm_message ("=");
    ncm_message ("|\n", cat_len);
    ncm_message ("# - |", cat_len);
    for (i = 0; i < 100; i++)
      ncm_message ("-");
    ncm_message ("|\n");
  }

  ncm_stats_dist1d_prepare (NCM_STATS_DIST1D (epdf1d));

  ncm_mset_fparams_set_vector (mcat->mset, save_params);
  ncm_vector_free (save_params);
  return NCM_STATS_DIST1D (epdf1d);
}
