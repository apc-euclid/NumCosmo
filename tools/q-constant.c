/***************************************************************************
 *            q-constant.c
 *
 *  Wed Jul 11 15:00:29 2007
 *  Copyright  2007  Sandro Dias Pinto Vitenti
 *  <sandro@isoftware.com.br>
 ****************************************************************************/
/*
 * numcosmo
 * Copyright (C) Sandro Dias Pinto Vitenti 2012 <sandro@isoftware.com.br>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */
#include <numcosmo/numcosmo.h>

#include <stdio.h>
#include <glib.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_statistics_double.h>

void test_series();

static gdouble z = 0.0;
static gdouble interval = 0.4;
static gint max_iter = 10000;
static gint snia_id = 0;
static gint max_snia = 100000;
static gboolean resample = FALSE;
static gboolean least_squares = FALSE;
static gboolean multimin = FALSE;
static gboolean print_data = FALSE;
static gboolean print_E = FALSE;
static gboolean verbose = FALSE;


static GOptionEntry entries[] =
{
  { "redshift",      'z', 0, G_OPTION_ARG_DOUBLE, &z,             "The initial redshift", NULL },
  { "interval",      'i', 0, G_OPTION_ARG_DOUBLE, &interval,      "The redshift interval size", NULL },
  { "max-iter",      'm', 0, G_OPTION_ARG_INT,    &max_iter,      "Max number of iterations used by the minimization algorithms", NULL },
  { "sample-id",     's', 0, G_OPTION_ARG_INT,    &snia_id,       "ID of the sample to use", NULL },
  { "resample",      'r', 0, G_OPTION_ARG_NONE,   &resample,      "Resample using LCDM (0.30, 0.70)", NULL },
  { "max-snia",      'n', 0, G_OPTION_ARG_INT,    &max_snia,      "Max number of SN Ia from the sample", NULL },
  { "least-squares", 'L', 0, G_OPTION_ARG_NONE,   &least_squares, "Use the least squares algorithm fitting H_0 also", NULL },
  { "multimin",      'M', 0, G_OPTION_ARG_NONE,   &multimin,      "Use the multimin algorithms marginalizing over H0+M", NULL },
  { "print-data",    'd', 0, G_OPTION_ARG_NONE,   &print_data,    "Print the fitted data", NULL },
  { "print-E",       'E', 0, G_OPTION_ARG_NONE,   &print_E,       "Print values of E in the interval", NULL },
  { "verbose",       'v', 0, G_OPTION_ARG_NONE,   &verbose,       "Be verbose", NULL },
  { NULL }
};

#define RESAMPLE_OMEGA_M 0.30
#define RESAMPLE_OMEGA_LAMBDA (1.0 - RESAMPLE_OMEGA_M)
#define RESAMPLE_OMEGA -1.0

