//The following ifndef/define/endif pattern is called a 
//scope guard, and prevents the C++ compiler (actually, preprocessor)
//from including a header file more than once.
#ifndef __FULL_UPDT_TEST_H_
#define __FULL_UPDT_TEST_H_

#include "full-update.h"
#include "itensor-linsys-solvers.h"
#include "nr3.h"
#include "linbcg.h"
#include "mins.h"
#include "mins_ndim.h"


itensor::Args fullUpdate_COMB_INV(MPO_3site const& uJ1J2, Cluster & cls, CtmEnv const& ctmEnv,
	std::vector<std::string> tn, std::vector<int> pl,
	itensor::Args const& args = itensor::Args::global());

itensor::ITensor psInv(itensor::ITensor const& M,
	itensor::Args const& args = itensor::Args::global());

itensor::Args fullUpdate_COMB_CG(MPO_3site const& uJ1J2, Cluster & cls, CtmEnv const& ctmEnv,
	std::vector<std::string> tn, std::vector<int> pl,
	itensor::Args const& args = itensor::Args::global());

struct Funcd {
	itensor::ITensor const& N;
	itensor::ITensor const& cmbKet;
	itensor::ITensor const& cmbBra;
	std::vector<double> const& vecB;
	double fconst;

	Funcd(itensor::ITensor const& NN, itensor::ITensor const& ccmbKet,
		itensor::ITensor const& ccmbBra, std::vector<double> const& vvecB, 
		double ffconst);

	Doub operator() (VecDoub_I &x);

	void df(VecDoub_I &x, VecDoub_O &deriv);
};

itensor::Args fullUpdate_CG(MPO_3site const& uJ1J2, Cluster & cls, CtmEnv const& ctmEnv,
	std::vector<std::string> tn, std::vector<int> pl,
	itensor::Args const& args = itensor::Args::global());

struct FuncCG {
	itensor::ITensor const& N, protoK;
	itensor::ITensor const& cmbX1, cmbX2, cmbX3;
	std::array<itensor::Index, 4> const& aux;
	std::vector<int> const& pl;
	double psiUNorm;
	double finit;
	double psiNorm;
	int evalCount;
	int dfCount;

	FuncCG(itensor::ITensor const& NN, itensor::ITensor const& pprotoK, 
		itensor::ITensor const& ccmbX1, itensor::ITensor const& ccmbX2,
		itensor::ITensor const& ccmbX3, 
		std::array<itensor::Index, 4> const& aaux,
		std::vector<int> const& ppl,
		double ppsiUNorm,
		double ffinit);

	Doub operator() (VecDoub_I &x);

	void df(VecDoub_I &x, VecDoub_O &deriv);
};

struct FuncCGV2 {
	itensor::ITensor const& N, protoK;
	itensor::ITensor const& cmbX1, cmbX2, cmbX3;
	std::array<itensor::Index, 4> const& aux;
	std::vector<int> const& pl;
	double psiUNorm;
	double finit;
	double psiNorm;
	int evalCount;
	int dfCount;

	FuncCGV2(itensor::ITensor const& NN, itensor::ITensor const& pprotoK, 
		itensor::ITensor const& ccmbX1, itensor::ITensor const& ccmbX2,
		itensor::ITensor const& ccmbX3, 
		std::array<itensor::Index, 4> const& aaux,
		std::vector<int> const& ppl,
		double ppsiUNorm,
		double ffinit);

	Doub operator() (VecDoub_I &x);

	void df(VecDoub_I &x, VecDoub_O &deriv);
};

itensor::Args fullUpdate_ALS_CG(MPO_3site const& uJ1J2, Cluster & cls, CtmEnv const& ctmEnv,
	std::vector<std::string> tn, std::vector<int> pl,
	itensor::Args const& args = itensor::Args::global());

struct FuncALS_CG {
	itensor::ITensor const& M;
	itensor::ITensor & K;
	itensor::ITensor cmbKet;
	Doub psiUNorm;
	Doub finit;
	Doub psiNorm;

	FuncALS_CG(itensor::ITensor const& MM, itensor::ITensor & KK,
		itensor::ITensor ccmbKet, double ppsiUNorm, double ffinit, double ppsiNorm);

	void setup(itensor::ITensor ccmbKet, double ppsiUNorm, double ffinit, double ppsiNorm);

	Doub operator() (VecDoub_I &x);

	void df(VecDoub_I &x, VecDoub_O &deriv);
};

