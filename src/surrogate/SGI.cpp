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

#include "surrogate/SGI.hpp"

using namespace std;
using namespace	sgpp::base;


SGI::SGI(
		ForwardModel* fm,
		const string& observed_data_file)
		: ForwardModel(fm->get_input_size(), fm->get_output_size())
{
	MPI_Comm_rank(MPI_COMM_WORLD, &(this->mpi_rank));
	MPI_Comm_size(MPI_COMM_WORLD, &(this->mpi_size));
#if (ENABLE_IMPI==1)
	mpi_status = -1;
#endif
	this->fullmodel.reset(fm);
	this->alphas.reset(new DataVector[output_size]);
	this->grid = nullptr;
	this->eval = nullptr;
	this->bbox = nullptr;

	this->maxpos_seq = 0;
	this->maxpos = 0.0;
	this->noise = 0.0;
	this->odata.reset( get_observed_data(observed_data_file,
			this->output_size, this->noise) );
	this->sigma = compute_posterior_sigma(
			this->odata.get(), this->output_size, this->noise);
}

std::size_t SGI::get_input_size()
{
	return this->input_size;
}

std::size_t SGI::get_output_size()
{
	return this->output_size;
}

void SGI::get_input_space(
			int dim,
			double& min,
			double& max)
{
	fullmodel->get_input_space(dim, min, max);
}

void SGI::run(const double* m, double* d)
{
	// Grid check
	if (!eval) {
		cout << "SGI model is not properly built. Progam abort!" << endl;
		exit(EXIT_FAILURE);
	}
	// Convert m into data vector
	DataVector point = arr_to_vec(m, input_size);
	// Evaluate m
	for (std::size_t j=0; j < output_size; j++) {
		d[j] = eval->eval(alphas[j], point);
	}
	return;
}

void SGI::build(
		double refine_portion,
		std::size_t init_grid_level,
		bool is_masterworker)
{
	std::size_t num_points, new_num_points;
	// find out whether it's grid initialization or refinement
	bool is_init = (!this->eval) ? true : false;

#if (ENABLE_IMPI==1)
	if (mpi_status != MPI_ADAPT_STATUS_JOINING) {
#endif

	if (is_init) {
		// 1. All: Construct grid
		bbox.reset(create_boundingbox());
	//	grid.reset(Grid::createLinearBoundaryGrid(input_size).release()); // create empty grid
		grid.reset(Grid::createModLinearGrid(input_size).release()); // create empty grid
		grid->getGenerator().regular(init_grid_level); // populate grid points
		grid->setBoundingBox(*bbox); // set up bounding box
		num_points = grid->getSize();
		// Master only
		if (is_master()) {
			// Print progress
			printf("\n...Initializing SGI model...\n");
			printf("%lu grid points to be added. Total # grid points = %lu.\n",
					num_points, num_points);
		}
	} else {
		if (is_master())
			printf("\n...Refining SGI model...\n");
		// 1. All: refine grid
		num_points = grid->getSize();
		refine_portion = fmax(0.0, refine_portion); // ensure portion is non-negative
		refine_grid(refine_portion);
		new_num_points = grid->getSize();
		if (is_master())
			printf("%lu grid points to be added. Total # grid points = %lu.\n",
					new_num_points-num_points, new_num_points);
	}

#if (ENABLE_IMPI==1)
		// Write grid to file (scratch file only needed by JOINING ranks during compute_grid_points)
		mpiio_write_grid();
	} else {
		num_points = CarryOver.gp_offset;
	}
#endif

	// 2. All: Compute data at each grid point (result written to MPI IO file)
	//			and find the max posterior point
	if (is_init) {
		compute_grid_points(0, is_masterworker);
	} else {
		compute_grid_points(num_points, is_masterworker);
	}
	mpi_find_global_update_maxpos();

#if (ENABLE_IMPI==1)
	// Delete scatch file
	mpiio_delete_grid();
#endif

	// 3. All: Create alphas
	update_alphas();

	// 4. Update op_eval
	eval.reset(sgpp::op_factory::createOperationEval(*grid).release());

	// Master: print grogress
	if (is_master()) {
		unique_ptr<double[]> m_maxpos (seg_to_coord_arr(maxpos_seq));
		printf("Max posterior = %.6f, at %s.\n",
				maxpos, arr_to_string(m_maxpos.get(), input_size).c_str());
		if (is_init)
			printf("...Initialize SGI model successful...\n");
		else
			printf("...Refine SGI model successful...\n");
	}
	return;
}


