#include <cmath>
#include <iostream>

#include "3x3-C/dsyevh3.h"

#include "constants.hpp"
#include "parameters.hpp"
#include "matprops.hpp"
#include "rheology.hpp"
#include "utils.hpp"


static void principal_stresses3(const double* s, double p[3], double v[3][3])
{
    /* s is a flattened stress vector, with the components {XX, YY, ZZ, XY, XZ, YZ}.
     * Returns the eigenvalues p and eignvectors v.
     * The eigenvalues are ordered such that p[0] <= p[1] <= p[2].
     */

    // unflatten s to a 3x3 tensor, only the upper part is needed.
    double a[3][3];
    a[0][0] = s[0];
    a[1][1] = s[1];
    a[2][2] = s[2];
    a[0][1] = s[3];
    a[0][2] = s[4];
    a[1][2] = s[5];

    dsyevh3(a, v, p);

    // reorder p and v
    if (p[0] > p[1]) {
        double tmp, b[3];
        tmp = p[0];
        p[0] = p[1];
        p[1] = tmp;
        for (int i=0; i<3; ++i)
            b[i] = v[i][0];
        for (int i=0; i<3; ++i)
            v[i][0] = v[i][1];
        for (int i=0; i<3; ++i)
            v[i][1] = b[i];
    }
    if (p[1] > p[2]) {
        double tmp, b[3];
        tmp = p[1];
        p[1] = p[2];
        p[2] = tmp;
        for (int i=0; i<3; ++i)
            b[i] = v[i][1];
        for (int i=0; i<3; ++i)
            v[i][1] = v[i][2];
        for (int i=0; i<3; ++i)
            v[i][2] = b[i];
    }
    if (p[0] > p[1]) {
        double tmp, b[3];
        tmp = p[0];
        p[0] = p[1];
        p[1] = tmp;
        for (int i=0; i<3; ++i)
            b[i] = v[i][0];
        for (int i=0; i<3; ++i)
            v[i][0] = v[i][1];
        for (int i=0; i<3; ++i)
            v[i][1] = b[i];
    }
}


static void principal_stresses2(const double* s, double p[2],
                                double& cos2t, double& sin2t)
{
    /* 's' is a flattened stress vector, with the components {XX, ZZ, XZ}.
     * Returns the eigenvalues 'p', and the direction cosine of the
     * eigenvectors in the X-Z plane.
     * The eigenvalues are ordered such that p[0] <= p[1].
     */

    // center and radius of Mohr circle
    double s0 = 0.5 * (s[0] + s[1]);
    double rad = second_invariant(s);

    // principal stresses in the X-Z plane
    p[0] = s0 - rad;
    p[1] = s0 + rad;

    {
        // direction cosine and sine of 2*theta
        const double eps = 1e-15;
        double a = 0.5 * (s[0] - s[1]);
        double b = - rad; // always negative
        if (b < -eps) {
            cos2t = a / b;
            sin2t = s[2] / b;
        }
        else {
            cos2t = 1;
            sin2t = 0;
        }
    }
}


static void elastic(double bulkm, double shearm, const double* de, double* s)
{
    /* increment the stress s according to the incremental strain de */
    double lambda = bulkm - 2. /3 * shearm;
    double dev = trace(de);

    for (int i=0; i<NDIMS; ++i)
        s[i] += 2 * shearm * de[i] + lambda * dev;
    for (int i=NDIMS; i<NSTR; ++i)
        s[i] += 2 * shearm * de[i];
}


static void maxwell(double bulkm, double shearm, double viscosity, double dt,
                    double dv, const double* de, double* s)
{
    // non-dimensional parameter: dt/ relaxation time
    double tmp = 0.5 * dt * shearm / viscosity;
    double f1 = 1 - tmp;
    double f2 = 1 / (1  + tmp);

    double dev = trace(de) / NDIMS;
    double s0 = trace(s) / NDIMS;

    // istropic stress is elastic
    s0 += bulkm * dv;

    // convert back to total stress
    for (int i=0; i<NDIMS; ++i)
        s[i] = ((s[i] - s0) * f1 + 2 * shearm * (de[i] - dev)) * f2 + s0;
    for (int i=NDIMS; i<NSTR; ++i)
        s[i] = (s[i] * f1 + 2 * shearm * de[i]) * f2;
}


