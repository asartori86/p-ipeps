#include "cluster-ev-builder.h"

using namespace itensor;

//Default constructor
EVBuilder::EVBuilder () {}

EVBuilder::EVBuilder (std::string in_name, Cluster const& in_cls, 
    CtmData const& in_cd) 
    : name(in_name), cls(in_cls), cd(in_cd) {        
}

/* 
 * TODO include (arbitrary) rotation matrix on physical index of 
 *      on-site tensor T as argument
 * 
 */
MpoNS EVBuilder::getTOT_DBG(MPO_1S mpo, std::string siteId,
        int primeLvl) const 
{
    /*
     * Construct on-site MPO given by the contraction of bra & ket 
     * on-site tensor T^dag & T through physical index s
     * 
     *      I7 I6              I3 I2
     *       | /       _       | /
     *      |T*|~~s'~~|O|~~s~~|T |          => 
     *      / |               / |
     *    I4  I5             I0 I1
     *
     *                     I7(x)I3
     *                       ||
     *  =>        I4(x)I0==|T*OT|==I6(x)I2
     *                       ||
     *                    I5(x)I1
     *
     * where indices I[4..7] correspond to bra on-site tensor and
     * indices I[0..3] to ket. (x) denotes a tensor product of indices.
     * To obtain final form of TOT we need to cast tensor product of 
     * indices, say I4(x)I0, into a single index I_XH
     * of size dim(I_XH) = dim(I*)^2 = D^2 = d
     * in accordance with CTM tensor network as defined in ctm-cluster.cc 
     * Therefore we define a conversion tensor Y as follows
     *            _
     *       I4--| \
     *           |Y --I_XH =: Y(h)
     *       I0--|_/
     *
     * with the only non-zero elements being
     *
     *   Y(I4=i, I0=j, I_XH=D*(i-1)+j) = 1.0
     *
     * then we obtain X with proper indices as
     *
     * TOT = Y(h)*Y(h')*Y(v)*Y(v')*|T*OT|
     *
     */

    ITensor const& T = cls.sites.at(siteId);
    // Get auxBond index of T
    auto auxI = noprime(findtype(T.inds(), AUXLINK));
    // Get physical index of T
    auto s = noprime(findtype(T.inds(), PHYS));

    if(auxI.m()*auxI.m() != cd.auxDimSite) {
        std::cout <<"ctmData.auxDimSite does not agree with onSiteT.dimD^2"
            << std::endl;
        exit(EXIT_FAILURE);
    }
    
    // Define combiner tensors Y*
    auto C04 = combiner(auxI, prime(auxI,4));
    auto C15 = prime(C04,1);
    auto C26 = prime(C04,2);
    auto C37 = prime(C04,3);

    // Construct MPO
    auto Op = getSpinOp(mpo, s);

    // if(isB) {
    //     std::cout << "site B - rotation on spin index" << "\n";
    //     // Operator corresponds to "odd" site of bipartite AKLT
    //     // state - perform rotation on physical indices
    //     /*
    //      * I(s)'--|Op|--I(s) => 
    //      * 
    //      * I(s)'''--|R1|--I(s)'--|Op|--I(s)--|R2|--I(s)''
    //      *
    //      * where Rot is a real symmetric rotation matrix, thus R1 = R2
    //      * defined below. Then one has to set indices of rotated
    //      * Op to proper prime level
    //      *
    //      */         
    //     auto R1 = ITensor(prime(s,3), prime(s,1));
    //     auto R2 = ITensor(s, prime(s,2));
    //     for(int i=1;i<=dimS;i++) {
    //         R1.set(prime(s,3)(i), prime(s,1)(dimS+1-i), pow(-1,i-1));
    //         R2.set(s(dimS+1-i), prime(s,2)(i), pow(-1,i-1));
    //     }
    //     PrintData(R1);
    //     PrintData(R2);
    //     Op = R1*Op*R2;
    //     PrintData(Op);
    //     Op.prime(-2);
    // }

    auto TOT = (T*Op*( conj(T).prime(AUXLINK,4).prime(PHYS,1) ))
        *C04*C15*C26*C37;

    // Define delta tensors D* to relabel combiner indices to I_XH, I_XV
    auto DH0 = delta(cd.I_XH, commonIndex(TOT,C04));
    auto DV0 = delta(cd.I_XV, commonIndex(TOT,C15));
    auto DH1 = delta(prime(cd.I_XH,1), commonIndex(TOT,C26));
    auto DV1 = delta(prime(cd.I_XV,1), commonIndex(TOT,C37));

    MpoNS result;
    result.nSite = 1;
    result.mpo.push_back(TOT*DH0*DV0*DH1*DV1);
    result.siteIds.push_back(siteId);

    return result;
}

/*
 * TODO consider imaginary part of the result as well
 *
 */
double EVBuilder::eV_1sO(MpoNS const& op, std::pair<int,int> site) const {
    if ( op.nSite != 1) {
        std::cout <<"MPO with #sites != 1 (#sites = "<< op.nSite 
            << std::endl;
        exit(EXIT_FAILURE);
    }
    if ( !(op.siteIds[0] == cls.cToS.at(std::make_pair(
        site.first % cls.sizeM, site.second % cls.sizeN)) ) ) {
        std::cout <<"WARNING: MPO constructed on site "<< op.siteIds[0]
            <<" inserted at site "<< cls.cToS.at(site) << std::endl;
    }

    auto ev = cd.C_LU;
    /*
     * Suppose sizeM >= sizeN, contract say left boundary of environment 
     *
     *   |C_LU|----I_U0
     *     |
     *   |T_L0|----I_XH(sizeN-1)*2
     *     |
     *    ...
     *     |
     * |T_Ln-1|----I_XH0
     *     |
     *   |C_LD|----I_D0 
     *
     */
    for ( int row=0; row<=cd.sizeN-1; row++ ) {
        ev.prime(HSLINK,2);
        ev *= cd.T_L[row];
    }
    ev *= cd.C_LD;

    /*
     * Contract the cluster+environment column by column
     *
     *   |C_LU|----I_U0
     *     |
     *   |T_L0|----I_XH(n-1)*2
     *     |
     *    ...              I_XV
     *     |                | 
     * |T_Ln-1|----I_XH0--| X  |--I_XH1
     *     |                |
     *     |               I_XV1<<I_XVn (via mapprime)
     *     |                |
     *   |C_LD|----I_D0---|T_D0|--I_D1 
     *
     */
    for ( int col=0; col<cd.sizeM; col++ ) {
        ev *= cd.T_D[col];
        ev.mapprime(cd.sizeN,1,VSLINK);

        for ( int row=cd.sizeN-1; row>=0; row-- ) {
            ev.mapprime(0,1,VSLINK);
            // substitute original on-site tensor for op at position site
            if ( site.first == row && site.second == col ) {
                std::cout <<"OP inserted at ("<< col <<","<< row <<")"
                    " -> "<< cls.cToS.at(std::make_pair(col % cls.sizeM, 
                        row % cls.sizeN)) << std::endl;
                ev *= prime(op.mpo[0], HSLINK, 2*(cd.sizeN-1-row));
            } else {
                ev *= prime(cd.sites.at(cd.cToS.at(std::make_pair(col,row))),
                    HSLINK, 2*(cd.sizeN-1-row));
            }
        }
        /*
         * After 1st full column insertion
         *
         *   |C_LU|----|T_U0|--I_U1
         *     |         |         
         *   |T_L0|----| X  |--I_XH(sizeN-1)*2+1
         *     |         |
         *    ...       ...
         *     |         | 
         * |T_LsizeN|--| X  |--I_XH1
         *     |         |
         *   |C_LD|----|T_D0|--I_D1 
         *
         */
        ev.prime(HSLINK,-1);
        ev *= cd.T_U[col];

    }

    /*
     * Contract with right edge from bottom to top
     *
     *   |C_LU|--...--|T_Um-1|--I_Um
     *     |            |         
     *   |T_L0|--...--| X  |--I_XH(n-1)*2+m
     *     |            |
     *    ...          ...
     *     |            |                      |
     * |T_Ln-1|--...--| X  |---I_XH0>>I_XHm--|T_Rn-1|
     *     |            |                      |
     *   |C_LD|--...--|T_Dm-1|--I_Dm---------|C_RD| 
     *
     */
    ev.prime(HSLINK,cd.sizeM);
    ev *= cd.C_RD;
    for ( int row=cd.sizeN-1; row>=0; row-- ) {
        ev.mapprime(2*(cd.sizeN-1-row)+cd.sizeM,cd.sizeM,HSLINK);
        ev *= cd.T_R[row];
    }
    ev *= cd.C_RU;

    return sumels(ev)/getNormSupercell(std::make_pair(1,1));
}

