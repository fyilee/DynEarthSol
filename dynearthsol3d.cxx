#include <cstdio>
#include <ctime>
#include <iostream>

#include "constants.hpp"
#include "parameters.hpp"
#include "matprops.hpp"
#include "mesh.hpp"

static void allocate_variables(Variables& var)
{
    const int n = var.nnode;
    const int e = var.nelem;

    var.volume.resize(e);
    var.volume_old.resize(e);
    var.volume_n.resize(n);

    var.mass.resize(n);
    var.tmass.resize(n);

    var.jacobian.resize(n);
    var.ejacobian.resize(e);

    var.temperature.resize(n);
    var.plstrain.resize(e);
    var.tmp0.resize(std::max(n,e));

    var.vel = new double2d(boost::extents[n][NDIMS]);
    var.force = new double2d(boost::extents[n][NDIMS]);

    var.strain_rate = new double2d(boost::extents[e][NSTR]);
    var.strain = new double2d(boost::extents[e][NSTR]);
    var.stress = new double2d(boost::extents[e][NSTR]);

    var.shpdx = new double2d(boost::extents[e][NODES_PER_ELEM]);
    if (NDIMS == 3) var.shpdy = new double2d(boost::extents[e][NODES_PER_ELEM]);
    var.shpdz = new double2d(boost::extents[e][NODES_PER_ELEM]);
}


static void create_matprops(const Param &par, Variables &var)
{
    // TODO: get material properties from cfg file
    var.mat = new MatProps(1, MatProps::rh_evp);
}


static double tetrahedron_volume(const double *a,
                                 const double *b,
                                 const double *c,
                                 const double *d)
{
    // TODO
    std::exit(-100);
    return 1;
}


static double triangle_area(const double *a,
                            const double *b,
                            const double *c)
{
    double ab0, ab1, ac0, ac1;

    // ab: vector from a to b
    ab0 = b[0] - a[0];
    ab1 = b[1] - a[1];
    // ac: vector from a to c
    ac0 = c[0] - a[0];
    ac1 = c[1] - a[1];

    // area = (cross product of ab and ac) / 2
    return (ab0*ac1 - ab1*ac0) / 2;
}


static void compute_volume(const double2d &coord, const int2d &connectivity,
                           double_vec &volume, double_vec &volume_n)
{
    const int nelem = connectivity.shape()[0];
    for (int e=0; e<nelem; ++e) {
        int n0 = connectivity[e][0];
        int n1 = connectivity[e][1];
        int n2 = connectivity[e][2];

        const double *a = &coord[n0][0];
        const double *b = &coord[n1][0];
        const double *c = &coord[n2][0];

        double vol;
        if (NDIMS == 3) {
            int n3 = connectivity[e][3];
            const double *d = &coord[n3][0];
            vol = tetrahedron_volume(a, b, c, d);
        }
        else {
            vol = triangle_area(a, b, c);
        }
        volume[e] = vol;
        //std::cout << e << ": volume =" << vol << '\n';
    }

    // volume_n is (node-averaged volume * NODES_PER_ELEM)
    // volume_n[n] is init'd to 0 by resize()
    for (int e=0; e<nelem; ++e) {
        for (int i=0; i<NODES_PER_ELEM; ++i) {
            int n = connectivity[e][i];
            volume_n[n] += volume[e];
        }
    }

    //for (int i=0; i<volume_n.size(); ++i)
    //    std::cout << i << ": volume_n = " << volume_n[i] << '\n';
}


