/***************************************************************************
 *            linear_cvodes.c
 *
 *  Thu Nov 12 12:55:40 2009
 *  Copyright  2009  Sandro Dias Pinto Vitenti
 *  <sandro@isoftware.com.br>
 ****************************************************************************/
/*
 * numcosmo
 * Copyright (C) Sandro Dias Pinto Vitenti 2012 <sandro@lapsandro>
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
#include "build_cfg.h"

#include "perturbations/linear.h"

#include <cvodes/cvodes_diag.h>
#include <cvodes/cvodes_band.h>
#include <cvodes/cvodes_bandpre.h>
#include <cvodes/cvodes_spbcgs.h>
#include <nvector/nvector_serial.h>

#ifdef HAVE_SUNDIALS_2_5_0
#  if SUNDIALS_BLAS_LAPACK == 1
#    include <cvodes/cvodes_lapack.h>      /* prototype for CVBand */
#  endif /* SUNDIALS_BLAS_LAPACK == 1 */
#elif defined (HAVE_SUNDIALS_2_6_0)
#  ifdef SUNDIALS_BLAS_LAPACK
#    include <cvodes/cvodes_lapack.h>      /* prototype for CVBand */
#  endif /* SUNDIALS_BLAS_LAPACK == 1 */
#endif /* HAVE_SUNDIALS_2_5_0 */

#include "linear_internal.h"

typedef struct _CVodesData
{
  gpointer cvode;
  gpointer cvode_stiff;
  gpointer cvode_nonstiff;
  gboolean malloc_nonstiff;
  gboolean malloc_stiff;
  gboolean sens_init;
  N_Vector yi;
  N_Vector y;
  N_Vector yQ;
  N_Vector yS;
  N_Vector abstol;
  gdouble *JD;
  gdouble *JL;
  gdouble *JU;
} CVodesData;

#define CVODES_DATA(a) ((CVodesData *)(a))

/* #define SIMUL_LOS_INT */

static gpointer cvodes_create (NcLinearPert *pert);
static void cvodes_init (NcLinearPert *pert);
static void cvodes_set_opts (NcLinearPert *pert);
static void cvodes_reset (NcLinearPert *pert);
static void cvodes_end_tight_coupling (NcLinearPert *pert);
static gboolean cvodes_evol_step (NcLinearPert *pert, gdouble g);
static gboolean cvodes_evol (NcLinearPert *pert, gdouble g);
static gboolean cvodes_update_los (NcLinearPert *pert);
static void cvodes_get_sources (NcLinearPert *pert, gdouble *S0, gdouble *S1, gdouble *S2);
static void cvodes_free (NcLinearPert *pert);
static void cvodes_print_stats (NcLinearPert *pert);
static gdouble cvodes_get_z (NcLinearPert *pert);
static gdouble cvodes_get_phi (NcLinearPert *pert);
static gdouble cvodes_get_c0 (NcLinearPert *pert);
static gdouble cvodes_get_b0 (NcLinearPert *pert);
static gdouble cvodes_get_c1 (NcLinearPert *pert);
static gdouble cvodes_get_b1 (NcLinearPert *pert);
static gdouble cvodes_get_theta2 (NcLinearPert *pert);
static gdouble cvodes_get (NcLinearPert *pert, guint n);
static gdouble cvodes_get_theta (NcLinearPert *pert, guint n);
static gdouble cvodes_get_theta_p (NcLinearPert *pert, guint n);
static gdouble cvodes_get_los_theta (NcLinearPert *pert, guint n);
static void cvodes_print_all (NcLinearPert *pert);

static NcLinearPertOdeSolver _cvodes_solver = {
  &cvodes_create,
  &cvodes_init,
  &cvodes_set_opts,
  &cvodes_reset,
  &cvodes_evol_step,
  &cvodes_evol,
  &cvodes_update_los,
  &cvodes_get_sources,
  &cvodes_free,
  &cvodes_print_stats,
  &cvodes_get_z,
  &cvodes_get_phi,
  &cvodes_get_c0,
  &cvodes_get_b0,
  &cvodes_get_c1,
  &cvodes_get_b1,
  &cvodes_get,
  &cvodes_get_theta,
  &cvodes_get_theta_p,
  &cvodes_get_los_theta,
	&cvodes_print_all,
  NULL
};
NcLinearPertOdeSolver *cvodes_solver = &_cvodes_solver;