double EVBuilder::eV_1sO_DBG(MpoNS const& op, std::pair<int,int> site) const {
    if ( op.nSite != 1) {
        std::cout <<"MPO with #sites != 1 (#sites = "<< op.nSite 
            << std::endl;
        exit(EXIT_FAILURE);
    }
    if ( !(op.siteIds[0] == cls.cToS.at(std::make_pair(
        site.first % cls.sizeM, site.second % cls.sizeN)) ) ) {
        std::cout <<"WARNING: MPO constructed on site "<< op.siteIds[0]
            <<" inserted at site "<< cls.cToS.at(site) << std::endl;
    }

    auto ev = cd.C_LU;
    /*
     * Suppose sizeM >= sizeN, contract say left boundary of environment 
     *
     *   |C_LU|----I_U0
     *     |
     *   |T_L0|----I_XH(sizeN-1)*2
     *     |
     *    ...
     *     |
     * |T_Ln-1|----I_XH0
     *     |
     *   |C_LD|----I_D0 
     *
     */
    for ( int row=0; row<=cd.sizeN-1; row++ ) {
        ev.prime(HSLINK,2);
        ev *= cd.T_L[row];
    }
    ev *= cd.C_LD;

    /*
     * Contract the cluster+environment column by column
     *
     *   |C_LU|----I_U0
     *     |
     *   |T_L0|----I_XH(n-1)*2
     *     |
     *    ...              I_XV
     *     |                | 
     * |T_Ln-1|----I_XH0--| X  |--I_XH1
     *     |                |
     *     |               I_XV1<<I_XVn (via mapprime)
     *     |                |
     *   |C_LD|----I_D0---|T_D0|--I_D1 
     *
     */
    for ( int col=0; col<cd.sizeM; col++ ) {
        ev *= cd.T_D[col];
        ev.mapprime(cd.sizeN,1,VSLINK);

        for ( int row=cd.sizeN-1; row>=0; row-- ) {
            ev.mapprime(0,1,VSLINK);
            // substitute original on-site tensor for op at position site
            if ( site.first == row && site.second == col ) {
                std::cout <<"OP inserted at ("<< col <<","<< row <<")"
                    " -> "<< cls.cToS.at(std::make_pair(col % cls.sizeM, 
                        row % cls.sizeN)) << std::endl;
                ev *= prime(op.mpo[0], HSLINK, 2*(cd.sizeN-1-row));
            } else {
                ev *= prime(cd.sites.at(cd.cToS.at(std::make_pair(col,row))),
                    HSLINK, 2*(cd.sizeN-1-row));
            }
        }
        /*
         * After 1st full column insertion
         *
         *   |C_LU|----|T_U0|--I_U1
         *     |         |         
         *   |T_L0|----| X  |--I_XH(sizeN-1)*2+1
         *     |         |
         *    ...       ...
         *     |         | 
         * |T_LsizeN|--| X  |--I_XH1
         *     |         |
         *   |C_LD|----|T_D0|--I_D1 
         *
         */
        ev.prime(HSLINK,-1);
        ev *= cd.T_U[col];

    }

    /*
     * Contract with right edge from bottom to top
     *
     *   |C_LU|--...--|T_Um-1|--I_Um
     *     |            |         
     *   |T_L0|--...--| X  |--I_XH(n-1)*2+m
     *     |            |
     *    ...          ...
     *     |            |                      |
     * |T_Ln-1|--...--| X  |---I_XH0>>I_XHm--|T_Rn-1|
     *     |            |                      |
     *   |C_LD|--...--|T_Dm-1|--I_Dm---------|C_RD| 
     *
     */
    ev.prime(HSLINK,cd.sizeM);
    ev *= cd.C_RD;
    for ( int row=cd.sizeN-1; row>=0; row-- ) {
        ev.mapprime(2*(cd.sizeN-1-row)+cd.sizeM,cd.sizeM,HSLINK);
        ev *= cd.T_R[row];
    }
    ev *= cd.C_RU;

    return sumels(ev)/getNormSupercell(std::make_pair(1,1));
}

/* 
 * TODO? include (arbitrary) rotation matrix on physical index of 
 *      on-site tensor TA, TB as argument
 * 
 */
