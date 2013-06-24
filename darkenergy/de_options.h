/***************************************************************************
 *            de_options.h
 *
 *  Sat Apr 24 14:40:43 2010
 *  Copyright  2010  Sandro Dias Pinto Vitenti
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

typedef struct _NcDERunEntries NcDERunEntries;

/**
 * NcDERunEntries:
 *
 * FIXME
 */
struct _NcDERunEntries
{
  gchar *runconf;
  gchar *saverun;
};

#define NC_DE_RUN_ENTRIES {NULL, NULL}

typedef struct _NcDEModelEntries NcDEModelEntries;

/**
 * NcDEModelEntries:
 *
 * FIXME
 */
struct _NcDEModelEntries
{
  gchar *model_name;
  gboolean flat;
  gboolean pos_Omega_x;
  gboolean Omega_k;
  gboolean help_names;
};

#define NC_DE_MODEL_ENTRIES {"NcHICosmoDEXcdm", FALSE, FALSE, FALSE, FALSE}

typedef struct _NcDEDataSimpleEntries NcDEDataSimpleEntries;

/**
 * NcDEDataSimpleEntries:
 *
 * FIXME
 */
struct _NcDEDataSimpleEntries
{
  gchar *snia_id;
  gchar *snia_prop;
  gchar **bao_id;
  gchar *cmb_id;
  gchar **H_id;
  gchar **H_BAO_id;
  gchar *cluster_id;
  gboolean H0_Hst;
  gboolean BBN;
  gboolean BBN_Ob;
  gboolean snia_use_det;
};

#define NC_DE_DATA_SIMPLE_ENTRIES {NULL, NULL, NULL, NULL, NULL, NULL, NULL, FALSE, FALSE, FALSE, FALSE}

typedef struct _NcDEDataClusterEntries NcDEDataClusterEntries;

/**
 * NcDEDataNcusterEntries:
 *
 * FIXME
 */
struct _NcDEDataClusterEntries
{
  gchar *window_name;
  gchar *transfer_name;
  gchar *multiplicity_name;
  gchar *clusterm_ser;
  gchar *clusterz_ser;
  gint mf_ds_index;    /* refers to the multiplicity function */
  gboolean use_true_data;
  gboolean binned;
  gboolean binmass;
  gboolean use_Mobs_local; /* sigma_lnM varies with z and lnM. (matching catalog)*/
  gboolean use_selection; /* selection function = completeness / purity (matching catalog) */
  gdouble area_survey;
  gint n_bins;
  gchar **cata_file;
  gchar *save_cata;
  gboolean print_mass_function;
};

#define NC_DE_DATA_CLUSTER_ENTRIES {"NcWindowTophat", "NcTransferFuncEH", "NcMultiplicityFuncTinkerMean", "NcClusterMassNodist", "NcClusterRedshiftNodist", 0, FALSE, FALSE, FALSE, FALSE, FALSE, 5000.0, 10, NULL, NULL, FALSE}

typedef struct _NcDEFitEntries NcDEFitEntries;

/**
 * NcDEFitEntries:
 *
 * FIXME
 */
struct _NcDEFitEntries
{
  gchar *file_out;
  gboolean fit;
  gchar *fit_type;
  gchar *fit_diff;
  gchar *fit_algo;
  gint nsigma;
  gint nsigma_fisher;
  gint bidim_cr[2];
  gchar **onedim_cr;
  gint max_iter;
  gboolean resample;
  gint msg_level;
  gint montecarlo;
  gint mc_ni;
  gchar *fiducial;
  gboolean mc_data;
  gboolean fisher;
  gboolean kinematics_sigma;
  gdouble kinematics_z;
  gint kinematics_n;
  gboolean qspline_cp;
  gdouble qspline_cp_sigma;
  gboolean save_fisher;
  gboolean save_best_fit;
  gchar *save_mset;
};

#ifdef NUMCOSMO_HAVE_NLOPT
#define NC_DE_FIT_ENTRIES { NULL, FALSE, "nlopt",   "numdiff-forward", NULL, -1, -1, {-1, -1}, NULL, NCM_FIT_MAXITER, FALSE, NCM_FIT_RUN_MSGS_SIMPLE, -1, 0, NULL, FALSE, FALSE, FALSE, 1.0, 100, FALSE, 1.0, FALSE, FALSE, NULL}
#else
#define NC_DE_FIT_ENTRIES { NULL, FALSE, "gsl-mms", "numdiff-forward", NULL, -1, -1, {-1, -1}, NULL, NCM_FIT_MAXITER, FALSE, NCM_FIT_RUN_MSGS_SIMPLE, -1, 0, NULL, FALSE, FALSE, FALSE, 1.0, 100, FALSE, 1.0, FALSE, FALSE, NULL}
#endif

GOptionGroup *nc_de_opt_get_run_group (NcDERunEntries *de_run);
GOptionGroup *nc_de_opt_get_model_group (NcDEModelEntries *de_model, GOptionEntry **de_model_entries);
GOptionGroup *nc_de_opt_get_data_simple_group (NcDEDataSimpleEntries *de_data_simple, GOptionEntry **de_data_simple_entries);
GOptionGroup *nc_de_opt_get_data_cluster_group (NcDEDataClusterEntries *de_data_cluster, GOptionEntry **de_data_cluster_entries);
GOptionGroup *nc_de_opt_get_fit_group (NcDEFitEntries *de_fit_group, GOptionEntry **de_fit_entries);