static void viscous(double bulkm, double viscosity, double total_dv,
                    const double* edot, double* s)
{
    /* Viscous Model + incompressibility enforced by bulk modulus */

    double dev = trace(edot) / NDIMS;

    for (int i=0; i<NDIMS; ++i)
        s[i] = 2 * viscosity * (edot[i] - dev) + bulkm * total_dv;
    for (int i=NDIMS; i<NSTR; ++i)
        s[i] = 2 * viscosity * edot[i];
}


static void elasto_plastic(double bulkm, double shearm,
                           double amc, double anphi, double anpsi,
                           double hardn, double ten_max,
                           const double* de, double& depls, double* s,
                           int &zzz)
{
    /* Elasto-plasticity (Mohr-Coulomb criterion) */

    // elastic trial stress
    elastic(bulkm, shearm, de, s);
    depls = 0;

    //
    // transform to principal stress coordinate system
    //
    // eigenvalues (principal stresses)
    double p[NDIMS];
#ifdef THREED
    // eigenvectors
    double v[3][3];
    principal_stresses3(s, p, v);
#else
    // In 2D, we only construct the eigenvectors from
    // cos(2*theta) and sin(2*theta) of Mohr circle
    double cos2t, sin2t;
    principal_stresses2(s, p, cos2t, sin2t);
#endif

    // composite (shear and tensile) yield criterion
    double fs = p[0] - p[NDIMS-1] * anphi + amc;
    double ft = p[NDIMS-1] - ten_max;

    if (fs > 0 && ft < 0) {
        // no failure
        return;
    }

    // yield, shear or tensile?
    double pa = std::sqrt(1 + anphi*anphi) + anphi;
    double ps = ten_max * anphi - amc;
    double h = p[NDIMS-1] - ten_max + pa * (p[0] - ps);
    double a1 = bulkm + 4. / 3 * shearm;
    double a2 = bulkm - 2. / 3 * shearm;

    double alam;
    if (h < 0) {
        // shear failure
        alam = fs / (a1 - a2*anpsi + a1*anphi*anpsi - a2*anphi + 2*std::sqrt(anphi)*hardn);
        p[0] -= alam * (a1 - a2 * anpsi);
#ifdef THREED
        p[1] -= alam * (a2 - a2 * anpsi);
#endif
        p[NDIMS-1] -= alam * (a2 - a1 * anpsi);

        // 2nd invariant of plastic strain
#ifdef THREED
        /* // plastic strain in the principle directions, depls2 is always 0
         * double depls1 = alam;
         * double depls3 = -alam * anpsi;
         * double deplsm = (depls1 + depls3) / 3;
         * depls = std::sqrt(((depls1-deplsm)*(depls1-deplsm) +
         *                    (-deplsm)*(-deplsm) +
         *                    (depls3-deplsm)*(depls3-deplsm) +
         *                    deplsm*deplsm) / 2);
         */
        // the equations above can be reduce to:
        depls = std::fabs(alam) * std::sqrt((7 + 4*anpsi + 7*anpsi*anpsi) / 18);
#else
        /* // plastic strain in the principle directions
         * double depls1 = alam;
         * double depls2 = -alam * anpsi;
         * double deplsm = (depls1 + depls2) / 2;
         * depls = std::sqrt(((depls1-deplsm)*(depls1-deplsm) +
         *                    (depls2-deplsm)*(depls2-deplsm) +
         *                    deplsm*deplsm) / 2);
         */
        // the equations above can be reduce to:
        depls = std::fabs(alam) * std::sqrt((3 + 2*anpsi + 3*anpsi*anpsi) / 8);
#endif
    }
    else {
        // tensile failure
        alam = ft / a1;
        p[0] -= alam * a2;
#ifdef THREED
        p[1] -= alam * a2;
#endif
        p[NDIMS-1] -= alam * a1;

        // 2nd invariant of plastic strain
#ifdef THREED
        /* double depls1 = 0;
         * double depls3 = alam;
         * double deplsm = (depls1 + depls3) / 3;
         * depls = std::sqrt(((depls1-deplsm)*(depls1-deplsm) +
         *                    (-deplsm)*(-deplsm) +
         *                    (depls3-deplsm)*(depls3-deplsm) +
         *                    deplsm*deplsm) / 2);
         */
        depls = std::fabs(alam) * std::sqrt(7. / 18);
#else
        /* double depls1 = 0;
         * double depls3 = alam;
         * double deplsm = (depls1 + depls3) / 2;
         * depls = std::sqrt(((depls1-deplsm)*(depls1-deplsm) +
         *                    (depls2-deplsm)*(depls2-deplsm) +
         *                    deplsm*deplsm) / 2);
         */
        depls = std::fabs(alam) * std::sqrt(3. / 8);
#endif
    }

    // rotate the principal stresses back to global axes
    {
#ifdef THREED
        double ss[3][3] = {{0,0,0},{0,0,0},{0,0,0}};
        for(int m=0; m<3; m++) {
            for(int n=m; n<3; n++) {
                for(int k=0; k<3; k++) {
                    ss[m][n] += v[m][k] * v[n][k] * p[k];
                }
            }
        }
        s[0] = ss[0][0];
        s[1] = ss[1][1];
        s[2] = ss[2][2];
        s[3] = ss[0][1];
        s[4] = ss[0][2];
        s[5] = ss[1][2];
#else
        double dc2 = (p[0] - p[1]) * cos2t;
        double dss = p[0] + p[1];
        s[0] = 0.5 * (dss + dc2);
        s[1] = 0.5 * (dss - dc2);
        s[2] = 0.5 * (p[0] - p[1]) * sin2t;
#endif
    }
}


