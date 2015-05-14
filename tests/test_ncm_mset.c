/***************************************************************************
 *            test_ncm_mset.c
 *
 *  Wed May 13 15:19:36 2015
 *  Copyright  2015  Sandro Dias Pinto Vitenti
 *  <sandro@isoftware.com.br>
 ****************************************************************************/
/*
 * numcosmo
 * Copyright (C) Sandro Dias Pinto Vitenti 2015 <sandro@isoftware.com.br>
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
#undef GSL_RANGE_CHECK_OFF
#endif /* HAVE_CONFIG_H */
#include <numcosmo/numcosmo.h>

#include <math.h>
#include <glib.h>
#include <glib-object.h>

typedef struct _TestNcmMSet
{
  NcmMSet *mset;
  GPtrArray *ma;
  GArray *ma_destroyed;
} TestNcmMSet;

void test_ncm_mset_new (TestNcmMSet *test, gconstpointer pdata);
void test_ncm_mset_free (TestNcmMSet *test, gconstpointer pdata);

void test_ncm_mset_setpeek (TestNcmMSet *test, gconstpointer pdata);
void test_ncm_mset_setpospeek (TestNcmMSet *test, gconstpointer pdata);
void test_ncm_mset_pushpeek (TestNcmMSet *test, gconstpointer pdata);
void test_ncm_mset_fparams (TestNcmMSet *test, gconstpointer pdata);

void test_ncm_mset_traps (TestNcmMSet *test, gconstpointer pdata);
void test_ncm_mset_invalid_get (TestNcmMSet *test, gconstpointer pdata);

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  ncm_cfg_init ();
  ncm_cfg_enable_gsl_err_handler ();
  
  g_test_add ("/numcosmo/ncm_mset/setpeek", TestNcmMSet, NULL, 
              &test_ncm_mset_new, 
              &test_ncm_mset_setpeek, 
              &test_ncm_mset_free);

  g_test_add ("/numcosmo/ncm_mset/setpospeek", TestNcmMSet, NULL, 
              &test_ncm_mset_new, 
              &test_ncm_mset_setpospeek, 
              &test_ncm_mset_free);

  g_test_add ("/numcosmo/ncm_mset/pushpeek", TestNcmMSet, NULL, 
              &test_ncm_mset_new, 
              &test_ncm_mset_pushpeek, 
              &test_ncm_mset_free);

  g_test_add ("/numcosmo/ncm_mset/fparams", TestNcmMSet, NULL, 
              &test_ncm_mset_new, 
              &test_ncm_mset_fparams, 
              &test_ncm_mset_free);

  g_test_add ("/numcosmo/ncm_mset/traps", TestNcmMSet, NULL, 
              &test_ncm_mset_new, 
              &test_ncm_mset_traps, 
              &test_ncm_mset_free);
#if !((GLIB_MAJOR_VERSION == 2) && (GLIB_MINOR_VERSION < 38))
  g_test_add ("/numcosmo/ncm_mset/invalid/get/subprocess", TestNcmMSet, NULL, 
              &test_ncm_mset_new, 
              &test_ncm_mset_invalid_get, 
              &test_ncm_mset_free);
#endif 
  g_test_run ();
}

void
test_ncm_mset_new (TestNcmMSet *test, gconstpointer pdata)
{
  test->mset = ncm_mset_empty_new ();
  test->ma   = g_ptr_array_new ();
  test->ma_destroyed = g_array_new (FALSE, TRUE, sizeof (gboolean));

  g_assert (test->mset != NULL);
  g_assert (NCM_IS_MSET (test->mset));

  g_assert_cmpuint (ncm_mset_total_len (test->mset), ==, 0);

  {
    NcHICosmoLCDM *cosmo = nc_hicosmo_lcdm_new ();
    gboolean f = FALSE;
    
    ncm_mset_set (test->mset, NCM_MODEL (cosmo));
    ncm_mset_set (test->mset, NCM_MODEL (cosmo));
    ncm_mset_set_pos (test->mset, NCM_MODEL (cosmo), 1);

    g_ptr_array_add (test->ma, cosmo);
    g_array_append_val (test->ma_destroyed, f);

    nc_hicosmo_free (NC_HICOSMO (cosmo));
  }

  {
    NcDistance *dist = nc_distance_new (5.0);
    NcSNIADistCov *snia = nc_snia_dist_cov_new (dist, 4);
    gboolean f = FALSE;

    ncm_mset_set (test->mset, NCM_MODEL (snia));

    g_ptr_array_add (test->ma, snia);
    g_array_append_val (test->ma_destroyed, f);

    nc_snia_dist_cov_free (snia);
  }
}

