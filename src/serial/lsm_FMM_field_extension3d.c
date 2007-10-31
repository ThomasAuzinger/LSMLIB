/*
 * File:        lsm_FMM_3d.c
 * Copyright:   (c) 2005-2006 Kevin T. Chu
 * Revision:    $Revision: 1.2 $
 * Modified:    $Date: 2006/08/13 13:30:52 $
 * Description: Implementation of 3D Fast Marching Method 
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>
#include "FMM_Core.h"
#include "FMM_Heap.h"
#include "lsm_fast_marching_method.h"


/*===================== lsm_FMM_3d Data Structures ====================*/
struct FMM_FieldData {
  double *phi;                 /* original level set function (input) */
  double *distance_function;   /* distance function (output)          */

  int num_extension_fields;    /* number of extension fields          */
  double **source_fields;      /* source fields to extend off of zero */
                               /* level set (input)                   */
  double **extension_fields;   /* computed extension field (output)   */
};


/*======================== lsm_FMM_3d Constants =========================*/
#define LSM_FMM_3D_NDIM                   (3)
#define LSM_FMM_3D_TRUE                   (1)
#define LSM_FMM_3D_FALSE                  (0)
#define LSM_FMM_3D_DEFAULT_UPDATE_VALUE   (0)


/*========================== error codes ============================*/
#define LSM_FMM_3D_ERR_SUCCESS                             (0)
#define LSM_FMM_3D_ERR_FMM_DATA_CREATION_ERROR             (1)
#define LSM_FMM_3D_ERR_INVALID_SPATIAL_DERIVATIVE_ORDER    (2)


/*========================= lsm_FMM_3d Macros ===========================*/
#define LSM_FMM_3D_IDX(i,j,k)     ( (i) + grid_dims[0]*(j)                 \
                                  + grid_dims[0]*grid_dims[1]*(k) )
#define LSM_FMM_3D_ABS(x)         ((x) > 0 ? (x) : -1.0*(x))
#define LSM_FMM_3D_IDX_OUT_OF_BOUNDS(i,j,k,grid_dims)                      \
  ( ((i)<0) || ((i)>(grid_dims)[0]-1) || ((j)<0) || ((j)>(grid_dims)[1]-1) \
    || ( ((grid_dims)[2]>0) && (((k)<0) || ((k)>(grid_dims)[2]-1)) ) )


/*================== Helper Functions Declarations ==================*/

/*
 * FMM_initializeFront_FieldExtension3d_Order1() implements the 
 * callback function required by FMM_Core::FMM_initializeFront() to find 
 * and initialize the front.  
 *
 * The approximation to the distance function is computed
 * using a first-order, O(h), interpolation scheme.  The
 * extension fields are calculated using a first-order
 * approximation to the grad(F)*grad(dist) = 0 equation.
 */
void FMM_initializeFront_FieldExtension3d_Order1(
  FMM_CoreData *fmm_core_data,
  FMM_FieldData *fmm_field_data,
  int num_dims,
  int *grid_dims,
  double *dx);

/* 
 * FMM_updateGridPoint_FieldExtension3d_Order1() computes and returns 
 * the updated distance of the specified grid point using values of 
 * neighbors that have status "KNOWN". 
 *
 * The approximation to the distance function is computed
 * using a first-order, O(h), finite-difference scheme.  The
 * extension fields are calculated using a first-order
 * approximation to the grad(F)*grad(dist) = 0 equation.
 */
double FMM_updateGridPoint_FieldExtension3d_Order1(
  FMM_CoreData *fmm_core_data,
  FMM_FieldData *fmm_field_data,
  int *grid_idx,
  int num_dims,
  int *grid_dims,
  double *dx);


/*==================== Function Definitions =========================*/


