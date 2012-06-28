/***************************************************************************
 *            nc_cluster_mass_lnnormal.c
 *
 *  Fri June 22 17:48:55 2012
 *  Copyright  2012  Mariana Penna Lima
 *  <pennalima@gmail.com>
 ****************************************************************************/
/*
 * numcosmo
 * Copyright (C) Mariana Penna Lima 2012 <pennalima@gmail.com>
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
 * SECTION:nc_cluster_mass_lnnormal
 * @title: Cluster Abundance Mass Ln Normal Distribution
 * @short_description: FIXME
 *
 * FIXME
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */
#include <numcosmo/numcosmo.h>

G_DEFINE_TYPE (NcClusterMassLnnormal, nc_cluster_mass_lnnormal, NC_TYPE_CLUSTER_MASS);

#define _NC_CLUSTER_MASS_LNNORMAL_BIAS 0.0
#define _NC_CLUSTER_MASS_LNNORMAL_SIGMA 0.04

enum
{
  PROP_0,
  PROP_LNMOBS_MIN,
  PROP_LNMOBS_MAX,
};

static gdouble
_nc_cluster_mass_lnnormal_p (NcClusterMass *clusterm, gdouble lnM, gdouble z, gdouble *lnM_obs, gdouble *lnM_obs_params)
{
  const gdouble lnMobs = lnM_obs[0];
  const gdouble lnM_bias = lnM_obs_params[NC_CLUSTER_MASS_LNNORMAL_BIAS];
  const gdouble sigma = lnM_obs_params[NC_CLUSTER_MASS_LNNORMAL_SIGMA];
  const gdouble sqrt2_sigma = M_SQRT2 * sigma;
  const gdouble x = (lnMobs - lnM - lnM_bias) / sqrt2_sigma;

  return M_2_SQRTPI / (2.0 * M_SQRT2) * exp (- x * x) / (sigma);
}

static gdouble
_nc_cluster_mass_lnnormal_intp (NcClusterMass *clusterm, gdouble lnM, gdouble z)
{
  NcClusterMassLnnormal *mlnn = NC_CLUSTER_MASS_LNNORMAL (clusterm);
  const gdouble sigma = _NC_CLUSTER_MASS_LNNORMAL_SIGMA;
  const gdouble sqrt2_sigma = M_SQRT2 * sigma;
  const gdouble x_min = (lnM - mlnn->lnMobs_min) / sqrt2_sigma;
  const gdouble x_max = (lnM - mlnn->lnMobs_max) / sqrt2_sigma;

  if (x_max > 4.0)
	return -(erfc (x_min) - erfc (x_max)) / 2.0;
  else
	return (erf (x_min) - erf (x_max)) / 2.0;
}

static gboolean
_nc_cluster_mass_lnnormal_resample (NcClusterMass *clusterm, gdouble lnM, gdouble z, gdouble *lnM_obs, gdouble *lnM_obs_params)
{
  NcClusterMassLnnormal *mlnn = NC_CLUSTER_MASS_LNNORMAL (clusterm);
  gsl_rng *rng = ncm_get_rng ();
  const gdouble sigma = _NC_CLUSTER_MASS_LNNORMAL_SIGMA;
  const gdouble bias = _NC_CLUSTER_MASS_LNNORMAL_BIAS;

  lnM_obs_params[NC_CLUSTER_MASS_LNNORMAL_BIAS] = bias;
  lnM_obs_params[NC_CLUSTER_MASS_LNNORMAL_SIGMA] = sigma;

  lnM_obs[0] = lnM + bias + gsl_ran_gaussian (rng, sigma);

  return (lnM_obs[0] <= mlnn->lnMobs_max) && (lnM_obs[0] >= mlnn->lnMobs_min);
}

static void
_nc_cluster_mass_lnnormal_p_limits (NcClusterMass *clusterm, gdouble *lnM_obs, gdouble *lnM_obs_params, gdouble *lnM_lower, gdouble *lnM_upper)
{
  const gdouble mean = lnM_obs[0] - lnM_obs_params[NC_CLUSTER_MASS_LNNORMAL_BIAS];
  const gdouble lnMl = mean - 7.0 * lnM_obs_params[NC_CLUSTER_MASS_LNNORMAL_SIGMA];
  const gdouble lnMu = mean + 7.0 * lnM_obs_params[NC_CLUSTER_MASS_LNNORMAL_SIGMA];

  *lnM_lower = lnMl;
  *lnM_upper = lnMu;

  return;
}

