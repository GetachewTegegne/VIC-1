#include <stdio.h>
#include <stdlib.h>
#include <vicNl.h>
#include <math.h>

static char vcid[] = "$Id$";

int  full_energy(char                 NEWCELL,
		 int                  gridcell,
                 int                  rec,
                 atmos_data_struct   *atmos,
                 dist_prcp_struct    *prcp,
                 dmy_struct          *dmy,
                 global_param_struct *gp,
		 lake_con_struct     *lake_con,
                 soil_con_struct     *soil_con,
                 veg_con_struct      *veg_con)
/**********************************************************************
	full_energy	Keith Cherkauer		January 8, 1997

  This subroutine controls the model core, it solves both the energy
  and water balance models, as well as frozen soils.  

  modifications:
  07-98 restructured to fix problems with distributed precipitation, 
        and to add the ability to solve the snow model at different 
	elevation bands within a single grid cell.                 KAC
  01-19-00 modified to work with the new atmosphere data structure 
           implemented when the radiation forcing routines were 
	   updated.  Also modified to use the new simplified
	   soil moisture storage for the frozen soil algorithm.    KAC
  12-01-00 modified to include the lakes and wetlands algorithm.   KAC
  11-18-02 Modified to handle blowing snow.  Also added debugging
           output for lake model.                                  LCB
  05-27-03 Updated passing of veg_con parameters for blowing snow
           to surface_fluxes.  Original did not account for the fact
           that veg_con is not allocated for veg = Nveg (bare soil)
           case.  This eliminates a memory error.                  KAC
  28-Sep-04 Added aero_resist_used to store the aerodynamic resistance
	    used in flux calculations.				TJB
  2006-Sep-23 Implemented flexible output configuration; now computation
	      of soil wetness and root zone soil moisture happens here.		TJB
  2006-Nov-07 Removed LAKE_MODEL option.					TJB
  2007-Apr-04 Modified to handle grid cell errors by returning to the
	      main subroutine, rather than ending the simulation.		GCT/KAC
  2007-May-01 Added case of SPATIAL_FROST = TRUE in modifications
	      from 2006-Sep-23.							GCT
  2007-Aug-10 Added features for EXCESS_ICE option.
	      Including calculating subsidence for each layer and
	      updating soil depth, effective porosity,
	      bulk density, and soil moisture and fluxes by calling
	      runoff function if subsidence occurs.				JCA
  2007-Sep-07 No longer resets ice content to previous time-step ice content if
	      subsidence has occurred.						JCA
  2007-Sep-19 Added MAX_SUBSIDENCE parameter to EXCESS_ICE option.		JCA
  2007-Sep-19 Fixed bug in subsidence calculation.				JCA
  2007-Nov-06 Added veg_con to parameter list of lakemain().  Replaced
	      lake.fraci with lake.areai.					LCB via TJB
  2008-Jan-23 Changed ice0 from a scalar to an array.  Previously,
	      when options.SNOW_BAND > 1, the value of ice0 computed
	      for earlier bands was always overwritten by the value
	      of ice0 computed for the final band (even if the final
	      band had 0 area).							JS via TJB
  2008-May-05 Changed moist from a scalar to an array (moist0).  Previously,
	      when options.SNOW_BAND > 1, the value of moist computed
	      for earlier bands was always overwritten by the value
	      of moist computed for the final band (even if the final
	      band had 0 area).							KAC via TJB
  2009-Jan-16 Modified aero_resist_used and Ra_used to become arrays of
	      two elements (surface and overstory); added
	      options.AERO_RESIST_CANSNOW.					TJB
  2009-May-17 Added asat to cell_data.						TJB
  2009-Jun-09 Modified to use extension of veg_lib structure to contain
	      bare soil information.						TJB
  2009-Jun-09 Modified to compute aero_resist for all potential evap
	      landcover types.							TJB
  2009-Jun-09 Cell_data structure now only stores final aero_resist
	      values (called "aero_resist").  Preliminary uncorrected
	      aerodynamic resistances for current vegetation and various
	      reference land cover types for use in potential evap
	      calculations is stored in temporary array aero_resist.		TJB
  2009-Jun-26 Simplified argument list of runoff() by passing all cell_data
	      variables via a single reference to the cell data structure.	TJB
  2009-Jul-22 Fixed error in assignment of cell.aero_resist.			TJB
  2009-Jul-31 Wetland portion of lake/wetland tile is now processed in
	      full_energy() instead of wetland_energy().  Lake funcions are
	      now called directly from full_energy instead of lakemain().	TJB

**********************************************************************/
{
  extern veg_lib_struct *veg_lib;
  extern option_struct   options;
#if LINK_DEBUG
  extern debug_struct    debug;
#endif

  char                   overstory;
  int                    i, j, p;
  int                    lidx;
  int                    Ndist;
  int                    dist;
  int                    iveg;
  int                    Nveg;
  int                    veg_class;
  int                    band;
  int                    Nbands;
  int                    ErrorFlag;
#if SPATIAL_FROST
  int                    frost_area;
#endif // SPATIAL_FROST
  double                 out_prec[2*MAX_BANDS];
  double                 out_rain[2*MAX_BANDS];
  double                 out_snow[2*MAX_BANDS];
  double                 out_short=0;
  double                 dp;
  double                 ice0[MAX_BANDS];
  double                 moist0[MAX_BANDS];
  double                 surf_atten;
  double                 Tend_surf;
  double                 Tend_grnd;
  double                 wind_h;
  double                 height;
  double                 displacement[3];
  double                 roughness[3];
  double                 ref_height[3];
  double               **aero_resist;
  double                 Cv;
  double                 Le;
  double                 Melt[2*MAX_BANDS];
  double                 bare_albedo;
  double                 snow_inflow[MAX_BANDS];
  double                 rainonly;
  double                 sum_runoff;
  double                 sum_baseflow;
  double                 tmp_wind[3];
  double                 tmp_mu;
  double                 tmp_total_moist;
  double                 gauge_correction[2];
  float 	         lag_one;
  float 	         sigma_slope;
  float  	         fetch;
  int                    pet_veg_class;
  double                 ldepth;
  double                 lakefrac;
  double                 fraci;
  double                 wetland_runoff;
  double                 wetland_baseflow;
  double                 oldvolume;
  double                 oldsnow;
  double                 snowprec;
  double                 rainprec;
  lake_var_struct       *lake_var;
  cell_data_struct    ***cell;
  veg_var_struct      ***veg_var;
  energy_bal_struct    **energy;
  energy_bal_struct     *ptr_energy;
  snow_data_struct     **snow;
  snow_data_struct      *tmp_snow;
  veg_var_struct        *tmp_veg[2];
  veg_var_struct        *wet_veg_var;
  veg_var_struct        *dry_veg_var;
  veg_var_struct         empty_veg_var;
  energy_bal_struct      lake_energy;
  snow_data_struct       lake_snow;
#if EXCESS_ICE
  int                    SubsidenceUpdate = 0;
  int                    index;
  char                   ErrStr[MAXSTRING];
  double                 max_ice_layer; //mm/mm
  double                 ave_ice_fract; //mm/mm
  double                 ave_ice, tmp_ice; //mm
  double                 ice_layer; //mm
  double                 subsidence[MAX_LAYERS]; //mm
  double                 total_subsidence; //m
  double                 tmp_subsidence; //mm
  double                 total_meltwater; //mm
  double                 tmp_depth, tmp_depth_prior; //m
  double                 ppt[2]; 
  double                 moist_prior[2][MAX_VEG][MAX_BANDS][MAX_LAYERS]; //mm
  double                 evap_prior[2][MAX_VEG][MAX_BANDS][MAX_LAYERS]; //mm
#endif //EXCESS_ICE
  double tmp_error;
  double old_storage[3];
  double new_storage;

  /* Allocate aero_resist array */
  aero_resist = (double**)calloc(N_PET_TYPES+1,sizeof(double*));
  for (p=0; p<N_PET_TYPES+1; p++) {
    aero_resist[p] = (double*)calloc(3,sizeof(double));
  }

  /* set local pointers */
  cell    = prcp->cell;
  energy  = prcp->energy;
  lake_var = &prcp->lake_var;
  snow    = prcp->snow;
  veg_var = prcp->veg_var;

  /* set variables for distributed precipitation */
  if(options.DIST_PRCP) 
    Ndist = 2;
  else 
    Ndist = 1;
  Nbands = options.SNOW_BAND;

  /* Set number of vegetation types */
  Nveg      = veg_con[0].vegetat_type_num;

  /** Set Damping Depth **/
  dp        = soil_con->dp;

  /* Compute gauge undercatch correction factors 
     - this assumes that the gauge is free of vegetation effects, so gauge
     correction is constant for the entire grid cell */
  if( options.CORRPREC && atmos->prec[NR] > 0 ) 
    correct_precip(gauge_correction, atmos->wind[NR], gp->wind_h, 
		   soil_con->rough, soil_con->snow_rough);
  else {
    gauge_correction[0] = 1;
    gauge_correction[1] = 1;
  }
  atmos->out_prec = 0;
  atmos->out_rain = 0;
  atmos->out_snow = 0;


  /* initialize prior moist and ice for subsidence calculations */
#if EXCESS_ICE
  for(iveg = 0; iveg <= Nveg; iveg++){
    for ( band = 0; band < Nbands; band++ ) {
      for ( dist = 0; dist < Ndist; dist++ ) {
	for(lidx=0;lidx<options.Nlayer;lidx++) {
	  moist_prior[dist][iveg][band][lidx] = cell[dist][iveg][band].layer[lidx].moist;
	  evap_prior[dist][iveg][band][lidx] = 0; //initialize
	}
      }
    }
  }
#endif //EXCESS_ICE


for (iveg=0; iveg<=Nveg; iveg++) {
old_storage[iveg] = cell[0][iveg][0].layer[0].moist+cell[0][iveg][0].layer[1].moist+cell[0][iveg][0].layer[2].moist;
}
  /**************************************************
    Solve Energy and/or Water Balance for Each
    Vegetation Type
  **************************************************/
  for(iveg = 0; iveg <= Nveg; iveg++){

    /** Solve Veg Type only if Coverage Greater than 0% **/
    if (veg_con[iveg].Cv > 0.0) {
      Cv = veg_con[iveg].Cv;

      /** Lake-specific processing **/
      if (veg_con[iveg].LAKE) {

        /* Update sarea to equal new surface area from previous time step. */
        ErrorFlag = get_depth(*lake_con, lake_var->volume, &ldepth);
        if (ErrorFlag == ERROR) {
          fprintf(stderr,"ERROR: problem in get_depth(): volume %f depth %f rec %d\n", lake_var->volume, ldepth, rec);
          return ( ErrorFlag );
        }
        ErrorFlag = get_sarea(*lake_con, ldepth, &(lake_var->sarea));
        if ( ErrorFlag == ERROR ) {
          fprintf(stderr, "Something went wrong in get_sarea; record = %d, depth = %f, sarea = %e\n",rec,ldepth, lake_var->sarea);
          return ( ErrorFlag );
        }
        lake_var->areai = lake_var->new_ice_area;

        // Initialize lake data structures
        band = 0;
        ErrorFlag = initialize_prcp(prcp, &lake_energy, &lake_snow, iveg, band,
                                *soil_con, lake_var, rec, *lake_con, &lakefrac, &fraci);
        if ( ErrorFlag == ERROR )
          // Return failure flag to main routine
          return ( ErrorFlag );

        Nbands = 1;
        Cv *= (1-lakefrac);

      }

      /**************************************************
        Initialize Model Parameters
      **************************************************/

      for(band = 0; band < Nbands; band++) {
	if(soil_con->AreaFract[band] > 0) {

	  /* Initialize energy balance variables */
	  energy[iveg][band].shortwave = 0;
	  energy[iveg][band].longwave  = 0.;

	  /* Initialize snow variables */
	  snow[iveg][band].vapor_flux        = 0.;
	  snow[iveg][band].canopy_vapor_flux = 0.;
	  snow_inflow[band]                  = 0.;
	  Melt[band*2]                       = 0.;

	}
      }

      /* Initialize precipitation storage */
      for ( j = 0; j < 2*MAX_BANDS; j++ ) {
        out_prec[j] = 0;
        out_rain[j] = 0;
        out_snow[j] = 0;
      }
    
      /** Define vegetation class number **/
      veg_class = veg_con[iveg].veg_class;

      /** Assign wind_h **/
      /** Note: this is ignored below **/
      wind_h = veg_lib[veg_class].wind_h;

      /** Compute Surface Attenuation due to Vegetation Coverage **/
      surf_atten = exp(-veg_lib[veg_class].rad_atten 
		   * veg_lib[veg_class].LAI[dmy[rec].month-1]);

      /* Initialize soil thermal properties for the top two layers */
      if(options.FULL_ENERGY || options.FROZEN_SOIL) {
	prepare_full_energy(iveg, Nveg, options.Nnode, prcp, 
			    soil_con, moist0, ice0);
      }

      /** Compute Bare (free of snow) Albedo **/
      bare_albedo = veg_lib[veg_class].albedo[dmy[rec].month-1];

      /*************************************
	Compute the aerodynamic resistance 
	for current veg cover and various
	types of potential evap
      *************************************/

      /* Loop over types of potential evap, plus current veg */
      /* Current veg will be last */
      for (p=0; p<N_PET_TYPES+1; p++) {

        /* Initialize wind speeds */
        tmp_wind[0] = atmos->wind[NR];
        tmp_wind[1] = -999.;
        tmp_wind[2] = -999.;
 
        /* Set surface descriptive variables */
        if (p < N_PET_TYPES_NON_NAT) {
	  pet_veg_class = veg_lib[0].NVegLibTypes+p;
        }
        else {
	  pet_veg_class = veg_class;
        }
        displacement[0] = veg_lib[pet_veg_class].displacement[dmy[rec].month-1];
        roughness[0]    = veg_lib[pet_veg_class].roughness[dmy[rec].month-1];
        overstory       = veg_lib[pet_veg_class].overstory;
	if (p >= N_PET_TYPES_NON_NAT)
          if ( roughness[0] == 0 ) roughness[0] = soil_con->rough;

        /* Estimate vegetation height */
        height = calc_veg_height(displacement[0]);

        /* Estimate reference height */
        if(displacement[0] < wind_h) 
          ref_height[0] = wind_h;
        else 
          ref_height[0] = displacement[0] + wind_h + roughness[0];

        /* Compute aerodynamic resistance over various surface types */
        /* Do this not only for current veg but also all types of PET */
        ErrorFlag = CalcAerodynamic(overstory, height,
		        veg_lib[pet_veg_class].trunk_ratio, 
                        soil_con->snow_rough, soil_con->rough, 
		        veg_lib[pet_veg_class].wind_atten,
			aero_resist[p], tmp_wind,
		        displacement, ref_height,
		        roughness);
        if ( ErrorFlag == ERROR ) return ( ERROR );  

      }

      /* Initialize final aerodynamic resistance values */
      for ( band = 0; band < Nbands; band++ ) {
        if( soil_con->AreaFract[band] > 0 ) {
          cell[WET][iveg][band].aero_resist[0] = aero_resist[N_PET_TYPES][0];
          cell[WET][iveg][band].aero_resist[1] = aero_resist[N_PET_TYPES][1];
        }
      }

      /**************************************************
        Store Water Balance Terms for Debugging
      **************************************************/

#if LINK_DEBUG
      if(debug.DEBUG || debug.PRT_MOIST || debug.PRT_BALANCE) {
        /** Compute current total moisture for water balance check **/
	store_moisture_for_debug(iveg, Nveg, prcp->mu, cell,
				 veg_var, snow, soil_con);
	if(debug.PRT_BALANCE) {
	  for(j=0; j<Ndist; j++) {
	    for(band=0; band<Nbands; band++) {
	      if(soil_con->AreaFract[band] > 0) {
		for(i=0; i<options.Nlayer+3; i++) {
		  debug.inflow[j][band][i]  = 0;
		  debug.outflow[j][band][i] = 0;
		}
	      }
	    }
	  }
	}
      }
#endif // LINK_DEBUG

      /******************************
        Solve ground surface fluxes 
      ******************************/
  
      for ( band = 0; band < Nbands; band++ ) {
	if( soil_con->AreaFract[band] > 0 ) {

	  wet_veg_var = &(veg_var[WET][iveg][band]);
	  dry_veg_var = &(veg_var[DRY][iveg][band]);
	  lag_one     = veg_con[iveg].lag_one;
	  sigma_slope = veg_con[iveg].sigma_slope;
	  fetch       = veg_con[iveg].fetch;

	  /* Initialize pot_evap */
	  for (p=0; p<N_PET_TYPES; p++)
	    cell[WET][iveg][band].pot_evap[p] = 0;

	  ErrorFlag = surface_fluxes(overstory, bare_albedo, height, ice0[band], moist0[band], 
#if EXCESS_ICE
				     SubsidenceUpdate, evap_prior[DRY][iveg][band],
				     evap_prior[WET][iveg][band],
#endif
				     prcp->mu[iveg], surf_atten, &(Melt[band*2]), &Le, 
				     aero_resist,
				     displacement, gauge_correction,
				     &out_prec[band*2], 
				     &out_rain[band*2], &out_snow[band*2],
				     ref_height, roughness, 
				     &snow_inflow[band], 
				     tmp_wind, veg_con[iveg].root, Nbands, Ndist, 
				     options.Nlayer, Nveg, band, dp, iveg, rec, veg_class, 
				     atmos, dmy, &(energy[iveg][band]), gp, 
				     &(cell[DRY][iveg][band]),
				     &(cell[WET][iveg][band]),
				     &(snow[iveg][band]), 
				     soil_con, dry_veg_var, wet_veg_var, 
				     lag_one, sigma_slope, fetch);
	  
	  if ( ErrorFlag == ERROR ) return ( ERROR );
	  
	  atmos->out_prec += out_prec[band*2] * Cv * soil_con->AreaFract[band];
	  atmos->out_rain += out_rain[band*2] * Cv * soil_con->AreaFract[band];
	  atmos->out_snow += out_snow[band*2] * Cv * soil_con->AreaFract[band];

          /********************************************************
            Compute soil wetness and root zone soil moisture
          ********************************************************/
          // Loop through distributed precipitation fractions
          for ( dist = 0; dist < 2; dist++ ) {
            cell[dist][iveg][band].rootmoist = 0;
            cell[dist][iveg][band].wetness = 0;
            for(lidx=0;lidx<options.Nlayer;lidx++) {
#if SPATIAL_FROST
              tmp_total_moist = cell[dist][iveg][band].layer[lidx].moist;
              for ( frost_area = 0; frost_area < FROST_SUBAREAS; frost_area++ ) {
                tmp_total_moist += cell[dist][iveg][band].layer[lidx].ice[frost_area];
	      }
#else
              tmp_total_moist = cell[dist][iveg][band].layer[lidx].moist + cell[dist][iveg][band].layer[lidx].ice;
#endif // SPATIAL_FROST 
              if (veg_con->root[lidx] > 0) {
                cell[dist][iveg][band].rootmoist += tmp_total_moist;
              }
#if EXCESS_ICE
	      cell[dist][iveg][band].wetness += (tmp_total_moist - soil_con->Wpwp[lidx])/(soil_con->effective_porosity[lidx]*soil_con->depth[lidx]*1000 - soil_con->Wpwp[lidx]);
#else
	      cell[dist][iveg][band].wetness += (tmp_total_moist - soil_con->Wpwp[lidx])/(soil_con->porosity[lidx]*soil_con->depth[lidx]*1000 - soil_con->Wpwp[lidx]);
#endif
            }
            cell[dist][iveg][band].wetness /= options.Nlayer;

          }

	} /** End Loop Through Elevation Bands **/
      } /** End Full Energy Balance Model **/

      /****************************
         Controls Debugging Output
      ****************************/
#if LINK_DEBUG
      
      for(j = 0; j < Ndist; j++) {
	tmp_veg[j] = veg_var[j][iveg];
      }
      ptr_energy = energy[iveg];
      tmp_snow   = snow[iveg];
      for(j = 0; j < Ndist; j++) {
	if(j == 0) 
	  tmp_mu = prcp->mu[iveg]; 
	else 
	  tmp_mu = 1. - prcp->mu[iveg]; 
	/** for debugging water balance: [0] = vegetation, 
	    [1] = ground snow, [2..Nlayer+1] = soil layers **/
	if(debug.PRT_BALANCE) {
	  for(band = 0; band < Nbands; band++) {
	    if(soil_con->AreaFract[band] > 0) {
	      debug.inflow[j][band][options.Nlayer+2] += out_prec[j+band*2] * soil_con->Pfactor[band];
	      debug.inflow[j][band][0]  = 0.;
	      debug.inflow[j][band][1]  = 0.;
	      debug.outflow[j][band][0] = 0.;
	      debug.outflow[j][band][1] = 0.;
	      debug.inflow[j][band][0] += out_prec[j+band*2] * soil_con->Pfactor[band]; 
	      debug.outflow[j][band][0] += veg_var[j][iveg][band].throughfall;
	      if(j == 0)
		debug.inflow[j][band][1] += snow_inflow[band];
	      debug.outflow[j][band][1] += Melt[band*2+j];
	    }
	  }  /** End loop through elevation bands **/
	}
         
	write_debug(atmos, soil_con, cell[j][iveg], ptr_energy, 
		    tmp_snow, tmp_veg[j], &(dmy[rec]), gp, out_short, 
		    tmp_mu, Nveg, iveg, rec, gridcell, j, NEWCELL); 
      }
#endif // LINK_DEBUG

    } /** end current vegetation type **/
  } /** end of vegetation loop **/

  for (p=0; p<N_PET_TYPES+1; p++) {
    free((char *)aero_resist[p]);
  }
  free((char *)aero_resist);

  /****************************
     Calculate Subsidence
  ****************************/
#if EXCESS_ICE
  total_subsidence = 0;
  total_meltwater = 0; //for lake model only
  for(lidx=0;lidx<options.Nlayer;lidx++) {//soil layer
    subsidence[lidx] = 0;
    if(soil_con->effective_porosity[lidx]>soil_con->porosity[lidx]){
      
      /* find average ice/porosity fraction and sub-grid with greatest ice/porosity fraction */
      ave_ice = 0;
      max_ice_layer = 0;
      for(band = 0; band < Nbands; band++) {//band
	if(soil_con->AreaFract[band] > 0) {
	  for(iveg = 0; iveg <= Nveg; iveg++){ //iveg  
	    if (veg_con[iveg].Cv  > 0.) {
	      Cv = veg_con[iveg].Cv;
	      for ( dist = 0; dist < Ndist; dist++ ) {// wet/dry
		if(dist==0) 
		  tmp_mu = prcp->mu[iveg];
		else 
		  tmp_mu = 1. - prcp->mu[iveg];
#if SPATIAL_FROST
		tmp_ice = 0;
		for ( frost_area = 0; frost_area < FROST_SUBAREAS; frost_area++ ) {//frost area
		  tmp_ice += (cell[dist][iveg][band].layer[lidx].ice[frost_area]
			      * soil_con->frost_fract[frost_area]);
		  ice_layer = cell[dist][iveg][band].layer[lidx].ice[frost_area];
		  if(ice_layer>=max_ice_layer)
		    max_ice_layer = ice_layer;
		} // frost area
#else //SPATIAL_FROST
		tmp_ice = cell[dist][iveg][band].layer[lidx].ice;
		ice_layer = cell[dist][iveg][band].layer[lidx].ice;
		if(ice_layer>=max_ice_layer)
		  max_ice_layer = ice_layer;	
#endif //SPATIAL_FROST
		ave_ice += tmp_ice * Cv * tmp_mu * soil_con->AreaFract[band];
	      }// wet/dry
	    }
	  }//iveg
	}
      } //band
      ave_ice_fract = ave_ice/soil_con->max_moist[lidx];

      /*check to see if threshold is exceeded by average ice/porosity fraction*/
      if(ave_ice_fract <= ICE_AT_SUBSIDENCE) {
	SubsidenceUpdate = 1;

	/*calculate subsidence based on maximum ice content in layer*/
	/*constrain subsidence by MAX_SUBSIDENCE*/
	tmp_depth_prior = soil_con->depth[lidx];//m
	tmp_subsidence = (1000.*tmp_depth_prior - max_ice_layer);//mm
	if(tmp_subsidence > MAX_SUBSIDENCE) 
	  tmp_subsidence = MAX_SUBSIDENCE;
	tmp_depth = tmp_depth_prior - tmp_subsidence/1000.;//m
	if(tmp_depth <= soil_con->min_depth[lidx])
	  tmp_depth = soil_con->min_depth[lidx];
	soil_con->depth[lidx] = (float)(int)(tmp_depth * 1000 + 0.5) / 1000;//m
	subsidence[lidx] = (tmp_depth_prior - soil_con->depth[lidx])*1000.;//mm
	total_subsidence += (tmp_depth_prior - soil_con->depth[lidx]);//m

	if(subsidence[lidx] > 0 ){
#if VERBOSE
	  fprintf(stderr,"Subsidence of %.3f m in layer %d:\n",subsidence[lidx]/1000.,lidx+1);
	  fprintf(stderr,"\t\tOccurred for record=%d: year=%d, month=%d, day=%d, hour=%d\n",rec,dmy[rec].year,dmy[rec].month,dmy[rec].day, dmy[rec].hour);
	  fprintf(stderr,"\t\tDepth of soil layer decreased from %.3f m to %.3f m.\n",tmp_depth_prior,soil_con->depth[lidx]);
#endif	  

	  /*update soil_con properties*/
#if VERBOSE
	  fprintf(stderr,"\t\tEffective porosity decreased from %.3f to %.3f.\n",soil_con->effective_porosity[lidx],1.0-(1.0-soil_con->effective_porosity[lidx])*tmp_depth_prior/soil_con->depth[lidx]);
#endif
	  soil_con->effective_porosity[lidx]=1.0-(1.0-soil_con->effective_porosity[lidx])*tmp_depth_prior/soil_con->depth[lidx];
	  if(tmp_depth <= soil_con->min_depth[lidx])
	    soil_con->effective_porosity[lidx]=soil_con->porosity[lidx];
#if VERBOSE
	  fprintf(stderr,"\t\tBulk density increased from %.2f kg/m^3 to %.2f kg/m^3.\n",soil_con->bulk_density[lidx],(1.0-soil_con->effective_porosity[lidx])*soil_con->soil_density[lidx]);
#endif
	  soil_con->bulk_density[lidx] = (1.0-soil_con->effective_porosity[lidx])*soil_con->soil_density[lidx]; //adjust bulk density
	  total_meltwater += soil_con->max_moist[lidx] - soil_con->depth[lidx] * soil_con->effective_porosity[lidx] * 1000.; //for lake model (uses prior max_moist, 
	                                                                                                                     //so must come before new max_moist calculation
	  soil_con->max_moist[lidx] = soil_con->depth[lidx] * soil_con->effective_porosity[lidx] * 1000.;
	  
	}//subsidence occurs
      }//threshold exceeded
      
    }//excess ice exists
  } //loop for each soil layer
  if(total_subsidence>0){

    /********update remaining soil_con properties**********/
#if VERBOSE
    fprintf(stderr,"Damping depth decreased from %.3f m to %.3f m.\n",soil_con->dp,soil_con->dp-total_subsidence);
#endif
    soil_con->dp -= total_subsidence;  //adjust damping depth

#if VERBOSE
    fprintf(stderr,"More updated parameters in soil_con: max_infil, Wcr, and Wpwp\n");
#endif  
    /* update Maximum Infiltration for Upper Layers */
    if(options.Nlayer==2)
      soil_con->max_infil = (1.0+soil_con->b_infilt)*soil_con->max_moist[0];
    else
      soil_con->max_infil = (1.0+soil_con->b_infilt)*(soil_con->max_moist[0]+soil_con->max_moist[1]);
    
    /* Soil Layer Critical and Wilting Point Moisture Contents */
    for(lidx=0;lidx<options.Nlayer;lidx++) {//soil layer
      soil_con->Wcr[lidx]  = soil_con->Wcr_FRACT[lidx] * soil_con->max_moist[lidx];
      soil_con->Wpwp[lidx] = soil_con->Wpwp_FRACT[lidx] * soil_con->max_moist[lidx];
      if(soil_con->Wpwp[lidx] > soil_con->Wcr[lidx]) {
	sprintf(ErrStr,"Updated wilting point moisture (%f mm) is greater than updated critical point moisture (%f mm) for layer %d.\n\tIn the soil parameter file, Wpwp_FRACT MUST be <= Wcr_FRACT.\n",
		soil_con->Wpwp[lidx], soil_con->Wcr[lidx], lidx);
	nrerror(ErrStr);
      }
      if(soil_con->Wpwp[lidx] < soil_con->resid_moist[lidx] * soil_con->depth[lidx] * 1000.) {
	sprintf(ErrStr,"Updated wilting point moisture (%f mm) is less than updated residual moisture (%f mm) for layer %d.\n\tIn the soil parameter file, Wpwp_FRACT MUST be >= resid_moist / (1.0 - bulk_density/soil_density).\n",
		soil_con->Wpwp[lidx], soil_con->resid_moist[lidx] * soil_con->depth[lidx] * 1000., lidx);
	nrerror(ErrStr);
      }
    }      

    /* If BASEFLOW = NIJSSEN2001 then convert ARNO baseflow
       parameters d1, d2, d3, and d4 to Ds, Dsmax, Ws, and c */
    if(options.BASEFLOW == NIJSSEN2001) {
      lidx = options.Nlayer-1;
      soil_con->Dsmax = soil_con->Dsmax_orig * 
	pow((double)(1./(soil_con->max_moist[lidx]-soil_con->Ws_orig)), -soil_con->c) +
	soil_con->Ds_orig * soil_con->max_moist[lidx];
      soil_con->Ds = soil_con->Ds_orig * soil_con->Ws_orig / soil_con->Dsmax_orig;
      soil_con->Ws = soil_con->Ws_orig/soil_con->max_moist[lidx];
#if VERBOSE
      fprintf(stderr,"More updated parameters in soil_con: Dsmax, Ds, Ws\n");
#endif    
    }
    
    /*********** update root fractions ***************/
    fprintf(stderr,"Updated parameter in veg_con: root\n");
    calc_root_fractions(veg_con, soil_con);

    /**********redistribute soil moisture (call runoff function)*************/
    /* If subsidence occurs, recalculate runoff, baseflow, and soil moisture
       using soil moisture values from previous time-step; i.e.
       as if prior runoff call did not occur.*/
    for(iveg = 0; iveg <= Nveg; iveg++){
      for ( band = 0; band < Nbands; band++ ) {
	for ( dist = 0; dist < Ndist; dist++ ) {
	  for(lidx=0;lidx<options.Nlayer;lidx++) {
	    cell[dist][iveg][band].layer[lidx].moist = moist_prior[dist][iveg][band][lidx];
	    cell[dist][iveg][band].layer[lidx].evap = evap_prior[dist][iveg][band][lidx];
	  }
	}
      }
    }
    for(iveg = 0; iveg <= Nveg; iveg++){
      if (veg_con[iveg].Cv  > 0.) {
	for ( band = 0; band < Nbands; band++ ) {
	  if( soil_con->AreaFract[band] > 0 ) { 

	    //set inflow for runoff call 
	    ppt[WET]=cell[WET][iveg][band].inflow;
	    ppt[DRY]=cell[DRY][iveg][band].inflow;
	    
	    ErrorFlag = runoff(&(cell[WET][iveg][band]), &(cell[DRY][iveg][band]), &(energy[iveg][band]), 
			       soil_con, ppt, 
			       SubsidenceUpdate,
#if SPATIAL_FROST
			       soil_con->frost_fract,
#endif // SPATIAL_FROST
			       prcp->mu[iveg], gp->dt, options.Nnode, band, rec, iveg);
	    if ( ErrorFlag == ERROR ) return ( ERROR );

	  }
	}//band
      }
    }//veg
  
    /**********interpolate nodal temperatures to new depths and recalculate thermal properties***********/
    ErrorFlag = update_thermal_nodes(prcp, Nveg, options.Nnode, Ndist, soil_con, veg_con);
    if ( ErrorFlag == ERROR ) return ( ERROR );

  }//subsidence occurs

  /********************************************
    Save subsidence for output
  ********************************************/
  for(lidx=0;lidx<options.Nlayer;lidx++)
    soil_con->subsidence[lidx] = subsidence[lidx];
  
#endif //EXCESS_ICE

  /****************************
     Run Lake Model           
  ****************************/

  /** Compute total runoff and baseflow for all vegetation types
      within each snowband. **/
  if ( options.LAKES && lake_con->Cl[0] > 0 ) {

    wetland_runoff = wetland_baseflow = 0;
    sum_runoff = sum_baseflow = 0;
	
    // Loop through snow elevation bands
    for ( band = 0; band < Nbands; band++ ) {
      if ( soil_con->AreaFract[band] > 0 ) {
	
	// Loop through all vegetation types
	for ( iveg = 0; iveg <= Nveg; iveg++ ) {
	  
	  /** Solve Veg Type only if Coverage Greater than 0% **/
	  if (veg_con[iveg].Cv  > 0.) {

	    Cv = veg_con[iveg].Cv;
	    if (veg_con[iveg].LAKE) Cv *= (1-lakefrac);

	    // Loop through distributed precipitation fractions
	    for ( dist = 0; dist < 2; dist++ ) {
	      
	      if ( dist == 0 ) 
		tmp_mu = prcp->mu[iveg]; 
	      else 
		tmp_mu = 1. - prcp->mu[iveg]; 
	      
              if (veg_con[iveg].LAKE) {
                wetland_runoff += ( cell[dist][iveg][band].runoff * tmp_mu
                            * Cv * soil_con->AreaFract[band] );
                wetland_baseflow += ( cell[dist][iveg][band].baseflow * tmp_mu
                            * Cv * soil_con->AreaFract[band] );
                cell[dist][iveg][band].runoff = 0;
                cell[dist][iveg][band].baseflow = 0;
              }
              else {
                sum_runoff += ( cell[dist][iveg][band].runoff * tmp_mu
                              * Cv * soil_con->AreaFract[band] );
                sum_baseflow += ( cell[dist][iveg][band].baseflow * tmp_mu
                                * Cv * soil_con->AreaFract[band] );
                cell[dist][iveg][band].runoff *= (1-lake_con->rpercent);
                cell[dist][iveg][band].baseflow *= (1-lake_con->rpercent);
              }

	    }
	  }
	}
      }
    }

    /** Run lake model **/
    iveg = lake_con->lake_idx;
    band = 0;
    lake_var->runoff_in   = sum_runoff * lake_con->rpercent + wetland_runoff;
    lake_var->baseflow_in = sum_baseflow * lake_con->rpercent + wetland_baseflow;
    rainonly = calc_rainonly(atmos->air_temp[NR], atmos->prec[NR], 
			     gp->MAX_SNOW_TEMP, gp->MIN_RAIN_TEMP, 1);
    if ( (int)rainonly == ERROR ) {
      return( ERROR );
    }

    /**********************************************************************
     * Solve the energy budget for the lake.
     **********************************************************************/

    oldvolume = lake_var->volume;
    oldsnow = lake_snow.swq;
    snowprec = gauge_correction[SNOW] * (atmos->prec[NR] - rainonly);
    rainprec = gauge_correction[SNOW] * rainonly;
    atmos->out_prec += (snowprec + rainprec) * lake_con->Cl[0] * lakefrac;

    ErrorFlag = solve_lake(snowprec, rainprec, atmos->air_temp[NR],
                           atmos->wind[NR], atmos->vp[NR] / 1000.,
                           atmos->shortwave[NR], atmos->longwave[NR],
                           atmos->vpd[NR] / 1000.,
                           atmos->pressure[NR] / 1000.,
                           atmos->density[NR], lake_var, *lake_con,
                           *soil_con, gp->dt, rec, &lake_energy, &lake_snow,
                           gp->wind_h, dmy[rec], fraci);
    if ( ErrorFlag == ERROR ) return (ERROR);

    /**********************************************************************
     * Solve the water budget for the lake.
     **********************************************************************/

    ErrorFlag = water_balance(lake_var, *lake_con, gp->dt, prcp, rec, iveg, band,
                              lakefrac, *soil_con,
#if EXCESS_ICE
                              SubsidenceUpdate, total_meltwater,
#endif
                              snowprec+rainprec, oldvolume,
                              oldsnow-lake_snow.swq, lake_snow.vapor_flux,
                              fraci);
    if ( ErrorFlag == ERROR ) return (ERROR);

    /**********************************************************************
     * Reallocate fluxes between lake and wetland fractions.
     **********************************************************************/

    update_prcp(prcp, &lake_energy, &lake_snow, lakefrac, lake_var,
                *lake_con, iveg, band, *soil_con);



#if LINK_DEBUG
    if ( debug.PRT_LAKE ) { 
      if ( rec == 0 ) {
	// print file header
	fprintf(debug.fg_lake,"Date,Rec,AeroResist,BasinflowIn,BaseflowOut");
	for ( i = 0; i < MAX_LAKE_NODES; i++ )
	  fprintf(debug.fg_lake, ",Density%i", i);
	fprintf(debug.fg_lake,",Evap,IceArea,IceHeight,LakeDepth,RunoffIn,RunoffOut,SurfaceArea,SnowDepth,SnowMelt");
	for ( i = 0; i < MAX_LAKE_NODES; i++ )
	  fprintf(debug.fg_lake, ",Area%i", i);
	fprintf(debug.fg_lake,",SWE");
	for ( i = 0; i < MAX_LAKE_NODES; i++ )
	  fprintf(debug.fg_lake, ",Temp%i", i);
	fprintf(debug.fg_lake,",IceTemp,Volume,Nodes");
	fprintf(debug.fg_lake,",AlbedoLake,AlbedoOver,AlbedoUnder,AtmosError,AtmosLatent,AtmosLatentSub,AtmosSensible,LongOverIn,LongUnderIn,LongUnderOut,NetLongAtmos,NetLongOver,NetLongUnder,NetShortAtmos,NetShortGrnd,NetShortOver,NetShortUnder");
	fprintf(debug.fg_lake,",ShortOverIn,ShortUnderIn,advection,deltaCC,deltaH,error,fusion,grnd_flux,latent,latent_sub,longwave,melt_energy,out_long_surface,refreeze_energy,sensible,shortwave");
	fprintf(debug.fg_lake,",Qnet,albedo,coldcontent,coverage,density,depth,mass_error,max_swq,melt,pack_temp,pack_water,store_coverage,store_swq,surf_temp,surf_water,swq,swq_slope,vapor_flux,last_snow,store_snow\n");
      }

      // print lake variables
      fprintf(debug.fg_lake, "%i/%i/%i %i:00:00,%i", dmy[rec].month, dmy[rec].day, dmy[rec].year, dmy[rec].hour, rec);
      fprintf(debug.fg_lake, ",%f", lake_var->aero_resist);
      fprintf(debug.fg_lake, ",%f", lake_var->baseflow_in);
      fprintf(debug.fg_lake, ",%f", lake_var->baseflow_out);
      for ( i = 0; i < MAX_LAKE_NODES; i++ )
	fprintf(debug.fg_lake, ",%f", lake_var->density[i]);
      fprintf(debug.fg_lake, ",%f", lake_var->evapw);
      fprintf(debug.fg_lake, ",%f", lake_var->areai);
      fprintf(debug.fg_lake, ",%f", lake_var->hice);
      fprintf(debug.fg_lake, ",%f", lake_var->ldepth);
      fprintf(debug.fg_lake, ",%f", lake_var->runoff_in);
      fprintf(debug.fg_lake, ",%f", lake_var->runoff_out);
      fprintf(debug.fg_lake, ",%f", lake_var->sarea);
      fprintf(debug.fg_lake, ",%f", lake_var->sdepth);
      fprintf(debug.fg_lake, ",%f", lake_var->snowmlt);
      for ( i = 0; i < MAX_LAKE_NODES; i++ )
	fprintf(debug.fg_lake, ",%f", lake_var->surface[i]);
      fprintf(debug.fg_lake, ",%f", lake_var->swe);
      for ( i = 0; i < MAX_LAKE_NODES; i++ )
	fprintf(debug.fg_lake, ",%f", lake_var->temp[i]);
      fprintf(debug.fg_lake, ",%f", lake_var->tempi);
      fprintf(debug.fg_lake, ",%f", lake_var->volume);
      fprintf(debug.fg_lake, ",%i", lake_var->activenod);
      
      // print lake energy variables
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].AlbedoLake);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].AlbedoOver);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].AlbedoUnder);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].AtmosError);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].AtmosLatent);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].AtmosLatentSub);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].AtmosSensible);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].LongOverIn);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].LongUnderIn);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].LongUnderOut);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].NetLongAtmos);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].NetLongOver);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].NetLongUnder);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].NetShortAtmos);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].NetShortGrnd);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].NetShortOver);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].NetShortUnder);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].ShortOverIn);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].ShortUnderIn);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].advection);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].deltaCC);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].deltaH);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].error);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].fusion);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].grnd_flux);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].latent);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].latent_sub);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].longwave);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].melt_energy);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].out_long_surface);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].refreeze_energy);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].sensible);
      fprintf(debug.fg_lake, ",%f", energy[lake_con->lake_idx][0].shortwave);
      
      // print lake snow variables
      fprintf(debug.fg_lake, ",%f", snow[lake_con->lake_idx][0].Qnet);
      fprintf(debug.fg_lake, ",%f", snow[lake_con->lake_idx][0].albedo);
      fprintf(debug.fg_lake, ",%f", snow[lake_con->lake_idx][0].coldcontent);
      fprintf(debug.fg_lake, ",%f", snow[lake_con->lake_idx][0].coverage);
      fprintf(debug.fg_lake, ",%f", snow[lake_con->lake_idx][0].density);
      fprintf(debug.fg_lake, ",%f", snow[lake_con->lake_idx][0].depth);
      fprintf(debug.fg_lake, ",%f", snow[lake_con->lake_idx][0].mass_error);
      fprintf(debug.fg_lake, ",%f", snow[lake_con->lake_idx][0].max_swq);
      fprintf(debug.fg_lake, ",%f", snow[lake_con->lake_idx][0].melt);
      fprintf(debug.fg_lake, ",%f", snow[lake_con->lake_idx][0].pack_temp);
      fprintf(debug.fg_lake, ",%f", snow[lake_con->lake_idx][0].pack_water);
      fprintf(debug.fg_lake, ",%f", snow[lake_con->lake_idx][0].store_coverage);
      fprintf(debug.fg_lake, ",%f", snow[lake_con->lake_idx][0].store_swq);
      fprintf(debug.fg_lake, ",%f", snow[lake_con->lake_idx][0].surf_temp);
      fprintf(debug.fg_lake, ",%f", snow[lake_con->lake_idx][0].surf_water);
      fprintf(debug.fg_lake, ",%f", snow[lake_con->lake_idx][0].swq);
      fprintf(debug.fg_lake, ",%f", snow[lake_con->lake_idx][0].swq_slope);
      fprintf(debug.fg_lake, ",%f", snow[lake_con->lake_idx][0].vapor_flux);
      fprintf(debug.fg_lake, ",%i", snow[lake_con->lake_idx][0].last_snow);
      fprintf(debug.fg_lake, ",%i\n", snow[lake_con->lake_idx][0].store_snow);

    }
#endif // LINK_DEBUG

  }

  return (0);
}