void _set_destroyed (gpointer b) { gboolean *destroyed = b; *destroyed = TRUE; }

void
test_ncm_mset_free (TestNcmMSet *test, gconstpointer pdata)
{
  NcmMSet *mset = test->mset;
  gboolean destroyed = FALSE;
  gint i;
  
  g_object_set_data_full (G_OBJECT (mset), "test-destroy", &destroyed, _set_destroyed);

  g_assert_cmpuint (test->ma_destroyed->len, ==, test->ma->len);

  for (i = 0; i < test->ma_destroyed->len; i++)
  {
    NcmModel *model = g_ptr_array_index (test->ma, i);
    g_array_index (test->ma_destroyed, gboolean, i) = FALSE;
    g_object_set_data_full (G_OBJECT (model), "test-destroy", &g_array_index (test->ma_destroyed, gboolean, i), _set_destroyed);
  }

  ncm_mset_free (mset);
  g_assert (destroyed);
  
  for (i = 0; i < test->ma_destroyed->len; i++)
  {
    g_assert (g_array_index (test->ma_destroyed, gboolean, i));
  }

  g_ptr_array_unref (test->ma);
  g_array_unref (test->ma_destroyed);
}

void
test_ncm_mset_setpeek (TestNcmMSet *test, gconstpointer pdata)
{
  NcClusterMass *mass = nc_cluster_mass_new_from_name ("NcClusterMassLnnormal");
  gboolean f = FALSE;

  ncm_mset_set (test->mset, NCM_MODEL (mass));
  g_ptr_array_add (test->ma, mass);
  g_array_append_val (test->ma_destroyed, f);

  g_assert (ncm_mset_peek (test->mset, nc_cluster_mass_id ()) == NCM_MODEL (mass));
  
  nc_cluster_mass_free (mass);
}

void
test_ncm_mset_setpospeek (TestNcmMSet *test, gconstpointer pdata)
{
  NcClusterMass *mass = nc_cluster_mass_new_from_name ("NcClusterMassLnnormal");
  gboolean f = FALSE;

  ncm_mset_set_pos (test->mset, NCM_MODEL (mass), 5);
  g_ptr_array_add (test->ma, mass);
  g_array_append_val (test->ma_destroyed, f);

  g_assert (ncm_mset_peek_pos (test->mset, nc_cluster_mass_id (), 5) == NCM_MODEL (mass));
  
  nc_cluster_mass_free (mass);
}

void
test_ncm_mset_pushpeek (TestNcmMSet *test, gconstpointer pdata)
{
  NcClusterMass *mass = nc_cluster_mass_new_from_name ("NcClusterMassLnnormal");
  gboolean f = FALSE;

  ncm_mset_push (test->mset, NCM_MODEL (mass));
  ncm_mset_push (test->mset, NCM_MODEL (mass));
  g_ptr_array_add (test->ma, mass);
  g_array_append_val (test->ma_destroyed, f);

  g_assert (ncm_mset_peek (test->mset, nc_cluster_mass_id ()) == NCM_MODEL (mass));
  g_assert (ncm_mset_peek_pos (test->mset, nc_cluster_mass_id (), 1) == NCM_MODEL (mass));
  
  nc_cluster_mass_free (mass);
}