int computeExtensionFields3d(
  double *distance_function,
  double **extension_fields,
  double *phi,
  double *mask,
  double **source_fields,
  int num_extension_fields,
  int spatial_discretization_order,
  int *grid_dims,
  double *dx)
{
  /* fast marching method data */
  FMM_CoreData *fmm_core_data;

  /* pointers to callback functions */
  updateGridPointFuncPtr updateGridPoint;
  initializeFrontFuncPtr initializeFront;

  /* auxiliary variables */
  int num_gridpoints;       /* number of grid points */
  int i,j,k;                /* loop variables */
  double *ptr;              /* pointer to field data */


  /******************************************************
   * set up appropriate grid point update and front
   * detection/initialization functions based on the
   * specified spatial derivative order
   ******************************************************/
  if (spatial_discretization_order == 1) {
    initializeFront = 
      &FMM_initializeFront_FieldExtension3d_Order1;
    updateGridPoint = &FMM_updateGridPoint_FieldExtension3d_Order1;
  } else if (spatial_discretization_order == 2) {
/*  KTC - ADD LATER
    initializeFront = 
      &FMM_initializeFront_FieldExtension3d_Order2;
    updateGridPoint = &FMM_updateGridPoint_FieldExtension3d_Order2;
*/
    fprintf(stderr,
           "ERROR: second-order spatial derivatives currently unsupported\n");
    return LSM_FMM_3D_ERR_INVALID_SPATIAL_DERIVATIVE_ORDER;
  } else {
    fprintf(stderr,
           "ERROR: Invalid spatial derivative order.  Only first-\n");
    fprintf(stderr,
           "       and second-order finite differences supported.\n");
    return LSM_FMM_3D_ERR_INVALID_SPATIAL_DERIVATIVE_ORDER;
  }

  /********************************************
   * set up FMM Field Data
   ********************************************/
  FMM_FieldData *fmm_field_data =
    (FMM_FieldData*) malloc(sizeof(FMM_FieldData));
  if (!fmm_field_data) return LSM_FMM_3D_ERR_FMM_DATA_CREATION_ERROR;
  fmm_field_data->phi = phi;
  fmm_field_data->distance_function = distance_function;
  fmm_field_data->num_extension_fields = num_extension_fields;
  fmm_field_data->source_fields = source_fields;
  fmm_field_data->extension_fields = extension_fields;

  /********************************************
   * initialize phi and extension fields
   ********************************************/
  num_gridpoints = 1;
  for (i = 0; i < LSM_FMM_3D_NDIM; i++) {
    num_gridpoints *= grid_dims[i];
  }
  for (i = 0, ptr = distance_function; i < num_gridpoints; i++, ptr++) {
    *ptr = LSM_FMM_3D_DEFAULT_UPDATE_VALUE;  
  }

  for (j = 0; j < num_extension_fields; j++) {
    for (i = 0, ptr = extension_fields[j]; i < num_gridpoints; i++, ptr++) {
      *ptr = LSM_FMM_3D_DEFAULT_UPDATE_VALUE;
    }
  }

  /********************************************
   * initialize FMM data
   ********************************************/
  fmm_core_data = FMM_Core_createFMM_CoreData(
    fmm_field_data, 
    LSM_FMM_3D_NDIM,
    grid_dims,
    dx,
    initializeFront,
    updateGridPoint);
  if (!fmm_core_data) return LSM_FMM_3D_ERR_FMM_DATA_CREATION_ERROR;

  /* mark grid points outside of domain */
  for (k = 0; k < grid_dims[2]; k++) {
    for (j = 0; j < grid_dims[1]; j++) {
      for (i = 0; i < grid_dims[0]; i++) {
        int idx_ijk = LSM_FMM_3D_IDX(i,j,k);

        if ((mask) && (mask[idx_ijk] < 0)) {
          int grid_idx[LSM_FMM_3D_NDIM];
          grid_idx[0] = i; grid_idx[1] = j; grid_idx[2] = k;
          FMM_Core_markPointOutsideDomain(fmm_core_data, grid_idx);
        }
      }
    }
  } /* end loop over grid to mark points outside of domain */

  /* initialize grid points around the front */ 
  FMM_Core_initializeFront(fmm_core_data); 

  /* update remaining grid points */
  while (FMM_Core_moreGridPointsToUpdate(fmm_core_data)) {
    FMM_Core_advanceFront(fmm_core_data);
  }

  /* clean up memory */
  FMM_Core_destroyFMM_CoreData(fmm_core_data);
  free(fmm_field_data);

  return LSM_FMM_3D_ERR_SUCCESS;
}

/* 
 * computeDistanceFunction3d() just calls computeExtensionFields3d()
 * with no source/extension fields (i.e. NULL source/extension field
 * pointers).
 */