static void compute_mass(const double2d &coord, const int2d &connectivity,
                         const double_vec &volume, const MatProps &mat,
                         double_vec &mass, double_vec &tmass)
{
    const int nelem = connectivity.shape()[0];
    for (int e=0; e<nelem; ++e) {
        for (int i=0; i<NODES_PER_ELEM; ++i) {
            int n = connectivity[e][i];
            // TODO
            const double maxvbcval = 1e-10;
            const double pseudo_factor = 1e5; // == 1/strain_inert in geoflac
            double pseudo_speed = maxvbcval * pseudo_factor;
            double pseudo_rho = mat.bulkm(e) / (pseudo_speed * pseudo_speed);
            mass[n] += pseudo_rho * volume[e] / NODES_PER_ELEM;
            tmass[n] += mat.density(e) * mat.cp(e) * volume[e] / NODES_PER_ELEM;
        }
    }
    //for (int i=0; i<mass.size(); ++i)
    //    std::cout << i << ": mass = " << mass[i] << '\n';

    //for (int i=0; i<tmass.size(); ++i)
    //    std::cout << i << ": tmass = " << tmass[i] << '\n';
}


void init(const Param& param, Variables& var)
{
    void create_matprops(const Param&, Variables&);

    create_new_mesh(param, var);
    allocate_variables(var);
    // XXX
    //create_nsupport(*connectivity, nnode, var.nsupport, var.support);
    create_matprops(param, var);

    compute_volume(*var.coord, *var.connectivity, var.volume, var.volume_n);
    compute_mass(*var.coord, *var.connectivity, var.volume, *var.mat,
                 var.mass, var.tmass);
};


void restart() {};
void update_temperature() {};
void update_strain_rate() {};
void update_stress() {};
void update_force() {};
void update_mesh() {};
void rotate_stress() {};


void output(const Param& param, const Variables& var)
{
    /* Not using C++ stream IO here since it can be much slower than C stdio. */

    using namespace std;
    char buffer[255];
    std::FILE* f;

    double run_time = double(std::clock()) / CLOCKS_PER_SEC;

    // info
    snprintf(buffer, 255, "%s.%s", param.sim.modelname.c_str(), "info");
    if (var.frame == 0)
        f = fopen(buffer, "w");
    else
        f = fopen(buffer, "a");

    snprintf(buffer, 255, "%6d\t%10d\t%12.6e\t%12.4e\t%12.6e\t%8d\t%8d\t%8d\n",
             var.frame, var.steps, var.time, var.dt, run_time,
             var.nnode, var.nelem, var.nseg);
    fputs(buffer, f);
    fclose(f);

    // coord
    snprintf(buffer, 255, "%s.%s.%06d", param.sim.modelname.c_str(), "coord", var.frame);
    f = fopen(buffer, "w");
    fwrite(var.coord->data(), sizeof(double), var.coord->num_elements(), f);
    fclose(f);

    // connectivity
    snprintf(buffer, 255, "%s.%s.%06d", param.sim.modelname.c_str(), "connectivity", var.frame);
    f = fopen(buffer, "w");
    fwrite(var.connectivity->data(), sizeof(int), var.connectivity->num_elements(), f);
    fclose(f);

}


int main(int argc, const char* argv[])
{
    //
    // read command line
    //
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " config_file\n";
        return -1;
    }

    Param param;
    void get_input_parameters(const char*, Param&);
    get_input_parameters(argv[1], param);

    //
    // run simulation
    //
    static Variables var; // declared as static to silence valgrind's memory leak detection
    var.time = 0;
    var.dt = 1e7;
    var.steps = 0;
    var.frame = 0;

    if (! param.sim.is_restarting) {
        init(param, var);
        output(param, var);
        var.frame ++;
    }
    else {
        restart();
        var.frame ++;
    }

    do {
        var.steps ++;
        var.time += var.dt;

        update_temperature();
        update_strain_rate();
        update_stress();
        update_force();
        update_mesh();
        rotate_stress();

        std::cout << "Step: " << var.steps << ", time:" << var.time << "\n";

        if ( (var.steps >= var.frame*param.sim.output_step_interval) ||
             (var.time >= var.frame*param.sim.output_time_interval) ) {
            output(param, var);
            std::cout << var.frame <<"-th output\n";
            var.frame ++;
        }

    } while (var.steps < param.sim.max_steps && var.time <= param.sim.max_time);

    return 0;
}
