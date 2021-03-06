#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include <gsl/gsl_blas.h>
#include <gsl/gsl_multifit.h>

struct patientStruct {
	int num;
	float * concentrations;
	float * times;
	float * doses;
	int size; // Number of entries in the concentrations, times, and doses arrays
	float sex;
	float age;
	float weight;
};

typedef struct patientStruct Patient;

struct databaseStruct {
	Patient * patients;
	int size; // Number of patients
};

typedef struct databaseStruct Database;

struct svmStruct {
	double means[5]; // Normalization constants
	double stds[5]; // Normalization constants
	double sigma; // Gaussian kernel
	double C; // Regularization
	gsl_matrix * trainFeat; // Training samples (support vectors) already normalized
	gsl_vector * trainY; // Training concentration
	gsl_vector * alpha; // Trained coefficients
};

typedef struct svmStruct SVM;

// Allocate the concentrations, times, and doses arrays
void createPatient(Patient * p, int size);

// Free the concentrations, times, and doses arrays
void deletePatient(Patient * p);

// Read a database from a file
Database readDatabase(const char * filename);

// Print a database
void printDatabase(const Database * db);

// Free a database
void deleteDatabase(Database * db);

// Returns the number of inliners, the 4 alpha coefficients, and the indices of the inliners.
// The inliners array contains the indices of the inliners and must be already allocated with the same size as x and y.
int ransac(const float * x, const float * y, int size, float threshold, int k, float alpha[4], int * inliners);

void predictGaussianSVM(const gsl_matrix * xTrain, const gsl_matrix * xTest, const gsl_vector * alpha, double sigma, gsl_vector * y);

void trainGaussianSVM(const gsl_matrix * xTrain, const gsl_vector * y, double * C, double * sigma, gsl_vector * alpha);

// Return predicted concentrations for certain time
int predictN(double start, double stop, int n, const Patient * p, float dose, const SVM * svm, gsl_vector * out);

// Find the "least-relevent" patient and update the training library
int leastRelevent(SVM * svm, const Patient * p);