std::pair< ITensor,ITensor > EVBuilder::get2STOT_DBG(OP_2S op2s,
    ITensor const& TA, ITensor const& TB) const 
{
    std::cout <<"===== EVBuilder::get2STOT_DBG called ====="
        << std::string(30,'=') << std::endl;
    std::cout <<">>>>> 1) initial |ket> on-site tensors <<<<<"<< std::endl;
    PrintData(TA);
    PrintData(TB);
    
    /*
     * 2-site operator acts on 2 physical indices
     * 
     *           A      B
     * <bra|     s'     s'''
     *          _|______|_   
     *         |____OP____|
     *           |      |
     *           s      s''  |ket>     
     *
     */
    auto s0 = findtype(TA.inds(), PHYS);
    auto s1 = prime(s0,1);
    auto s2 = prime(findtype(TB.inds(), PHYS), 2);
    auto s3 = prime(s2,1);
    // Assume s0 is different then s2S

    auto Op = ITensor(s0, s1, s2, s3);
    
    // check dimensions of phys indices on TA and TB
    if( s0.m() != s2.m() ) {
        std::cout <<"On-site tensors TA and TB have different dimension of"
            <<" phys index"<< std::endl;
        exit(EXIT_FAILURE);
    }
    int dimS = s0.m();

    switch(op2s) {
        case OP2S_Id: { // Identity operator
            std::cout <<">>>>> 2) Constructing OP2S_Id <<<<<"<< std::endl;  
            for(int i=1;i<=dimS;i++) {
                for(int j=1;j<=dimS;j++){
                    Op.set(s0(i),s2(j),s1(i),s3(j), 1.+0._i);
                }
            }
            break;
        }
        case OP2S_AKLT_S2_H: { // H of AKLT-S2 on square lattice
            std::cout <<">>>>> 2) Constructing OP2S_AKLT-S2-H <<<<<"
                << std::endl;
            // Loop over <bra| indices
            int rS = dimS-1; // Label of SU(2) irrep in Dyknin notation
            int mbA, mbB, mkA, mkB;
            double hVal;
            for(int bA=1;bA<=dimS;bA++) {
            for(int bB=1;bB<=dimS;bB++) {
                // Loop over |ket> indices
                for(int kA=1;kA<=dimS;kA++) {
                for(int kB=1;kB<=dimS;kB++) {
                    // Use Dynkin notation to specify irreps
                    mbA = -(rS) + 2*(bA-1);
                    mbB = -(rS) + 2*(bB-1);
                    mkA = -(rS) + 2*(kA-1);
                    mkB = -(rS) + 2*(kB-1);
                    // Loop over possible values of m given by tensor product
                    // of 2 spin (dimS-1) irreps (In Dynkin notation)
                    hVal = 0.0;
                    for(int m=-2*(rS);m<=2*(rS);m=m+2) {
                        if ((mbA+mbB == m) && (mkA+mkB == m)) {
                            
                            //DEBUG
                            std::cout <<"<"<< mbA <<","<< mbB <<"|"<< m 
                                <<"> x <"<< m <<"|"<< mkA <<","<< mkB 
                                <<"> = "<< SU2_getCG(rS, rS, 2*rS, mbA, mbB, m)
                                <<" x "<< SU2_getCG(rS, rS, 2*rS, mkA, mkB, m)
                                << std::endl;

                        hVal += SU2_getCG(rS, rS, 2*rS, mbA, mbB, m) 
                            *SU2_getCG(rS, rS, 2*rS, mkA, mkB, m);
                        }
                    }
                    if((bA == kA) && (bB == kB)) {
                        // add 2*Id(bA,kA;bB,kB) == 
                        //    sqrt(2)*Id(bA,kA)(x)sqrt(2)*Id(bB,kB)
                        Op.set(s0(kA),s2(kB),s1(bA),s3(bB),hVal+sqrt(2.0));
                    } else {
                        Op.set(s0(kA),s2(kB),s1(bA),s3(bB),hVal);
                    }
                }}
            }}
            break;
        }
        case OP2S_SS: { 
            // S^vec_i * S^vec_i+1 =
            // = s^z_i*s^z_i+1 + 1/2(s^+_i*s^-_i+1 + s^-_i*s^+_i+1)
            std::cout <<">>>>> 2) Constructing OP2S_SS <<<<<"<< std::endl;
    
            Index sBra = Index("sBra", dimS);
            Index sKet = prime(sBra);
            ITensor Sz = getSpinOp(MPO_S_Z, sBra);
            ITensor Sp = getSpinOp(MPO_S_P, sBra);
            ITensor Sm = getSpinOp(MPO_S_M, sBra);
            
            double hVal;
            // Loop over <bra| indices
            for(int bA=1;bA<=dimS;bA++) {
            for(int bB=1;bB<=dimS;bB++) {
                // Loop over |ket> indices
                for(int kA=1;kA<=dimS;kA++) {
                for(int kB=1;kB<=dimS;kB++) {
                
                    hVal = Sz.real(sBra(bA),sKet(kA))
                        *Sz.real(sBra(bB),sKet(kB))+0.5*(
                        Sp.real(sBra(bA),sKet(kA))
                        *Sm.real(sBra(bB),sKet(kB))+
                        Sm.real(sBra(bA),sKet(kA))
                        *Sp.real(sBra(bB),sKet(kB)));

                    Op.set(s0(kA),s2(kB),s1(bA),s3(bB),hVal);
                }}
            }}
            break;
        }
        default: {
            std::cout <<"Invalid OP_2S selection"<< std::endl;
            exit(EXIT_FAILURE);
            break;
        }
    }

    // Perform SVD
    /*         __
     * I(s)---|  |--I(s)''    =>
     *        |OP|            =>
     * I(s)'--|__|--I(s)'''   =>
     *            ___                      ___  
     * => I(s)---|   |         _          |   |--I(s)''
     * =>        |OpA|--I(o)--|S|--I(o)'--|OpB|       
     * => I(s)'--|___|                    |___|--I(s)'''
     *
     */
    auto OpA = ITensor(s0, s1);
    ITensor OpB, S; 

    std::cout <<">>>>> 3) Performing SVD OP2S -> OpA * S * OpB <<<<<"
        << std::endl;
    svd(Op, OpA, S, OpB);
    
    Print(OpA);
    PrintData(S);
    Print(OpB);
    
    //create a lambda function
    //which returns the square of its argument
    auto sqrt_T = [](Real r) { return sqrt(r); };
    S.apply(sqrt_T);

    // Absorb singular values (symmetrically) into OpA, OpB
    OpA = ( OpA*S )*delta(commonIndex(S,OpB), commonIndex(OpA,S));
    OpB = S*OpB.prime(PHYS,-2);
    
    std::cout <<">>>>> 4) Absorbing sqrt(S) to both OpA and OpB <<<<<"
        << std::endl;
    PrintData(OpA);
    PrintData(OpB);

    // Contract OpA and OpB with <bra| & |ket> on-site tensors TA and TB 
    /*
     * TODO write comment about this process
     *
     */
    auto auxIA = noprime(findtype(TA.inds(), AUXLINK));
    auto auxIB = noprime(findtype(TB.inds(), AUXLINK));

    // Define combiner tensors Y*
    auto C04A = combiner(auxIA, prime(auxIA,4));
    auto C15A = prime(C04A,1);
    auto C26A = prime(C04A,2);
    auto C37A = prime(C04A,3);
    auto C04B = combiner(auxIB, prime(auxIB,4));
    auto C15B = prime(C04B,1);
    auto C26B = prime(C04B,2);
    auto C37B = prime(C04B,3);

    OpA = (TA*OpA*( conj(TA).prime(AUXLINK,4).prime(PHYS,1) ))
        *C04A*C15A*C26A*C37A;
    OpB = (TB*OpB*( conj(TB).prime(AUXLINK,4).prime(PHYS,1) ))
        *C04B*C15B*C26B*C37B;

    // Define delta tensors D* to relabel combiner indices to I_XH, I_XV
    auto DH0A = delta(cd.I_XH, commonIndex(OpA,C04A));
    auto DV0A = delta(cd.I_XV, commonIndex(OpA,C15A));
    auto DH1A = delta(prime(cd.I_XH,1), commonIndex(OpA,C26A));
    auto DV1A = delta(prime(cd.I_XV,1), commonIndex(OpA,C37A));
    auto DH0B = delta(cd.I_XH, commonIndex(OpB,C04B));
    auto DV0B = delta(cd.I_XV, commonIndex(OpB,C15B));
    auto DH1B = delta(prime(cd.I_XH,1), commonIndex(OpB,C26B));
    auto DV1B = delta(prime(cd.I_XV,1), commonIndex(OpB,C37B));

    OpA = OpA*DH0A*DV0A*DH1A*DV1A;
    OpB = OpB*DH0B*DV0B*DH1B*DV1B;

    std::cout <<">>>>> 5) contracting OpA,OpB with TA,TB on site tensors <<<<<"
        << std::endl;
    Print(OpA);
    Print(OpB);

    std::cout <<"===== EVBuilder::get2STOT_DBG done ====="
        << std::string(32,'=') << std::endl;
    return std::make_pair(OpA, OpB);
}

