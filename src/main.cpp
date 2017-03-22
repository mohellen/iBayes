// iBayes - Elastic Bayesian Inference Framework with Sparse Grid
// Copyright (C) 2015-today Ao Mo-Hellenbrand
//
// All copyrights remain with the respective authors.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

#include <eanalysis/EA.hpp>
#include <model/ForwardModel.hpp>
#include <model/NS.hpp>
#include <surrogate/SGI.hpp>
#include <mcmc/MCMC.hpp>
#include <mcmc/MetropolisHastings.hpp>

#include <mpi.h>
#include <iostream>
#include <cmath>
#include <sys/time.h>


using namespace std;

double true_input[] = {1.0, 0.8, 3.0, 1.5, 5.5, 0.2, 8.2, 1.0};


void test_ns_mpi()
{
#if(1==0)
	int mpisize, mpirank, mpistatus;
	MPI_Comm_size(MPI_COMM_WORLD, &mpisize);
	MPI_Comm_rank(MPI_COMM_WORLD, &mpirank);

	string inputfile = "./input/obstacles_in_flow.dat";

	NS* fm = new NS(inputfile, 1, 1);
	std::size_t input_size = fm->get_input_size();
	std::size_t output_size = fm->get_output_size();

	double noise;
	double* od = ForwardModel::get_observed_data(inputfile, output_size, noise);

	for (int p=0; p < mpisize; p++) {
		if (mpirank == p) {
			printf("\nRank %d: Observed data\n", mpirank);
			for (size_t j=0; j < output_size; j++)
				printf("%.6f ", od[j]);
			printf("\n");
		}
	}

	double* m = new double[input_size];
	m[0] = 1.0;
	m[1] = 0.8;
	m[2] = 3.0;
	m[3] = 1.5;
	m[4] = 5.5;
	m[5] = 0.2;
	m[6] = 8.2;
	m[7] = 1.0;

	double* d = new double[output_size];
	fm->run(m, d);

	for (int p=0; p < mpisize; p++) {
		if (mpirank == p) {
			printf("\nRank %d: Output data\n", mpirank);
			for (size_t j=0; j < output_size; j++)
				printf("%.6f ", d[j]);
			printf("\n");
		}
	}

	double sigma = ForwardModel::compute_posterior_sigma(od, output_size, noise);
	double pos = ForwardModel::compute_posterior(od, d, output_size, sigma);

	for (int p=0; p < mpisize; p++) {
		if (mpirank == p) {
			printf("\nRank %d: sigma = %.6f, posterior = %.6f\n", mpirank, sigma, pos);
		}
	}
#endif
}

void test_sgi_mpi() {
#if (1==0)
	int mpisize, mpirank, mpistatus;
	MPI_Comm_size(MPI_COMM_WORLD, &mpisize);
	MPI_Comm_rank(MPI_COMM_WORLD, &mpirank);

	string inputfile = "./input/ns_obs1.dat";

	// Create forward models
	unique_ptr<NS>  full (new NS(inputfile, 1, 1));	 /// full model
	unique_ptr<SGI> ssgi (new SGI(inputfile, "./output/ssgi_", 1, 1)); /// static sgi
	unique_ptr<SGI> asgi (new SGI(inputfile, "./output/asgi_", 1, 1)); /// adaptive sgi

	// Get problem dimensions
	std::size_t input_size = full->get_input_size();
	std::size_t output_size = full->get_output_size();

	// Initialize the true input vector
	unique_ptr<double[]> m (new double[input_size]);
	for (std::size_t i=0; i < input_size; i++)
		m[i] = true_input[i];

	// Error analysis objects
	unique_ptr<EA> eas (new EA(full.get(), ssgi.get()));
	eas->add_test_points(20);

	unique_ptr<EA> eaa (new EA(full.get(), asgi.get()));
	eaa->copy_test_points(eas.get());

	// Construct Static SGI
	ssgi->build(0.1, 2, false);
	double errs = eas->compute_model_error();

	// Construct Adaptive SGI
	double erra = 0.0, erra_old = -1.0, tol = 0.1;
	int count = 0;
	while (true) {
		asgi->build(0.1, 2, false);
		erra = eaa->compute_model_error();
		count += 1;
		if(mpirank == MASTER) {
			printf("\nRefinement # %d\n", count);
			printf("Surrogate model error: %.6f\n", erra);
		}
		if ((erra - erra_old < 0) && (fabs(erra - erra_old) < tol)) break;
		erra_old = erra;
	}


//	double tol = 0.01;
//	double noise, sigma, pos, err;
//	double* od = ForwardModel::get_observed_data(inputfile, output_size, noise);
//	sigma = ForwardModel::compute_posterior_sigma(od, output_size, noise);
//	int count = 0;
////	while (true) {
////		sm->build(0.1, 2, false);
////		err = ea->err();
////		count += 1;
////		if(mpirank == MASTER) {
////			printf("\nRefinement # %d\n", count);
////			printf("Surrogate model error: %.6f\n", err);
////		}
////		if (err <= tol) break;
////	}
//
//	if(mpirank == MASTER) {
//		printf("\n\n ----- NEW -----\n\n");
//	}
//
//	sm.reset(new SGI(inputfile, 1, 1));
//	system("rm -rf output/*");
//	ea.reset(new EA(fm.get(), sm.get(), m.get()));
//
//	int it;
//	for (it=1; it < 3; it++) {
//		sm->build(0.1, 2, false);
//		err = ea->err();
//		if(mpirank == MASTER) {
//			printf("\nRefinement # %d\n", it);
//			printf("Surrogate model error: %.6f\n", err);
//		}
//	}
//
//	if(mpirank == MASTER) {
//		printf("\n\n ----- Delete sgi -----\n\n");
//	}
//
//	sm.reset(new SGI(inputfile, 1, 1));
//	sm->duplicate("","","");
//	ea.reset(new EA(fm.get(), sm.get(), m.get()));
//
//	while (true) {
//		sm->build(0.1, 4, false);
//		err = ea->err();
//		if(mpirank == MASTER) {
//			printf("\nRefinement # %d\n", it);
//			printf("Surrogate model error: %.6f\n", err);
//		}
//		it += 1;
//		if (err <= tol) break;
//	}


#endif
}


void test_mcmc() {
#if (1==1)
	MCMC* mcmc = new MetropolisHastings();

	double* m = mcmc->get_last_sample("./input/test.dat");

	printf("Last sample = [%f, %f, %f]\n", m[0], m[1], m[2]);

#endif
}



int main(int argc, char* argv[]) {
	int mpistatus;

#if (ENABLE_IMPI==1)
	MPI_Init_adapt(&argc, &argv, &mpistatus);
#else
	MPI_Init(&argc, &argv);
#endif

	test_ns_mpi();
	test_sgi_mpi();
	test_mcmc();

#if (ENABLE_IMPI==1)
	printf("\n~~~~~~This is awesome!!~~~~~\n");
#endif

	MPI_Finalize();
	return 0;
}