int main (int argc, const char * argv[]) {
	
	if (argc < 3) {
		fprintf(stderr, "Usage: %s database_train.txt database_test.txt\n", argv[0]);
		return EXIT_FAILURE;
	}
	
	// Read in the data
	printf("Training database:\n");
	Database dbtrain = readDatabase(argv[1]);
	//printDatabase(&dbtrain);
	
	printf("\nTesting database:\n");
	Database dbtest = readDatabase(argv[2]);
	//printDatabase(&dbtest);
	
	// Create the trainFeat matrix + RANSAC
	int nbSamplesTrain = 0;
	
	for (int i = 0; i < dbtrain.size; ++i)
		nbSamplesTrain += dbtrain.patients[i].size;
	
	int nbSamplesTest = 0;
	
	for (int i = 0; i < dbtest.size; ++i)
		nbSamplesTest += dbtest.patients[i].size;
	
	gsl_matrix * trainFeat = gsl_matrix_alloc(nbSamplesTrain, 5);
	gsl_matrix * testFeat = gsl_matrix_alloc(nbSamplesTest, 5);
	
	float * x = malloc(nbSamplesTrain * sizeof(float));
	float * y = malloc(nbSamplesTrain * sizeof(float));
	
	for (int i = 0, j = 0; i < dbtrain.size; ++i) {
		for (int k = 0; k < dbtrain.patients[i].size; ++k, ++j) {
			gsl_matrix_set(trainFeat, j, 0, dbtrain.patients[i].times[k]);
			gsl_matrix_set(trainFeat, j, 1, dbtrain.patients[i].doses[k]);
			gsl_matrix_set(trainFeat, j, 2, dbtrain.patients[i].sex);
			gsl_matrix_set(trainFeat, j, 3, dbtrain.patients[i].age);
			gsl_matrix_set(trainFeat, j, 4, dbtrain.patients[i].weight);
			
			x[j] = dbtrain.patients[i].times[k];
			y[j] = dbtrain.patients[i].concentrations[k];
		}
	}
	
	for (int i = 0, j = 0; i < dbtest.size; ++i) {
		for (int k = 0; k < dbtest.patients[i].size; ++k, ++j) {
			gsl_matrix_set(testFeat, j, 0, dbtest.patients[i].times[k]);
			//gsl_matrix_set(testFeat, j, 0, 24);
			gsl_matrix_set(testFeat, j, 1, dbtest.patients[i].doses[k]);
			gsl_matrix_set(testFeat, j, 2, dbtest.patients[i].sex);
			gsl_matrix_set(testFeat, j, 3, dbtest.patients[i].age);
			gsl_matrix_set(testFeat, j, 4, dbtest.patients[i].weight);
		}
	}
	
	float alpha[3];
	int * inliners = malloc(nbSamplesTrain * sizeof(int));
	
	int nbInliners = ransac(x, y, nbSamplesTrain, 500.0f, 4, alpha, inliners);
	
	printf("# inliners = %d / %d, alpha = %f %f %f\n", nbInliners, nbSamplesTrain, alpha[0], alpha[1], alpha[2]);
	
	for (int i = 0; i < nbInliners; ++i)
		for (int j = 0; j < 5; ++j)
			gsl_matrix_set(trainFeat, i, j, gsl_matrix_get(trainFeat, inliners[i], j));
	
	trainFeat->size1 = nbInliners;
	nbSamplesTrain = trainFeat->size1;
	
	SVM svm;
	
	// Normalize the trainFeat matrix
	for (int i = 0; i < 5; ++i) {
		float mean=0;
		float std=0;
		
		for (int j = 0; j < nbSamplesTrain; ++j) {
			float x = gsl_matrix_get(trainFeat, j, i);
			mean += x;
			std += x * x;
		}
		
		mean /= nbSamplesTrain;
		std = sqrt(std / nbSamplesTrain - mean * mean);
		
		printf("Feature %d, mean %f, std %f\n", i, mean, std);
		
		if (std == 0){
			std = 1;
		}		
		for (int j = 0; j < nbSamplesTrain; ++j)
			gsl_matrix_set(trainFeat, j, i, (gsl_matrix_get(trainFeat, j, i) - mean) / std);
		
		for (int j = 0; j < nbSamplesTest; ++j)
			gsl_matrix_set(testFeat, j, i, (gsl_matrix_get(testFeat, j, i) - mean) / std);
		
		svm.means[i] = mean;
		svm.stds[i] = std;

	}
	/*
	printf("First training line:");
	for (int j = 0; j < 5; ++j)
		printf(" %f", gsl_matrix_get(trainFeat, 0, j));
	printf("\n");
	
	printf("First testing line:");
	for (int j = 0; j < 5; ++j)
		printf(" %f", gsl_matrix_get(testFeat, 0, j));
	printf("\n");
	
	printf("Second testing line:");
	for (int j = 0; j < 5; ++j)
		printf(" %f", gsl_matrix_get(testFeat, 1, j));
	printf("\n");*/
	
	// SVM
	svm.trainFeat = trainFeat;
	svm.trainY = gsl_vector_calloc(nbSamplesTrain);
	svm.alpha = gsl_vector_calloc(nbSamplesTrain);
//	gsl_vector * out = gsl_vector_calloc(nbSamplesTest);
	gsl_vector * signal = gsl_vector_calloc(nbSamplesTest);
	
	for (int j = 0; j < nbSamplesTrain; ++j)
		gsl_vector_set(svm.trainY, j, y[inliners[j]]);
	
	svm.C = 1;
	svm.sigma = 1;
	
	trainGaussianSVM(svm.trainFeat, svm.trainY, &svm.C, &svm.sigma, svm.alpha);
    
    
    ////////////////////////////// predictN() //////////////////////////////////////////////////////
    // Predict the drug concentrations w.r.t a certain start time, stop time, and sampling period.
    // Currently, 'p' is defined by given a patient number (0~73) referring to the complete_test.txt.
    // 'out' gives the predicted concentration values.
    double start = 1.0;
    double stop = 24.0;
    int period = 24;
    float dose = 400.0;
    Patient p = dbtest.patients[0];
    gsl_vector * out = gsl_vector_calloc(period);
    
    
    if (predictN(start, stop, period, &p, dose, &svm, out))
        return -1;
    
    // un-normalize
    //for (int i = 0; i < period; ++i)
    // gsl_vector_set(out, i, (gsl_vector_get(out,i) * std_y) + mean_y);
    
    ////////////////////////////// predictN() //////////////////////////////////////////////////////
    /*
    for (int j = 0; j < period; ++j){
        if (gsl_vector_get(out,j) < 750){
            gsl_vector_set(signal, j, -1);
            //printf("j = %d, sig = %d", j, 2);
        }
        else if (gsl_vector_get(out,j) > 1500){
            gsl_vector_set(signal, j, 1);
            //printf("j = %d, sig = %d", j, 1);
        }
        else {
            gsl_vector_set(signal, j, 0);
            //printf("j = %d, sig = %d", j, 0); //here 有问题。会出现error
        }
    }
    
    
     printf("\nout =");
     
     for (int j = 0; j < period; ++j)
     printf(" %f", gsl_vector_get(out,j));
     printf("\nsignal =");
     printf("\n");
     for (int j = 0; j < period; ++j)
     printf(" %.0f", gsl_vector_get(signal,j));
     */
    
	
    //	// update the library
    //	for (int i = 0; i < dbtest.size; ++i) {
    //		Patient upd_p = dbtest.patients[i];
    //
    //		for (int j = 0; j < upd_p.size; ++j) {
    //			printf("updat patient %d, sample %d; ", i, j);
    //			if(leastRelevent(&svm, &upd_p))
    //				return -1;
    //		}
    //	}
	
	
	// free
	gsl_vector_free(svm.trainY);
	gsl_vector_free(svm.alpha);
	gsl_vector_free(signal);
	//gsl_vector_free(out);
	gsl_matrix_free(trainFeat);
	gsl_matrix_free(testFeat);
	free(x);
	free(y);
	free(inliners);
	
    return 0;


}