std::pair< ITensor,ITensor > EVBuilder::get2STOT(OP_2S op2s,
    ITensor const& TA, ITensor const& TB) const 
{
    std::cout <<"===== EVBuilder::get2STOT_DBG called ====="
        << std::string(30,'=') << std::endl;
    /*
     * 2-site operator acts on 2 physical indices
     * 
     *           A      B
     * <bra|     s'     s'''
     *          _|______|_   
     *         |____OP____|
     *           |      |
     *           s      s''  |ket>     
     *
     */
    auto s0 = findtype(TA.inds(), PHYS);
    auto s1 = prime(s0,1);
    auto s2 = prime(findtype(TB.inds(), PHYS), 2);
    auto s3 = prime(s2,1);
    // Assume s0 is different then s2

    auto Op = ITensor(s0, s1, s2, s3);
    
    // check dimensions of phys indices on TA and TB
    if( s0.m() != s2.m() ) {
        std::cout <<"On-site tensors TA and TB have different dimension of"
            <<" phys index"<< std::endl;
        exit(EXIT_FAILURE);
    }
    int dimS = s0.m();

    switch(op2s) {
        case OP2S_Id: { // Identity operator
            std::cout <<">>>>> Constructing OP2S_Id <<<<<"<< std::endl; 
            for(int i=1;i<=dimS;i++) {
                for(int j=1;j<=dimS;j++){
                    Op.set(s0(i),s2(j),s1(i),s3(j), 1.+0._i);
                }
            }
            break;
        }
        case OP2S_AKLT_S2_H: { // H of AKLT-S2 on square lattice
            std::cout <<">>>>> Constructing OP2S_AKLT-S2-H <<<<<"<< std::endl;
            // Loop over <bra| indices
            int rS = dimS-1; // Label of SU(2) irrep in Dyknin notation
            int mbA, mbB, mkA, mkB;
            double hVal;
            for(int bA=1;bA<=dimS;bA++) {
            for(int bB=1;bB<=dimS;bB++) {
                // Loop over |ket> indices
                for(int kA=1;kA<=dimS;kA++) {
                for(int kB=1;kB<=dimS;kB++) {
                    // Use Dynkin notation to specify irreps
                    mbA = -(rS) + 2*(bA-1);
                    mbB = -(rS) + 2*(bB-1);
                    mkA = -(rS) + 2*(kA-1);
                    mkB = -(rS) + 2*(kB-1);
                    // Loop over possible values of m given by tensor product
                    // of 2 spin (dimS-1) irreps (In Dynkin notation)
                    hVal = 0.0;
                    for(int m=-2*(rS);m<=2*(rS);m=m+2) {
                        if ((mbA+mbB == m) && (mkA+mkB == m)) {

                        hVal += SU2_getCG(rS, rS, 2*rS, mbA, mbB, m) 
                            *SU2_getCG(rS, rS, 2*rS, mkA, mkB, m);
                        }
                    }
                    if((bA == kA) && (bB == kB)) {
                        // add 2*Id(bA,kA;bB,kB) == 
                        //    sqrt(2)*Id(bA,kA)(x)sqrt(2)*Id(bB,kB)
                        Op.set(s0(kA),s2(kB),s1(bA),s3(bB),hVal+sqrt(2.0));
                    } else {
                        Op.set(s0(kA),s2(kB),s1(bA),s3(bB),hVal);
                    }
                }}
            }}
            break;
        }
        case OP2S_SS: {
            std::cout <<">>>>> Constructing OP2S_SS <<<<<"<< std::endl;
            // S^vec_i * S^vec_i+1 =
            // = s^z_i*s^z_i+1 + 1/2(s^+_i*s^-_i+1 + s^-_i*s^+_i+1)
    
            Index sBra = Index("sBra", dimS);
            Index sKet = prime(sBra);
            ITensor Sz = getSpinOp(MPO_S_Z, sBra);
            ITensor Sp = getSpinOp(MPO_S_P, sBra);
            ITensor Sm = getSpinOp(MPO_S_M, sBra);
            
            double hVal;
            // Loop over <bra| indices
            for(int bA=1;bA<=dimS;bA++) {
            for(int bB=1;bB<=dimS;bB++) {
                // Loop over |ket> indices
                for(int kA=1;kA<=dimS;kA++) {
                for(int kB=1;kB<=dimS;kB++) {
                
                    hVal = Sz.real(sBra(bA),sKet(kA))
                        *Sz.real(sBra(bB),sKet(kB))+0.5*(
                        Sp.real(sBra(bA),sKet(kA))
                        *Sm.real(sBra(bB),sKet(kB))+
                        Sm.real(sBra(bA),sKet(kA))
                        *Sp.real(sBra(bB),sKet(kB)));

                    Op.set(s0(kA),s2(kB),s1(bA),s3(bB),hVal);
                }}
            }}
            break;
        }
        default: {
            std::cout <<"Invalid OP_2S selection"<< std::endl;
            exit(EXIT_FAILURE);
            break;
        }
    }

    // Perform SVD
    /*         __
     * I(s)---|  |--I(s)''    =>
     *        |OP|            =>
     * I(s)'--|__|--I(s)'''   =>
     *            ___                      ___  
     * => I(s)---|   |         _          |   |--I(s)''
     * =>        |OpA|--I(o)--|S|--I(o)'--|OpB|       
     * => I(s)'--|___|                    |___|--I(s)'''
     *
     */
    auto OpA = ITensor(s0, s1);
    ITensor OpB, S; 

    svd(Op, OpA, S, OpB);
    
    //create a lambda function
    //which returns the square of its argument
    auto sqrt_T = [](Real r) { return sqrt(r); };
    S.apply(sqrt_T);

    // Absorb singular values (symmetrically) into OpA, OpB
    OpA = ( OpA*S )*delta(commonIndex(S,OpB), commonIndex(OpA,S));
    OpB = S*OpB.prime(PHYS,-2);

    auto auxIA = noprime(findtype(TA.inds(), AUXLINK));
    auto auxIB = noprime(findtype(TB.inds(), AUXLINK));

    // Define combiner tensors Y*
    auto C04A = combiner(auxIA, prime(auxIA,4));
    auto C15A = prime(C04A,1);
    auto C26A = prime(C04A,2);
    auto C37A = prime(C04A,3);
    auto C04B = combiner(auxIB, prime(auxIB,4));
    auto C15B = prime(C04B,1);
    auto C26B = prime(C04B,2);
    auto C37B = prime(C04B,3);

    OpA = (TA*OpA*( conj(TA).prime(AUXLINK,4).prime(PHYS,1) ))
        *C04A*C15A*C26A*C37A;
    OpB = (TB*OpB*( conj(TB).prime(AUXLINK,4).prime(PHYS,1) ))
        *C04B*C15B*C26B*C37B;

    // Define delta tensors D* to relabel combiner indices to I_XH, I_XV
    auto DH0A = delta(cd.I_XH, commonIndex(OpA,C04A));
    auto DV0A = delta(cd.I_XV, commonIndex(OpA,C15A));
    auto DH1A = delta(prime(cd.I_XH,1), commonIndex(OpA,C26A));
    auto DV1A = delta(prime(cd.I_XV,1), commonIndex(OpA,C37A));
    auto DH0B = delta(cd.I_XH, commonIndex(OpB,C04B));
    auto DV0B = delta(cd.I_XV, commonIndex(OpB,C15B));
    auto DH1B = delta(prime(cd.I_XH,1), commonIndex(OpB,C26B));
    auto DV1B = delta(prime(cd.I_XV,1), commonIndex(OpB,C37B));

    OpA = OpA*DH0A*DV0A*DH1A*DV1A;
    OpB = OpB*DH0B*DV0B*DH1B*DV1B;

    std::cout <<"===== EVBuilder::get2STOT_DBG done ====="
        << std::string(32,'=') << std::endl;
    return std::make_pair(OpA, OpB);
}