static gint cvodes_step (realtype lambda, N_Vector y, N_Vector ydot, gpointer user_data);
static gint cvodes_band_J (_NCM_SUNDIALS_INT_TYPE N, _NCM_SUNDIALS_INT_TYPE mupper, _NCM_SUNDIALS_INT_TYPE mlower, realtype lambda, N_Vector y, N_Vector fy, DlsMat J, gpointer user_data, N_Vector tmp1, N_Vector tmp2, N_Vector tmp3);
static gint cvodes_Jv (N_Vector v, N_Vector Jv, realtype lambda, N_Vector y, N_Vector fy, gpointer user_data, N_Vector tmp);
static gint cvodes_Mz_r (realtype lambda, N_Vector y, N_Vector fy, N_Vector r, N_Vector z, realtype gamma, realtype delta, gint lr, gpointer user_data, N_Vector tmp);

#ifdef SIMUL_LOS_INT
static gint cvodes_lineofsight (realtype lambda, N_Vector y, N_Vector yQdot, gpointer user_data);
#endif

static gpointer
cvodes_create (NcLinearPert *pert)
{
  CVodesData *data = g_slice_new (CVodesData);

  data->malloc_nonstiff = FALSE;
  data->malloc_stiff = FALSE;
  data->sens_init = FALSE;

  if (FALSE)
  {
    data->yi = N_VNew_Serial(pert->sys_size);
    data->y = N_VNew_Serial(pert->sys_size);
    data->yS = N_VNew_Serial(pert->sys_size);

#ifdef SIMUL_LOS_INT
    data->yQ = N_VNew_Serial(pert->los_table->len);
#endif
    data->abstol = N_VNew_Serial(pert->sys_size);
  }

  data->cvode_nonstiff = CVodeCreate(CV_ADAMS, CV_FUNCTIONAL);
  NCM_CVODE_CHECK ((void *)data->cvode_nonstiff, "CVodeCreate", 0, NULL);

  data->cvode_stiff = CVodeCreate(CV_BDF, CV_NEWTON);
  NCM_CVODE_CHECK ((void *)data->cvode_stiff, "CVodeCreate", 0, NULL);

  data->cvode = data->cvode_stiff;

  data->JD = g_new (gdouble, pert->sys_size);
  data->JL = g_new (gdouble, pert->sys_size - 1);
  data->JU = g_new (gdouble, pert->sys_size - 1);

  return data;
}

