#include "itensor-linsys-solvers.h" 

namespace itensor {

template<typename T>
void
linsystemImpl(ITensor & A, 
          	  ITensor & B,
          	  ITensor & X,
          	  LinSysSolver & solver,
          	  Args const& args)
{
    auto dbg = args.getBool("dbg",false);

    auto other  = commonIndex(A,B);
    auto active = (other == A.inds().front()) ? A.inds().back() : A.inds().front();  

    auto ib    = B.inds().front();
    auto dummy = Index("dummy",1);

    if (dbg) {
    	std::cout<<"[linsystemImpl] indices of A: "<< std::endl;
    	std::cout<<"[linsystemImpl] active: "<< active <<" other: "
    		<< other << std::endl;
    	std::cout<<"[linsystemImpl] indices of B: "<< ib << std::endl;
    }

	Vec<T> XX;
    auto RA = toMatRefc<T>(A,active,dummy);

    auto extractT = [](Dense<T> const& d) { return d.store; };
	auto storageB = applyFunc(extractT,B.store());
    auto RB = makeVecRef<T>(storageB.data(), storageB.size());
    
    // o--A--a--X = o--B
    linsystemRef(RA,RB,XX,solver,args);

    X = ITensor({dummy,active},Dense<T>{move(XX.storage())});
    X *= (B.scale().real0()/A.scale().real0());

    // absorb dummy index (TODO if necessary)
    auto combX = combiner(dummy,active);
    X *= combX;
    X *= delta(combinedIndex(combX),active);
    if (dbg) {
    	std::cout<<"[linsystemImpl] combX: "<< combX;
    	PrintData(X);
    }
}

template<typename I>
void
linsystemRank2(
           ITensorT<I>  & A, 
           ITensorT<I>  & B,
           ITensorT<I>  & X,
           LinSysSolver & solver,
           Args const& args)
{
    if(isComplex(A))
        {
        linsystemImpl<Cplx>(A,B,X,solver,args);
        return;
        }
    linsystemImpl<Real>(A,B,X,solver,args);
}
template
void
linsystemRank2(
            ITensor &,
            ITensor &,
            ITensor &,
            LinSysSolver &,
            Args const&);


void LinSysSolver::solve(
      ITensor& A,
      ITensor& B,
      ITensor& X,
      Args const& args) 
 	{  
        // To be overloaded by derived classes
        std::cout<<"[LinSysSolver::solve] called"<<std::endl;
    	linsystemRank2(A,B,X,*this,args);
    }


template<typename T>
void
linsystemMatVec(
	   MatRefc<T>  const& A,
       VecRef<T>  const& B,
       VecRef<T>  const& X,
       LinSysSolver & solver,
       Args const& args)
{
        solver.solve(A,B,X,args);
}
template void linsystemMatVec(MatRefc<Real> const&,VecRef<Real> const&, VecRef<Real> const&, 
    LinSysSolver & solver, Args const& args);
template void linsystemMatVec(MatRefc<Cplx> const&,VecRef<Cplx> const&, VecRef<Cplx> const&, 
	LinSysSolver & solver, Args const& args);


void PseudoInvSolver::solve(
      	ITensor & A,
      	ITensor & B,
      	ITensor & X,
      	Args const& args) 
 	{
    	double machine_eps = std::numeric_limits<double>::epsilon();
		auto dbg = args.getBool("dbg",false);
		auto dbgLvl = args.getInt("dbgLevel",0);
		auto svd_cutoff = args.getReal("pseudoInvCutoff",machine_eps);
		auto svd_maxLogGap = args.getReal("pseudoInvMaxLogGap",0.0);

		if(dbg && (dbgLvl >= 1)) { 
			std::cout<<"[PseudoInvSolver::solve] called"<<std::endl;
			std::cout << "svd_cutoff = " << svd_cutoff << std::endl;
		}
		PrintData(A);

		auto const i0 = A.inds()[0];
		auto const i1 = A.inds()[1];
		auto const b0 = commonIndex(A,B);

		// suppose Ax = b, with indices i0--A--i1--x = b0--B . Thus b0==i0
		ITensor U(b0), S, VT;
		svd(A, U, S, VT);



		// Invert and apply cutoff
		std::vector<double> elems_regInvS; elems_regInvS.reserve(i0.m());
		int countCTF = 0;
		auto const s1 = commonIndex(U,S);
		auto const s2 = commonIndex(S,VT);
		for (int idm=1; idm<=s1.m(); idm++) {
			if (S.real(s1(idm),s2(idm))/S.real(s1(1),s2(1)) > svd_cutoff) {
				elems_regInvS.emplace_back( 1.0/S.real(s1(idm),s2(idm)) );
			} else {
				countCTF += 1;
				elems_regInvS.emplace_back(0.0);
			}
		}
		auto regInvS = diagTensor(elems_regInvS, s1,s2);

		if(dbg && (dbgLvl >= 1)) { 
			std::cout<<"regInvDM.scale(): "<< regInvS.scale() << std::endl; 
			std::cout<<"cutoff/total: "<< countCTF <<" / "<< s1.m() << std::endl; 
		}

		X = conj(VT)*(regInvS*(conj(U)*B));
    }

} //namespace itensor