/* 
 * TODO implement evaluation on arbitrary large cluster
 * 
 */
double EVBuilder::eV_2sO_DBG(std::pair< ITensor,ITensor > const& Op,
    std::pair<int,int> siteA, std::pair<int,int> siteB) const
{
    std::cout <<"===== EVBuilder::eV_2sO_DBG called ====="
        << std::string(32,'=') << std::endl;
    
    // determine number of cells=clusters in row/col direction required
    // to form the supercell
    int nR = std::ceil(std::max(1+siteA.first,1+siteB.first)/
        ((float)cd.sizeN));
    int nC = std::ceil(std::max(1+siteA.second,1+siteB.second)/
        ((float)cd.sizeM));

    std::cout <<"Required supercell: "<< nR <<"x"<< nC << std::endl;

    /*
     * Get the size of "super"cell assuming span along "col" direction
     * is bigger then along "row" direction
     *
     *   <-- col --> 
     *  |   |_|_|_|..|_|
     * row  |_|_|_|..|_|             _
     *  |   |_|_|_|..|_| where each |_| cell is a copy of cluster
     *
     */
    if ( nC < nR ) {
        std::cout <<"Number of columns < number of rows in supercell"
            << std::endl;
        exit(EXIT_FAILURE);
    }

    // Contract TN with Op inserted
    auto tN = cd.C_LU;

    for ( int row=0; row<nR*cd.sizeN; row++ ) {
        tN.prime(HSLINK,2);
        if ( (row > 0) && (row % cd.sizeN == 0) ) {
            tN.prime(LLINK, -cd.sizeN);
        }
        tN *= cd.T_L[row % cd.sizeN];
    }
    tN *= cd.C_LD;
    std::cout <<">>>>> 1) Left edge constructed <<<<<"<< std::endl;
    Print(tN);

    for ( int col=0; col<nC*cd.sizeM; col++ ) {
        if ( (col > 0) && (col % cd.sizeM == 0) ) {
            tN.prime(DLINK, -cd.sizeM);
            tN.prime(ULINK, -cd.sizeM);
        }
        tN *= cd.T_D[col % cd.sizeM];
        tN.mapprime(cd.sizeN,1,VSLINK);

        for ( int row=nR*cd.sizeN-1; row>=0; row-- ) {
            tN.mapprime(0,1,VSLINK);
            if ( siteA.first == col && siteA.second == row ) {
                std::cout <<"OpA inserted at ("<< col <<","<< row <<")"
                    " -> "<< cls.cToS.at(std::make_pair(col % cls.sizeM,
                     row % cls.sizeN)) << std::endl;
                tN *= prime(Op.first, HSLINK, 2*(nR*cd.sizeN-1-row));
            } else if ( siteB.first == col && siteB.second == row ) {
                std::cout <<"OpB inserted at ("<< col <<","<< row <<")"
                    " -> "<< cls.cToS.at(std::make_pair(col % cls.sizeM,
                     row % cls.sizeN)) << std::endl;
                tN *= prime(Op.second, HSLINK, 2*(nR*cd.sizeN-1-row));
            } else {
                tN *= prime( cd.sites.at(cd.cToS.at(
                    std::make_pair(col % cd.sizeN, row % cd.sizeM))),
                    HSLINK, 2*(nR*cd.sizeN-1-row) );
            }
        }
        tN.prime(HSLINK,-1);
        tN *= cd.T_U[col % cd.sizeM];

        std::cout << ">>>>> Appended col: "<< col <<" col mod sizeM: "
            << col % cd.sizeM <<" <<<<<"<< std::endl;
        Print(tN);
    }
    std::cout <<">>>>> 2) "<< nC*cd.sizeM 
        << " cols appended <<<<<"<< std::endl;
    Print(tN);

    tN.prime(HSLINK,cd.sizeM);
    tN *= cd.C_RD;
    for ( int row=nR*cd.sizeN-1; row>=0; row-- ) {
        tN.mapprime(2*(nR*cd.sizeN-1-row)+cd.sizeM,cd.sizeM,HSLINK);
        std::cout <<"HSLINK "<< 2*(nR*cd.sizeN-1-row)+cd.sizeM <<" -> "<<  
            cd.sizeM << std::endl;
        
        tN *= cd.T_R[row % cd.sizeN];

        if ( (row > 0) && (row % cd.sizeN == 0) ) {
            tN.prime(RLINK, cd.sizeN);
            std::cout <<"RLINK RESET +"<< cd.sizeN <<" prime"<< std::endl;
        }
    }
    tN *= cd.C_RU;
    std::cout <<">>>>> 3) contraction with right edge <<<<<"<< std::endl;
    Print(tN);

    std::cout <<"===== EVBuilder::eV_2sO_DBG done ====="
        << std::string(34,'=') << std::endl;
    return sumels(tN)/getNormSupercell_DBG(std::make_pair(nR,nC));
}

double EVBuilder::eV_2sO(std::pair< ITensor,ITensor > const& Op,
    std::pair<int,int> siteA, std::pair<int,int> siteB) const
{
    std::cout <<"===== EVBuilder::eV_2sO called ====="<< std::string(36,'=')
        << std::endl;
    
    // determine number of cells=clusters in row/col direction required
    // to form the supercell
    int nR = std::ceil(std::max(1+siteA.first,1+siteB.first)/
        ((float)cd.sizeN));
    int nC = std::ceil(std::max(1+siteA.second,1+siteB.second)/
        ((float)cd.sizeM));

    std::cout <<"Required supercell: "<< nR <<"x"<< nC << std::endl;

    /*
     * Get the size of "super"cell assuming span along "col" direction
     * is bigger then along "row" direction
     *
     *   <-- col --> 
     *  |   |_|_|_|..|_|
     * row  |_|_|_|..|_|             _
     *  |   |_|_|_|..|_| where each |_| cell is a copy of cluster
     *
     */
    if ( nC < nR ) {
        std::cout <<"Number of columns < number of rows in supercell"
            << std::endl;
        exit(EXIT_FAILURE);
    }

    // Contract TN with Op inserted
    auto tN = cd.C_LU;

    for ( int row=0; row<nR*cd.sizeN; row++ ) {
        tN.prime(HSLINK,2);
        if ( (row > 0) && (row % cd.sizeN == 0) ) {
            tN.prime(LLINK, -cd.sizeN);
        }
        tN *= cd.T_L[row % cd.sizeN];
    }
    tN *= cd.C_LD;

    for ( int col=0; col<nC*cd.sizeM; col++ ) {
        if ( (col > 0) && (col % cd.sizeM == 0) ) {
            tN.prime(DLINK, -cd.sizeM);
            tN.prime(ULINK, -cd.sizeM);
        }
        tN *= cd.T_D[col % cd.sizeM];
        tN.mapprime(cd.sizeN,1,VSLINK);

        for ( int row=nR*cd.sizeN-1; row>=0; row-- ) {
            tN.mapprime(0,1,VSLINK);
            if ( siteA.first == row && siteA.second == col ) {
                std::cout <<"OpA inserted at ("<< row <<","<< col <<")"
                    " -> "<< cls.cToS.at(std::make_pair(row % cls.sizeN,
                     col % cls.sizeM)) << std::endl;
                tN *= prime(Op.first, HSLINK, 2*(nR*cd.sizeN-1-row));
            } else if ( siteB.first == row && siteB.second == col ) {
                std::cout <<"OpB inserted at ("<< row <<","<< col <<")"
                    " -> "<< cls.cToS.at(std::make_pair(row % cls.sizeN,
                     col % cls.sizeM)) << std::endl;
                tN *= prime(Op.second, HSLINK, 2*(nR*cd.sizeN-1-row));
            } else {
                tN *= prime( cd.sites.at(cd.cToS.at(
                    std::make_pair(row % cd.sizeN,col % cd.sizeM))),
                    HSLINK, 2*(nR*cd.sizeN-1-row) );
            }
        }
        tN.prime(HSLINK,-1);
        tN *= cd.T_U[col % cd.sizeM];
    }

    tN.prime(HSLINK,cd.sizeM);
    tN *= cd.C_RD;
    for ( int row=nR*cd.sizeN-1; row>=0; row-- ) {
        tN.mapprime(2*(nR*cd.sizeN-1-row)+cd.sizeM,cd.sizeM,HSLINK);
        tN *= cd.T_R[row % cd.sizeN];
        if ( (row > 0) && (row % cd.sizeN == 0) ) {
            tN.prime(RLINK, cd.sizeN);
        }
    }
    tN *= cd.C_RU;

    std::cout <<"===== EVBuilder::eV_2sO done ====="<< std::string(38,'=')
        << std::endl;
    return sumels(tN)/getNormSupercell(std::make_pair(nR,nC));
}