static void
cvodes_set_opts (NcLinearPert *pert)
{
  gint flag, i;
  CVodesData *data = CVODES_DATA (pert->solver->data);
//  flag = CVodeSetMinStep (pert->cvode, 1e-12);
//  NCM_CVODE_CHECK (&flag, "CVodeSetMinStep", 1, FALSE);

  if (pert->pws->tight_coupling)
  {
    N_VConst (pert->tc_abstol, data->abstol);
    for (i = 0; i <= NC_PERT_THETA_P2; i++)
      NV_Ith_S(data->abstol, i) = 0.0;
    NV_Ith_S(data->abstol, NC_PERT_T) = pert->tc_abstol;

    flag = CVodeSVtolerances (data->cvode, pert->tc_reltol, data->abstol);
    NCM_CVODE_CHECK (&flag, "CVodeSVtolerances", 1,);
  }
  else
  {
    N_VConst (pert->abstol, data->abstol);
    for (i = 0; i <= NC_PERT_THETA_P2; i++)
      NV_Ith_S(data->abstol, i) = 0.0;

    flag = CVodeSVtolerances (data->cvode, pert->reltol, data->abstol);
    NCM_CVODE_CHECK (&flag, "CVodeSVtolerances", 1,);
  }

  flag = CVodeSetMaxNumSteps(data->cvode, 1000000);
  NCM_CVODE_CHECK (&flag, "CVodeSetMaxNumSteps", 1,);

  flag = CVodeSetUserData (data->cvode, pert);
  NCM_CVODE_CHECK (&flag, "CVodeSetUserData", 1,);

  if (FALSE)
  {
    flag = CVDiag (data->cvode);
    NCM_CVODE_CHECK (&flag, "CVDiag", 1,);
  }
  else if (FALSE)
  {
    /* Call CVSpgmr to specify the linear solver CVSPGMR
     with left preconditioning and the maximum Krylov dimension maxl */
//    flag = CVSpgmr (data->cvode, PREC_LEFT, 0);
    flag = CVSpbcg (data->cvode, PREC_LEFT, 0);
//    flag = CVSptfqmr (data->cvode, PREC_LEFT, 0);
    NCM_CVODE_CHECK (&flag, "CVSpgmr", 1,);

    if (FALSE)
    {
      /* Call CVBandPreInit to initialize band preconditioner */
      flag = CVBandPrecInit (data->cvode, pert->sys_size, 6, 6);
      NCM_CVODE_CHECK (&flag, "CVBandPrecInit", 1,);
    }
    else
    {
      /* perturbations_step_Mz_r */
			flag = CVSpilsSetJacTimesVecFn (data->cvode, &cvodes_Jv);
			NCM_CVODE_CHECK (&flag, "CVSpilsSetJacTimesVecFn", 1,);

			flag = CVSpilsSetPreconditioner (data->cvode, NULL, cvodes_Mz_r);
      NCM_CVODE_CHECK (&flag, "CVSpilsSetPreconditioner", 1,);
    }
  }
  else
  {
		if (pert->pws->tight_coupling)
			flag = CVBand (data->cvode, pert->sys_size, 5, 6);
		else
			flag = CVBand (data->cvode, pert->sys_size, 4, 4);

		NCM_CVODE_CHECK (&flag, "CVBand", 1,);

    if (FALSE)
    {
      flag = CVDlsSetBandJacFn (data->cvode, cvodes_band_J);
      NCM_CVODE_CHECK (&flag, "CVDlsSetBandJacFn", 1,);
    }
  }

  flag = CVodeSetStabLimDet (data->cvode, FALSE);
  NCM_CVODE_CHECK (&flag, "CVodeSetStabLimDet", 1,);

  flag = CVodeSetStopTime (data->cvode, pert->lambdaf);
  NCM_CVODE_CHECK (&flag, "CVodeSetStopTime", 1,);

  if (FALSE)
  {
    N_VConst (0.0, data->yS);
    CVSensRhsFn fS = NULL;
    int flag_cv = CV_STAGGERED;

    if (data->sens_init)
    {
      flag = CVodeSensReInit (data->cvode, flag_cv, &data->yS);
      NCM_CVODE_CHECK (&flag, "CVodeSensInit", 1,);
    }
    else
    {
      flag = CVodeSensInit (data->cvode, 1, flag_cv, fS, &data->yS);
      NCM_CVODE_CHECK (&flag, "CVodeSensInit", 1,);
    }

    flag = CVodeSensEEtolerances (data->cvode);
    NCM_CVODE_CHECK (&flag, "CVodeSensEEtolerances", 1,);

    flag = CVodeSetSensParams (data->cvode, &pert->pws->k, NULL, NULL);
    NCM_CVODE_CHECK (&flag, "CVodeSetSensParams", 1,);

    data->sens_init = TRUE;
  }

#ifdef SIMUL_LOS_INT
  flag = CVodeSetQuadErrCon (data->cvode, FALSE);
  NCM_CVODE_CHECK (&flag, "CVodeSetQuadErrCon", 1,);

  flag = CVodeQuadSStolerances (data->cvode, 1e-7, 1e-120);
  NCM_CVODE_CHECK (&flag, "CVodeQuadSStolerances", 1,);
#endif

  return;
}