/*********************************************
 *********************************************
 *       		 Private Methods
 *********************************************
 *********************************************/

/***************************
 * Grid related operations
 ***************************/
DataVector SGI::arr_to_vec(const double *& in, std::size_t size)
{
	DataVector out = DataVector(size);
	for (std::size_t i=0; i < size; i++)
		out[i] = in[i];
	return out;
}

double* SGI::vec_to_arr(DataVector& in)
{
	std::size_t size = in.getSize();
	double* out = new double[size];
	for (std::size_t i=0; i < size; i++)
		out[i] = in[i];
	return out;
}

double* SGI::seg_to_coord_arr(std::size_t seq)
{
	// Get grid point coordinate
	DataVector gp_coord (input_size);
	grid->getStorage().get(seq)->getCoordsBB(gp_coord, grid->getBoundingBox());
	return vec_to_arr(gp_coord);
}

string SGI::vec_to_str(DataVector& v)
{
	std::ostringstream oss;
	oss << "[" << std::fixed << std::setprecision(4);
	for (std::size_t i=0; i < v.getSize()-1; i++)
		oss << v[i] << ", ";
	oss << v[v.getSize()-1] << "]";
	return oss.str();
}

BoundingBox* SGI::create_boundingbox()
{
	BoundingBox* bb = new BoundingBox(input_size);
	DimensionBoundary db;
	double min, max;
	for(int i=0; i < input_size; i++) {
		get_input_space(i, min, max);
		db.leftBoundary = min;
		db.rightBoundary = max;
		db.bDirichletLeft  = false;
		db.bDirichletRight = false;
		bb->setBoundary(i, db);
	}
	return bb;
}

void SGI::update_alphas()
{
#if (SGI_OUT_TIMER==1)
	double tic = MPI_Wtime();
#endif
	// read raw data
	std::size_t num_gps = grid->getSize();
	unique_ptr<double[]> data (new double[output_size * num_gps]);
	mpiio_partial_data(true, 0, num_gps-1, data.get());

	// re-allocate alphas
	for (std::size_t j=0; j < output_size; j++)
		alphas[j].resize(num_gps);

	// unpack raw data into alphas
	for (std::size_t i=0; i < num_gps; i++)
		for (std::size_t j=0; j < output_size; j++)
			alphas[j].set(i, data[i*output_size+j]);

	// hierarchize alphas
//	unique_ptr<OperationHierarchisation> hier (sgpp::op_factory::createOperationHierarchisation(*grid));
	auto hier = sgpp::op_factory::createOperationHierarchisation(*grid);
	for (std::size_t j=0; j<output_size; j++)
		hier->doHierarchisation(alphas[j]);

#if (SGI_OUT_TIMER==1)
	if (is_master())
		printf("Rank %d: created alphas in %.5f seconds.\n",
				mpi_rank, MPI_Wtime()-tic);
#endif
	return;
}

void SGI::refine_grid(double portion)
{
#if (SGI_OUT_TIMER==1)
	double tic = MPI_Wtime();
#endif
	std::size_t num_gps = this->grid->getSize();
	std::size_t refine_gps = num_gps * fmax(0.0, portion);
	DataVector refine_idx (num_gps);

	// Read posterior from file
	unique_ptr<double[]> pos (new double[num_gps]);
	mpiio_partial_posterior(true, 0, num_gps-1, &pos[0]);

	// For each gp, compute the refinement index
	double data_norm;
	for (std::size_t i=0; i<num_gps; i++) {
		data_norm = 0;
		for (std::size_t j=0; j<output_size; j++) {
			data_norm += (alphas[j][i] * alphas[j][i]);
		}
		data_norm = sqrt(data_norm);
		// refinement_index = |alpha| * posterior
		refine_idx[i] = data_norm * pos[i];
	}
	// refine grid
	grid->refine(refine_idx, std::size_t(ceil(double(num_gps)*portion)));

#if (SGI_OUT_TIMER==1)
	if (is_master())
		printf("Rank %d: refined grid in %.5f seconds.\n", mpi_rank, MPI_Wtime()-tic);
#endif
	return;
}