/* 
 * TODO implement for NxM supercell with N(rows)>M(cols)
 * 
 */
double EVBuilder::getNormSupercell_DBG(std::pair<int,int> sc) const {
    //  N x M == sc.first x sc.second supercell
    std::cout <<"===== EVBuilder::getNormSupercell_DBG called ====="
        << std::string(22,'=') << std::endl;
    std::cout << "required supercell ("<< sc.first <<","<< sc.second <<")"
        << std::endl;
    
    // check N =< M condition
    if ( sc.first > sc.second ) {
        std::cout <<"Number of columns < number of rows in supercell"
            << std::endl;
        exit(EXIT_FAILURE);
    }

    auto tN = cd.C_LU;

    // Construct left edge
    for ( int row=0; row<sc.first*cd.sizeN; row++ ) {
        tN.prime(HSLINK,2);
        // Reset index primeLevel when entering new cell=(copy of)cluster
        if ( (row > 0) && (row % cd.sizeN == 0) ) {
            tN.prime(LLINK, -cd.sizeN);
        }
        tN *= cd.T_L[row % cd.sizeN];
    }
    tN *= cd.C_LD;

    std::cout <<">>>>> 1) Left edge constructed <<<<<"<< std::endl;
    Print(tN);

    for ( int col=0; col<sc.second*cd.sizeM; col++ ) {
        if ( (col > 0) && (col % cd.sizeM == 0) ) {
            tN.prime(DLINK, -cd.sizeM);
            tN.prime(ULINK, -cd.sizeM);
        }
        tN *= cd.T_D[col % cd.sizeM];
        tN.mapprime(cd.sizeN,1,VSLINK);
        std::cout<<"T_D["<< col % cd.sizeM <<"] inserted"<<std::endl;
        Print(tN);

        for ( int row=sc.first*cd.sizeN-1; row>=0; row-- ) {
            tN.mapprime(0,1,VSLINK);
            tN *= prime( cd.sites.at(cd.cToS.at(
                std::make_pair(col % cd.sizeM,row % cd.sizeN))),
                HSLINK, 2*(sc.first*cd.sizeN-1-row) );
        }

        tN.prime(HSLINK,-1);
        tN *= cd.T_U[col % cd.sizeM];

        std::cout << ">>>>> Appended col: "<< col <<" col mod sizeM: "
            << col % cd.sizeM <<" <<<<<"<< std::endl;
        Print(tN);
    }

    std::cout <<">>>>> 2) "<< sc.second*cd.sizeM 
        << " cols appended <<<<<"<< std::endl;
    Print(tN);

    tN.prime(HSLINK,cd.sizeM);
    tN *= cd.C_RD;
    Print(tN);
    for ( int row=sc.first*cd.sizeN-1; row>=0; row-- ) {
        tN.mapprime(2*(sc.first*cd.sizeN-1-row)+cd.sizeM,cd.sizeM,HSLINK);
        std::cout <<"HSLINK "<< 2*(sc.first*cd.sizeN-1-row)+cd.sizeM <<" -> "<<  
            cd.sizeM << std::endl;

        tN *= cd.T_R[row % cd.sizeN];
        Print(tN);

        if ( (row > 0) && (row % cd.sizeN == 0) ) {
            tN.prime(RLINK, cd.sizeN);
        }
    }
    tN *= cd.C_RU;

    std::cout <<">>>>> 3) contraction with right edge <<<<<"<< std::endl;
    Print(tN);

    std::cout <<"===== EVBuilder::getNormSupercell_DBG done ====="
        << std::string(24,'=') << std::endl;
    return sumels(tN);
}

double EVBuilder::getNormSupercell(std::pair<int,int> sc) const {
    // check N =< M condition
    std::cout <<"===== EVBuilder::getNormSupercell called ====="
        << std::string(26,'=') << std::endl;
    std::cout << "required supercell ("<< sc.first <<","<< sc.second <<")"
        << std::endl;

    if ( sc.first > sc.second ) {
        std::cout <<"Number of columns < number of rows in supercell"
            << std::endl;
        exit(EXIT_FAILURE);
    }

    auto tN = cd.C_LU;

    for ( int row=0; row<sc.first*cd.sizeN; row++ ) {
        tN.prime(HSLINK,2);
        // Reset index primeLevel when entering new cell=(copy of)cluster
        if ( (row > 0) && (row % cd.sizeN == 0) ) {
            tN.prime(LLINK, -cd.sizeN);
        }
        tN *= cd.T_L[row % cd.sizeN];
    }
    tN *= cd.C_LD;

    for ( int col=0; col<sc.second*cd.sizeM; col++ ) {
        if ( (col > 0) && (col % cd.sizeM == 0) ) {
            tN.prime(DLINK, -cd.sizeM);
            tN.prime(ULINK, -cd.sizeM);
        }
        tN *= cd.T_D[col % cd.sizeM];
        tN.mapprime(cd.sizeN,1,VSLINK);

        for ( int row=sc.first*cd.sizeN-1; row>=0; row-- ) {
            tN.mapprime(0,1,VSLINK);
            tN *= prime( cd.sites.at(cd.cToS.at(
                std::make_pair(col % cd.sizeM,row % cd.sizeN))),
                HSLINK, 2*(sc.first*cd.sizeN-1-row) );
        }
        tN.prime(HSLINK,-1);
        tN *= cd.T_U[col % cd.sizeM];
    }

    tN.prime(HSLINK,cd.sizeM);
    tN *= cd.C_RD;
    for ( int row=sc.first*cd.sizeN-1; row>=0; row-- ) {
        tN.mapprime(2*(sc.first*cd.sizeN-1-row)+cd.sizeM,cd.sizeM,HSLINK);

        tN *= cd.T_R[row % cd.sizeN];

        if ( (row > 0) && (row % cd.sizeN == 0) ) {
            tN.prime(RLINK, cd.sizeN);
        }
    }
    tN *= cd.C_RU;

    std::cout <<"===== EVBuilder::getNormSupercell done ====="
        << std::string(28,'=') << std::endl;
    return sumels(tN);
}