void createPatient(Patient * p, int size)
{
	p->concentrations = malloc(size * sizeof(float));
	p->times = malloc(size * sizeof(float));
	p->doses = malloc(size * sizeof(float));
	p->size = size;
}

void deletePatient(Patient * p)
{
	free(p->concentrations);
	free(p->times);
	free(p->doses);
	p->concentrations = NULL;
	p->times = NULL;
	p->doses = NULL;
	p->size = 0;
}

Database readDatabase(const char * filename)
{
	Database db = {NULL, 0};
	Patient p = {-1, NULL, NULL, NULL, 0, 0, 0, 0};
	FILE * file = fopen(filename, "r");
	
	if (file == NULL) {
		fprintf(stderr, "Could not open file %s.\n", filename);
		return db;
	}
	
	while (!feof(file)) {
		// Try to read a whole line
		int num;
		float concentration;
		float time;
		float dose;
		float sex;
		float age;
		float weight;
		fscanf(file, "%d %f %f %f %f %f %f\n", &num, &concentration, &time, &dose, &sex, &age, &weight);
		
		if (!ferror(file)) {
			if (num == p.num) { // Still the same patient
				++p.size;
				p.concentrations = realloc(p.concentrations, p.size * sizeof(float));
				p.times = realloc(p.times, p.size * sizeof(float));
				p.doses = realloc(p.doses, p.size * sizeof(float));
				p.concentrations[p.size - 1] = concentration;
				p.times[p.size - 1] = time;
				p.doses[p.size - 1] = dose;
			}
			else { // A new patient
				// Add the previous patient to the database if there is one
				if (p.num != -1) {
					++db.size;
					db.patients = realloc(db.patients, db.size * sizeof(Patient));
					db.patients[db.size - 1] = p;
				}
				
				// Create a new patient
				createPatient(&p, 1);
				p.num = num;
				p.concentrations[0] = concentration;
				p.times[0] = time;
				p.doses[0] = dose;
				p.sex = sex;
				p.age = age;
				p.weight = weight;
			}
		}
	}
	
	// Add the previous patient to the database if there is one
	if (p.num != -1) {
		++db.size;
		db.patients = realloc(db.patients, db.size * sizeof(Patient));
		db.patients[db.size - 1] = p;
	}
	
	fclose(file);
	
	return db;
}