// itensor::Args fullUpdate_ALS_LSCG(MPO_3site const& uJ1J2, Cluster & cls, CtmEnv const& ctmEnv,
// 	std::vector<std::string> tn, std::vector<int> pl,
// 	itensor::Args const& args = itensor::Args::global());

// itensor::Args fullUpdate_LSCG(MPO_3site const& uJ1J2, Cluster & cls, CtmEnv const& ctmEnv,
// 	std::vector<std::string> tn, std::vector<int> pl,
// 	itensor::Args const& args = itensor::Args::global());

// using CG implemented by ITensor
itensor::Args fullUpdate_ALS3S_IT(MPO_3site const& uJ1J2, Cluster & cls, 
	CtmEnv const& ctmEnv,
	std::vector<std::string> tn, std::vector<int> pl,
	itensor::LinSysSolver const& ls,
	itensor::Args const& args = itensor::Args::global());

struct FUlinSys {
	itensor::ITensor & M;
	itensor::ITensor & B;
	itensor::ITensor & A;
	itensor::ITensor cmbA;
	itensor::ITensor cmbKet;
	itensor::Args const& args;

	double res  = 0.0;
	double nres = 0.0; // residue normalized by right hand side
	bool dbg    = false;

	FUlinSys(itensor::ITensor & MM, itensor::ITensor & BB, 
		itensor::ITensor & AA, itensor::ITensor ccmbA, itensor::ITensor ccmbKet,
		itensor::Args const& aargs = itensor::Args::global());

    void solve(itensor::ITensor const& b, itensor::ITensor & x, Int &iter, Doub &err,
    	itensor::LinSysSolver const& ls,
    	itensor::Args const& args = itensor::Args::global());
};

// itensor::Args fullUpdate_ALS_PINV_IT(MPO_3site const& uJ1J2, Cluster & cls, CtmEnv const& ctmEnv,
// 	std::vector<std::string> tn, std::vector<int> pl,
// 	itensor::Args const& args = itensor::Args::global());

// itensor::Args fullUpdate_LSCG_IT(MPO_3site const& uJ1J2, Cluster & cls, CtmEnv const& ctmEnv,
// 	std::vector<std::string> tn, std::vector<int> pl,
// 	itensor::Args const& args = itensor::Args::global());


// itensor::Args fullUpdate_CG_IT(MPO_3site const& uJ1J2, Cluster & cls, CtmEnv const& ctmEnv,
// 	std::vector<std::string> tn, std::vector<int> pl,
// 	itensor::Args const& args = itensor::Args::global());

itensor::Args fullUpdate_ALS4S_LSCG_IT(OpNS const& uJ1J2, Cluster & cls, CtmEnv const& ctmEnv,
	std::vector<std::string> tn, std::vector<int> pl,
	itensor::Args const& args = itensor::Args::global());

itensor::Args fullUpdate_CG_full4S(OpNS const& uJ1J2, Cluster & cls, CtmEnv const& ctmEnv,
	std::vector<std::string> tn, std::vector<int> pl,
	itensor::Args const& args = itensor::Args::global());

struct FU4SiteGradMin {
	std::vector< itensor::ITensor > const& pc; // protocorners 
	std::array< itensor::Index, 4 > const& aux;  // aux indices
	std::vector< std::string > const& tn;      // site IDs
	std::vector< int > const& pl;              // primelevels of aux indices          
	itensor::ITensor const& op4s;              // four-site operator
	std::vector< itensor::ITensor > & rX;
	std::vector< itensor::Index > const& iQX;
	itensor::Args const& args;

	itensor::ITensor protoK;
	std::vector< itensor::ITensor > g, xi, h;

	double epsdistf;
	double normUPsi; // <psi|U^dag U|psi>
	double inst_normPsi;
	double inst_overlap;

	FU4SiteGradMin(
		std::vector< itensor::ITensor > const& ppc, // protocorners 
		std::array< itensor::Index, 4 > const& aaux,  // aux indices
		std::vector< std::string > const& ttn,      // site IDs
		std::vector< int > const& ppl,              // primelevels of aux indices          
		itensor::ITensor const& oop4s,              // four-site operator
		std::vector< itensor::ITensor > & rrX,
		std::vector< itensor::Index > const& iiQX,
		itensor::Args const& aargs);

	// Linear Model
	itensor::Real operator()(itensor::Vec<itensor::Real> const& x, 
			itensor::Vec<itensor::Real> & grad);

	// void minimize();
	
	std::vector<double> func() const;

	// double linmin(double fxi, std::vector< itensor::ITensor > const& g);

	void gradient(std::vector< itensor::ITensor > & grad);
};

#endif