void EVBuilder::setCtmData(CtmData const& new_cd) {
    cd = new_cd;
}


// std::complex<double> ExpValBuilder::expVal_1sO1sO_H(int dist, 
//         itensor::ITensor const& op1, itensor::ITensor const& op2)
// {
    
//     auto X = ExpValBuilder::getTOT(MPO_Id, 0, env.i_Xh, env.i_Xv,
//             false);
//     /*
//      * Construct the "left" part tensor L
//      *  _    __                __
//      * |C|--|T |--I(T_u)'     |  |--I(T_u)'
//      *  |    |            ==> |  |
//      * |T|--|O1|--I(XH)'  ==> |L |--I(Xh)'
//      *  |    |            ==> |  |
//      * |C|--|T |--I(T_d)'     |__|--I(T_d)'
//      *
//      */
//     auto L   = env.C_lu*env.T_l*env.C_ld;
//     L = L*env.T_u*op1*env.T_d;
//     auto LId = env.C_lu*env.T_l*env.C_ld;
//     LId = LId*env.T_u*X*env.T_d;
//     //DEBUG Print(L);

//     /*
//      * Contract L with "dist" copies of a column
//      *
//      * I(T_u)--|T|--I(T_u)'
//      *          |
//      *  I(Xh)--|X|--I(Xh)'
//      *          |
//      * I(T_d)--|T|--I(T_d)'
//      *
//      */ 
//     //DEBUG std::cout << "Inserting "<< dist <<" T_u--X--T_d column" 
//     //DEBUG         << "\n";
//     for(int i=0;i<dist;i++) {
//         L.noprime();
//         LId.noprime();
//         L = L*env.T_u*X*env.T_d;
//         LId = LId*env.T_u*X*env.T_d;
//     }

//     /*
//      * Construct the "right" part tensor R
//      *          __    _                 __
//      * I(T_u)--|T |--|C|       I(T_u)--|  |
//      *          |     |   ==>          |  |
//      *  I(Xh)--|O2|--|T|  ==>   I(Xh)--|R |
//      *          |     |   ==>          |  |
//      * I(T_d)--|T |--|C|       I(T_d)--|__|
//      *
//      */
//     auto R = env.C_ru*env.T_r*env.C_rd;
//     R = R*env.T_u*op2*env.T_d;
//     auto RId = env.C_ru*env.T_r*env.C_rd;
//     RId = RId*env.T_u*X*env.T_d;
//     //Print(R);

//     L.noprime();
//     LId.noprime();
//     // Contract (L*col^dist)*R
//     auto ccBare = L*R;
//     auto ccNorm = LId*RId;
//     //DEBUG PrintData(ccBare);
//     //DEBUG PrintData(ccNorm);

//     return sumelsC(ccBare)/sumelsC(ccNorm);
// }

// std::complex<double> ExpValBuilder::expVal_1sO1sO_V(int dist, 
//         itensor::ITensor const& op1, itensor::ITensor const& op2)
// {
    
//     auto X = ExpValBuilder::getTOT(MPO_Id, 0, env.i_Xh, env.i_Xv,
//             false);
//     /*
//      * Construct the "Up" part tensor U
//      *  _      __      _            ________________ 
//      * |C|----|T |----|C|          |_______U________|
//      *  |      |       |      ==>    |      |     |
//      * |T|----|O1|----|T|     ==>  I(Tl)' I(Xv)'  I(Tr)'
//      *  |      |       |      ==>
//      * I(Tl)' I(Xv)'  I(Tr)'
//      *
//      */
//     auto U   = env.C_lu*env.T_u*env.C_ru;
//     U = U*env.T_l*op1*env.T_r;
//     auto UId = env.C_lu*env.T_u*env.C_ru;
//     UId = UId*env.T_l*X*env.T_r;
//     //DEBUG Print(L);

//     /*
//      * Contract U with "dist" copies of a row
//      *
//      * I(Tl)   I(Xv)   I(Tr)
//      *  |       |       |
//      * |T|-----|X|-----|T|
//      *  |       |       |
//      * I(Tl)'  I(Xv)'  I(Tr)'
//      *
//      */ 
//     //DEBUG std::cout << "Inserting "<< dist << " T_l--X--T_r row"
//     //DEBUG          << "\n";
//     for(int i=0;i<dist;i++) {
//         U.noprime();
//         UId.noprime();
//         U = U*env.T_l*X*env.T_r;
//         UId = UId*env.T_l*X*env.T_r;
//     }

//     /*
//      * Construct the "down" part tensor D
//      *                     
//      * I(Tl)  I(Xv)  I(Tr)
//      *  |      |       |   ==>          
//      * |T|----|O2|----|T|  ==>   I(Tl) I(Xv)  I(Tr)
//      *  |      |       |   ==>    _|____|_____|_
//      * |C|----|T |----|C|        |_____D________|
//      *
//      */
//     auto D = env.C_ld*env.T_d*env.C_rd;
//     D = D*env.T_l*op2*env.T_r;
//     auto DId = env.C_ld*env.T_d*env.C_rd;
//     DId = DId*env.T_l*X*env.T_r;
//     //DEBUG Print(R);

//     U.noprime();
//     UId.noprime();
//     // Contract (L*col^dist)*R
//     auto ccBare = U*D;
//     auto ccNorm = UId*DId;
//     //DEBUG PrintData(ccBare);
//     //DEBUG PrintData(ccNorm);

//     return sumelsC(ccBare)/sumelsC(ccNorm);
// }

// std::complex<double> ExpValBuilder::expVal_2sOV2sOV_H(int dist, 
//             ExpValBuilder::Mpo2S const& op1, 
//             ExpValBuilder::Mpo2S const& op2)
// {
//     //std::cout << "DEBUG ##### expVal 2sOV2sOV_H #####" << "\n";

//     auto X = ExpValBuilder::getTOT(MPO_Id, 0, env.i_Xh, env.i_Xv,
//             false);

//     /*
//      * Construct the "left" part tensor L
//      *  _            
//      * |C|--I(T_u)  
//      *  |    
//      * |T|--I(XH)
//      *  |   
//      * |T|--I(XH)''
//      *  |             
//      * |C|--I(T_d)
//      *
//      *
//      */
//     auto LA  = env.C_lu*env.T_l;
//     LA = LA.prime(prime(env.i_Tl,1),-1)*prime(env.T_l, env.i_Xh, 2)*env.C_ld;
//     auto LId = LA;
    
//     /*
//      * Construct the "left" part tensor L
//      *  _    __               _    __                 __
//      * |C|--|T |--I(T_u)'    |C|--|T |--I(T_u)'      |  |--I(T_u)
//      *  |    |                |    |                 |  |
//      * |T|--|O1B|-I(XH)'  => |T|--|O1B|-I(XH)'   ==> |  |--I(Xh)
//      *  |    v''          =>  |    |             ==> |L |
//      * |T|-h''            => |T|--|O1A|-I(XH)'3  ==> |  |--I(Xh)''
//      *  |                     |    |                 |  |
//      * |C|--I(T_d)           |C|--|T |--I(T_d)'      |__|--I(T_d)
//      *
//      */ 
//     LA = LA*env.T_u*prime(op1.opB, prime(env.i_Xv,1), 1);
//     LA = LA*prime(op1.opA, env.i_Xh,prime(env.i_Xh,1),env.i_Xv,
//         prime(env.i_Xv,1), 2)*prime(env.T_d, prime(env.i_Xv,1), 2);
//     LA.prime(-1);