static void elasto_plastic2d(double bulkm, double shearm,
                             double amc, double anphi, double anpsi,
                             double hardn, double ten_max,
                             const double* de, double& depls, double* s,
                             int &failure_mode)
{
    /* Elasto-plasticity (Mohr-Coulomb criterion) */

    /* This function is derived from geoFLAC.
     * The original code in geoFLAC assumes 2D plain strain formulation,
     * i.e. there are 3 principal stresses and only 2 principal strains
     * (Strain_yy, Strain_xy, Strain_yz must be 0).
     * Here, the code is adopted to pure 2D or 3D plane strain.
     */

    depls = 0;
    failure_mode = 0;

    // elastic trial stress
    double a1 = bulkm + 4. / 3 * shearm;
    double a2 = bulkm - 2. / 3 * shearm;
    double sxx = s[0] + de[1]*a2 + de[0]*a1;
    double szz = s[1] + de[0]*a2 + de[1]*a1;
    double sxz = s[2] + de[2]*2*shearm;
    double syy = (de[0] + de[1]) * a2; // Stress YY component, plain strain


    //
    // transform to principal stress coordinate system
    //
    // eigenvalues (principal stresses)
    double p[3];

    // In 2D, we only construct the eigenvectors from
    // cos(2*theta) and sin(2*theta) of Mohr circle
    double cos2t, sin2t;
    int n1, n2;

    {
        // center and radius of Mohr circle
        double s0 = 0.5 * (sxx + szz);
        double rad = 0.5 * std::sqrt((sxx-szz)*(sxx-szz) + 4*sxz*sxz);

        // principal stresses in the X-Z plane
        double si = s0 - rad;
        double sii = s0 + rad;

        // direction cosine and sine of 2*theta
        const double eps = 1e-15;
        double a = 0.5 * (sxx - szz);
        double b = - rad; // always negative
        if (b < -eps) {
            cos2t = a / b;
            sin2t = sxz / b;
        }
        else {
            cos2t = 1;
            sin2t = 0;
        }

        // sort p.s.
#if 1
        //
        // 3d plane strain
        //
        if (syy > sii) {
            // syy is minor p.s.
            n1 = 0;
            n2 = 1;
            p[0] = si;
            p[1] = sii;
            p[2] = syy;
        }
        else if (syy < si) {
            // syy is major p.s.
            n1 = 1;
            n2 = 2;
            p[0] = syy;
            p[1] = si;
            p[2] = sii;
        }
        else {
            // syy is intermediate
            n1 = 0;
            n2 = 2;
            p[0] = si;
            p[1] = syy;
            p[2] = sii;
        }
#else
        /* XXX: This case gives unreasonable result. Don't know why... */

        //
        // pure 2d case
        //
        n1 = 0;
        n2 = 2;
        p[0] = si;
        p[1] = syy;
        p[2] = sii;
#endif
    }

    // Possible tensile failure scenarios
    // 1. S1 (least tensional or greatest compressional principal stress) > ten_max:
    //    all three principal stresses must be greater than ten_max.
    //    Assign ten_max to all three and don't do anything further.
    if( p[0] >= ten_max ) {
        s[0] = ten_max;
        s[1] = ten_max;
        s[2] = 0.0;
        failure_mode = 1;
        return;
    }

    // 2. S2 (intermediate principal stress) > ten_max:
    //    S2 and S3 must be greater than ten_max.
    //    Assign ten_max to these two and continue to the shear failure block.
    if( p[1] >= ten_max ) {
        p[1] = p[2] = ten_max;
        failure_mode = 2;
    }

    // 3. S3 (most tensional or least compressional principal stress) > ten_max:
    //    Only this must be greater than ten_max.
    //    Assign ten_max to S3 and continue to the shear failure block.
    else if( p[2] >= ten_max ) {
        p[2] = ten_max;
        failure_mode = 3;
    }


    // shear yield criterion
    double fs = p[0] - p[2] * anphi + amc;
    if (fs >= 0.0) {
        // Tensile failure case S2 or S3 could have happened!!
        // XXX: Need to rationalize why exit without doing anything further.
        s[0] = sxx;
        s[1] = szz;
        s[2] = sxz;
        return;
    }

    failure_mode += 10;

    // shear failure
    const double alam = fs / (a1 - a2*anpsi + a1*anphi*anpsi - a2*anphi + hardn);
    p[0] -= alam * (a1 - a2 * anpsi);
    p[1] -= alam * (a2 - a2 * anpsi);
    p[2] -= alam * (a2 - a1 * anpsi);

    // 2nd invariant of plastic strain
    depls = 0.5 * std::fabs(alam + alam * anpsi);

    //***********************************
    // The following seems redundant but... this is how it goes in geoFLAC.
    //
    // Possible tensile failure scenarios
    // 1. S1 (least tensional or greatest compressional principal stress) > ten_max:
    //    all three principal stresses must be greater than ten_max.
    //    Assign ten_max to all three and don't do anything further.
    if( p[0] >= ten_max ) {
        s[0] = ten_max;
        s[1] = ten_max;
        s[2] = 0.0;
        failure_mode += 100;
        return;
    }

    // 2. S2 (intermediate principal stress) > ten_max:
    //    S2 and S3 must be greater than ten_max.
    //    Assign ten_max to these two and continue to the shear failure block.
    if( p[1] >= ten_max ) {
        p[1] = p[2] = ten_max;
        failure_mode += 100;
    }

    // 3. S3 (most tensional or least compressional principal stress) > ten_max:
    //    Only this must be greater than ten_max.
    //    Assign ten_max to S3 and continue to the shear failure block.
    else if( p[2] >= ten_max ) {
        p[2] = ten_max;
        failure_mode += 100;
    }
    //***********************************


    // rotate the principal stresses back to global axes
    {
        double dc2 = (p[n1] - p[n2]) * cos2t;
        double dss = p[n1] + p[n2];
        s[0] = 0.5 * (dss + dc2);
        s[1] = 0.5 * (dss - dc2);
        s[2] = 0.5 * (p[n1] - p[n2]) * sin2t;
    }
}


