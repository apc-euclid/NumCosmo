/***************************************************************************
 *            nc_data_cmb_dist_priors.c
 *
 *  Thu Apr 22 15:56:44 2010
 *  Copyright  2010  Sandro Dias Pinto Vitenti
 *  <sandro@isoftware.com.br>
 ****************************************************************************/
/*
 * numcosmo
 * Copyright (C) 2012 Sandro Dias Pinto Vitenti <sandro@isoftware.com.br>
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
 * SECTION:nc_data_cmb_dist_priors
 * @title: NcDataCMBDistPriors
 * @short_description: Cosmic microwave background data -- distance priors.
 *
 * FIXME
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */
#include "build_cfg.h"

#include "data/nc_data_cmb_dist_priors.h"
#include "nc_enum_types.h"

enum
{
  PROP_0,
  PROP_DIST,
  PROP_SIZE,
};

G_DEFINE_TYPE (NcDataCMBDistPriors, nc_data_cmb_dist_priors, NCM_TYPE_DATA_GAUSS);

static void
nc_data_cmb_dist_priors_init (NcDataCMBDistPriors *cmb_dist_priors)
{
  cmb_dist_priors->dist = NULL;
}

static void
_nc_data_cmb_dist_priors_constructed (GObject *object)
{
  /* Chain up : start */
  G_OBJECT_CLASS (nc_data_cmb_dist_priors_parent_class)->constructed (object);
}