int computeDistanceFunction3d(
  double *distance_function,
  double *phi,
  double *mask,
  int spatial_discretization_order,
  int *grid_dims,
  double *dx)
{
  return computeExtensionFields3d(
           distance_function,
           0, /*  NULL extension fields pointer */
           phi,
           mask,
           0, /*  NULL source fields pointer */
           0, /*  zero extension fields to compute */
           spatial_discretization_order,
           grid_dims,
           dx);
}

void FMM_initializeFront_FieldExtension3d_Order1(
  FMM_CoreData *fmm_core_data,
  FMM_FieldData *fmm_field_data,
  int num_dims,
  int *grid_dims,
  double *dx)
{
  /* FMM Field Data variables */
  double *phi = fmm_field_data->phi;
  double *distance_function = fmm_field_data->distance_function;
  int num_extension_fields = fmm_field_data->num_extension_fields;
  double **source_fields = fmm_field_data->source_fields;
  double **extension_fields = fmm_field_data->extension_fields;

  /* grid variables */
  int grid_idx[LSM_FMM_3D_NDIM];
  int offset[LSM_FMM_3D_NDIM];
  int neighbor[LSM_FMM_3D_NDIM];
  int on_interface = LSM_FMM_3D_FALSE;
  int borders_interface = LSM_FMM_3D_FALSE;

  /* distance function variables */
  double phi_cur;
  double phi_minus;
  double phi_plus;
  double dist_minus;
  double dist_plus;
  double dist_dir;
  int use_plus; 

  double dist_inv_sq; 
  double dist_inv_sq_dir; 

  /* variables for extension field calculations */
  double *extension_fields_cur;
  double *extension_fields_div_dist_sq;
  double *extension_fields_minus;
  double *extension_fields_plus;

  /* auxilliary variables */
  int i,j,k;  /* loop variables for grid */
  int idx_ijk;
  int idx_neighbor;
  int m;    /* loop variable for extension fields */
  int l;    /* extra loop variable */
  int dir;  /* loop variable over spatial dimensions */

  /* unused function parameters */
  (void) num_dims;


  /* allocate memory for extension field calculations */
  extension_fields_cur = (double*) malloc(num_extension_fields*sizeof(double));
  extension_fields_div_dist_sq = 
    (double*) malloc(num_extension_fields*sizeof(double));
  extension_fields_minus = 
    (double*) malloc(num_extension_fields*sizeof(double));
  extension_fields_plus = 
    (double*) malloc(num_extension_fields*sizeof(double));

  /*
   * loop through cells in grid to find the border of the zero level set
   */
  for (k=0; k<grid_dims[2]; k++) {
    for (j=0; j<grid_dims[1]; j++) {
      for (i=0; i<grid_dims[0]; i++) {
        on_interface = LSM_FMM_3D_FALSE;
        borders_interface = LSM_FMM_3D_FALSE;

        /* compute index for (i,j,k) grid point */
        idx_ijk = LSM_FMM_3D_IDX(i,j,k);

        /* get data values at the current grid point */
        phi_cur = phi[idx_ijk];
        for (m = 0; m < num_extension_fields; m++) {
          extension_fields_cur[m] = source_fields[m][idx_ijk];
        }

        /* zero out accumulation variables */
        dist_inv_sq = 0; 
        for (m = 0; m < num_extension_fields; m++) {
          extension_fields_div_dist_sq[m] = 0;
        }
    
        /* loop over neighbors */
        for (dir = 0; dir < LSM_FMM_3D_NDIM; dir++) {
          for (l = 0; l < LSM_FMM_3D_NDIM; l++) { /* reset offset */
            offset[l] = 0; 
          }

          /* reset plus and minus distances */
          dist_plus = DBL_MAX;
          dist_minus = DBL_MAX;
 
          /* reset plus and minus extension field values */
          for (m = 0; m < num_extension_fields; m++) {
            extension_fields_minus[m] = 0;
            extension_fields_plus[m] = 0;
          }
    
          /* calculate distance to interface in minus direction */
          offset[dir] = -1;
          neighbor[0] = i+offset[0]; 
          neighbor[1] = j+offset[1];
          neighbor[2] = k+offset[2];
          if (!LSM_FMM_3D_IDX_OUT_OF_BOUNDS(neighbor[0],neighbor[1],neighbor[2],
                                            grid_dims)) {

            idx_neighbor = LSM_FMM_3D_IDX(neighbor[0],neighbor[1],neighbor[2]);
            phi_minus = phi[idx_neighbor];
            if (phi_minus*phi_cur <= 0) {

              if (phi_cur == 0) {
                dist_minus = 0.0;
              } else {
                dist_minus = phi_cur/(phi_cur-phi_minus);

                for (m = 0; m < num_extension_fields; m++) {
                  extension_fields_minus[m] = extension_fields_cur[m] 
                    + dist_minus*(source_fields[m][idx_neighbor]
                                 -extension_fields_cur[m]);
                }
      
                /* multiply back in the units for dist_minus */
                dist_minus *= dx[dir];
              }
            }
          }
          
          /* calculate distance to interface in plus direction */
          offset[dir] = 1;
          neighbor[0] = i+offset[0]; 
          neighbor[1] = j+offset[1];
          neighbor[2] = k+offset[2];
          if (!LSM_FMM_3D_IDX_OUT_OF_BOUNDS(neighbor[0],neighbor[1],neighbor[2],
                                            grid_dims)) {

            idx_neighbor = LSM_FMM_3D_IDX(neighbor[0],neighbor[1],neighbor[2]);
            phi_plus = phi[idx_neighbor];
            if (phi_plus*phi_cur <= 0) {

              if (phi_cur == 0) {
                dist_plus = 0.0;
              } else {
                dist_plus = phi_cur/(phi_cur-phi_plus);
    
                for (m = 0; m < num_extension_fields; m++) {
                  extension_fields_plus[m] = extension_fields_cur[m] 
                    + dist_plus*(source_fields[m][idx_neighbor]
                                -extension_fields_cur[m]);
                }
    
                /* multiply back in the units for dist_plus */
                dist_plus *= dx[dir];
              }
            }
          }

          /* check if current grid point lies on or borders the interface */
          if ( (dist_plus == 0) || (dist_minus == 0) ) {

            /* the current grid point lies directly on the interface.   */
            /* no need to check other directions, so break out of loop. */
            on_interface = LSM_FMM_3D_TRUE;
            break;

          } else if ( (dist_plus < DBL_MAX) || (dist_minus < DBL_MAX) ) {
              borders_interface = LSM_FMM_3D_TRUE; 
    
              if (dist_plus < dist_minus) {
                dist_dir = dist_plus;
                use_plus = LSM_FMM_3D_TRUE;
              } else {
                dist_dir = dist_minus;
                use_plus = LSM_FMM_3D_FALSE;
              }
    
              /* update 1/dist^2 and ext_field/dist^2 values with */
              /* information from current coordinate direction    */
              dist_inv_sq_dir = 1/dist_dir/dist_dir;
              dist_inv_sq += dist_inv_sq_dir; 
    
              if (use_plus) {
                for (m = 0; m < num_extension_fields; m++) {
                  extension_fields_div_dist_sq[m] += 
                    extension_fields_plus[m]*dist_inv_sq_dir;
                }
              } else {
                for (m = 0; m < num_extension_fields; m++) {
                  extension_fields_div_dist_sq[m] += 
                    extension_fields_minus[m]*dist_inv_sq_dir;
              }
            }

          } /* end cases: current grid point on or borders interface */
    
        } /* end loop over neighbors */

        /* set distance function and extension field of grid points */
        /* on or bordering the zero level set                       */   
        if (on_interface) {

          distance_function[idx_ijk] = 0.0;

          /* compute extension field value */
          for (m = 0; m < num_extension_fields; m++) {
            extension_fields[m][idx_ijk] = extension_fields_cur[m];
          }

          /* set grid point as an initial front point */
          grid_idx[0] = i; grid_idx[1] = j; grid_idx[2] = k;
          FMM_Core_setInitialFrontPoint(fmm_core_data, grid_idx,
                                        distance_function[idx_ijk]);

        } else if (borders_interface) { 
  
          /* compute updated value for the signed distance function */
          if (phi_cur > 0)
            distance_function[idx_ijk] = 1/sqrt(dist_inv_sq);
          else 
            distance_function[idx_ijk] = -1/sqrt(dist_inv_sq);
    
          /* compute extension field value */
          for (m = 0; m < num_extension_fields; m++) {
            extension_fields[m][idx_ijk] = 
              extension_fields_div_dist_sq[m]/dist_inv_sq;
          }
  
          /* set grid point as an initial front point */
          grid_idx[0] = i; grid_idx[1] = j; grid_idx[2] = k;
          FMM_Core_setInitialFrontPoint(fmm_core_data, grid_idx,
                                        distance_function[idx_ijk]);
  
        } /* end handling grid points on or near interface */
        
      }
    }
  }  /* end loop over grid */

  /* clean up memory */
  free(extension_fields_cur); 
  free(extension_fields_div_dist_sq); 
  free(extension_fields_minus);
  free(extension_fields_plus);
}