static void
cvodes_reset (NcLinearPert *pert)
{
  gint flag;
  CVodesData *data = CVODES_DATA (pert->solver->data);

  if (((data->cvode == data->cvode_stiff) ? (!data->malloc_stiff) : (!data->malloc_nonstiff)))
  {
    flag = CVodeInit (data->cvode, &cvodes_step, pert->pws->lambda, data->y);
    NCM_CVODE_CHECK (&flag, "CVodeMalloc", 1,);
#ifdef SIMUL_LOS_INT
    flag = CVodeQuadInit (data->cvode, &cvodes_lineofsight, data->yQ);
    NCM_CVODE_CHECK (&flag, "CVodeQuadInit", 1,);
#endif
    (data->cvode == data->cvode_stiff) ? (data->malloc_stiff = TRUE) : (data->malloc_nonstiff = TRUE);
  }
  else
  {
    flag = CVodeReInit (data->cvode, pert->pws->lambda, data->y);
    NCM_CVODE_CHECK (&flag, "CVodeReInit", 1,);
#ifdef SIMUL_LOS_INT
    CVodeQuadReInit (data->cvode, data->yQ);
    NCM_CVODE_CHECK (&flag, "CVodeQuadReInit", 1,);
#endif
  }

  cvodes_set_opts (pert);
  return;
}

static gboolean
cvodes_update_los (NcLinearPert *pert)
{
  CVodesData *data = CVODES_DATA (pert->solver->data);
  gint flag;
  gdouble gi;
  flag = CVodeGetQuad(data->cvode, &gi, data->yQ);
  NCM_CVODE_CHECK (&flag, "CVodeGetQuad", 1, FALSE);
  return TRUE;
}

static gboolean
cvodes_evol_step (NcLinearPert *pert, gdouble lambda)
{
  gint flag;
  gdouble lambdai;
  CVodesData *data = CVODES_DATA (pert->solver->data);

  flag = CVode (data->cvode, lambda, data->y, &lambdai, CV_ONE_STEP);
  NCM_CVODE_CHECK (&flag, "cvodes_evol_step", 1, FALSE);

  pert->pws->dlambda = lambdai - pert->pws->lambda_int;
  pert->pws->lambda_int = lambdai;
  pert->pws->lambda = lambdai;

  if (pert->pws->tight_coupling && pert->pws->tight_coupling_end)
    cvodes_end_tight_coupling (pert);

  if (lambdai == lambda)
    return TRUE;
  else
    return FALSE;
}

static gboolean
cvodes_evol (NcLinearPert *pert, gdouble lambda)
{
  gint flag;
  gdouble lambdai = 0.0;
	gdouble last_print = 0.0;
	//N_Vector ew = N_VNew_Serial (pert->sys_size);
	//N_Vector ele = N_VNew_Serial (pert->sys_size);
  CVodesData *data = CVODES_DATA (pert->solver->data);

  while (lambda > pert->pws->lambda_int)
  {
    flag = CVode (data->cvode, lambda, data->y, &lambdai, CV_ONE_STEP);
    NCM_CVODE_CHECK (&flag, "cvodes_evol[evol]", 1, FALSE);
    pert->pws->dlambda = lambdai - pert->pws->lambda_int;
    pert->pws->lambda_int = lambdai;
    pert->pws->lambda = lambdai;
		if (FALSE || (FALSE && fabs (lambdai / last_print) > 1.001))
		{
			cvodes_print_all (pert);
			last_print = lambdai;
		}

		if (FALSE)
		{
			guint i;
			//CVodeGetErrWeights (data->cvode, ew);
			//CVodeGetEstLocalErrors (data->cvode, ele);
			//cvodes_print_stats (pert);
			printf ("%.15g %d %d ", lambdai, pert->pws->tight_coupling, pert->pws->tight_coupling_end);
			for (i = 0; i < pert->sys_size; i++)
				printf ("%.15g ", NV_Ith_S (data->y, i));
			//for (i = 0; i < pert->sys_size; i++)
			//i = 9;
				//printf ("%.15g %.15g %.15g %.15g ", NV_Ith_S (data->y, i), NV_Ith_S (ew, i),  NV_Ith_S (ele, i), NV_Ith_S (ew, i) * NV_Ith_S (ele, i));
			//i = 10;
				//printf ("%.15g %.15g %.15g %.15g ", NV_Ith_S (data->y, i), NV_Ith_S (ew, i),  NV_Ith_S (ele, i), NV_Ith_S (ew, i) * NV_Ith_S (ele, i));
			printf ("\n");
		}


    if (flag == CV_TSTOP_RETURN)
      break;
    if (pert->pws->tight_coupling && pert->pws->tight_coupling_end)
    {
      if (!(lambda > pert->pws->lambda_int))
      {
        flag = CVodeGetDky (data->cvode, lambda, 0, data->y);
        NCM_CVODE_CHECK (&flag, "cvodes_evol[interp]", 1, FALSE);
        pert->pws->lambda = lambda;
      }
      cvodes_end_tight_coupling (pert);
      if (!(lambda > pert->pws->lambda_int))
        return TRUE;
    }
  }

  if (lambda == pert->pws->lambda)
    return TRUE;
  else if ((lambda <= pert->pws->lambda_int) && (lambda > (pert->pws->lambda_int - pert->pws->dlambda)))
  {
    flag = CVodeGetDky (data->cvode, lambda, 0, data->y);
    NCM_CVODE_CHECK (&flag, "cvodes_evol[interp]", 1, FALSE);
    pert->pws->lambda = lambda;
  }
  else
    g_error ("# cvodes_evol cannot evolve backwards");

#ifdef SIMUL_LOS_INT
  flag = CVodeGetQuad(data->cvode, &gi, data->yQ);
  NCM_CVODE_CHECK (&flag, "CVodeGetQuad", 1, FALSE);
#endif

  return TRUE;
}