gint
main (gint argc, gchar *argv[])
{
  GError *error = NULL;
  GOptionContext *context;
  NcmDataset *dset;
  NcmLikelihood *lh;
  NcmFit *fit = NULL;
  NcHICosmoQConst *qconst;
  NcHICosmoDEXcdm *xcdm;
  NcDistance *dist;
  NcmMSet *mset, *mset_xcdm;

  ncm_cfg_init ();

  context = g_option_context_new ("- test the q constant model");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_parse (context, &argc, &argv, &error);

  qconst = nc_hicosmo_qconst_new ();
  xcdm = nc_hicosmo_de_xcdm_new ();
  mset = ncm_mset_new (NCM_MODEL (qconst), NULL);
  mset_xcdm = ncm_mset_new (NCM_MODEL (xcdm), NULL);
  dist = nc_distance_new (2.0);

  ncm_model_param_set (NCM_MODEL (xcdm), NC_HICOSMO_DE_H0, ncm_c_hubble_cte_hst ());
  ncm_model_param_set (NCM_MODEL (xcdm), NC_HICOSMO_DE_OMEGA_C, RESAMPLE_OMEGA_M);
  ncm_model_param_set (NCM_MODEL (xcdm), NC_HICOSMO_DE_OMEGA_X, RESAMPLE_OMEGA_LAMBDA);
  ncm_model_param_set (NCM_MODEL (xcdm), NC_HICOSMO_DE_XCDM_W, RESAMPLE_OMEGA);

  dset = ncm_dataset_new ();
  if (snia_id != -1)
  {
    NcmData *snia = nc_data_dist_mu_new (dist, snia_id);
    ncm_dataset_append_data (dset, snia);
    ncm_data_free (snia);
  }

  if (resample)
    ncm_dataset_resample (dset, mset_xcdm);

  lh = ncm_likelihood_new (dset);

#define SIM_NUM 20000
  if (least_squares)
  {
    gint i;
    gsl_vector *gen_q_0 = gsl_vector_alloc (SIM_NUM);
    fit = ncm_fit_new (NCM_FIT_TYPE_GSL_LS, NULL, lh, mset, NCM_FIT_GRAD_ANALYTICAL);
    if (!ncm_fit_run (fit, verbose))
      g_error ("Fail to fit, use different initial conditions");
    ncm_fit_log_info (fit);
    ncm_fit_numdiff_m2lnL_covar (fit);
    ncm_fit_log_covar (fit);

    if (FALSE)
    {
      for (i = 0; i < SIM_NUM; i++)
      {
        ncm_dataset_resample (lh->dset, mset_xcdm);
        ncm_fit_log_info (fit);
        ncm_fit_numdiff_m2lnL_covar (fit);
        ncm_fit_log_covar (fit);
        gsl_vector_set (gen_q_0, i, ncm_mset_param_get (fit->mset, NC_HICOSMO_ID, NC_HICOSMO_QCONST_Q));
        if ( i%10 == 0 )
        {
          printf ("# sample size = %d\n", i+1);
          printf ("q: meam = %g, sigma = %g\n", gsl_stats_mean (gen_q_0->data, gen_q_0->stride, i+1),
                  gsl_stats_sd (gen_q_0->data, gen_q_0->stride, i+1));
          fflush (stdout);
        }
      }
      printf ("# sample size = %d\n", i);
      printf ("q: meam = %g, sigma = %g\n", gsl_stats_mean (gen_q_0->data, gen_q_0->stride, i),
              gsl_stats_sd (gen_q_0->data, gen_q_0->stride, i));
    }
  }

  if (multimin)
  {
    if (z == 0.0 || TRUE)
    {
      ncm_model_param_set (NCM_MODEL (qconst), NC_HICOSMO_QCONST_OMEGA_T, 1.0);
      ncm_mset_param_set_ftype (mset, NC_HICOSMO_ID, NC_HICOSMO_QCONST_OMEGA_T, NCM_PARAM_TYPE_FIXED);
    }

    fit = ncm_fit_new (NCM_FIT_TYPE_GSL_LS, NULL, lh, mset, NCM_FIT_GRAD_ANALYTICAL);

    if (!ncm_fit_run (fit, verbose))
      g_error ("Fail to fit, use different initial conditions");
  }

  if (print_E)
  {
    gdouble dz[] = {0.09, 0.17, 0.27, 0.4, 0.88, 1.3, 1.43, 1.53, 1.75};
    gint i;
    gint n = sizeof (dz)/sizeof(gdouble);
    gdouble E = ncm_mset_param_get (fit->mset, NC_HICOSMO_ID, NC_HICOSMO_QCONST_E);
    for (i = 0; i < n && dz[i] <= interval; i++)
    {
      gdouble dE = nc_hicosmo_qlinear_dE (dz[i], z, ncm_mset_param_get (fit->mset, NC_HICOSMO_ID, NC_HICOSMO_QCONST_Q), 0.0);
      printf ("\t%g\t%g\t%g\n", dz[i], E * dE, E * dE * ncm_c_hubble_cte_wmap ());
    }
  }

  ncm_model_free (NCM_MODEL (qconst));
  ncm_model_free (NCM_MODEL (xcdm));
  ncm_mset_free (mset);
  ncm_mset_free (mset_xcdm);
  ncm_likelihood_free (lh);
  ncm_dataset_free (dset);
  return 0;
}