static void
nc_data_cmb_dist_priors_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  NcDataCMBDistPriors *cmb_dist_priors = NC_DATA_CMB_DIST_PRIORS (object);
  g_return_if_fail (NC_IS_DATA_CMB_DIST_PRIORS (object));

  switch (prop_id)
  {
    case PROP_DIST:
      nc_distance_clear (&cmb_dist_priors->dist);
      cmb_dist_priors->dist = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
nc_data_cmb_dist_priors_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  NcDataCMBDistPriors *cmb_dist_priors = NC_DATA_CMB_DIST_PRIORS (object);
  g_return_if_fail (NC_IS_DATA_CMB_DIST_PRIORS (object));

  switch (prop_id)
  {
    case PROP_DIST:
      g_value_set_object (value, cmb_dist_priors->dist);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
nc_data_cmb_dist_priors_dispose (GObject *object)
{
  NcDataCMBDistPriors *cmb_dist_priors = NC_DATA_CMB_DIST_PRIORS (object);

  nc_distance_clear (&cmb_dist_priors->dist);
  
  /* Chain up : end */
  G_OBJECT_CLASS (nc_data_cmb_dist_priors_parent_class)->dispose (object);
}

static void
nc_data_cmb_dist_priors_finalize (GObject *object)
{
  
  /* Chain up : end */
  G_OBJECT_CLASS (nc_data_cmb_dist_priors_parent_class)->finalize (object);
}

static void _nc_data_cmb_dist_priors_prepare (NcmData *data, NcmMSet *mset);
static void _nc_data_cmb_dist_priors_mean_func (NcmDataGauss *gauss, NcmMSet *mset, NcmVector *vp);

static void
nc_data_cmb_dist_priors_class_init (NcDataCMBDistPriorsClass *klass)
{
  GObjectClass* object_class = G_OBJECT_CLASS (klass);
  NcmDataClass *data_class   = NCM_DATA_CLASS (klass);
  NcmDataGaussClass *gauss_class = NCM_DATA_GAUSS_CLASS (klass);

  object_class->constructed  = &_nc_data_cmb_dist_priors_constructed;
  object_class->set_property = &nc_data_cmb_dist_priors_set_property;
  object_class->get_property = &nc_data_cmb_dist_priors_get_property;
  object_class->dispose      = &nc_data_cmb_dist_priors_dispose;
  object_class->finalize     = &nc_data_cmb_dist_priors_finalize;

  g_object_class_install_property (object_class,
                                   PROP_DIST,
                                   g_param_spec_object ("dist",
                                                        NULL,
                                                        "Distance object",
                                                        NC_TYPE_DISTANCE,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB));

  data_class->prepare    = &_nc_data_cmb_dist_priors_prepare;
  gauss_class->mean_func = &_nc_data_cmb_dist_priors_mean_func;
}

static void
_nc_data_cmb_dist_priors_prepare (NcmData *data, NcmMSet *mset)
{
  NcDataCMBDistPriors *cmb_dist_priors = NC_DATA_CMB_DIST_PRIORS (data);
  NcHICosmo *cosmo = NC_HICOSMO (ncm_mset_peek (mset, nc_hicosmo_id ()));
  nc_distance_prepare_if_needed (cmb_dist_priors->dist, cosmo);
}

static void 
_nc_data_cmb_dist_priors_mean_func (NcmDataGauss *gauss, NcmMSet *mset, NcmVector *vp)
{
  NcDataCMBDistPriors *cmb_dist_priors = NC_DATA_CMB_DIST_PRIORS (gauss);
  NcHICosmo *cosmo = NC_HICOSMO (ncm_mset_peek (mset, nc_hicosmo_id ()));
  
  const gdouble Ascale = nc_distance_acoustic_scale (cmb_dist_priors->dist, cosmo);
  const gdouble Rlss = nc_distance_shift_parameter_lss (cmb_dist_priors->dist, cosmo);
  const gdouble zdec = nc_distance_decoupling_redshift (cmb_dist_priors->dist, cosmo);

  ncm_vector_set (vp, 0, Ascale);
  ncm_vector_set (vp, 1, Rlss);
  ncm_vector_set (vp, 2, zdec);
}

/**
 * nc_data_cmb_dist_priors_new:
 * @dist: a #NcDistance.
 * @id: a #NcDataCMBId.
 *
 * This function allocates memory for a new #NcmData object and sets its properties to the values from
 * the input arguments.
 *
 * Returns: A #NcmData.
 */
NcmData *
nc_data_cmb_dist_priors_new (NcDistance *dist, NcDataCMBId id)
{
  NcmData *data = g_object_new (NC_TYPE_DATA_CMB_DIST_PRIORS,
                                "dist", dist,
                                NULL);

  nc_data_cmb_dist_priors_set_sample (NC_DATA_CMB_DIST_PRIORS (data), id);
  
  return data;
}

/***************************************************************************
 * WMAP5 Distance priors data (arXiv:0803.0547), (astro-ph/0604051)
 *
 ****************************************************************************/

static gdouble nc_cmb_dist_priors_wmap5_bestfit[] = { 302.1000, 1.710, 1090.04000 };
static gdouble nc_cmb_dist_priors_wmap5_inv_cov[][3] =
{ { 1.8000,   27.968,   -1.10300 },
  { 27.968, 5667.577,  -92.26300 },
  { -1.103,  -92.263,    2.92300 } 
};

/***************************************************************************
 * WMAP7 Distance priors data (arXiv:1001.4538): tables 9 and 10
 *
 ****************************************************************************/

static gdouble nc_cmb_dist_priors_wmap7_bestfit[] = { 302.0900, 1.725, 1091.30000 };
static gdouble nc_cmb_dist_priors_wmap7_inv_cov[][3] =
{ { 2.3050,   29.698,   -1.333 },
  { 29.698, 6825.270,  -113.18 },
  { -1.333,  -113.18,    3.414 } };

/***************************************************************************
 * WMAP9 Distance priors data (arXiv:1212.5226): table 11
 *
 ****************************************************************************/

static gdouble nc_cmb_dist_priors_wmap9_bestfit[] = { 302.4, 1.7246, 1090.88 };
static gdouble nc_cmb_dist_priors_wmap9_inv_cov[][3] =
{ {  3.182,    18.253,   -1.429 },
  { 18.253, 11887.879, -193.808 },
  { -1.429,  -193.808,    4.556 } };


/**
 * nc_data_cmb_dist_priors_set_sample:
 * @cmb_dist_priors: a #NcDataCMBDistPriors.
 * @id: a #NcDataCMBId.
 *
 * This function sets the elements of both a vector and a matrix to the best-fit and the inverse covariance matrix 
 * values, respectively, of the CMB distance priors sample specified by @id.    
 *
 */
void
nc_data_cmb_dist_priors_set_sample (NcDataCMBDistPriors *cmb_dist_priors, NcDataCMBId id)
{
  NcmData *data = NCM_DATA (cmb_dist_priors);
  NcmDataGauss *gauss = NCM_DATA_GAUSS (cmb_dist_priors);
  gint i, j;
  
  g_assert (id < NC_DATA_CMB_NSAMPLES);

  ncm_data_gauss_set_size (gauss, 3);

  switch (id)
  {
    case NC_DATA_CMB_DIST_PRIORS_WMAP5:
    {
      ncm_data_set_desc (data, "WMAP5 distance priors");
      for (i = 0; i < 3; i++)
      {
        ncm_vector_set (gauss->y, i, nc_cmb_dist_priors_wmap5_bestfit[i]);
        for (j = 0; j < 3; j++)
          ncm_matrix_set (gauss->inv_cov, i, j, 
                          nc_cmb_dist_priors_wmap5_inv_cov[i][j]);
      }
      break;
    }
    case NC_DATA_CMB_DIST_PRIORS_WMAP7:
    {
      ncm_data_set_desc (data, "WMAP7 distance priors");
      for (i = 0; i < 3; i++)
      {
        ncm_vector_set (gauss->y, i, nc_cmb_dist_priors_wmap7_bestfit[i]);
        for (j = 0; j < 3; j++)
          ncm_matrix_set (gauss->inv_cov, i, j, 
                          nc_cmb_dist_priors_wmap7_inv_cov[i][j]);
      }
      break;
    }
    case NC_DATA_CMB_DIST_PRIORS_WMAP9:
    {
      ncm_data_set_desc (data, "WMAP9 distance priors");
      for (i = 0; i < 3; i++)
      {
        ncm_vector_set (gauss->y, i, nc_cmb_dist_priors_wmap9_bestfit[i]);
        for (j = 0; j < 3; j++)
          ncm_matrix_set (gauss->inv_cov, i, j, 
                          nc_cmb_dist_priors_wmap9_inv_cov[i][j]);
      }
      break;
    }
    default:
      g_assert_not_reached ();
  }

  ncm_data_set_init (data, TRUE);
}
