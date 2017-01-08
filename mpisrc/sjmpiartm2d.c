//
// Created by hsa on 12/12/16.
//

#include "../lib/sjinc.h"
#include <mpi.h>

int main(int argc, char *argv[]) {

    //! Runtime
    int is = 0, flag = 1;
    double tstart, tend, Tstart, Tend;

    //! MPI
    int rankid, nrank;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rankid);
    MPI_Comm_size(MPI_COMM_WORLD, &nrank);

    //! Survey
    sjssurvey sur;
    flag &= sjssurvey_init(&sur);
    flag &= sjssurvey_getparas(&sur, argc, argv);

    //! Model
    sjsgeology geo;
    flag &= sjsgeo_init(&geo);
    flag &= sjsgeo_getparas2d(&geo, argc, argv, "vp");
    flag &= sjsgeo_getparas2d(&geo, argc, argv, "ipp");

    //! Wave
    sjswave wav;
    flag &= sjswave_init(&wav);
    flag &= sjswave_getparas(&wav, argc, argv, "recz");

    //! Option
    sjsoption opt;
    flag &= sjsoption_init(&opt);
    flag &= sjsoption_getparas(&opt, argc, argv);

    //! ------------------------ RTM2D ------------------------
    if (flag) {
        //! Time
        if (rankid == 0) {
            Tstart = (double) clock();
            printf("------------------------ 2D Acoustic RTM start ------------------------\n");
        }

        //! Model
        float **nmig = sjmflloc2d(sur.gnx, sur.gnz);
        geo.gipp2d = sjmflloc2d(sur.gnx, sur.gnz);
        geo.gvp2d = sjmflloc2d(sur.gnx, sur.gnz);
        sjreadsuall(geo.gvp2d[0], sur.gnx, sur.gnz, geo.vpfile);
        MPI_Bcast(geo.gvp2d[0], sur.gnx * sur.gnz, MPI_FLOAT, 0, MPI_COMM_WORLD);

        //! Rtm
        for (is = rankid; is < sur.ns; is += nrank) {
            //! Time
            tstart = (double) clock();

            //! Survey
            sjssurvey_readis(&sur, is);

            //! Model
            geo.vp2d = sjmflloc2d(sur.nx, sur.nz);
            geo.ipp2d = sjmflloc2d(sur.nx, sur.nz);
            geo.nipp2d = sjmflloc2d(sur.nx, sur.nz);
            sjextract2d(geo.gvp2d, sur.x0, sur.z0, sur.nx, sur.nz, geo.vp2d);

            //! Forward simulaion
            wav.recz = sjmflloc2d(sur.nr, opt.nt);
            wav.snapz2d = sjmflloc3d(opt.nsnap, sur.nx, sur.nz);
            sjawfd2d(&sur, &geo, &wav, &opt);

            //! Read record
            sjreadsu(wav.recz[0], sur.nr, opt.nt, sizeof(float), sur.tr, 0, wav.reczfile);

            //! Adjoint image
            opt.ystacksrc = 0;
            sjawrtmfd2d(&sur, &geo, &wav, &opt);

            //! Laplace filter
            sjfilter2d(geo.ipp2d, sur.nx, sur.nz, "laplace");

            //! Stacking
            sjvecaddf(&geo.gipp2d[sur.x0][sur.z0], sur.nx * sur.nz, 1.0f, &geo.gipp2d[sur.x0][sur.z0], 1.0f, &geo.ipp2d[0][0]);
            sjvecaddf(&nmig[sur.x0][sur.z0], sur.nx * sur.nz, 1.0f, &nmig[sur.x0][sur.z0], 1.0f, &geo.nipp2d[0][0]);

            //! Free
            sjmfree2d(geo.vp2d);
            sjmfree2d(geo.ipp2d);
            sjmfree2d(geo.nipp2d);
            sjmfree2d(wav.recz);
            sjmfree2d(wav.snapz2d);

            //! Time
            tend = (double) clock();
            printf("Single shot RTM complete - %d/%d - time=%fs.\n", is + 1, sur.ns,
                   (tend - tstart) / CLOCKS_PER_SEC);
            printf("Rankid=%d, sx=%d, sz=%d, rx=%d to %d, rz=%d to %d.\n\n", rankid,
                   sur.sx + sur.x0, sur.sz + sur.z0,
                   sur.rx[0] + sur.x0, sur.rx[sur.nr - 1] + sur.x0,
                   sur.rz[0] + sur.z0, sur.rz[sur.nr - 1] + sur.z0);
        }

        MPI_Barrier(MPI_COMM_WORLD);

        if (rankid == 0) {
            //! Reduce
            MPI_Reduce(MPI_IN_PLACE, geo.gipp2d[0], sur.gnx * sur.gnz, MPI_FLOAT, MPI_SUM, 0, MPI_COMM_WORLD);
            MPI_Reduce(MPI_IN_PLACE, nmig[0], sur.gnx * sur.gnz, MPI_FLOAT, MPI_SUM, 0, MPI_COMM_WORLD);

            //! Source
            sjvecdivf(geo.gipp2d[0], sur.gnx * sur.gnz, 1.0, geo.gipp2d[0], nmig[0], 0.00001f);

            //! Cut surface
            sjsetsurface(geo.gipp2d, sur.gnx, 30, 0.0f);

            //! Output
            sjwritesuall(geo.gipp2d[0], sur.gnx, sur.gnz, opt.ds, geo.ippfile);

            //! Time
            Tend = (double) clock();
            printf("Acoustic RTM complete - time=%fs.\n\n\n", (Tend - Tstart) / CLOCKS_PER_SEC);
        } else {
            //! Reduce
            MPI_Reduce(geo.gipp2d[0], geo.gipp2d[0], sur.gnx * sur.gnz, MPI_FLOAT, MPI_SUM, 0, MPI_COMM_WORLD);
            MPI_Reduce(nmig[0], nmig[0], sur.gnx * sur.gnz, MPI_FLOAT, MPI_SUM, 0, MPI_COMM_WORLD);
        }

        //! Free
        sjmfree2d(geo.gipp2d);
        sjmfree2d(geo.gvp2d);
        sjmfree2d(nmig);

    } else {
        printf("\nExamples:   sjmpiartm2d sur=sur.su vp=vp.su recz=recz.su ipp=mig.su\n");
        sjbasicinformation();
    }

    //------------------------ MPI finish ------------------------//
    MPI_Finalize();

    return 0;
}
