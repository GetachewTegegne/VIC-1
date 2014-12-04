/******************************************************************************
 * @section DESCRIPTION
 *
 * This subroutine opens a model state file and verifys that the starting date,
 * number of layers and number of thermal nodes in the file agrees with what
 * was defined in the model global control file.
 *
 * @section LICENSE
 *
 * The Variable Infiltration Capacity (VIC) macroscale hydrological model
 * Copyright (C) 2014 The Land Surface Hydrology Group, Department of Civil
 * and Environmental Engineering, University of Washington.
 *
 * The VIC model is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *****************************************************************************/

#include <vic_def.h>
#include <vic_run.h>
#include <vic_driver_classic.h>

/******************************************************************************
 * @brief    This subroutine opens a model state file and verifys that the
             starting date, number of layers and number of thermal nodes in the
             file agrees with what was defined in the model global control file.
 *****************************************************************************/
FILE *
check_state_file(char  *init_state_name,
                 size_t Nlayer,
                 size_t Nnodes,
                 int   *startrec)
{
    extern option_struct options;

    FILE                *init_state;
    char                 ErrStr[MAXSTRING];
    size_t               tmp_Nlayer;
    size_t               tmp_Nnodes;
    unsigned short       startday, startmonth, startyear;

    /* open state file */
    if (options.BINARY_STATE_FILE) {
        init_state = open_file(init_state_name, "rb");
    }
    else {
        init_state = open_file(init_state_name, "r");
    }

    /* Initialize startrec */
    *startrec = 0;

    /* Check state date information */
    if (options.BINARY_STATE_FILE) {
        fread(&startyear, sizeof(int), 1, init_state);
        fread(&startmonth, sizeof(int), 1, init_state);
        fread(&startday, sizeof(int), 1, init_state);
    }
    else {
        fscanf(init_state, "%hu %hu %hu\n", &startyear, &startmonth, &startday);
    }

    /* Check simulation options */
    if (options.BINARY_STATE_FILE) {
        fread(&tmp_Nlayer, sizeof(int), 1, init_state);
        fread(&tmp_Nnodes, sizeof(int), 1, init_state);
    }
    else {
        fscanf(init_state, "%zu %zu\n", &tmp_Nlayer, &tmp_Nnodes);
    }
    if (tmp_Nlayer != Nlayer) {
        sprintf(ErrStr,
                "The number of soil moisture layers in the model state file "
                "(%zu) does not equal that defined in the global control file "
                "(%zu).  Check your input files.", tmp_Nlayer, Nlayer);
        nrerror(ErrStr);
    }
    if (tmp_Nnodes != Nnodes) {
        sprintf(ErrStr,
                "The number of soil thermal nodes in the model state file "
                "(%zu) does not equal that defined in the global control file "
                "(%zu).  Check your input files.", tmp_Nnodes, Nnodes);
        nrerror(ErrStr);
    }

    return(init_state);
}