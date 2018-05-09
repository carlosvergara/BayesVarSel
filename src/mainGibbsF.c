#include "R.h"
#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_linalg.h>
#include <R_ext/Utils.h>
//#include <gsl/gsl_sf_hyperg.h>
//#include <gsl/gsl_permutation.h>
//#include <gsl/gsl_permute_vector.h>
//#include <gsl/gsl_heapsort.h>
//#include <gsl/gsl_sort.h>
//#include <gsl/gsl_sort_vector.h>
#include<time.h>
#include <gsl/gsl_rng.h> 
#include <gsl/gsl_randist.h>


#include "auxiliaryfuncs.h"
//#include "auxiliaryfuncs.c"
#include "Gibbsauxiliaryfuncs.h"
//#include "Gibbsauxiliaryfuncs.c"
#include "allBF.h"
//#include "allBF.c"
#include "priorprob.h"

void GibbsRobustFSB (char *pI[], int *pn, int *pp, int *pSAVE, char *homePath[], int *pBurnin, double *time, int *pknull, int *pnthin, int *pseed)
{
	//Version where the null model is only the error term and vs is performed over the whole design
	//matrix. It keeps track of the possibility that this is used in combination with a previous
	//transformation of the data on which the null ORIGINAL model had knull covariates (eg. knull=1 if
	//the original null model is only the intercept
	
	//9-5-18 version of GibbsRobust with Factors and prior probabilities obtained with SB-SB
	
	void R_CheckUserInterrupt(void);
	
	clock_t tiempo_ejec=clock();
	
	gsl_set_error_handler_off();
	
	//PARAMETERS: (R version)
	int n=*pn;
	int p=*pp;
	int SAVE=*pSAVE;
	int knull=*pknull;
	int nthin=*pnthin;
	//The files have an index attached:
	char subindex[100];
	strcpy(subindex,*pI);
	//where the files are read and writen the results:
	char home[100];
	strcpy(home,*homePath);
	//-----------
	
    gsl_rng * ran = gsl_rng_alloc(gsl_rng_mt19937);
    gsl_rng_set(ran, *pseed);
	//double info=pow(2,p);
	
	Rprintf("The problem has %d variables and %d observations\n", p, n);
	//Rprintf("The whole problem would have %f different competing models\n", info);
	
	//Data files: (R version)
	char strtmp[100] = ""; 
	
	char nfileR1[100] = "/Dependent.txt";
	strcpy(strtmp,home);
	strcat(strtmp,nfileR1);
	strcpy(nfileR1,strtmp);
	FILE * fResponse = fopen(nfileR1, "r");
	
	char nfileR2[100] = "/Design.txt";
	strcpy(strtmp,home);
	strcat(strtmp,nfileR2);
	strcpy(nfileR2,strtmp);
	FILE * fDesign = fopen(nfileR2, "r");
	//-----------
	
	
	//Factors:
	//positions is a file that contains, in binary, the positions of each of the vars
	//(either factor and its levels or numerical vars) in the design matrix
	char nfileR12[100] = "/positions.txt";
	strcpy(strtmp,home);
	strcat(strtmp,nfileR12);
	strcpy(nfileR12,strtmp);
	FILE * fpositions = fopen(nfileR12, "r");
	int i;
	int nofvars=0; //Number of different explanatory variables (either factors or numerical)
  while((i=fgetc(fpositions))!=EOF)
    {
        if (i == '\n')
            nofvars++;
    }
		
  Rprintf("Number of explanatory variables: %d\n", nofvars);	
	
		
	//Factors:
	//positionsx is a file that contains, in binary, the positions of numerical vars) in the design matrix
	char nfileR13[100] = "/positionsx.txt";
	strcpy(strtmp,home);
	strcat(strtmp,nfileR13);
	strcpy(nfileR13,strtmp);
	FILE * fpositionsx = fopen(nfileR13, "r");
			
	
	//The initial model:
	char nfileR3[100] = "/initialmodel.txt";
	strcpy(strtmp,home);
	strcat(strtmp,nfileR3);
	strcpy(nfileR3,strtmp);
	FILE * finitM = fopen(nfileR3, "r");
		
	//The prior probabilities:
	char nfileR4[100] = "/priorprobs.txt";
	strcpy(strtmp,home);
	strcat(strtmp,nfileR4);
	strcpy(nfileR4,strtmp);
	FILE * fPriorProb = fopen(nfileR4, "r");	
		
	//File that contain all visited models after burnin
	char nfile10[100]="/AllModels";
	strcpy(strtmp,home);
	strcat(strtmp,nfile10);
	strcpy(nfile10,strtmp);
	FILE * fAllModels = fopen(strcat(nfile10,subindex), "w");
	
	//File that contain all BF's of previuos models
	char nfile11[100]="/AllBF";
	strcpy(strtmp,home);
	strcat(strtmp,nfile11);
	strcpy(nfile11,strtmp);
	FILE * fAllBF = fopen(strcat(nfile11,subindex), "w");
	
	
	int cont=0; //cont is used for the thining
	
	gsl_vector * y=gsl_vector_calloc(n);
	gsl_matrix * X=gsl_matrix_calloc(n,p);
	
	
	
	//Put the values in the response file into the vector y	
	gsl_vector_fscanf(fResponse, y);
	fclose(fResponse);
	//and those of the design
	gsl_matrix_fscanf(fDesign, X);
	fclose(fDesign);	
	
  //Factors:
	gsl_vector * positionsx = gsl_vector_calloc(p);
  gsl_matrix * positions = gsl_matrix_calloc(nofvars, p);	
	gsl_vector_fscanf(fpositionsx, positionsx);
	fclose(fpositionsx);
	gsl_matrix_fscanf(fpositions, positions);
	fclose(fpositions);	
	
	
	//Get the Sum of squared errors for the null model:
	//SSEnull is the sum of squared errors of the model with no covariates
	//Q=SSE/SSEnull
	double SSEnull=0.0, Q=0.0;

	SSEnull = gsl_blas_dnrm2(y);
	SSEnull = pow(SSEnull,2);

	//k2 will contain the number of covariates in each of the models visited
	int k2=0;
	int k2e=0;
	
	//the vector with the inclusion probs:
	gsl_vector * incl_prob=gsl_vector_calloc(p);
	
	//the matrix with the joint inclusion probs:
	gsl_matrix * joint_incl_prob=gsl_matrix_calloc(p,p);
	
	//the vector with the posterior probs of each dimension
	//position 0 contains contains Pr(M|dim=knull+0,data)(=Pr(MNull|data)), position 1 contains Pr(M|dim=knull+1,data),
	//...position p contains Pr(M|dim=knull+p,data)(=Pr(Mfull|data)).
	gsl_vector * dimension_prob=gsl_vector_calloc(p+1);
	
	//index is a vector of 0 and 1's with the binary expression saying which covariates are active
	//(ie index is just the binary expression of model, an integer between 1 and N)
	gsl_vector * index = gsl_vector_calloc(p);
	
	//We fill it with the initial model
	gsl_vector_fscanf(finitM, index);
	fclose(finitM);

	//the prior probabilities are read once in a vector
	//this vector is accesed to compute them
	//this vector only used in the "User"-type routines (are in all for
	//simplicity in maintenance)
	//priorvector which is read from a vector located in the working directory
	//call knull the dimension of the ORIGINAL null model
	//is a (p+1)-vector: position 0 contains Pr(M|dim=knull+0)(=Pr(MNull)), position 1 contains Pr(M|dim=knull+1),
	//...position p contains Pr(M|dim=knull+p)(=Pr(Mfull)).
	gsl_vector *priorvector = gsl_vector_calloc(p+1);
	gsl_vector_fscanf(fPriorProb, priorvector);
	fclose(fPriorProb);
	
	//The HPM of the models visited:
	gsl_vector * HPM = gsl_vector_calloc(p);
	gsl_vector_memcpy(HPM, index);
	
	//hatbetap will store the mle of the k2-dimensional beta but, inserted
	//in a p-dimensional vector (with corresponding positions)
	gsl_vector * hatbetap=gsl_vector_calloc(p);
	//meanhatbetap will contain the posterior mean of the betahats's (model averaged)
	gsl_vector * meanhatbetap=gsl_vector_calloc(p);
	
	// //////////////////////////////////////////////
	int iter=1, component=1, oldcomponent=1, newcomponent=1;
	
	//In what follows we call, for a given model Mi, PBF=BF(Mi vs M0)*UPr(Mi)
	//where UPr(Mi) is the unnormalized prior probability
	//this is the calculation for the initial model:
	double oldPBF=0.0, newPBF=0.0;

    k2=(int) gsl_blas_dasum(index);
    if (k2>0){
        Q=Gibbsstatistics(p, n, SSEnull, X, y, index, &k2, hatbetap);
        //the bayes factor in favor of Mi and against M0
        k2e=k2+knull;
        oldPBF= RobustBF21fun(n,k2e,knull,Q)*Constpriorprob(p,k2);
    }
    else{
        Q=1.0;
        k2e=k2+knull;
        gsl_vector_set_zero(hatbetap);
        oldPBF= 1.0*Constpriorprob(p,k2);
    }
    
	double HPMBF=oldPBF;
	double ratio=0.0;
    double BF=0.0;
	
	//Burnin
	//Interpret old as current and new as proposal
	for (iter=1; iter<(*pBurnin+1); iter++){
		for (component=0; component<p; component++)
		{
			oldcomponent=gsl_vector_get(index, component);
			gsl_vector_set(index, component, 1-oldcomponent);
            k2=(int) gsl_blas_dasum(index);
            if (k2>0){
                Q=Gibbsstatistics(p, n, SSEnull, X, y, index, &k2, hatbetap);
                k2e=k2+knull;
                newPBF= RobustBF21fun(n,k2e,knull,Q)*Constpriorprob(p,k2);
            }
            else{
                Q=1.0;
                gsl_vector_set_zero(hatbetap);
                newPBF= 1.0*Constpriorprob(p,k2);
            }
            ratio=(oldcomponent*(oldPBF-newPBF)+newPBF)/(newPBF+oldPBF);
            newcomponent=gsl_ran_bernoulli(ran, ratio);
			if (newcomponent==oldcomponent){
				gsl_vector_set(index, component, newcomponent);
				k2=k2-1+2*oldcomponent;
			}
			else
				oldPBF=newPBF;
		}
		R_CheckUserInterrupt();
	}
	
	//main loop
	//Interpret old as current and new as proposal
	for (iter=1; iter<(SAVE+1); iter++){
		cont=0;
		while (cont<nthin){
		for (component=0; component<p; component++)
		{
			oldcomponent=gsl_vector_get(index, component);
			gsl_vector_set(index, component, 1-oldcomponent);
            k2=(int) gsl_blas_dasum(index);

            if (k2>0){
                Q=Gibbsstatistics(p, n, SSEnull, X, y, index, &k2, hatbetap);
                k2e=k2+knull;
                BF=RobustBF21fun(n,k2e,knull,Q);
                //Rprintf("with %d,covariates,vs %d covariates,  %d, data, and %.20f the BayesFactor is %.20f \n", k2e,knull,n, Q, BF);
                newPBF= RobustBF21fun(n,k2e,knull,Q)*Constpriorprob(p,k2);
            }
            else{
                Q=1.0;
                gsl_vector_set_zero(hatbetap);
                newPBF= 1.0*Constpriorprob(p,k2);
            }

            ratio=(oldcomponent*(oldPBF-newPBF)+newPBF)/(newPBF+oldPBF);
			newcomponent=gsl_ran_bernoulli(ran, ratio);
			if (newcomponent==oldcomponent){
				gsl_vector_set(index, component, newcomponent);
				k2=k2-1+2*oldcomponent;
			}
			else
				oldPBF=newPBF;
		}
			cont++;
			R_CheckUserInterrupt();
		}
		
		//update the summaries
		//inclusion probs:
		gsl_blas_daxpy(1.0, index, incl_prob);		
		//joint inclusion probs:
		gsl_blas_dger(1.0, index, index, joint_incl_prob);		
		//mean of betahats
		gsl_blas_daxpy(1.0, hatbetap, meanhatbetap);		
		//probability of dimensions:
		gsl_vector_set(dimension_prob,k2,gsl_vector_get(dimension_prob,k2)+1.0);
		//HPM
		if (oldPBF>HPMBF)
			{
			HPMBF=oldPBF;
			gsl_vector_memcpy(HPM, index);
			}
		//Write to the file the visited model
		my_gsl_vector_fprintf(fAllModels, index, "%f");
		//and the BF's
		fprintf(fAllBF, "%.20f\n", oldPBF/Constpriorprob(p,k2)); 
      

	}
	
	//normalize:
	gsl_vector_scale(incl_prob, 1.0/SAVE);
	gsl_vector_scale(meanhatbetap, 1.0/SAVE);
	gsl_vector_scale(dimension_prob, 1.0/SAVE);
	gsl_matrix_scale(joint_incl_prob, 1.0/SAVE);
	
		
	//Write the results:
	char nfile1[100]="/LastModel";
	strcpy(strtmp,home);
	strcat(strtmp,nfile1);
	strcpy(nfile1,strtmp);
	FILE * fLastModel = fopen(strcat(nfile1,subindex), "w");
	
	char nfile2[100]="/MostProbModels";
	strcpy(strtmp,home);
	strcat(strtmp,nfile2);
	strcpy(nfile2,strtmp);
	FILE * fModels = fopen(strcat(nfile2,subindex), "w");
	
	char nfile3[100]="/InclusionProb";
	strcpy(strtmp,home);
	strcat(strtmp,nfile3);
	strcpy(nfile3,strtmp);
	FILE * fInclusion = fopen(strcat(nfile3,subindex), "w");
	
	char nfile5[100]="/ProbDimension";
	strcpy(strtmp,home);
	strcat(strtmp,nfile5);
	strcpy(nfile5,strtmp);
	FILE * fDim = fopen(strcat(nfile5,subindex), "w");
	
	char nfile8[100]="/JointInclusionProb";
	strcpy(strtmp,home);
	strcat(strtmp,nfile8);
	strcpy(nfile8,strtmp);
	FILE * fJointInclusion = fopen(strcat(nfile8,subindex), "w");
	
	char nfile9[100]="/betahat";
	strcpy(strtmp,home);
	strcat(strtmp,nfile9);
	strcpy(nfile9,strtmp);
	FILE * fbetahat = fopen(strcat(nfile9,subindex), "w");
	//---------
	
	gsl_vector_fprintf(fbetahat, meanhatbetap, "%.20f");
	gsl_vector_fprintf(fInclusion, incl_prob, "%.20f");
	gsl_vector_fprintf(fDim, dimension_prob, "%.20f");
	my_gsl_matrix_fprintf(fJointInclusion, joint_incl_prob, "%.20f");	
	gsl_vector_fprintf(fModels, HPM, "%f");
	gsl_vector_fprintf(fLastModel, index, "%f");
	
	
	fclose(fLastModel);
	fclose(fModels);	
	fclose(fInclusion);
	fclose(fDim);
	fclose(fJointInclusion);
	fclose(fbetahat);
	fclose(fAllModels);
	fclose(fAllBF);
	fclose(fpositions);
	fclose(fpositionsx);
	

	
	gsl_vector_free (y);
	gsl_matrix_free (X);	
	gsl_vector_free (index);
	
	gsl_vector_free(incl_prob);
	gsl_matrix_free(joint_incl_prob);
	gsl_vector_free(dimension_prob);
	gsl_vector_free(hatbetap);
	gsl_vector_free(meanhatbetap);
		
	gsl_rng_free (ran);
		
	//Rprintf("Tiempo de ejecucion: %f seg\n", (double) (clock()-tiempo_ejec)/CLOCKS_PER_SEC);	
	*time=(double) (clock()-tiempo_ejec)/CLOCKS_PER_SEC;

}