/***************************
 * MPI related operations
 ***************************/
bool SGI::is_master()
{
	if (mpi_rank == 0) {
#if (ENABLE_IMPI==1)
		if (mpi_status != MPI_ADAPT_STATUS_JOINING)
#endif
			return true;
	}
	return false;
}

void SGI::mpiio_write_grid_master()
{
	if (!is_master()) return; // Only the master performs this operation.

	// Pack grid into Char array
	string sg_str = grid->getStorage().serialize(1);
	size_t count = sg_str.size();
	unique_ptr<char[]> buff (new char[count]);
	strcpy(buff.get(), sg_str.c_str());
	// Write to file
	string ofile = string(OUTPATH) + "/grid.mpibin";
	MPI_File fh;
	if (MPI_File_open(MPI_COMM_SELF, ofile.c_str(), MPI_MODE_CREATE|MPI_MODE_WRONLY, MPI_INFO_NULL, &fh)
			!= MPI_SUCCESS) {
		cout << "MPI write grid file open failed. Operation aborted! " << endl;
		exit(EXIT_FAILURE);
	}
	MPI_File_write_at(fh, 0, buff.get(), count, MPI_CHAR, MPI_STATUS_IGNORE);
	MPI_File_close(&fh);
	return;
}

void SGI::mpiio_write_grid()
{
	// Pack grid into Char array
	string sg_str = grid->getStorage().serialize(1);
	size_t count = sg_str.size();
	unique_ptr<char[]> buff (new char[count]);
	strcpy(buff.get(), sg_str.c_str());

	// Get local range (local protion to write)
	std::size_t lmin, lmax;
	mpina_get_local_range(0, count-1, lmin, lmax);

	// Write to file
	string ofile = string(OUTPATH) + "/grid.mpibin";
	MPI_File fh;
	if (MPI_File_open(MPI_COMM_SELF, ofile.c_str(), MPI_MODE_CREATE|MPI_MODE_WRONLY, MPI_INFO_NULL, &fh)
			!= MPI_SUCCESS) {
		cout << "MPI write grid file open failed. Operation aborted! " << endl;
		exit(EXIT_FAILURE);
	}
	MPI_File_write_at(fh, lmin-1, buff.get(), lmax-lmin+1, MPI_CHAR, MPI_STATUS_IGNORE);
	MPI_File_close(&fh);
	return;
}

void SGI::mpiio_read_grid()
{}

void SGI::mpiio_delete_grid()
{
	string ofile = string(OUTPATH) + "/grid.mpibin";
	MPI_File fh;
	if (MPI_File_open(MPI_COMM_SELF, ofile.c_str(), MPI_MODE_WRONLY, MPI_INFO_NULL, &fh)
			== MPI_SUCCESS) {
		MPI_File_delete(ofile.c_str(), MPI_INFO_NULL);
	}
}

void SGI::mpiio_partial_data(
		bool is_read,
		std::size_t seq_min,
		std::size_t seq_max,
		double* buff)
{
	// Do something only when seq_min <= seq_max
	if (seq_min > seq_max) return;

	string ofile;
	ofile = string(OUTPATH) + "/data.mpibin";;
	MPI_File fh;
	if (is_read) { // Read data from file
		if (MPI_File_open(MPI_COMM_SELF, ofile.c_str(), MPI_MODE_RDONLY, MPI_INFO_NULL, &fh)
				!= MPI_SUCCESS) {
			cout << "MPI read file open failed. Operation aborted! " << endl;
			exit(EXIT_FAILURE);
		}
		// offset is in # of bytes, and is ALWAYS calculated from beginning of file.
		MPI_File_read_at(fh, seq_min*output_size*sizeof(double), buff,
				(seq_max-seq_min+1)*output_size, MPI_DOUBLE, MPI_STATUS_IGNORE);

	} else { // Write data to file
		if (MPI_File_open(MPI_COMM_SELF, ofile.c_str(), MPI_MODE_CREATE | MPI_MODE_WRONLY, MPI_INFO_NULL, &fh)
				!= MPI_SUCCESS) {
			cout << "MPI write file open failed. Operation aborted! " << endl;
			exit(EXIT_FAILURE);
		}
		// offset is in # of bytes, and is ALWAYS calculated from beginning of file.
		MPI_File_write_at(fh, seq_min*output_size*sizeof(double), buff,
				(seq_max-seq_min+1)*output_size, MPI_DOUBLE, MPI_STATUS_IGNORE);
	}
	MPI_File_close(&fh);
	return;
}