void update_stress(const Variables& var, tensor_t& stress,
                   tensor_t& strain, double_vec& plstrain,
                   double_vec& delta_plstrain, tensor_t& strain_rate)
{
    const int rheol_type = var.mat->rheol_type;

    // #pragma omp parallel for default(none)                              \
    //     shared(var, stress, strain, plstrain, delta_plstrain, strain_rate, std::cerr)
    for (int e=0; e<var.nelem; ++e) {
        // stress, strain and strain_rate of this element
        double* s = stress[e];
        double* es = strain[e];
        double* edot = strain_rate[e];

        // anti-mesh locking correction on strain rate
        if(1){
            double div = trace(edot);
            //double div2 = ((*var.volume)[e] / (*var.volume_old)[e] - 1) / var.dt;
            for (int i=0; i<NDIMS; ++i) {
                edot[i] += ((*var.edvoldt)[e] - div) / NDIMS;
            }
        }

        // update strain with strain rate
        for (int i=0; i<NSTR; ++i) {
            es[i] += edot[i] * var.dt;
        }

        // modified strain increment
        double de[NSTR];
        for (int i=0; i<NSTR; ++i) {
            de[i] = edot[i] * var.dt;
        }

        switch (rheol_type) {
        case MatProps::rh_elastic:
            {
                double bulkm = var.mat->bulkm(e);
                double shearm = var.mat->shearm(e);
                elastic(bulkm, shearm, de, s);
            }
            break;
        case MatProps::rh_viscous:
            {
                double bulkm = var.mat->bulkm(e);
                double viscosity = var.mat->visc(e);
                double total_dv = trace(es);
                viscous(bulkm, viscosity, total_dv, edot, s);
            }
            break;
        case MatProps::rh_maxwell:
            {
                double bulkm = var.mat->bulkm(e);
                double shearm = var.mat->shearm(e);
                double viscosity = var.mat->visc(e);
                double dv = (*var.volume)[e] / (*var.volume_old)[e] - 1;
                maxwell(bulkm, shearm, viscosity, var.dt, dv, de, s);
            }
            break;
        case MatProps::rh_ep:
        case MatProps::rh_ep2d:
            {
                double depls = 0;
                double bulkm = var.mat->bulkm(e);
                double shearm = var.mat->shearm(e);
                double amc, anphi, anpsi, hardn, ten_max;
                var.mat->plastic_props(e, plstrain[e],
                                       amc, anphi, anpsi, hardn, ten_max);
                int failure_mode;
                if (rheol_type == MatProps::rh_ep) {
                    elasto_plastic(bulkm, shearm, amc, anphi, anpsi, hardn, ten_max,
                                   de, depls, s, failure_mode);
                }
                else {
                    elasto_plastic2d(bulkm, shearm, amc, anphi, anpsi, hardn, ten_max,
                                     de, depls, s, failure_mode);
                }
                plstrain[e] += depls;
                delta_plstrain[e] = depls;
            }
            break;
        case MatProps::rh_evp:
        case MatProps::rh_evp2d:
            {
                double depls = 0;
                double bulkm = var.mat->bulkm(e);
                double shearm = var.mat->shearm(e);
                double viscosity = var.mat->visc(e);
                double dv = (*var.volume)[e] / (*var.volume_old)[e] - 1;
                // stress due to maxwell rheology
                double sv[NSTR];
                for (int i=0; i<NSTR; ++i) sv[i] = s[i];
                maxwell(bulkm, shearm, viscosity, var.dt, dv, de, sv);
                double svII = second_invariant2(sv);

                double amc, anphi, anpsi, hardn, ten_max;
                var.mat->plastic_props(e, plstrain[e],
                                       amc, anphi, anpsi, hardn, ten_max);
                // stress due to elasto-plastic rheology
                double sp[NSTR];
                for (int i=0; i<NSTR; ++i) sp[i] = s[i];
                int failure_mode;
                if (rheol_type == MatProps::rh_evp) {
                    elasto_plastic(bulkm, shearm, amc, anphi, anpsi, hardn, ten_max,
                                   de, depls, s, failure_mode);
                }
                else {
                    elasto_plastic2d(bulkm, shearm, amc, anphi, anpsi, hardn, ten_max,
                                     de, depls, s, failure_mode);
                }
                double spII = second_invariant2(sp);

                // use the smaller as the final stress
                if (svII < spII)
                    for (int i=0; i<NSTR; ++i) s[i] = sv[i];
                else {
                    for (int i=0; i<NSTR; ++i) s[i] = sp[i];
                    plstrain[e] += depls;
                    delta_plstrain[e] = depls;
                }
            }
            break;
        default:
            std::cerr << "Error: unknown rheology type: " << rheol_type << "\n";
            std::exit(1);
            break;
        }
        // std::cout << "stress " << e << ": ";
        // print(std::cout, s, NSTR);
        // std::cout << '\n';
    }
}