void deleteDatabase(Database * db)
{
	int i;
	
	for (i = 0; i < db->size; ++i)
		deletePatient(&db->patients[i]);
	
	free(db->patients);
	db->patients = NULL;
	db->size = 0;
}

void printDatabase(const Database * db)
{
	int i, j;
	
	for (i = 0; i < db->size; ++i) {
		printf("Patient %d: sex %f, age %f, weight %f\n    concentrations:", db->patients[i].num,
			   db->patients[i].sex, db->patients[i].age, db->patients[i].weight);
		
		for (j = 0; j < db->patients[i].size; ++j)
			printf(" %f", db->patients[i].concentrations[j]);
		
		printf("\n    times:");
		
		for (j = 0; j < db->patients[i].size; ++j)
			printf(" %f", db->patients[i].times[j]);
		
		printf("\n    doses:");
		
		for (j = 0; j < db->patients[i].size; ++j)
			printf(" %f", db->patients[i].doses[j]);
		
		printf("\n");
	}
}

int ransac(const float * x, const float * y, int size, float threshold, int k, float * alpha, int * inliners)
{
	gsl_multifit_linear_workspace * work = gsl_multifit_linear_alloc(k, 3); // Required by GSL
	gsl_matrix * mx = gsl_matrix_alloc(size, 3); // Matlab: m
	gsl_matrix * mx2 = gsl_matrix_alloc(k, 3); // Matlab: m(r(1:k),:)
	gsl_vector * my = gsl_vector_alloc(k); // Matlab: y(r(1:k))
	gsl_vector * malpha = gsl_vector_alloc(3); // Matlab: a
	gsl_matrix * mcov = gsl_matrix_alloc(3, 3); // Required by GSL
	gsl_vector * mdist = gsl_vector_alloc(size); // Matlab: m * a
	int * randperm = malloc(k * sizeof(int)); // Matlab: r
	double chisq;
	int i, j, n, nbInliners;
	
	// Fill the matrix mx
	// Matlab: m = [x.^(-2) log(x) (1-exp(-x))];
	for (j = 0; j < size; ++j) {
		gsl_matrix_set(mx, j, 0, pow(x[j],-2));
		gsl_matrix_set(mx, j, 1, log(x[j]));
		gsl_matrix_set(mx, j, 2, 1.0 - exp(-x[j]));
	}
	
	// Matlab: inliners = 0;
	nbInliners = 0;
	
	// Matlab: for i = 1:100000
	for (i = 0; i < 100000; ++i) {
		// Sample k indices
		// Matlab: r = randperm(size(x,1));
		for (j = 0; j < k; ++j)
			randperm[j] = rand() % size;
		
		for (j = 0; j < k; ++j) {
			gsl_matrix_set(mx2, j, 0, gsl_matrix_get(mx, randperm[j], 0));
			gsl_matrix_set(mx2, j, 1, gsl_matrix_get(mx, randperm[j], 1));
			gsl_matrix_set(mx2, j, 2, gsl_matrix_get(mx, randperm[j], 2));
			gsl_vector_set(my, j, y[randperm[j]]);
		}
		
		// Matlab: a = m(r(1:k),:) \ y(r(1:k));
		gsl_multifit_linear(mx2, my, malpha, mcov, &chisq, work);
		
		// Count the number of inliners
		// Matlab: m * a
		gsl_blas_dgemv(CblasNoTrans, 1.0, mx, malpha, 0.0, mdist);
		
		// Matlab: n = sum(abs(m * a - y) < th);
		n = 0;
		
		for (j = 0; j < size; ++j)
			if (fabs(gsl_vector_get(mdist, j) - y[j]) < threshold)
				++n;
		
		// Matlab: if n > inliners
		if (n > nbInliners) {
			printf("RANSAC trial %d, # inliners = %d, chisq = %f, alpha = %f %f %f\n", i, n, chisq,
				   gsl_vector_get(malpha, 0), gsl_vector_get(malpha, 1), gsl_vector_get(malpha, 2));
			// Matlab: inliners = n;
			nbInliners = n;
			
			// Matlab: alpha = a;
			for (j = 0; j < 3; ++j)
				alpha[j] = gsl_vector_get(malpha, j);
			
			n = 0;
			
			for (j = 0; j < size; ++j)
				if (fabs(gsl_vector_get(mdist, j) - y[j]) < threshold)
					inliners[n++] = j;
		}
	}
	
	gsl_multifit_linear_free(work);
	gsl_matrix_free(mx);
	gsl_matrix_free(mx2);
	gsl_vector_free(my);
	gsl_vector_free(malpha);
	gsl_matrix_free(mcov);
	gsl_vector_free(mdist);
	free(randperm);
	
	return nbInliners;
}