void SGI::mpiio_partial_posterior(
		bool is_read,
		std::size_t seq_min,
		std::size_t seq_max,
		double* buff)
{
	// Do something only when seq_min <= seq_max
	if (seq_min > seq_max) return;

	string ofile;
	ofile = string(OUTPATH) + "/pos.mpibin";;
	MPI_File fh;
	if (is_read) { // Read data from file
	if (MPI_File_open(MPI_COMM_SELF, ofile.c_str(), MPI_MODE_RDONLY, MPI_INFO_NULL, &fh)
			!= MPI_SUCCESS) {
		cout << "MPI read file open failed. Operation aborted! " << endl;
		exit(EXIT_FAILURE);
	}
	// offset is in # of bytes, and is ALWAYS calculated from beginning of file.
	MPI_File_read_at(fh, seq_min*sizeof(double), buff,
			(seq_max-seq_min+1), MPI_DOUBLE, MPI_STATUS_IGNORE);

	} else { // Write data to file
	if (MPI_File_open(MPI_COMM_SELF, ofile.c_str(), MPI_MODE_CREATE | MPI_MODE_WRONLY, MPI_INFO_NULL, &fh)
			!= MPI_SUCCESS) {
		cout << "MPI write file open failed. Operation aborted! " << endl;
		exit(EXIT_FAILURE);
	}
	// offset is in # of bytes, and is ALWAYS calculated from beginning of file.
	MPI_File_write_at(fh, seq_min*sizeof(double), buff,
			(seq_max-seq_min+1), MPI_DOUBLE, MPI_STATUS_IGNORE);
	}
	MPI_File_close(&fh);
	return;
}

void SGI::mpi_find_global_update_maxpos()
{
	if (mpi_size <= 1) return;
	struct {
		double mymaxpos;
		int myrank;
	} in, out;
	in.mymaxpos = maxpos;
	in.myrank = mpi_rank;
	MPI_Allreduce(&in, &out, 1, MPI_DOUBLE_INT, MPI_MAXLOC, MPI_COMM_WORLD);
	maxpos = out.mymaxpos;
	MPI_Bcast(&maxpos_seq, 1, MPI_UNSIGNED_LONG, out.myrank, MPI_COMM_WORLD);
	return;
}

/***************************
 * Core computation
 ***************************/
void SGI::compute_grid_points(
		std::size_t gp_offset,
		bool is_masterworker)
{
#if (SGI_OUT_TIMER==1)
	double tic = MPI_Wtime(); // start the timer
#endif
	// NOTE: both "Master-minion" or "Naive" schemes can run under MPI & iMPI
	//		settings, but only the "Master-minion" scheme uses the iMPI features.
	if (is_masterworker) {
		// Master-worker style
		if (is_master())
			mpimw_master_compute(gp_offset);
		else
			mpimw_worker_compute(gp_offset);
	} else {
		// MPI native style (default)
		std::size_t num_gps = grid->getSize();
		std::size_t mymin, mymax;
		mpina_get_local_range(gp_offset, num_gps-1, mymin, mymax);
		mpi_compute_range(mymin, mymax);
	}
	MPI_Barrier(MPI_COMM_WORLD);

#if (SGI_OUT_TIMER==1)
	if (is_master())
		printf("%d Ranks: computed %lu grid points (%lu to %lu) in %.5f seconds.\n",
				mpi_size, grid->getSize()-gp_offset,
				gp_offset, grid->getSize()-1, MPI_Wtime()-tic);
#endif
	return;
}