double FMM_updateGridPoint_FieldExtension3d_Order1(
  FMM_CoreData *fmm_core_data,
  FMM_FieldData *fmm_field_data,
  int *grid_idx,
  int num_dims,
  int *grid_dims,
  double *dx)
{
  int *gridpoint_status = FMM_Core_getGridPointStatusDataArray(fmm_core_data);

  /* FMM Field Data variables */
  double *distance_function = fmm_field_data->distance_function;
  int num_extension_fields = fmm_field_data->num_extension_fields;
  double **extension_fields = fmm_field_data->extension_fields;

  /* variables for extension field calculations */
  double *extension_fields_numerator;
  double *extension_fields_denominator;

  /* variables used in distance function update */
  PointStatus  neighbor_status;
  int use_plus[LSM_FMM_3D_NDIM];
  double phi_upwind[LSM_FMM_3D_NDIM];
  int dir_used[LSM_FMM_3D_NDIM];
  double phi_plus;
  double inv_dx_sq; 
  int offset[LSM_FMM_3D_NDIM]; 
  int neighbor[LSM_FMM_3D_NDIM];

  /* variables used in extension field update */
  double ext_val_neighbor;
  double dist_diff; 

  /* coefficients of quadratic equation for the updated distance function */
  double phi_A = 0;
  double phi_B = 0;
  double phi_C = 0;
  double discriminant;
  double dist_updated;

  /* auxilliary variables */
  int dir;  /* loop variable for spatial directions */
  int k;    /* loop variable for extension fields */
  int l;    /* extra loop variable */ 
  int idx_cur_gridpoint, idx_neighbor;

  /* unused function parameters */
  (void) num_dims;

  /* allocate memory extension field calculations */
  extension_fields_numerator = 
    (double*) malloc(num_extension_fields*sizeof(double));
  extension_fields_denominator = 
    (double*) malloc(num_extension_fields*sizeof(double));;
  for (k = 0; k < num_extension_fields; k++) {
    extension_fields_numerator[k] = 0;
    extension_fields_denominator[k] = 0;
  }

  /* calculate update to distance function */
  for (dir = 0; dir < LSM_FMM_3D_NDIM; dir++) { /* loop over coord directions */
    for (l = 0; l < LSM_FMM_3D_NDIM; l++) { /* reset offset */
      offset[l] = 0; 
    }

    /* changed to true if has KNOWN neighbor */
    dir_used[dir] = LSM_FMM_3D_FALSE;  

    /* find "upwind" direction and phi value */
    phi_upwind[dir] = DBL_MAX;

    /* check minus direction */
    offset[dir] = -1;
    neighbor[0] = grid_idx[0] + offset[0];
    neighbor[1] = grid_idx[1] + offset[1];
    neighbor[2] = grid_idx[2] + offset[2];
    if (!LSM_FMM_3D_IDX_OUT_OF_BOUNDS(neighbor[0],neighbor[1],neighbor[2],
                                      grid_dims)) {
      idx_neighbor = LSM_FMM_3D_IDX(neighbor[0],neighbor[1],neighbor[2]);
      neighbor_status = (PointStatus) gridpoint_status[idx_neighbor];
      if (KNOWN == neighbor_status) {
        phi_upwind[dir] = distance_function[idx_neighbor];
        use_plus[dir] = LSM_FMM_3D_FALSE;
        dir_used[dir] = LSM_FMM_3D_TRUE;
      }
    }

    /* check plus direction */
    offset[dir] = 1;
    neighbor[0] = grid_idx[0] + offset[0];
    neighbor[1] = grid_idx[1] + offset[1];
    neighbor[2] = grid_idx[2] + offset[2];
    if (!LSM_FMM_3D_IDX_OUT_OF_BOUNDS(neighbor[0],neighbor[1],neighbor[2],
                                      grid_dims)) {
      idx_neighbor = LSM_FMM_3D_IDX(neighbor[0],neighbor[1],neighbor[2]);
      neighbor_status = (PointStatus) gridpoint_status[idx_neighbor];
      if (KNOWN == neighbor_status) {
        phi_plus = distance_function[idx_neighbor];

        /* 
         * choosing the upwind direction to be the direction
         * with the smaller abs(phi) value gives a consistent 
         * solution to the "upwind" Eikonal equation.
         * NOTE: to avoid having to use the C math library,
         *       we define our own ABS macro
         */
        if (LSM_FMM_3D_ABS(phi_plus) < LSM_FMM_3D_ABS(phi_upwind[dir])) {
          phi_upwind[dir] = phi_plus;
          use_plus[dir] = LSM_FMM_3D_TRUE;
          dir_used[dir] = LSM_FMM_3D_TRUE;
        }
      }
    }

    /*
     * accumulate coefficients for updated distance function
     * if either of the neighbors are "known"
     */
    if (phi_upwind[dir] < DBL_MAX) {
      /* accumulate coefs for updated distance function */ 
      inv_dx_sq = 1/dx[dir]/dx[dir];
      phi_A += inv_dx_sq;
      phi_B += phi_upwind[dir]*inv_dx_sq;
      phi_C += phi_upwind[dir]*phi_upwind[dir]*inv_dx_sq;
    }

  } /* loop over coordinate directions */

  /* complete computation of phi_B and phi_C */
  phi_B *= -2.0;
  phi_C -= 1;  /* F_ijk = 1 for a distance function calculation */

  /* compute updated distance function by solving quadratic equation */
  discriminant = phi_B*phi_B - 4*phi_A*phi_C;
  dist_updated = DBL_MAX;
  if (discriminant >= 0) {
    if (phi_B < 0) { /* distance function of neighbors is positive */
      dist_updated = (-phi_B + sqrt(discriminant))/2/phi_A;
    } else if (phi_B > 0) { /* distance function of neighbors is negative */
      dist_updated = (-phi_B - sqrt(discriminant))/2/phi_A;
    } else { 
      /* grid point is ON the interface, so keep it there */
      dist_updated = 0;
    } 
  } else {
    fprintf(stderr,"ERROR: phi update - discriminant negative!!!\n");
    if (phi_B > 0) { /* distance function of neighbors is negative */
      dist_updated *= -1;
    }
  }


  /* calculate extension field values */
  for (dir = 0; dir < LSM_FMM_3D_NDIM; dir++) { /* loop over coord directions */

    /*
     * only accumulate values from the current direction if this
     * direction was used in the update of the distance function
     */
    if (dir_used[dir]) {
      for (l = 0; l < LSM_FMM_3D_NDIM; l++) { /* reset offset */
        offset[l] = 0; 
      }
      offset[dir] = (use_plus[dir] ? 1 : -1);
      neighbor[0] = grid_idx[0] + offset[0];
      neighbor[1] = grid_idx[1] + offset[1];
      neighbor[2] = grid_idx[2] + offset[2];
      idx_neighbor = LSM_FMM_3D_IDX(neighbor[0],neighbor[1],neighbor[2]);
  
      for (k = 0; k < num_extension_fields; k++) {
        ext_val_neighbor = extension_fields[k][idx_neighbor];
        dist_diff = dist_updated - phi_upwind[dir];
        extension_fields_numerator[k] += ext_val_neighbor*dist_diff;
        extension_fields_denominator[k] += dist_diff;
      }
    }
  } /* loop over coordinate directions */


  /* set updated quantities */
  idx_cur_gridpoint = LSM_FMM_3D_IDX(grid_idx[0],grid_idx[1],grid_idx[2]);
  distance_function[idx_cur_gridpoint] = dist_updated;
  for (k = 0; k < num_extension_fields; k++) {
    extension_fields[k][idx_cur_gridpoint] =
      extension_fields_numerator[k]/extension_fields_denominator[k];
  }

  /* free memory allocated for extension field calculations */
  free(extension_fields_numerator);
  free(extension_fields_denominator);


  return dist_updated;
}