static void
_nc_cluster_mass_lnnormal_n_limits (NcClusterMass *clusterm, gdouble *lnM_lower, gdouble *lnM_upper)
{
  NcClusterMassLnnormal *mlnn = NC_CLUSTER_MASS_LNNORMAL (clusterm);
  const gdouble lnMl = mlnn->lnMobs_min - 7.0 * _NC_CLUSTER_MASS_LNNORMAL_SIGMA;
  const gdouble lnMu = mlnn->lnMobs_max + 7.0 * _NC_CLUSTER_MASS_LNNORMAL_SIGMA;

  *lnM_lower = lnMl;
  *lnM_upper = lnMu;

  return;
}

guint _nc_cluster_mass_lnnormal_obs_len (NcClusterMass *clusterm) { return 1; }
guint _nc_cluster_mass_lnnormal_obs_params_len (NcClusterMass *clusterm) { return 2; }

static void
_nc_cluster_mass_lnnormal_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  NcClusterMassLnnormal *mlnm = NC_CLUSTER_MASS_LNNORMAL (object);
  g_return_if_fail (NC_IS_CLUSTER_MASS_LNNORMAL (object));

  switch (prop_id)
  {
    case PROP_LNMOBS_MIN:
      mlnm->lnMobs_min = g_value_get_double (value);
      break;
	case PROP_LNMOBS_MAX:
      mlnm->lnMobs_max = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
_nc_cluster_mass_lnnormal_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  NcClusterMassLnnormal *mlnm = NC_CLUSTER_MASS_LNNORMAL (object);
  g_return_if_fail (NC_IS_CLUSTER_MASS_LNNORMAL (object));

  switch (prop_id)
  {
    case PROP_LNMOBS_MIN:
      g_value_set_double (value, mlnm->lnMobs_min);
      break;
	case PROP_LNMOBS_MAX:
      g_value_set_double (value, mlnm->lnMobs_max);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
nc_cluster_mass_lnnormal_init (NcClusterMassLnnormal *mlnm)
{
  mlnm->lnMobs_min = 0.0;
  mlnm->lnMobs_max = 0.0;
}

static void
nc_cluster_mass_lnnormal_finalize (GObject *object)
{

  /* Chain up : end */
  G_OBJECT_CLASS (nc_cluster_mass_lnnormal_parent_class)->finalize (object);
}

static void
nc_cluster_mass_lnnormal_class_init (NcClusterMassLnnormalClass *klass)
{
  GObjectClass* object_class = G_OBJECT_CLASS (klass);
  NcClusterMassClass *parent_class = NC_CLUSTER_MASS_CLASS (klass);

  parent_class->P              = &_nc_cluster_mass_lnnormal_p;
  parent_class->intP           = &_nc_cluster_mass_lnnormal_intp;
  parent_class->resample       = &_nc_cluster_mass_lnnormal_resample;
  parent_class->P_limits       = &_nc_cluster_mass_lnnormal_p_limits;
  parent_class->N_limits       = &_nc_cluster_mass_lnnormal_n_limits;
  parent_class->obs_len        = &_nc_cluster_mass_lnnormal_obs_len;
  parent_class->obs_params_len = &_nc_cluster_mass_lnnormal_obs_params_len;

  parent_class->impl = NC_CLUSTER_MASS_IMPL_ALL;

  object_class->finalize     =  &nc_cluster_mass_lnnormal_finalize;
  object_class->set_property = &_nc_cluster_mass_lnnormal_set_property;
  object_class->get_property = &_nc_cluster_mass_lnnormal_get_property;

  /**
   * NcClusterMassLnnormal:lnMobs_min:
   *
   * FIXME Set correct values (limits)
   */
  g_object_class_install_property (object_class,
                                   PROP_LNMOBS_MIN,
                                   g_param_spec_double ("lnMobs-min",
                                                        NULL,
                                                        "Minimum LnMobs",
                                                        11.0 * M_LN10, G_MAXDOUBLE, log (5.0) + 13.0 * M_LN10,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB));

  /**
   * NcClusterMassLnnormal:lnMobs_max:
   *
   * FIXME Set correct values (limits)
   */
  g_object_class_install_property (object_class,
                                   PROP_LNMOBS_MAX,
                                   g_param_spec_double ("lnMobs-max",
                                                        NULL,
                                                        "Maximum LnMobs",
                                                        11.0 * M_LN10, G_MAXDOUBLE, 16.0 * M_LN10,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB));

}
