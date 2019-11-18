#include <cmath>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstdlib>
#include <random>
#include <string>
#include <mpi.h>

//importerer disse for å beregne tiden:
#include <ctime>
#include <ratio>
#include <chrono>

using namespace  std;
using namespace std::chrono;

void initializelattice(int, int**, double&, double&);
void initialize_random(int,int **,double&, double&);
void metropolissampling(int, int **, double&, double&, double *, int&);
void expectationvalues_mc(double,ofstream&,ofstream&,int,int,int**,bool&,double*,int);
void expectationValues_T(double,ofstream &,int ,int ,int**,bool &,int,int,int);
void exact(int);
void output(ofstream &,int, double ,double *,int ,int );

inline int periodic(int i, int limit, int add) {
	return (i + limit + add) % (limit);
}

int main(int argc, char* argv[]) {
	int l, MC_cycles; l = 20; MC_cycles = 1e6;
	double T = 2.4;

	bool random = true; // true = random spin matrix, false = all spins pointing upwards
	double temp_step = 0.01;                            // Steps in temperature
  double initial_temp = 2.0;                          // Initial temperature
	double final_temp = 2.4;                            // Final temperature

	int my_rank,numprocs; //variables for parallell
	char *outfilename;
	if(my_rank ==0){
		exact(l);
	}
	high_resolution_clock::time_point t1 = high_resolution_clock::now(); //Start tid
	MPI_Init(&argc,&argv);
	MPI_Comm_size (MPI_COMM_WORLD, &numprocs);
  MPI_Comm_rank (MPI_COMM_WORLD, &my_rank);

	//determines no of intervalls per processor, start of loop and end of the loop.
	int no_intervalls = MC_cycles/numprocs;
	int myloop_begins = my_rank*no_intervalls +1;
	int myloop_ends = (my_rank + 1)*no_intervalls;
	if((my_rank == numprocs-1) && (myloop_ends < MC_cycles)){
		myloop_ends = MC_cycles;
	}

	//broadcast to alle nodes common variables
	MPI_Bcast (&l, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Bcast (&initial_temp, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	MPI_Bcast (&final_temp, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	MPI_Bcast (&temp_step, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);

	// defining SPIN MATRIX
	int **spin_matrix;
	spin_matrix = new int*[l];
	for(int i=0;i<l;i++)
			spin_matrix[i] = new int[l];
	for(int i=0;i<l;i++){
			for(int j=0;j<l;j++){
					spin_matrix[i][j] = 1;
			}
		}

	//defining w-vector probility distribution
	double w[17];
  for(int dE = -8; dE <= 8; dE++) w[dE+8] = 0;
  for(int dE = -8; dE <= 8; dE+=4) w[dE+8] = exp(-dE/T);


	// Create files
  char filename_MC[1000];
  sprintf(filename_MC, "ExpectationValues_MC_%d_%.1f_%d.txt", l, T, random);
  char filename_E[1000];
  sprintf(filename_E,"Energy_MC_%d_%.1f_%d.txt",l,T,random);

	ofstream file_MC(filename_MC); // File for expectation values
  ofstream file_E(filename_E); // File for energies

	int numberOfMonteCarloCycles = myloop_ends - myloop_begins;
  expectationvalues_mc(T,file_MC,file_E,l,numberOfMonteCarloCycles,spin_matrix,random,w,my_rank);

  file_MC.close();
  file_E.close();

	//Temperature-variations
	// char filename_T[1000];
	// sprintf(filename_T,"ExpectationValues_T_%d_%d_0.01.txt", l, random);
	// ofstream file_T(filename_T);
	//
	// for(double temperature = initial_temp; temperature <= final_temp; temperature += temp_step){
	// 	expectationValues_T(temperature,file_T,l,MC_cycles,spin_matrix,random,myloop_begins,myloop_ends,my_rank);
	// }
	// file_T.close();

	MPI_Finalize();
	high_resolution_clock::time_point t2 = high_resolution_clock::now();//slutt tid
	duration<double> time_span = duration_cast<duration<double>>(t2 - t1);
	cout << "CPU time: " << time_span.count() << " s" << endl;

	// CLEAR MEMORY
	for(int i=0;i<l;i++) {
			delete[] spin_matrix[i];
	}
	delete[] spin_matrix;

	return 0;
}
void initializelattice(int ls,int **spin_matrix,double& E, double& M){
    // Function that initializes the spin matrix (ordered), magnetization and energy

    // Setup spin matrix and initial magnetization
    for(int y =0; y < ls; y++) {
        for (int x= 0; x < ls; x++){
            spin_matrix[y][x] = 1; // spin orientation for the ground state
            M +=  (double) spin_matrix[y][x];
        }
    }

    // Setup initial energy
    for(int y =0; y < ls; y++) {
        for (int x= 0; x < ls; x++){
            E -=  (double) spin_matrix[y][x]*(spin_matrix[periodic(y,ls,-1)][x] + spin_matrix[y][periodic(x,ls,-1)]);
        }
    }
}
void initialize_random(int l,int **spin_matrix,double& E, double& M){
    // Function that initializes the spin matrix with randomly oriented spins, magnetization and energy

    default_random_engine generator;
    uniform_real_distribution<double> distribution(0.0,1.0);

    // Setup spin matrix
    double random;
    for(int x=0;x<l;x++){
        for(int y=0;y<l;y++){
            random = distribution(generator);
            if(random<0.5) spin_matrix[x][y] = -1;
            else spin_matrix[x][y] = 1;
        }
    }
    // Setup intial magnetization
    for(int y =0; y < l; y++) {
        for (int x= 0; x < l; x++){
            M +=  (double) spin_matrix[y][x];
        }
    }
    // Setup initial energy
    for(int y =0; y < l; y++) {
        for (int x= 0; x < l; x++){
            E -=  (double) spin_matrix[y][x]*(spin_matrix[periodic(y,l,-1)][x]
						 + spin_matrix[y][periodic(x,l,-1)]);
        }
    }
}
void metropolissampling(int l, int **spin_matrix, double& E, double&M, double *w, int& accepted_configs)
{
	// initialize the seed and call the mersenne algo
	random_device rd;
	mt19937_64 gen(rd());
	// then set up the uniform distribution for x \in [[0, 1]
	uniform_real_distribution<double> distribution(0.0, 1.0);

	for (int x = 0; x < l; x++) {
		for (int y = 0; y < l; y++) {
			int ix = (int)(distribution(gen) * (double)l);
			int iy = (int)(distribution(gen) * (double)l);
			int deltaE = 2 * spin_matrix[ix][iy] *
				(spin_matrix[ix][periodic(iy, l, -1)] +
					spin_matrix[periodic(ix, l, -1)] [iy] +
					spin_matrix[ix][periodic(iy, l, 1)] +
					spin_matrix[periodic(ix, l, 1)][iy]);

			if (distribution(gen) <= w[deltaE+8]){
				spin_matrix[ix][iy] *= -1.0;
				M += (double)2 * spin_matrix[ix][iy];
				E += (double)deltaE;
				accepted_configs += 1;
			}
		}
	}
}
void expectationvalues_mc(double T,ofstream &file,ofstream &fileE,int l,int mc,int**spin_matrix,bool &rand,double *w,int my_rank){
	// Initialize array for expectation values
    double average[5];
		// Initialize sums
    double M = 0;
    double E = 0;
    int accepted_configs = 0; // Initialize count of accepted configurations
		int countstart = 0;

		for(int i=0;i<5;i++) average[i] = 0.0;

		// Setup spin matrix
    if(rand) initialize_random(l,spin_matrix,E,M);
    else initializelattice(l,spin_matrix,E,M);

		double test, Eprev;
    int minimum_mcs;
		bool count = false;
		bool first = true;

		for(int cycles=1; cycles <= mc;cycles++){
			Eprev = E; // save previous energy value
      metropolissampling(l,spin_matrix,E,M,w,accepted_configs);

			if(count && my_rank == 0){
					fileE << E/l/l << endl; // Write energies to file
			}
			// Update expectation values
      average[0] += E; average[1] += E*E;
      average[2] += M; average[3] += M*M; average[4] += fabs(M);

			// Test energy difference to find minimum number of MC cycles before ground state is reached
      if(cycles > 1){
          test = fabs((Eprev-E)/Eprev);
          if (test < 0.01) countstart = 1;
			}

      if (countstart == 1 && first){
            minimum_mcs = cycles;
            first = false;
            count = true;
        }
				if (my_rank == 0) {
            output(file,cycles,T,average,accepted_configs,l);
        }
			}
			if (my_rank == 0){
	        cout << "Minimum Monte Carlo sykluser (likevekt):" << "\t" << minimum_mcs << endl;
	    }
}
void expectationValues_T(double temp,ofstream &file,int l,int mc,int**spin_matrix,bool &rand,int myloop_begins,int myloop_ends,int my_rank){
	double w[17], average[5],total_average[5];
	for(int dE = -8; dE <= 8; dE++){w[dE+8] = 0;}
	for(int dE = -8; dE <= 8; dE++){w[dE+8] = exp(-dE/temp);}

	for(int i = 0; i < 5; i++){
		average[i] = 0;
		total_average[i] = 0;
	}

	double E, M;
	E = 0.0; M = 0.0;
	int accepted_configs = 0;

	// Setup spin matrix
	if(rand) initialize_random(l,spin_matrix,E,M);
	else initializelattice(l,spin_matrix,E,M);

	for(int cycles = myloop_begins; cycles <= myloop_ends;cycles++){
		metropolissampling(l,spin_matrix,E,M,w,accepted_configs);
		// Update expectation values
		average[0] += E; average[1] += E*E;
		average[2] += M; average[3] += M*M; average[4] += fabs(M);
	}

	for(int i = 0;i<5;i++){
		MPI_Reduce(&average[i],&total_average[i],1,MPI_DOUBLE,MPI_SUM,0,MPI_COMM_WORLD);
	}
	if(my_rank ==0){
		output(file,mc,temp,total_average,accepted_configs,l);
	}

}

void exact(int l){
	double z, e_eks, e2_eks, cv, m_eks, m2_eks, x, sig2,t,m_abs;
	t = 1.0;
	z = (4 * cosh(8) + 12);
	e_eks = -8 * sinh(8)/(cosh(8) + 3);
	e2_eks = 64*cosh(8)/(cosh(8) + 3);
	sig2 = (64/(cosh(8) + 3)) * (cosh(8) - (sinh(8) * sinh(8) / (cosh(8) + 3)));
	cv = sig2 /(t * t);
	m_eks = 0;
	m2_eks = 8 * exp(8) + 8 / (cosh(8) + 3);
	m_abs = 2 * exp(8) + 4 / (cosh(8) + 3);
	x = (8 * exp(8) + 8) / (cosh(8) + 3) /t;
	cout <<"E = " << e_eks/l/l << endl;
	cout << "Var(E)= " << sig2/l/l << endl;
	cout << "Cv = " << cv/l/l << endl;
	cout << "M = " << m_abs /l/l<< endl;
	cout << "X = " << x/l/l << endl;
}
void output(ofstream &file,int MC_cycles, double T,double *average,int accepted_configs,int l)
{
	double norm = 1/((double) (MC_cycles));
  double E_exp = average[0]*norm;
  double E_exp2 = average[1]*norm;
	double M_exp = average[2]*norm;
  double M_exp2 = average[3]*norm;
  double M_abs = average[4]*norm;
  double Cv = (E_exp2 - E_exp*E_exp)/T/T;
  double chi = (M_exp2 - M_abs*M_abs)/T;
	double VarE = (E_exp2 - E_exp*E_exp);

  E_exp /= l*l;
  M_abs /= l*l;
  Cv /= l*l;
  chi /= l*l;
	VarE /= l*l;
	file << setw(15) << setprecision(8) << MC_cycles;
	file << setw(15) << setprecision(8) << T;
	file << setw(15) << setprecision(8) << E_exp;
	file << setw(15) << setprecision(8) << Cv;
	file << setw(15) << setprecision(8) << M_abs;
	file << setw(15) << setprecision(8) << chi;
	file << setw(15) << setprecision(8) << accepted_configs*norm;
	file << setw(15) << setprecision(8) << VarE
	<< endl;
}