static void
cvodes_free (NcLinearPert *pert)
{
  CVodesData *data = CVODES_DATA (pert->solver->data);
  CVodeFree (&data->cvode_stiff);
  CVodeFree (&data->cvode_nonstiff);

  g_free (data->JD);
  g_free (data->JL);
  g_free (data->JU);

  g_slice_free (CVodesData, data);
}

static void
cvodes_print_stats (NcLinearPert *pert)
{
  CVodesData *data = CVODES_DATA (pert->solver->data);
  ncm_util_cvode_print_stats (data->cvode);
}

#define LINEAR_VECTOR_PREPARE N_Vector y = CVODES_DATA(pert->solver->data)->y
#define LINEAR_VECTOR_SET_ALL(v,c,n) N_VConst (c, v)
#define LINEAR_VEC_COMP(v,i) NV_Ith_S(v,i)
#define LINEAR_MATRIX_E(M,i,j) BAND_ELEM (M,i,j)
#define LINEAR_VEC_ABSTOL(pert) (CVODES_DATA(pert->solver->data)->abstol)
#define LINEAR_VEC_LOS_THETA(pert) (CVODES_DATA(pert->solver->data)->yQ)
#define LINEAR_STEP_RET gint
#define LINEAR_NAME_SUFFIX(base) cvodes_##base
#define LINEAR_STEP_PARAMS realtype lambda, N_Vector y, N_Vector ydot, gpointer user_data
#define LINEAR_JAC_PARAMS _NCM_SUNDIALS_INT_TYPE N, _NCM_SUNDIALS_INT_TYPE mupper, _NCM_SUNDIALS_INT_TYPE mlower, realtype lambda, N_Vector y, N_Vector fy, DlsMat J, gpointer user_data, N_Vector tmp1, N_Vector tmp2, N_Vector tmp3
#define LINEAR_STEP_RET_VAL return 0
#define LINEAR_JAC_UNUSED G_STMT_START { NCM_UNUSED (N); NCM_UNUSED (mupper); NCM_UNUSED (mlower); NCM_UNUSED (y); NCM_UNUSED (fy); NCM_UNUSED (tmp1); NCM_UNUSED (tmp2); NCM_UNUSED (tmp3); } G_STMT_END 

#include "linear_generic.c"