void SGI::mpi_compute_range(
		const std::size_t& seq_min,
		const std::size_t& seq_max)
{
	// IMPORTANT: Ensure workload is at least 1 grid point!
	// This also prevents overriding wrong data to data files!
	if (seq_max < seq_min) return;

#if (SGI_OUT_RANK_PROGRESS==1)
	printf("Rank %d: computing grid points %lu to %lu.\n",
			mpi_rank, seq_min, seq_max);
#endif
	// Compute data & posterior
	DataVector gp_coord (input_size);
	GridStorage* gs = &(grid->getStorage());

	std::size_t load = std::size_t(fmax(0, seq_max - seq_min + 1));
	unique_ptr<double[]> data (new double[load * output_size]);
	unique_ptr<double[]> pos (new double[load]);

	unique_ptr<double[]> m;
	double* d = nullptr;
	double* p = nullptr;

	std::size_t i,dim;  // loop index
	for (i=seq_min; i <= seq_max; i++) {
		// Set output pointer
		d = &data[0] + (i-seq_min) * output_size;
		p = &pos[0] + (i-seq_min);
		// Get grid point coordinate
		gs->get(i)->getCoordsBB(gp_coord, *bbox);
		m.reset(vec_to_arr(gp_coord));
		// compute with full model
		fullmodel->run(m.get(), d);
		// compute posterior
		*p = compute_posterior(odata.get(), d, output_size, sigma);
		// Find max posterior
		if (*p > maxpos) {
			maxpos = *p;
			maxpos_seq = i;
		}
#if (SGI_OUT_GRID_POINTS==1)
		printf("Rank %d: grid point %lu at %s completed, pos = %.6f.\n",
				mpi_rank, i, vec_to_str(gp_coord).c_str(), *p);
#endif
	}
	// Write results to file
	mpiio_partial_data(false, seq_min, seq_max, data.get());
	mpiio_partial_posterior(false, seq_min, seq_max, pos.get());
	return;
}

void SGI::mpina_get_local_range(
		const std::size_t& gmin,
		const std::size_t& gmax,
		std::size_t& lmin,
		std::size_t& lmax)
{
	std::size_t num_gps = gmax - gmin + 1;
	std::size_t trunk = num_gps / mpi_size;
	std::size_t rest = num_gps % mpi_size;

	if (mpi_rank < rest) {
		lmin = gmin + mpi_rank * (trunk + 1);
		lmax = lmin + trunk;
	} else {
		lmin = gmin + mpi_rank * trunk + rest;
		lmax = lmin + trunk - 1;
	}
	return;
}

void SGI::mpimw_get_job_range(
		const std::size_t& jobid,
		const std::size_t& seq_offset,
		std::size_t& seq_min,
		std::size_t& seq_max)
{
	seq_min = seq_offset + jobid * MPIMW_TRUNK_SIZE;
	seq_max = min( seq_min + MPIMW_TRUNK_SIZE - 1, grid->getSize()-1 );
}