void predictGaussianSVM(const gsl_matrix * xTrain, const gsl_matrix * xTest, const gsl_vector * alpha, double sigma, gsl_vector * y)
{
	gsl_vector * xTrain2 = gsl_vector_calloc(xTrain->size1);
	gsl_matrix * kernel = gsl_matrix_alloc(xTest->size1, xTrain->size1);
	
	int i, j;
	
	assert(xTrain->size2 == xTest->size2); // Same number of features
	
	// Matlab: Xtrain2 = sum(Xtrain.^2, 2);
	for (i = 0; i < xTrain->size1; ++i) {
		double dot = 0.0;
		gsl_vector_const_view row = gsl_matrix_const_row(xTrain, i);
		gsl_blas_ddot(&row.vector, &row.vector, &dot);
		gsl_vector_set(xTrain2, i, dot);
	}
	
	// Matlab: Xtest2 = sum(Xtest.^2, 2);
	for (i = 0; i < xTest->size1; ++i) {
		double dot = 0.0;
		gsl_vector_const_view row = gsl_matrix_const_row(xTest, i);
		gsl_blas_ddot(&row.vector, &row.vector, &dot);
		
		// Matlab: D = repmat(Xtest2, [1 N]) + repmat(Xtrain2', [M 1])
		for (j = 0; j < xTrain->size1; ++j)
			gsl_matrix_set(kernel, i, j, gsl_vector_get(xTrain2, j) + dot);
	}
	
	// Matlab: - 2 * (Xtest * Xtrain');
	gsl_blas_dgemm(CblasNoTrans, CblasTrans,-2.0, xTest, xTrain, 1.0, kernel);
	
	if (sigma <= 0.0) {
		// Matlab: sigma = sqrt(1 / mean(mean(D)));
		double sum = 0.0;
		
		for (i = 0; i < xTest->size1; ++i)
			for (j = 0; j < xTrain->size1; ++j)
				sum += gsl_matrix_get(kernel, i, j);
		
		sigma = sum / (xTest->size1 * xTrain->size1);
	}
	
	// Matlab: K = exp(-D / (2 * sigma^2));
	sigma =-1.0 / (2.0 * sigma * sigma);
	
	for (i = 0; i < xTest->size1; ++i)
		for (j = 0; j < xTrain->size1; ++j)
			gsl_matrix_set(kernel, i, j, exp(gsl_matrix_get(kernel, i, j) * sigma));
	
	// Matlab: y = K * alpha;
	gsl_blas_dgemv(CblasNoTrans, 1.0, kernel, alpha, 0.0, y);
	
	gsl_vector_free(xTrain2);
	gsl_matrix_free(kernel);
}