static gint
cvodes_Jv (N_Vector v, N_Vector Jv, realtype lambda, N_Vector y, N_Vector fy, gpointer user_data, N_Vector tmp)
{
  NcLinearPert *pert = (NcLinearPert *)user_data;
  gboolean tight_coupling = pert->pws->tight_coupling;
  gboolean tight_coupling_end = pert->pws->tight_coupling_end;
  gint ret;

  NCM_UNUSED (y);
  NCM_UNUSED (fy);
  NCM_UNUSED (tmp);

  ret = cvodes_step (lambda, v, Jv, user_data);
  pert->pws->tight_coupling = tight_coupling;
  pert->pws->tight_coupling_end = tight_coupling_end;

  return ret;
}

static gint
cvodes_Mz_r (realtype lambda, N_Vector yo, N_Vector fy, N_Vector r, N_Vector z, realtype gamma, realtype delta, gint lr, gpointer user_data, N_Vector tmp)
{
  NcLinearPert *pert = (NcLinearPert *)user_data;
  gboolean tight_coupling; /* = pert->pws->tight_coupling;*/
  gboolean tight_coupling_end; /* = pert->pws->tight_coupling_end; */
  N_Vector a, b, c;
  guint i;

  NCM_UNUSED (yo);
  NCM_UNUSED (fy);
  NCM_UNUSED (delta);
  NCM_UNUSED (lr);
  
  a = tmp;
  b = z;
  N_VAddConst (r, 0.0, b);

  while (TRUE)
  {
    cvodes_step (lambda, b, a, user_data);
    for (i = 0; i < pert->sys_size; i++)
    {
      NV_Ith_S (b, i) = gamma * NV_Ith_S (a, i) + NV_Ith_S (r, i);
    }

    printf ("%.15g * %.15g + %.15g = %.15g\n", gamma, NV_Ith_S (a, 0), NV_Ith_S (r, 0), NV_Ith_S (b, 0));
    c = a; a = b; b = c;
  }


  pert->pws->tight_coupling = tight_coupling;
  pert->pws->tight_coupling_end = tight_coupling_end;

  return 0;
}