void
test_ncm_mset_fparams (TestNcmMSet *test, gconstpointer pdata)
{
  NcClusterMass *mass = nc_cluster_mass_new_from_name ("NcClusterMassLnnormal");
  NcClusterMass *benson = nc_cluster_mass_new_from_name ("NcClusterMassBenson");
  gboolean f = FALSE;

  ncm_mset_set_pos (test->mset, NCM_MODEL (mass), 10);
  
  ncm_mset_push (test->mset, NCM_MODEL (benson));
  ncm_mset_push (test->mset, NCM_MODEL (benson));
  ncm_mset_push (test->mset, NCM_MODEL (mass));
  ncm_mset_push (test->mset, NCM_MODEL (benson));

  ncm_mset_set_pos (test->mset, NCM_MODEL (mass), 1);

  g_ptr_array_add (test->ma, mass);
  g_array_append_val (test->ma_destroyed, f);

  g_ptr_array_add (test->ma, benson);
  g_array_append_val (test->ma_destroyed, f);

  ncm_mset_param_set_all_ftype (test->mset, NCM_PARAM_TYPE_FREE);
  ncm_mset_prepare_fparam_map (test->mset);

  g_assert_cmpuint (ncm_mset_total_len (test->mset), ==, ncm_mset_fparam_len (test->mset));

  ncm_mset_push (test->mset, NCM_MODEL (mass));
  g_assert_cmpuint (ncm_mset_total_len (test->mset), ==, ncm_mset_fparam_len (test->mset));

  ncm_mset_param_set_ftype (test->mset, NCM_MSET_MID (nc_cluster_mass_id (), 1), 0, NCM_PARAM_TYPE_FIXED);

  g_assert_cmpint (ncm_mset_param_get_ftype (test->mset, NCM_MSET_MID (nc_cluster_mass_id (), 1), 0), ==, NCM_PARAM_TYPE_FIXED);
  g_assert_cmpint (ncm_mset_param_get_ftype (test->mset, NCM_MSET_MID (nc_cluster_mass_id (), 2), 0), ==, NCM_PARAM_TYPE_FIXED);
  g_assert_cmpint (ncm_mset_param_get_ftype (test->mset, NCM_MSET_MID (nc_cluster_mass_id (), 4), 0), ==, NCM_PARAM_TYPE_FIXED);
  g_assert_cmpint (ncm_mset_param_get_ftype (test->mset, NCM_MSET_MID (nc_cluster_mass_id (), 10), 0), ==, NCM_PARAM_TYPE_FIXED);

  g_assert_cmpuint (ncm_mset_total_len (test->mset), ==, ncm_mset_fparam_len (test->mset) + 1);

  ncm_mset_param_set_all_ftype (test->mset, NCM_PARAM_TYPE_FIXED);
  g_assert_cmpuint (ncm_mset_fparam_len (test->mset), ==, 0);

  ncm_mset_param_set_ftype (test->mset, NCM_MSET_MID (nc_cluster_mass_id (), 1), 0, NCM_PARAM_TYPE_FREE);
  g_assert_cmpuint (ncm_mset_fparam_len (test->mset), ==, 1);

  {
    NcmMSetPIndex *pi = ncm_mset_fparam_get_pi (test->mset, 0);
    g_assert_cmpuint (pi->mid, ==, NCM_MSET_MID (nc_cluster_mass_id (), 1));
    g_assert_cmpuint (pi->pid, ==, 0);
  }

  ncm_mset_fparam_set (test->mset, 0, 123.505);
  ncm_assert_cmpdouble (ncm_mset_fparam_get (test->mset, 0), ==, 123.505);
  ncm_assert_cmpdouble (ncm_mset_param_get (test->mset, NCM_MSET_MID (nc_cluster_mass_id (), 1), 0), ==, 123.505);
  ncm_assert_cmpdouble (ncm_mset_param_get (test->mset, NCM_MSET_MID (nc_cluster_mass_id (), 2), 0), ==, 123.505);
  ncm_assert_cmpdouble (ncm_mset_param_get (test->mset, NCM_MSET_MID (nc_cluster_mass_id (), 4), 0), ==, 123.505);
  ncm_assert_cmpdouble (ncm_mset_param_get (test->mset, NCM_MSET_MID (nc_cluster_mass_id (), 10), 0), ==, 123.505);
  
  nc_cluster_mass_free (mass);
  nc_cluster_mass_free (benson);
}

void
test_ncm_mset_traps (TestNcmMSet *test, gconstpointer pdata)
{
#if !((GLIB_MAJOR_VERSION == 2) && (GLIB_MINOR_VERSION < 38))
  g_test_trap_subprocess ("/numcosmo/ncm_mset/invalid/get/subprocess", 0, 0);
  g_test_trap_assert_failed ();
#endif
}

void
test_ncm_mset_invalid_get (TestNcmMSet *test, gconstpointer pdata)
{
  g_assert (ncm_mset_get (test->mset, 34 * NCM_MSET_MAX_SUBMODEL + 5) != NULL);
}