void trainGaussianSVM(const gsl_matrix * xTrain, const gsl_vector * y, double * C, double * sigma, gsl_vector * alpha)
{
	gsl_matrix * kernel = gsl_matrix_calloc(xTrain->size1, xTrain->size1);
	gsl_multifit_linear_workspace * work = gsl_multifit_linear_alloc(xTrain->size1, xTrain->size1);
	gsl_matrix * cov = gsl_matrix_alloc(xTrain->size1, xTrain->size1);
	double chisq;
	int i, j;
	
	assert(y->size == xTrain->size1);
	
	// Matlab: X2 = sum(X.^2, 2);
	// Matlab: D = repmat(X2, [1 N]) + repmat(X2', [N 1])
	for (i = 0; i < xTrain->size1; ++i) {
		double dot = 0.0;
		gsl_vector_const_view row = gsl_matrix_const_row(xTrain, i);
		gsl_blas_ddot(&row.vector, &row.vector, &dot);
		
		for (j = 0; j < xTrain->size1; ++j) {
			gsl_matrix_set(kernel, i, j, gsl_matrix_get(kernel, i, j) + dot);
			gsl_matrix_set(kernel, j, i, gsl_matrix_get(kernel, j, i) + dot);
		}
	}
	
	// Matlab: - 2 * (X * X');
	gsl_blas_dgemm(CblasNoTrans, CblasTrans,-2.0, xTrain, xTrain, 1.0, kernel);
	
	if (*C <= 0.0)
		*C = 1000.0;
	
	if (*sigma <= 0.0) {
		double sum = 0.0;
		
		for (i = 0; i < xTrain->size1; ++i)
			for (j = 0; j < xTrain->size1; ++j)
				sum += gsl_matrix_get(kernel, i, j);
		
		*sigma = sum / (xTrain->size1 * xTrain->size1);
	}
	
	double sigmaInv =-1.0 / (2.0 * (*sigma) * (*sigma));
	double Cinv = 1.0 / *C;
	
	for (i = 0; i < xTrain->size1; ++i) {
		for (j = 0; j < xTrain->size1; ++j)
			gsl_matrix_set(kernel, i, j, exp(gsl_matrix_get(kernel, i, j) * sigmaInv));
		
		gsl_matrix_set(kernel, i, i, gsl_matrix_get(kernel, i, i) + Cinv);
	}
	
	gsl_multifit_linear(kernel, y, alpha, cov, &chisq, work);
	
	gsl_matrix_free(kernel);
	gsl_multifit_linear_free(work);
	gsl_matrix_free(cov);
}

int predictN(double start, double stop, int n, const Patient * p, float dose, const SVM * svm, gsl_vector * out)
{
	if ((start < 0) || (start >= stop) || (n < 1) || !p || !svm || !svm->trainFeat || !svm->alpha ||
		(svm->trainFeat->size1 != svm->alpha->size) || (svm->sigma <= 0) || !out)
		return -1;
	
	gsl_matrix * testFeat = gsl_matrix_alloc(n, 5);
	
	// Normalize the testFeat matrix
	for (int j = 0; j < n; ++j)
		gsl_matrix_set(testFeat, j, 0, (start + j * (stop - start) / (n-1) - svm->means[0]) / svm->stds[0]);
	
	for (int j = 0; j < n; ++j)
		gsl_matrix_set(testFeat, j, 1, (dose - svm->means[1]) / svm->stds[1]);
	
	for (int j = 0; j < n; ++j)
		gsl_matrix_set(testFeat, j, 2, (p->sex - svm->means[2]) / svm->stds[2]);
	
	for (int j = 0; j < n; ++j)
		gsl_matrix_set(testFeat, j, 3, (p->age - svm->means[3]) / svm->stds[3]);
	
	for (int j = 0; j < n; ++j)
		gsl_matrix_set(testFeat, j, 4, (p->weight - svm->means[4]) / svm->stds[4]);
	
	printf("\nsigma: %f\n", svm->sigma);
	
	
	predictGaussianSVM(svm->trainFeat, testFeat, svm->alpha, svm->sigma, out);
	
	printf("\nout:");
	for (int i = 0; i < n; ++i)
		printf(" %f", gsl_vector_get(out, i));
	
	gsl_matrix_free(testFeat);
	
	printf("\n\n fen ge xian\n\n");
	return 0;
}