#ifdef SIMUL_LOS_INT
static gint
cvodes_lineofsight (realtype lambda, N_Vector y, N_Vector yQdot, gpointer user_data)
{
  NcLinearPert *pert = (NcLinearPert *)user_data;
  NcParams *cp = pert->params;
  const gdouble x = exp (-g + pert->g0);
  const gdouble taubar = nc_recomb_dtau_dlambda (pert->recomb, lambda);
  gdouble kdeta;
  gdouble opt = nc_recomb_tau (pert->recomb, pert->cosmo, lambda);
  gdouble tau_log_abs_taubar = -opt + log(fabs(taubar));

  if (tau_log_abs_taubar > GSL_LOG_DBL_MIN + 0.01)
  {
    gint i;
    const gdouble Omega_r0 = params->model->Omega_r0.f (params, NC_FUNCTION_CONST);
    const gdouble Omega_m0 = params->model->Omega_m0.f (params, NC_FUNCTION_CONST); /* FIXME */
    const gdouble Omega_b0 = params->model->Omega_b0.f (params, NC_FUNCTION_CONST);
    const gdouble Omega_c0 = Omega_m0 - Omega_b0;
    const gdouble x2 = x*x;
    const gdouble x3 = x2*x;
    const gdouble k = pert->pws->k;
    const gdouble k2 = k*k;
    const gdouble E2 = params->model->E2.f (params, x-1.0);
    const gdouble E = sqrt(E2);
    const gdouble k_E = k / E;
    const gdouble kx_E = x * k_E;
    const gdouble kx_3E = kx_E / 3.0;
    const gdouble k2x_3E = k * kx_3E;
    const gdouble k2x_3E2 = k2x_3E / E;
    const gdouble k2x2_3E2 = x * k2x_3E2;
    const gdouble dErm2_dx = (3.0 * Omega_m0 * x2 + 4.0 * Omega_r0 * x3);
    const gdouble psi = -PHI - 12.0 * x2 / k2 * Omega_r0 * THETA2;
    const gdouble PI = THETA2 + THETA_P0 + THETA_P2;
    const gdouble exp_tau = exp (-opt);
    const gdouble taubar_exp_tau = GSL_SIGN(taubar) * exp(tau_log_abs_taubar);
    const gdouble R0 = 4.0 * Omega_r0 / (3.0 * Omega_b0);
    const gdouble R = R0 * x;
    gdouble dphi, b1, theta0;

    if (pert->pws->tight_coupling)
    {
      dphi = psi - k2x2_3E2 * PHI - x / (2.0 * E2) *
      (
        dErm2_dx * (PHI - C0)
        -(3.0 * Omega_b0 * dB0 * x2 + 4.0 * Omega_r0 * x3 * dTHETA0)
        );
      theta0 = dTHETA0 + (C0 - PHI);
      b1 = (R * (U - T)) / (R + 1.0) + V - kx_3E * (C0 - PHI);
    }
    else
    {
      dphi = psi - k2x2_3E2 * PHI - x / (2.0 * E2) *
      (
        dErm2_dx * PHI
        -(3.0 * (Omega_c0 * C0 * x2 + Omega_b0 * B0 * x2) + 4.0 * Omega_r0 * x3 * THETA0)
        );
      theta0 = THETA0 - PHI;
      b1 = B1;
    }

    kdeta = pert->pws->k * (pert->eta0 - nc_scale_factor_t_x (pert->a, x));
    for (i = 0; i < pert->los_table->len; i++)
    {
      guint l = g_array_index(pert->los_table, guint, i);
      gdouble jl, jlp1, djl, d2jl;
      gdouble kdeta2 = kdeta * kdeta;
      NcmSpline *jl_spline = ncm_calc_spherical_bessel_spline (l, 4000.0);
      NcmSpline *jlp1_spline = ncm_calc_spherical_bessel_spline (l+1, 4000.0);

      if (kdeta2 / (6.0 + 4.0 * l) < 1e-8 && TRUE)
      {
        const gdouble log_pi_2 = log (sqrt(M_PI) / 2.0);
        gdouble lga = lgamma (3.0/2.0 + l);
        gdouble lga_p1 = lga + log (3.0/2.0 + l);
        gdouble log_x_2 = log (kdeta / 2.0);
        gdouble logjl = l * log_x_2 - lga + log_pi_2;
        gdouble logjlp1 = (l + 1.0) * log_x_2 - lga_p1 + log_pi_2;
        jl = (logjl < GSL_LOG_DBL_MIN) ? 0.0 : exp (logjl);
        jlp1 = (logjlp1 < GSL_LOG_DBL_MIN) ? 0.0 : exp (logjlp1);
      }
      else
      {
//        jl = gsl_sf_bessel_jl (l, kdeta);
//        jlp1 = gsl_sf_bessel_jl (l+1, kdeta);
        jl = ncm_spline_eval (jl_spline, kdeta);
        jlp1 = ncm_spline_eval (jlp1_spline, kdeta);
      }

      djl = kdeta != 0 ? l * jl / kdeta - jlp1 : 0.0;
      d2jl = kdeta != 0 ? ((l * (l-1.0) - kdeta2) * jl + 2.0 * kdeta * jlp1) / kdeta2 : 0.0;

      //printf ("%d %.15g %.15g %.15g %.15g\n", l, kdeta, jl, djl, d2jl);
      //printf ("[%d] %g %g | %g %g\n", l, jl, gsl_sf_bessel_jl (l, kdeta), jlp1, gsl_sf_bessel_jl (l+1, kdeta));
      NV_Ith_S(yQdot, i) =
        -exp_tau * (dphi * jl - kx_E * psi * djl)
        -taubar_exp_tau * (theta0 * jl + 3.0 * b1 * djl + PI * (3.0 * d2jl + jl) / 4.0);
      //printf ("%d %g\n", l, NV_Ith_S(yQdot, i));
    }
  }
  else
    N_VConst (0.0, yQdot);

  return 0;
}
#endif

#undef LINEAR_VECTOR_PREPARE
#undef LINEAR_VECTOR_SET_ALL
#undef LINEAR_VEC_COMP
#undef LINEAR_STEP_RET
#undef LINEAR_NAME_SUFFIX
#undef LINEAR_STEP_PARAMS
#undef LINEAR_STEP_RET_VAL
