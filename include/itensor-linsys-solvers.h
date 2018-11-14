#ifndef _ITENSOR_LINSYS_SOLVERS_H
#define _ITENSOR_LINSYS_SOLVERS_H

#include "itensor/all.h"

namespace itensor {

// Direct Solver
// 
// Linear system of equations
//
// The routine solves for X the system of linear equations A*X = B,
// where A is an n-by-n matrix, the columns of matrix B are individual right-hand sides,
// and the columns of X are the corresponding solutions.
//
// Resulting X is returned in B
//


struct LinSysSolver {
  	
  	virtual void 
    solve(
      	ITensor & A,
      	ITensor & B, 
      	ITensor & X,
      	Args const& args);

    virtual void 
    solve(
      	MatRefc<Real>	const& A,
      	VecRef<Real>  const& B, 
      	VecRef<Real>  const& X,
      	Args const& args) 
    {  
        // To be overloaded by derived classes
        std::cout<<"[LinSysSolver::solve<Real>] called"<<std::endl;
    }

    virtual void 
    solve(
      	MatRefc<Cplx>  const& A,
      	VecRef<Cplx>	const& B, 
     	VecRef<Cplx>	const& X, 
     	Args const& args) 
    {
        // To be overloaded by derived classes
        std::cout<<"[LinSysSolver::solve<Cplx>] called"<<std::endl;
    }
};

struct PseudoInvSolver : LinSysSolver {
	
	virtual void 
    solve(
      ITensor & A,
      ITensor & B, 
      ITensor & X,
      Args const& args) override;
};

template<class I>
void 
linsystem(ITensorT<I> A,
          ITensorT<I> B,
          ITensorT<I> & X,
          LinSysSolver & solver,
          Args const& args = Args::global());

template<class I>
void 
linsystem(ITensorT<I> A,
          ITensorT<I> B,
          ITensorT<I> & X,
          LinSysSolver & solver,
          Args const& args)
{
    //if(!args.defined("IndexName")) args.add("IndexName","ind_x");
    auto dbg = args.getBool("dbg",false);

    // Ax = b: we expect indices of A to be in in two groups, i0,i1 where
    // indices i0--A--i1--x = i0--B.

    // Find matching indices of A and B
    std::vector<I> mi; mi.reserve(rank(B));
    for(const auto& i : B.inds()) {
    	if (hasindex(A,i)) { mi.emplace_back(i); }
    	else { throw std::runtime_error("[linsystem] A and B indices do not match"); }
    }

    // Find matching indices of A and X
    if (not X) throw std::runtime_error("[linsystem] X has no indices");
    std::vector<I> oi; oi.reserve(rank(X));
    for(const auto& i : X.inds()) {
    	if (hasindex(A,i)) { oi.emplace_back(i); }
    	else { throw std::runtime_error("[linsystem] A and X indices do not match"); }
    }
    
    auto cmbX = combiner(std::move(oi), args);
    auto cmbB = combiner(std::move(mi), args);
    if (combinedIndex(cmbX).m() != combinedIndex(cmbB).m())
    	throw std::runtime_error("[linsystem] A cannot be a square matrix");
    A = (cmbB*A)*cmbX;
    B *= cmbB;
    //X *= cmbX;

    if (dbg) {
      std::cout<<"[linsystem] ";
      PrintData(A);
      std::cout<<"[linsystem] ";
      PrintData(B);
      std::cout<<"[linsystem] ";
      Print(X);
    }

    solver.solve(A,B,X,args);

    // restore original indices
    X = cmbX * X;
} //linsystem


template<typename I>
void
linsystemRank2(
		   ITensorT<I>  & A, 
           ITensorT<I>  & B,
           ITensorT<I>  & X,
           LinSysSolver & solver,
           Args const& args);


template<class MatA, class VecB, class VecX,
         class = stdx::require<
         hasMatRange<MatA>,
         hasVecRange<VecB>,
         hasVecRange<VecX>
         >>
void
linsystemRef(
	    MatA && A,
        VecB && B,
        VecX && X,
        LinSysSolver & solver,
        Args const& args);


template<class MatA, 
         class VecB,
         class VecX,
         class>
void
linsystemRef(
		MatA && A,
        VecB && B,
        VecX && X,
        LinSysSolver & solver,
        Args const& args)
{
	resize(X,nrows(A));
	linsystemMatVec(makeRef(A),makeRef(B),makeRef(X),solver,args);
}

template<typename T>
void
linsystemMatVec(
	   MatRefc<T> const& A, 
       VecRef<T> const& D, 
       VecRef<T> const& V,
       LinSysSolver & solver,
       Args const& args);

} // namespace itensor

#endif