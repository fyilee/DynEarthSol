#ifndef DYNEARTHSOL3D_PARAMETERS_HPP
#define DYNEARTHSOL3D_PARAMETERS_HPP

#include <string>
#include <utility>
#include <vector>

#include "constants.hpp"
#include "array2d.hpp"

typedef std::pair<double,double> double_pair;

typedef std::vector<double> double_vec;
typedef std::vector<int> int_vec;

typedef Array2D<double,NDIMS> arrayd2;
typedef Array2D<double,NSTR> tensord2;
typedef Array2D<double,NODES_PER_ELEM> shapefn;

typedef Array2D<int,NODES_PER_ELEM> conn_t;
typedef Array2D<int,NDIMS> segment_t;
typedef Array2D<int,1> segflag_t;

//
// Structures for input parameters
//
struct Sim {
    double max_time_in_yr;
    double output_time_interval_in_yr;
    int max_steps;
    int output_step_interval;
    bool is_restarting;

    std::string modelname;
};

struct Mesh {
    int meshing_option;

    double xlength, ylength, zlength;
    double resolution;
    // for 2D only
    double min_angle;
    // for 3D only
    double min_tet_angle, max_ratio;

    double_pair refined_zonex, refined_zoney, refined_zonez;
};

struct Control {
    double gravity;
    double inertial_scaling;
    double damping_factor;
};

struct BC {
    double surface_temperature;
    double mantle_temperature;

    double max_vbc_val;

    double wrinkler_delta_rho;
    int wrinkler_foundation;
};

struct Mat {
    int rheol_type;
    int nmat;
    double visc_min;
    double visc_max;
    double therm_diff_max;
};

struct Param {
    Sim sim;
    Mesh mesh;
    Control control;
    BC bc;
    Mat mat;
};


//
// Structures for model variables
//
class MatProps;
struct Variables {
    double time;
    double dt;
    int steps;
    int frame;

    int nnode;
    int nelem;
    int nseg;

    double compensation_pressure;

    // These 4 arrays are allocated by external library
    arrayd2 *coord;
    conn_t *connectivity;
    segment_t *segment;
    segflag_t *segflag;

    int_vec *bcflag;
    std::vector< std::pair<int,int> > bfacets[6];

    std::vector<int_vec> *support, *egroups;

    double_vec *volume, *volume_old, *volume_n;
    double_vec *mass, *tmass;
    double_vec *dvoldt, *edvoldt;
    double_vec *temperature, *plstrain;
    double_vec *tmp0;

    arrayd2 *vel, *force;
    tensord2 *strain_rate, *strain, *stress;
    shapefn *shpdx, *shpdy, *shpdz;

    MatProps *mat;
};

#endif