//     LId = LId*env.T_u*prime(X, prime(env.i_Xv,1), 1);
//     LId = LId*prime(X,2)*prime(env.T_d, prime(env.i_Xv,1), 2);
//     LId.prime(-1);

//     for (int i=0; i<dist; i++) {
//         LA = LA*env.T_u*prime(X, prime(env.i_Xv,1), 1);
//         LA = LA*prime(X,2)*prime(env.T_d, prime(env.i_Xv,1), 2);
//         LA.prime(-1);

//         LId = LId*env.T_u*prime(X, prime(env.i_Xv,1), 1);
//         LId = LId*prime(X,2)*prime(env.T_d, prime(env.i_Xv,1), 2);
//         LId.prime(-1);
//     }
    
//     /*
//      * Construct the "left" part tensor L
//      *           _            
//      * I(T_u)'--|C|  
//      *           |    
//      *  I(XH)'--|T|
//      *           |   
//      * I(XH)'3--|T|
//      *           |             
//      * I(T_d)'--|C|
//      *
//      */
//     auto RB = env.C_ru*env.T_r;
//     RB = RB.prime(prime(env.i_Tr,1),-1)*prime(env.T_r, prime(env.i_Xh,1), 2)
//         *env.C_rd;
//     auto RId = RB;

//     /*
//      * Construct the "right" part tensor R
//      *         __    _               __     _                __
//      * I(T_u)-|T |--|C|     I(T_u)--|T |---|C|      I(T_u)--|  |
//      *         |     |               |      |               |  |
//      * I(XH)-|O2B|--|T|  =>  I(XH)--|O2B|--|T|  ==>  I(XH)--|  |
//      *         v''   |   =>          |      |   ==>         |R |
//      *          h'2-|T|  => I(XH)''-|O1A|--|T|  ==> I(XH)''-|  |
//      *               |               |      |               |  |
//      *     I(T_d)'--|C|     I(T_d)--|T |---|C|      I(T_d)--|__|
//      *
//      */
//     RB = RB*env.T_u*prime(op2.opB, prime(env.i_Xv,1), 1);
//     RB = RB*prime(op2.opA, env.i_Xh,prime(env.i_Xh,1),env.i_Xv,
//         prime(env.i_Xv,1), 2)*prime(env.T_d, prime(env.i_Xv,1), 2);   

//     RId = RId*env.T_u*prime(X, prime(env.i_Xv,1), 1);
//     RId = RId*prime(X,2)*prime(env.T_d, prime(env.i_Xv,1), 2);

//     // Contract L*R
//     auto ccBare = LA*RB;
//     auto ccNorm = LId*RId;
//     //DEBUG PrintData(ccBare);
//     //DEBUG PrintData(ccNorm);

//     return sumelsC(ccBare)/sumelsC(ccNorm);
// }

// std::complex<double> ExpValBuilder::expVal_2sOH2sOH_H(int dist, 
//             ExpValBuilder::Mpo2S const& op1, 
//             ExpValBuilder::Mpo2S const& op2)
// {
//     auto X = ExpValBuilder::getTOT(MPO_Id, 0, env.i_Xh, env.i_Xv,
//             false);
//     /*
//      * Construct the "left" part tensor L
//      *  _    __    __                 __
//      * |C|--|T |--|T |--I(T_u)'      |  |--I(T_u)'
//      *  |    |     |             ==> |  |
//      * |T|--|O1A|=|O1B|--I(XH)'  ==> |L |--I(Xh)'
//      *  |    |     |             ==> |  |
//      * |C|--|T |--|T |--I(T_d)'      |__|--I(T_d)'
//      *
//      */
//     auto L   = env.C_lu*env.T_l*env.C_ld;
//     L = L*env.T_u*op1.opA*env.T_d;
//     L.noprime();
//     L = L*env.T_u*op1.opB*env.T_d;

//     auto LId = env.C_lu*env.T_l*env.C_ld;
//     LId = LId*env.T_u*X*env.T_d;
//     LId.noprime();
//     LId = LId*env.T_u*X*env.T_d;
//     //DEBUG Print(L);

//     /*
//      * Contract L with "dist" copies of a column
//      *
//      * I(T_u)--|T|--I(T_u)'
//      *          |
//      *  I(Xh)--|X|--I(Xh)'
//      *          |
//      * I(T_d)--|T|--I(T_d)'
//      *
//      */ 
//     //DEBUG std::cout << "Inserting "<< dist <<" T_u--X--T_d column" 
//     //DEBUG         << "\n";
//     for(int i=0;i<dist;i++) {
//         L.noprime();
//         LId.noprime();
//         L = L*env.T_u*X*env.T_d;
//         LId = LId*env.T_u*X*env.T_d;
//     }

//     /*
//      * Construct the "right" part tensor R
//      *          __    __     _                 __
//      * I(T_u)--|T |--|T |---|C|       I(T_u)--|  |
//      *          |     |      |   ==>          |  |
//      *  I(Xh)--|O2A|=|O2B|--|T|  ==>   I(Xh)--|R |
//      *          |     |      |   ==>          |  |
//      * I(T_d)--|T |--|T |---|C|       I(T_d)--|__|
//      *
//      * for "dist" even, otherwise A and B are exchanged
//      *
//      */
//     auto R = env.C_ru*env.T_r*env.C_rd;
//     if (dist % 2 == 0) { 
//         R = R*env.T_u*op2.opB*env.T_d;
//         R.prime();
//         R = R*env.T_u*op2.opA*env.T_d;
//     } else {
//         R = R*env.T_u*op2.opA*env.T_d;
//         R.prime();
//         R = R*env.T_u*op2.opB*env.T_d;
//     }

//     auto RId = env.C_ru*env.T_r*env.C_rd;
//     RId = RId*env.T_u*X*env.T_d;
//     RId.prime();
//     RId = RId*env.T_u*X*env.T_d;
//     //Print(R);

//     L.noprime();
//     LId.noprime();
//     // Contract (L*col^dist)*R
//     auto ccBare = L*R;
//     auto ccNorm = LId*RId;
//     //DEBUG PrintData(ccBare);
//     //DEBUG PrintData(ccNorm);

//     return sumelsC(ccBare)/sumelsC(ccNorm);
// }

// TODO use getSpinOp defined in su2.h to get spin operator
ITensor EVBuilder::getSpinOp(MPO_1S mpo, Index const& s) const {

    SU2O su2o;
    switch(mpo) {
        case MPO_Id: {
            su2o = SU2_Id;
            break;    
        }
        case MPO_S_Z: {
            su2o = SU2_S_Z;
            break;
        }
        case MPO_S_Z2: {
            su2o = SU2_S_Z2;
            break;
        }
        case MPO_S_P: {
            su2o = SU2_S_P;
            break;
        }
        case MPO_S_M: {
            su2o = SU2_S_M;
            break;
        }
        default: {
            std::cout << "Invalid MPO selection" << std::endl;
            exit(EXIT_FAILURE);
            break;
        }
    }

    return SU2_getSpinOp(su2o, s);
}

std::ostream& EVBuilder::print(std::ostream& s) const {
    s << "ExpValBuilder("<< name <<")";
    return s;
}

std::ostream& operator<<(std::ostream& s, EVBuilder const& ev) {
    return ev.print(s);
}