void SGI::mpimw_worker_compute(std::size_t gp_offset)
{
	// Setup variables
	int job_todo, job_done; // use separate buffers for send and receive
	std::size_t seq_min, seq_max;
	MPI_Status status;

	while (true) {
		// Receive a signal from MASTER
		MPI_Recv(&job_todo, 1, MPI_INT, MASTER, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

		if (status.MPI_TAG == MPIMW_TAG_TERMINATE) break;

		if (status.MPI_TAG == MPIMW_TAG_WORK) {
			// get the job range and compute
			mpimw_get_job_range(job_todo, gp_offset, seq_min, seq_max);
			mpi_compute_range(seq_min, seq_max);
			// tell master the job is done
			job_done = job_todo;
			MPI_Send(&job_done, 1, MPI_INT, MASTER, job_done, MPI_COMM_WORLD);
		}
#if (ENABLE_IMPI==1)
		if (status.MPI_TAG == MPIMW_TAG_ADAPT) impi_adapt();
#endif
	} // end while
	return;
}

void SGI::mpimw_master_compute(std::size_t gp_offset)
{
	std::size_t added_gps;
	int num_jobs, jobid, worker, scnt=0;
	MPI_Status status;
	unique_ptr<int[]> jobs; // use array to have unique send buffer
	vector<int> jobs_done;

#if (ENABLE_IMPI==1)
	double tic, toc;
	int jobs_per_tic;
#endif

	// Determine total # jobs (compute only the newly added points)
	added_gps = grid->getSize() - gp_offset;
	num_jobs = (added_gps % MPIMW_TRUNK_SIZE > 0) ?
			added_gps/MPIMW_TRUNK_SIZE + 1 :
			added_gps/MPIMW_TRUNK_SIZE;

	// Initialize job queues
	jobs_done.reserve(num_jobs);
	jobs.reset(new int[num_jobs]);
	for (int i = 0; i < num_jobs; i++)
		jobs[i] = i;

#if (ENABLE_IMPI==1)
	tic = MPI_Wtime();
	jobs_per_tic = 0;
#endif

	// Seed workers if any
	if (mpi_size > 1)
		mpimw_seed_workers(num_jobs, scnt, jobs.get());

	// As long as not all jobs are done, keep working...
	while (jobs_done.size() < num_jobs) {
		if (mpi_size > 1) {
			// #1. Receive a finished job
			MPI_Recv(&jobid, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
			worker = status.MPI_SOURCE;
			jobs_done.push_back(jobid); // mark the job as done
#if (ENABLE_IMPI==1)
			jobs_per_tic++;
#endif
			// #2. Send another job if any
			if (scnt < num_jobs) {
				MPI_Send(&jobs[scnt], 1, MPI_INT, worker, MPIMW_TAG_WORK, MPI_COMM_WORLD);
				scnt++;
			}
		} else { // if there is NO worker
			// #1. Take out job
			jobid = jobs[scnt];
			scnt++;
			// #2. Compute a job
			std::size_t seq_min, seq_max;
			mpimw_get_job_range(jobid, gp_offset, seq_min, seq_max);
			mpi_compute_range(seq_min, seq_max);
			jobs_done.push_back(jobid);
#if (ENABLE_IMPI==1)
			jobs_per_tic++;
#endif
		}

		// #3. Check for adaption every IMPI_ADAPT_FREQ seconds
#if (ENABLE_IMPI==1)
		toc = MPI_Wtime()-tic;
		if (toc >= IMPI_ADAPT_FREQ) {
			// performance measure: # gps computed per second
			printf("PERFORMANCE MEASURE: # forward simulations per second = %.6f\n",
					double(jobs_per_tic * MPIMW_TRUNK_SIZE)/toc);
			// Only when there are remaining jobs, it's worth trying to adapt
			if (scnt < num_jobs) {
				// Prepare minions for adapt (receive done jobs, then send adapt signal)
				if (mpi_size > 1)
					prepare_minions_for_adapt(jobs_done, jobs_per_tic);
				// Adapt
				impi_adapt();
				// Seed minions again
				if (mpi_size > 1)
					seed_minions(num_jobs, scnt, jobs.get());
			}
			// reset timer
			tic = MPI_Wtime();
			jobs_per_tic = 0;
		} // end if-toc
#endif
	} // end while

	// All jobs done
	if (mpi_size > 1) {
		for (int mi=1; mi < mpi_size; mi++)
			MPI_Send(&jobid, 1, MPI_INT, mi, MPIMW_TAG_TERMINATE, MPI_COMM_WORLD);
	}
	return;
}

void SGI::mpimw_seed_workers(
		const int& num_jobs,
		int& scnt,
		int* jobs)
{
	// the smaller of (remainning jobs) or (# workers)
	int size = int(fmin(num_jobs-scnt, mpi_size-1));
	unique_ptr<MPI_Request[]> tmp_req (new MPI_Request[size]);
	for (int i=0; i < size; i++) {
		MPI_Isend(&jobs[scnt], 1, MPI_INT, i+1, MPIMW_TAG_WORK,
				MPI_COMM_WORLD, &tmp_req[i]);
		scnt++;
	}
	MPI_Waitall(size, tmp_req.get(), MPI_STATUS_IGNORE);
	return;
}