int leastRelevent(SVM * svm, const Patient * p)
{
	if (!svm || !p)
		return -1;
	
	// Take the time and concentration value from the new patient
	float t = p->times[0];
	float conc = p->concentrations[0];
	
	double testFeat[4];
	
	// Normalize the testFeat matrix
	testFeat[0] = (p->doses[0] - svm->means[1]) / svm->stds[1];
	testFeat[1] = (p->sex - svm->means[2]) / svm->stds[2];
	testFeat[2] = (p->age - svm->means[3]) / svm->stds[3];
	testFeat[3] = (p->weight - svm->means[4]) / svm->stds[4];
	
	// Duplicate the training library
	gsl_matrix * xTrain = svm->trainFeat;
	gsl_matrix * xTest = svm->trainFeat;
	
	// Update the xTest with the time value from the new patient
	for (int i = 0; i < xTrain->size1; ++i)
		gsl_matrix_set(xTest, i, 0, (t - svm->means[0]) / svm->stds[0]);
	
	// Predict the concentration values for all the training patients 
	// at the time when the new patient got measured
	gsl_vector * out = gsl_vector_calloc(xTrain->size1);
	predictGaussianSVM(xTrain, xTest, svm->alpha, svm->sigma, out);
	
	// Compute the mean and std of concentration values including the new measurement
	// Normalize the concentration values
	float Mval = conc;
	float Sval = conc * conc;
	
	for (int j = 0; j < out->size; ++j) {
		float x = gsl_vector_get(out, j);
		Mval += x;
		Sval += x * x;
	}
	Mval /= (out->size + 1);
	Sval = sqrt(Sval / (out->size + 1) - Mval * Mval);
	
	for (int j = 0; j < out->size; ++j)
		gsl_vector_set(out, j, (gsl_vector_get(out,j) - Mval) / Sval);
	
	conc = (conc - Mval) / Sval;
		
	// Compute the largest distance in the sense of concentration difference
	double largestDist = 0.0;
	int largestLoc = 0;
	
	for (int i = 0; i < svm->trainFeat->size1; ++i) {
		double d = 0.0;
		
		d = (gsl_vector_get(out,i) - conc) *
			(gsl_vector_get(out,i) - conc);
		
		for (int j = 0; j < 4; ++j)
			d += (gsl_matrix_get(svm->trainFeat, i ,j) - testFeat[j]) *
				 (gsl_matrix_get(svm->trainFeat, i ,j) - testFeat[j]);
		
		if ( d > largestDist) {
			largestDist = d;
			largestLoc = i;
		}
	}

	printf("Replaced the sample %d\n",largestLoc);
	
	// Update the library with the new paitent's sample
	gsl_matrix_set(svm->trainFeat, largestLoc, 0, (p->times[0] - svm->means[0]) / svm->stds[0]);
	
	for (int j = 0; j < 4; ++j)
		gsl_matrix_set(svm->trainFeat, largestLoc, j+1, testFeat[j]);
	
	gsl_vector_set(svm->trainY, largestLoc, p->concentrations[0]);
	
	trainGaussianSVM(svm->trainFeat, svm->trainY, &svm->C, &svm->sigma, svm->alpha);
	
	return 0;
}
