#include "cluster-ev-builder.h"

using namespace itensor;

EVBuilder::TransferOpVecProd::TransferOpVecProd(EVBuilder const& eev, 
    CtmData_Full const& ccd, std::pair<int,int> ss0, std::string ddir) 
    : ev(eev), cd(ccd), s0(ss0), dir(ddir) {}

void EVBuilder::TransferOpVecProd::operator() (double const* const x, double* const y) {
    int N = cd.auxDimSite * cd.auxDimEnv * cd.auxDimEnv;
    
    auto i  = Index("i",N);
    auto ip = prime(i);

    // copy x
    std::vector<double> cpx(N); 
    std::copy(x, x+N, cpx.data());

    //auto vecRefX = makeVecRef(cpx.data(),cpx.size()); 

    auto isX = IndexSet(i);
    auto X = ITensor(isX,Dense<double>(std::move(cpx)));
    
    ITensor cmbX;
    std::pair<int,int> s1,s2;
    if (dir=="HORIZONTAL") {
        cmbX = combiner(prime(cd.I_U),prime(cd.I_XH),prime(cd.I_D));
        s1 = getNextSite(ev.cls, s0, 2);
        s2 = getNextSite(ev.cls, s1, 2);
    } 
    else if (dir=="VERTICAL") {
        cmbX = combiner(prime(cd.I_L),prime(cd.I_XV),prime(cd.I_R));
        s1 = getNextSite(ev.cls, s0, 1);
        s2 = getNextSite(ev.cls, s1, 1);
    }
    else {
        std::cout<<"[TransferOpVecProd] Unsupported option: "<< dir << std::endl;
        exit(EXIT_FAILURE);
    }

    X *= delta(combinedIndex(cmbX),i);
    X *= cmbX;

    if (dir=="HORIZONTAL") {
        X *= cd.T_U[cd.cToS.at(s1)];
        X *= cd.sites[cd.cToS.at(s1)];
        X *= cd.T_D[cd.cToS.at(s1)];
    } 
    else if (dir=="VERTICAL") {
        X *= cd.T_L[cd.cToS.at(s1)];
        X *= cd.sites[cd.cToS.at(s1)];
        X *= cd.T_R[cd.cToS.at(s1)];
    }

    X.prime();

    if (dir=="HORIZONTAL") {
        X *= cd.T_U[cd.cToS.at(s2)];
        X *= cd.sites[cd.cToS.at(s2)];
        X *= cd.T_D[cd.cToS.at(s2)];
    } 
    else if (dir=="VERTICAL") {
        X *= cd.T_L[cd.cToS.at(s2)];
        X *= cd.sites[cd.cToS.at(s2)];
        X *= cd.T_R[cd.cToS.at(s2)];
    }

    cmbX.noprime();
    X *= cmbX;
    X *= delta(combinedIndex(cmbX),i);

    X.scaleTo(1.0);

    auto extractReal = [](Dense<Real> const& d) {
        return d.store;
    };

    auto xData = applyFunc(extractReal,X.store());
    std::copy(xData.data(), xData.data()+N, y);
}

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
MpoNS EVBuilder::getTOT(MPO_1S mpo, std::string siteId,
        int primeLvl, bool DBG) const 
{
    // Construct MPO
    auto op = getSpinOp(mpo, 
        noprime(findtype(cls.sites.at(siteId).inds(), PHYS)), DBG);

    return getTOT(op, siteId, primeLvl, DBG);
}

MpoNS EVBuilder::getTOT(MPO_1S mpo, std::pair<int ,int> site, int primeLvl,
        bool DBG) const
{
    // shift site to elementary supercell
    auto shift_site = site;
    shift_site.first  = shift_site.first % cls.sizeM;
    shift_site.second = shift_site.second % cls.sizeN;
    auto siteId = cls.cToS.at(shift_site);

    // Construct MPO
    auto op = getSpinOp(mpo, 
        noprime(findtype(cls.sites.at(siteId).inds(), PHYS)), DBG);

    return getTOT(op, siteId, primeLvl, DBG);
}

MpoNS EVBuilder::getTOT(ITensor const& op, std::string siteId,
        int primeLvl, bool DBG) const 
{   
    if(DBG) std::cout <<"===== getTOT called for site: "<< siteId 
        <<" ====="<< std::endl;
    if( op.r() < 2 ) {
        std::cout <<"getTOT op.rank() < 2"<< std::endl;
        exit(EXIT_FAILURE);
    }
    // TODO proper check 2 PHYS with prime level 0 and 1, everything else is AUX
    // if ((op.inds()[0].type() != PHYS) || (op.inds()[1].type() != PHYS)) {
    //     std::cout <<"getTOT op does not have two PHYS indices"<< std::endl;
    //     exit(EXIT_FAILURE);
    // }
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

    // Get physical index of T and op
    auto s   = noprime(findtype(T.inds(), PHYS));
    auto opI = noprime(findtype(op.inds(), PHYS));

    if(DBG) { Print(s);
        Print(opI); }

    ITensor D = (s==opI) ? ITensor(1.0) : delta(s,opI); 
    auto TOT = (T* D*op*prime(D,1) * ( conj(T).prime(AUXLINK,4).prime(PHYS,1) ))
        *C04*C15*C26*C37;

    // Define delta tensors D* to relabel combiner indices to I_XH, I_XV
    auto DH0 = delta(cd.I_XH, commonIndex(TOT,C04));
    auto DV0 = delta(cd.I_XV, commonIndex(TOT,C15));
    auto DH1 = delta(prime(cd.I_XH,1), commonIndex(TOT,C26));
    auto DV1 = delta(prime(cd.I_XV,1), commonIndex(TOT,C37));

    TOT = TOT*DH0*DV0*DH1*DV1;

    MpoNS result;
    result.nSite = 1;
    result.mpo.push_back(TOT);
    result.siteIds.push_back(siteId);

    if(DBG) std::cout <<"===== getTOT done ====="<< siteId << std::endl;

    return result;
}

/*
 * TODO consider imaginary part of the result as well
 * TODO optimize memory usage (potentially)
 *
 */
double EVBuilder::eV_1sO_1sENV(MPO_1S op1s, 
    std::pair<int,int> site, bool DBG) const 
{
    auto mpo = getTOT(op1s, cls.cToS.at(site), 0, DBG);
    return eV_1sO_1sENV(mpo, site, DBG);
} 

double EVBuilder::eV_1sO_1sENV(MpoNS const& op, 
    std::pair<int,int> site, bool DBG) const 
{
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

    // Move site to unit cell
    site.first  = site.first % cd_f.sizeM;
    site.second = site.second % cd_f.sizeN;

    auto ev = cd_f.C_LU[cd_f.cToS.at(site)];
    ev *= cd_f.T_L[cd_f.cToS.at(site)];
    ev *= cd_f.C_LD[cd_f.cToS.at(site)];

    ev *= cd_f.T_U[cd_f.cToS.at(site)];
    // substitute original on-site tensor for op at position site
    if(DBG) std::cout <<"OP inserted at ("<< site.first <<","<< site.second <<") -> "
        << cls.cToS.at(site) << std::endl;
    ev *= op.mpo[0];
    ev *= cd_f.T_D[cd_f.cToS.at(site)];

    ev *= cd_f.C_RU[cd_f.cToS.at(site)];
    ev *= cd_f.T_R[cd_f.cToS.at(site)];
    ev *= cd_f.C_RD[cd_f.cToS.at(site)];

    return sumels(ev)/getNorm_Rectangle(DBG, site, site);
}

/* 
 * TODO implement evaluation on arbitrary large cluster
 * 
 */
double EVBuilder::eV_2sO_Rectangle(
    std::pair< itensor::ITensor,itensor::ITensor > const& Op,
    std::pair<int,int> s1, std::pair<int,int> s2, bool DBG) const 
{
    return get2SOPTN(DBG, Op, s1, s2) / getNorm_Rectangle(DBG, s1, s2); 
}

// TODO REDUNDANCY FOR HANDLING s1 = s2 case
double EVBuilder::getNorm_Rectangle(bool DBG, std::pair<int,int> s1, 
        std::pair<int,int> s2) const 
{
    auto o1(getTOT(MPO_Id, cls.cToS.at(
        std::make_pair(s1.first % cls.sizeM, s1.second % cls.sizeN)), 0, DBG));
    auto o2(getTOT(MPO_Id, cls.cToS.at(
        std::make_pair(s2.first % cls.sizeM, s2.second % cls.sizeN)), 0, DBG));

    return get2SOPTN(DBG, std::make_pair(o1.mpo[0], o2.mpo[0]), s1, s2);
}

double EVBuilder::get2SOPTN(bool DBG,
        std::pair<ITensor,ITensor> const& Op,
        std::pair<int,int> s1, std::pair<int,int> s2) const 
{
    if(DBG) std::cout <<"===== EVBuilder::get2SOPTN called ====="
        << std::string(34,'=') << std::endl;
    /*
     *  Contract network defined as a rectangle by sites s1 and s2 
     *
     *  C T  T  T  T  C 
     *  T s1 .  .  .  T 
     *  T .  .  .  s2 T
     *  C T  T  T  T  C
     *
     */
    bool singleSite = false; // Assume s1 != s2
    bool wBEh       = true;  // Assume width of rectangle >= height 

    // Perform some coord manipulation (assumed coords are >= 0) and
    // s1.first <= s2.second && s1.second <= s2.second
    if ( (s1.first < 0) || (s1.second < 0) || (s2.first < 0) || (s2.second <0)
        || (s1.first > s2.first) || (s1.second > s2.second) ) {
        std::cout <<"Improper coordinates of sites s1, s2"<< std::endl;
        exit(EXIT_FAILURE);
    }

    if ( (s1.first == s2.first) && (s1.second == s2.second) ) {
        if(DBG) std::cout <<"s1 = s2 => Computing norm for single site"
            << std::endl;
        singleSite = true;
    }

    int sXdiff = s2.first - s1.first;
    int sYdiff = s2.second - s1.second;
    if ( !(sXdiff >= sYdiff) ) {
        if(DBG) std::cout <<"TN Width < Height => contracting row by row"
            << std::endl;
        wBEh = false;
    }

    // shift s1 to supercell
    s1.first  = s1.first % cls.sizeM;
    s1.second = s1.second % cls.sizeN;
    // shift s2 wrt to new position of s1
    s2.first  = s1.first + sXdiff;
    s2.second = s1.second + sYdiff;

    std::pair< int, int > s(s1);
    ITensor tN;

    if (wBEh) {
        // Construct LEFT edge
        if(DBG) std::cout << "C_LU["<< s.first <<","<< s.second <<"]"<<std::endl;

        tN = cd_f.C_LU[cd_f.cToS.at(s)];

        for ( int row=s1.second; row <= s2.second; s.second = ++row ) {
            if(DBG) std::cout<<"["<< s.first <<","<< row <<"] =>"
                <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"<<std::endl;

            s.second = s.second % cd_f.sizeN;
            tN.prime(HSLINK,2);
            tN.noprime(LLINK);
            tN *= cd_f.T_L[cd_f.cToS.at(s)];
        }
        s.second -= 1;
        if(DBG) std::cout << "C_LD["<< s.first <<","<< s.second <<"] =>"
            <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"<<std::endl;

        s.second = s.second % cd_f.sizeN;
        tN *= cd_f.C_LD[cd_f.cToS.at(s)];

        if(DBG) std::cout <<">>>>> 1) Left edge constructed <<<<<"<< std::endl;
        if(DBG) Print(tN);

        s = s1;
        for ( int col=s1.first; col <= s2.first; s.first = ++col ) {
            s.second = s1.second;
            if(DBG) std::cout<<"T_U["<< col <<","<< s.second <<"] =>"
                <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;

            s.first = s.first % cd_f.sizeM;
            tN.noprime(ULINK, DLINK);
            tN *= cd_f.T_U[cd_f.cToS.at(s)];
            
            if(DBG) Print(tN);

            for ( int row=s1.second; row<=s2.second; s.second = ++row ) {
                if(DBG) std::cout<<"["<< col <<","<< s.second <<"] =>"
                    <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"
                    <<std::endl;

                s.second = s.second % cd_f.sizeN; 
                tN.noprime(VSLINK);

                if ((col==s1.first) && (row == s1.second)) {
                    if(DBG) std::cout <<"Op.first inserted at ["<< s.first <<","
                        << s.second <<"] -> "<< cls.cToS.at(s) << std::endl;
                    tN *= prime(Op.first, HSLINK, 2*(sYdiff-row+s1.second));
                } else if ((col==s2.first) && (row == s2.second)) {
                    if(DBG) std::cout <<"Op.second inserted at ["<< s.first 
                        <<","<< s.second <<"] -> "<< cls.cToS.at(s) 
                        << std::endl;
                    tN *= prime(Op.second, HSLINK, 2*(sYdiff-row+s1.second));
                } else {
                    tN *= prime( cd_f.sites[cd_f.cToS.at(s)],
                        HSLINK, 2*(sYdiff-row+s1.second) );
                }
                
            }
            tN.prime(HSLINK,-1);
            
            s.second -= 1;
            if(DBG) std::cout <<"T_D["<< col <<","<< s.second <<"] =>"
                <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"<<std::endl;

            s.second = s.second % cd_f.sizeN;
            tN *= cd_f.T_D[cd_f.cToS.at(s)];

            if(DBG) std::cout << ">>>>> Appended col X= "<< col 
                <<" col mod sizeM: "<< col % cd_f.sizeM <<" <<<<<"<< std::endl;
            if(DBG) Print(tN);
        }

        if(DBG) std::cout <<">>>>> 2) "<< sXdiff+1 <<" cols appended <<<<<"
            << std::endl;

        // Construct RIGHT edge
        s = std::make_pair(s2.first, s1.second); 
        if(DBG) std::cout << "C_RU["<< s2.first <<","<< s1.second <<"] =>"
            <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;
        
        s.first = s.first % cd_f.sizeM;
        tN *= cd_f.C_RU[cd_f.cToS.at(s)];
        if(DBG) Print(tN);
        
        for ( int row=s1.second; row <= s2.second; s.second = ++row ) {
            tN.noprime(RLINK);
            tN.mapprime(2*(sYdiff-row+s1.second), 1,HSLINK);
            if(DBG) std::cout <<"HSLINK "<< 2*(sYdiff-row+s1.second) <<" -> "<<  
                1 << std::endl;

            if(DBG) std::cout<<"["<< s2.first <<","<< s.second <<"] =>"
                    <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"
                    <<std::endl;

            s.second = s.second % cd_f.sizeN;

            tN *= cd_f.T_R[cd_f.cToS.at(s)];
        }

        s.second -= 1;
        if(DBG) std::cout <<"C_RD["<< s2.first <<","<< s.second <<"] =>"
                <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"<<std::endl;
        
        s.second = s.second % cd_f.sizeN;        
        tN *= cd_f.C_RD[cd_f.cToS.at(s)];

        if(DBG) std::cout <<">>>>> 3) contraction with right edge <<<<<"
            << std::endl;
        if(DBG) Print(tN);
    } else {
        // Construct UP edge
        if(DBG) std::cout <<"C_LU["<< s.first <<","<< s.second <<"]"<<std::endl;

        tN = cd_f.C_LU[cd_f.cToS.at(s)];

        for ( int col=s1.first; col <= s2.first; s.first = ++col ) {
            if(DBG) std::cout<<"["<< col <<","<< s.second <<"] =>"
                <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;

            s.first = s.first % cd_f.sizeM;
            tN.prime(VSLINK,2);
            tN.noprime(ULINK);
            tN *= cd_f.T_U[cd_f.cToS.at(s)];
        }
        s.first -= 1;
        if(DBG) std::cout << "C_RU["<< s.first <<","<< s.second <<"] =>"
            <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;

        s.first = s.first % cd_f.sizeM;
        tN *= cd_f.C_RU[cd_f.cToS.at(s)];

        if(DBG) std::cout <<">>>>> 1) up edge constructed <<<<<"<< std::endl;
        if(DBG) Print(tN);

        s = s1;
        for ( int row=s1.second; row <= s2.second; s.second = ++row ) {
            s.first = s1.first;
            if(DBG) std::cout<<"T_L["<< s.first <<","<< row <<"] =>"
                <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"<<std::endl;

            s.second = s.second % cd_f.sizeN;
            tN.noprime(LLINK, RLINK);
            tN *= cd_f.T_L[cd_f.cToS.at(s)];
            
            if(DBG) Print(tN);

            for ( int col=s1.first; col<=s2.first; s.first = ++col ) {
                if(DBG) std::cout<<"["<< s.first <<","<< row <<"] =>"
                    <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"
                    <<std::endl;

                s.first = s.first % cd_f.sizeM;
                tN.noprime(HSLINK);

                if ((col==s1.first) && (row == s1.second)) {
                    if(DBG) std::cout <<"Op.first inserted at ["<< s.first <<","
                        << s.second <<"] -> "<< cls.cToS.at(s) << std::endl;
                    tN *= prime(Op.first, VSLINK, 2*(sXdiff-col+s1.first));
                } else if ((col==s2.first) && (row == s2.second)) {
                    if(DBG) std::cout <<"Op.second inserted at ["<< s.first 
                        <<","<< s.second <<"] -> "<< cls.cToS.at(s) 
                        << std::endl;
                    tN *= prime(Op.second, VSLINK, 2*(sXdiff-col+s1.first));
                } else {
                    tN *= prime( cd_f.sites[cd_f.cToS.at(s)],
                        VSLINK, 2*(sXdiff-col+s1.first) );
                }
            
            }
            tN.prime(VSLINK,-1);
            
            s.first -= 1;
            if(DBG) std::cout <<"T_R["<< s.first <<","<< row <<"] =>"
                <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;

            s.first = s.first % cd_f.sizeM;
            tN *= cd_f.T_R[cd_f.cToS.at(s)];

            if(DBG) std::cout <<">>>>> Appended row Y= "<< row <<
                " row mod sizeN: "<< row % cd_f.sizeN <<" <<<<<"<< std::endl;
            if(DBG) Print(tN);
        }

        if(DBG) std::cout <<">>>>> 2) "<< sYdiff+1 <<" rows appended <<<<<"
            << std::endl;

        s = std::make_pair(s1.first, s2.second); 
        if(DBG) std::cout << "C_LD["<< s1.first <<","<< s2.second <<"] =>"
            <<"["<< s.first <<","<< s.second  % cd_f.sizeN <<"]"<<std::endl;
        
        s.second = s.second % cd_f.sizeN;
        tN *= cd_f.C_LD[cd_f.cToS.at(s)];
        if(DBG) Print(tN);
        
        for ( int col=s1.first; col <= s2.first; s.first = ++col ) {
            tN.noprime(DLINK);
            tN.mapprime(2*(sXdiff-col+s1.first), 1,VSLINK);
            if(DBG) std::cout <<"VSLINK "<< 2*(sXdiff-col+s1.first) <<" -> "<<  
                1 << std::endl;

            if(DBG) std::cout<<"["<< s.first <<","<< s2.second <<"] =>"
                <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"
                <<std::endl;

            s.first = s.first % cd_f.sizeM;

            tN *= cd_f.T_D[cd_f.cToS.at(s)];
        }

        s.first -= 1;
        if(DBG) std::cout <<"C_RD["<< s.first <<","<< s2.second <<"] =>"
                <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;
        
        s.first = s.first % cd_f.sizeM;        
        tN *= cd_f.C_RD[cd_f.cToS.at(s)];

        if(DBG) std::cout <<">>>>> 3) contraction with right edge <<<<<"
            << std::endl;
        if(DBG) Print(tN);
    }

    if ( tN.r() > 0 ) {
        std::cout <<"Unexpected rank r="<< tN.r() << std::endl;
        exit(EXIT_FAILURE);
    }
    if(DBG) std::cout <<"===== EVBuilder::get2SOPTN done ====="
        << std::string(36,'=') << std::endl;

    return sumels(tN);
}

double EVBuilder::evalSS(std::pair<int,int> s1, std::pair<int,int> s2, 
    std::vector<double> coefs, bool DBG) const
{
    // find corresponding sites in elementary cluster
    auto e_s1 = std::make_pair(s1.first % cls.sizeM, s1.second % cls.sizeN);
    auto e_s2 = std::make_pair(s2.first % cls.sizeM, s2.second % cls.sizeN);
    // find the index of site given its elem position within cluster
    auto pI1 = noprime(findtype(cls.sites.at(cls.cToS.at(e_s1)), PHYS));
    auto pI2 = noprime(findtype(cls.sites.at(cls.cToS.at(e_s2)), PHYS));

    auto opId = get2SiteSpinOP(OP2S_Id, pI1, pI2, DBG);
    auto n = contract2Smpo(opId, s1, s2, DBG);

    auto SpSm = std::make_pair(getSpinOp(MPO_S_P, pI1), getSpinOp(MPO_S_M, pI2));
    auto SmSp = std::make_pair(getSpinOp(MPO_S_M, pI1), getSpinOp(MPO_S_P, pI2));
    auto SzSz = std::make_pair(getSpinOp(MPO_S_Z, pI1), getSpinOp(MPO_S_Z, pI2));

    auto spsm = contract2Smpo(SpSm, s1, s2, DBG);
    auto smsp = contract2Smpo(SmSp, s1, s2, DBG);
    auto szsz = contract2Smpo(SzSz, s1, s2, DBG);

    double ss =  (coefs[2] * szsz + 0.5 * (spsm + smsp))/n;
    return ss;
}

double EVBuilder::eval2Smpo(OP_2S op2s,
        std::pair<int,int> s1, std::pair<int,int> s2, bool DBG) const
{
    return contract2Smpo(op2s, s1, s2, DBG)/contract2Smpo(OP2S_Id, s1, s2, DBG);
}

double EVBuilder::eval2Smpo(std::pair<ITensor,ITensor> const& Op,
        std::pair<int,int> s1, std::pair<int,int> s2, bool DBG) const
{
    // find corresponding sites in elementary cluster
    auto e_s1 = std::make_pair(s1.first % cls.sizeM, s1.second % cls.sizeN);
    auto e_s2 = std::make_pair(s2.first % cls.sizeM, s2.second % cls.sizeN);
    // find the index of site given its elem position within cluster
    auto pI1 = noprime(findtype(cls.sites.at(cls.cToS.at(e_s1)), PHYS));
    auto pI2 = noprime(findtype(cls.sites.at(cls.cToS.at(e_s2)), PHYS));

    // Assume Op.first and Op.second have both pair of physical indices with primes 0 and 1
    auto tmpOp = Op;
    auto tmpPI1 = noprime(findtype(tmpOp.first, PHYS));
    auto tmpPI2 = noprime(findtype(tmpOp.second,PHYS));
    
    tmpOp.first  *= delta(tmpPI1,pI1);
    tmpOp.first  *= delta(prime(tmpPI1),prime(pI1));
    tmpOp.second *= delta(tmpPI2,pI2);
    tmpOp.second *= delta(prime(tmpPI2),prime(pI2));

    return contract2Smpo(tmpOp, s1, s2, DBG)/contract2Smpo(OP2S_Id, s1, s2, DBG);
}

double EVBuilder::contract2Smpo(OP_2S op2s,
        std::pair<int,int> s1, std::pair<int,int> s2, bool DBG) const 
{
    // find corresponding sites in elementary cluster
    auto e_s1 = std::make_pair(s1.first % cls.sizeM, s1.second % cls.sizeN);
    auto e_s2 = std::make_pair(s2.first % cls.sizeM, s2.second % cls.sizeN);
    // find the index of site given its elem position within cluster
    auto pI1 = noprime(findtype(cls.sites.at(cls.cToS.at(e_s1)), PHYS));
    auto pI2 = noprime(findtype(cls.sites.at(cls.cToS.at(e_s2)), PHYS));

    auto op = get2SiteSpinOP(op2s, pI1, pI2, DBG);

    return contract2Smpo(op, s1, s2, DBG);
}

double EVBuilder::contract2Smpo(std::pair<ITensor,ITensor> const& Op,
        std::pair<int,int> s1, std::pair<int,int> s2, bool DBG) const 
{
    if(DBG) std::cout <<"===== EVBuilder::contract2Smpo called ====="
        << std::string(34,'=') << std::endl;
    /*
     *  Contract network defined as a rectangle by sites s1 and s2 
     *
     *  C T  T  T  T  C 
     *  T s1 .  .  .  T 
     *  T .  .  .  s2 T
     *  C T  T  T  T  C
     *
     */
    bool singleSite = false; // Assume s1 != s2
    bool s1bs2      = true;  // Assume s1.first <= s2.first
    bool wBEh       = true;  // Assume width of rectangle >= height 

    // Perform some coord manipulation (assumed coords are >= 0) and
    // s1.first <= s2.second && s1.second <= s2.second
    if ( (s1.first < 0) || (s1.second < 0) || (s2.first < 0) || (s2.second <0)
        || (s1.second > s2.second) ) {
        std::cout <<"Improper coordinates of sites s1, s2"<< std::endl;
        exit(EXIT_FAILURE);
    }

    if ( (s1.first == s2.first) && (s1.second == s2.second) ) {
        if(DBG) std::cout <<"s1 = s2 => Computing norm for single site"
            << std::endl;
        singleSite = true;
    }

    if (s1.first > s2.first) {
        if(DBG) std::cout <<"s1.first > s2.first => s1 defines upper right corner"
            << std::endl;
        s1bs2 = false;
    }

    int sXdiff = s2.first - s1.first;
    int sYdiff = s2.second - s1.second;
    if ( !(abs(sXdiff) >= abs(sYdiff)) ) {
        if(DBG) std::cout <<"TN Width < Height => contracting row by row"
            << std::endl;
        wBEh = false;
    }

    ITensor tN;

    if (wBEh && s1bs2) {
        // shift s1 to supercell
        s1.first  = s1.first % cls.sizeM;
        s1.second = s1.second % cls.sizeN;
        // shift s2 wrt to new position of s1
        s2.first  = s1.first + sXdiff;
        s2.second = s1.second + sYdiff;

        std::pair< int, int > s(s1);

        // Construct LEFT edge
        if(DBG) std::cout << "C_LU["<< s.first <<","<< s.second <<"]"<<std::endl;

        tN = cd_f.C_LU[cd_f.cToS.at(s)];

        for ( int row=s1.second; row <= s2.second; s.second = ++row ) {
            if(DBG) std::cout<<"["<< s.first <<","<< row <<"] =>"
                <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"<<std::endl;

            s.second = s.second % cd_f.sizeN;
            tN.prime(HSLINK,2);
            tN.noprime(LLINK);
            tN = tN * cd_f.T_L[cd_f.cToS.at(s)];
        }
        s.second -= 1;
        if(DBG) std::cout << "C_LD["<< s.first <<","<< s.second <<"] =>"
            <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"<<std::endl;

        s.second = s.second % cd_f.sizeN;
        tN *= cd_f.C_LD[cd_f.cToS.at(s)];

        if(DBG) std::cout <<">>>>> 1) Left edge constructed <<<<<"<< std::endl;
        if(DBG) Print(tN);

        for ( int col=s1.first; col <= s2.first; s.first = ++col ) {
            s.second = s1.second;
            if(DBG) std::cout<<"T_U["<< col <<","<< s.second <<"] =>"
                <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;

            s.first = s.first % cd_f.sizeM;
            tN.noprime(ULINK, DLINK);
            tN = tN * cd_f.T_U[cd_f.cToS.at(s)];
            
            if(DBG) Print(tN);

            for ( int row=s1.second; row<=s2.second; s.second = ++row ) {
                if(DBG) std::cout<<"["<< col <<","<< s.second <<"] =>"
                    <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"
                    <<std::endl;

                s.second = s.second % cd_f.sizeN; 
                tN.noprime(VSLINK);

                if ((col==s1.first) && (row == s1.second)) {
                    // contract Op.first with bra and ket on-site tensors of site s1
                    auto mpo_s1 = getTOT(Op.first, cls.cToS.at(s), 0, DBG);
                    if(DBG) std::cout <<"Op.first inserted at ["<< s.first <<","
                        << s.second <<"] -> "<< cls.cToS.at(s) << std::endl;
                    tN = tN * prime(mpo_s1.mpo[0], HSLINK, 2*(sYdiff-row+s1.second));
                } else if ((col==s2.first) && (row == s2.second)) {
                    // contract Op.second with bra and ket on-site tensors of site s2
                    auto mpo_s2 = getTOT(Op.second, cls.cToS.at(s), 0, DBG);
                    if(DBG) std::cout <<"Op.second inserted at ["<< s.first 
                        <<","<< s.second <<"] -> "<< cls.cToS.at(s) 
                        << std::endl;
                    tN = tN * prime(mpo_s2.mpo[0], HSLINK, 2*(sYdiff-row+s1.second));
                } else {
                    tN = tN * prime( cd_f.sites[cd_f.cToS.at(s)],
                        HSLINK, 2*(sYdiff-row+s1.second) );
                }
                
            }
            tN.prime(HSLINK,-1);
            
            s.second -= 1;
            if(DBG) std::cout <<"T_D["<< col <<","<< s.second <<"] =>"
                <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"<<std::endl;

            s.second = s.second % cd_f.sizeN;
            tN *= cd_f.T_D[cd_f.cToS.at(s)];

            if(DBG) std::cout << ">>>>> Appended col X= "<< col 
                <<" col mod sizeM: "<< col % cd_f.sizeM <<" <<<<<"<< std::endl;
            if(DBG) Print(tN);
        }

        if(DBG) std::cout <<">>>>> 2) "<< sXdiff+1 <<" cols appended <<<<<"
            << std::endl;

        // Construct RIGHT edge
        s = std::make_pair(s2.first, s1.second); 
        if(DBG) std::cout << "C_RU["<< s2.first <<","<< s1.second <<"] =>"
            <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;
        
        s.first = s.first % cd_f.sizeM;
        tN *= cd_f.C_RU[cd_f.cToS.at(s)];
        if(DBG) Print(tN);
        
        for ( int row=s1.second; row <= s2.second; s.second = ++row ) {
            tN.noprime(RLINK);
            tN.mapprime(2*(sYdiff-row+s1.second), 1,HSLINK);
            if(DBG) std::cout <<"HSLINK "<< 2*(sYdiff-row+s1.second) <<" -> "<<  
                1 << std::endl;

            if(DBG) std::cout<<"["<< s2.first <<","<< s.second <<"] =>"
                    <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"
                    <<std::endl;

            s.second = s.second % cd_f.sizeN;

            tN *= cd_f.T_R[cd_f.cToS.at(s)];
        }

        s.second -= 1;
        if(DBG) std::cout <<"C_RD["<< s2.first <<","<< s.second <<"] =>"
                <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"<<std::endl;
        
        s.second = s.second % cd_f.sizeN;        
        tN *= cd_f.C_RD[cd_f.cToS.at(s)];

        if(DBG) std::cout <<">>>>> 3) contraction with right edge <<<<<"
            << std::endl;
        if(DBG) Print(tN);
    } else if(!wBEh && s1bs2) {
        // shift s1 to supercell
        s1.first  = s1.first % cls.sizeM;
        s1.second = s1.second % cls.sizeN;
        // shift s2 wrt to new position of s1
        s2.first  = s1.first + sXdiff;
        s2.second = s1.second + sYdiff;

        std::pair< int, int > s(s1);

        // Construct UP edge
        if(DBG) std::cout <<"C_LU["<< s.first <<","<< s.second <<"]"<<std::endl;

        tN = cd_f.C_LU[cd_f.cToS.at(s)];

        for ( int col=s1.first; col <= s2.first; s.first = ++col ) {
            if(DBG) std::cout<<"["<< col <<","<< s.second <<"] =>"
                <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;

            s.first = s.first % cd_f.sizeM;
            tN.prime(VSLINK,2);
            tN.noprime(ULINK);
            tN *= cd_f.T_U[cd_f.cToS.at(s)];
        }
        s.first -= 1;
        if(DBG) std::cout << "C_RU["<< s.first <<","<< s.second <<"] =>"
            <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;

        s.first = s.first % cd_f.sizeM;
        tN *= cd_f.C_RU[cd_f.cToS.at(s)];

        if(DBG) std::cout <<">>>>> 1) up edge constructed <<<<<"<< std::endl;
        if(DBG) Print(tN);

        s = s1;
        for ( int row=s1.second; row <= s2.second; s.second = ++row ) {
            s.first = s1.first;
            if(DBG) std::cout<<"T_L["<< s.first <<","<< row <<"] =>"
                <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"<<std::endl;

            s.second = s.second % cd_f.sizeN;
            tN.noprime(LLINK, RLINK);
            tN *= cd_f.T_L[cd_f.cToS.at(s)];
            
            if(DBG) Print(tN);

            for ( int col=s1.first; col<=s2.first; s.first = ++col ) {
                if(DBG) std::cout<<"["<< s.first <<","<< row <<"] =>"
                    <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"
                    <<std::endl;

                s.first = s.first % cd_f.sizeM;
                tN.noprime(HSLINK);

                if ((col==s1.first) && (row == s1.second)) {
                    auto mpo_s1 = getTOT(Op.first, cls.cToS.at(s), 0, DBG);
                    if(DBG) std::cout <<"Op.first inserted at ["<< s.first <<","
                        << s.second <<"] -> "<< cls.cToS.at(s) << std::endl;
                    tN *= prime(mpo_s1.mpo[0], VSLINK, 2*(sXdiff-col+s1.first));
                } else if ((col==s2.first) && (row == s2.second)) {
                    auto mpo_s2 = getTOT(Op.second, cls.cToS.at(s), 0, DBG);
                    if(DBG) std::cout <<"Op.second inserted at ["<< s.first 
                        <<","<< s.second <<"] -> "<< cls.cToS.at(s) 
                        << std::endl;
                    tN *= prime(mpo_s2.mpo[0], VSLINK, 2*(sXdiff-col+s1.first));
                } else {
                    tN *= prime( cd_f.sites[cd_f.cToS.at(s)],
                        VSLINK, 2*(sXdiff-col+s1.first) );
                }
            
            }
            tN.prime(VSLINK,-1);
            
            s.first -= 1;
            if(DBG) std::cout <<"T_R["<< s.first <<","<< row <<"] =>"
                <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;

            s.first = s.first % cd_f.sizeM;
            tN *= cd_f.T_R[cd_f.cToS.at(s)];

            if(DBG) std::cout <<">>>>> Appended row Y= "<< row <<
                " row mod sizeN: "<< row % cd_f.sizeN <<" <<<<<"<< std::endl;
            if(DBG) Print(tN);
        }

        if(DBG) std::cout <<">>>>> 2) "<< sYdiff+1 <<" rows appended <<<<<"
            << std::endl;

        s = std::make_pair(s1.first, s2.second); 
        if(DBG) std::cout << "C_LD["<< s1.first <<","<< s2.second <<"] =>"
            <<"["<< s.first <<","<< s.second  % cd_f.sizeN <<"]"<<std::endl;
        
        s.second = s.second % cd_f.sizeN;
        tN *= cd_f.C_LD[cd_f.cToS.at(s)];
        if(DBG) Print(tN);
        
        for ( int col=s1.first; col <= s2.first; s.first = ++col ) {
            tN.noprime(DLINK);
            tN.mapprime(2*(sXdiff-col+s1.first), 1,VSLINK);
            if(DBG) std::cout <<"VSLINK "<< 2*(sXdiff-col+s1.first) <<" -> "<<  
                1 << std::endl;

            if(DBG) std::cout<<"["<< s.first <<","<< s2.second <<"] =>"
                <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"
                <<std::endl;

            s.first = s.first % cd_f.sizeM;

            tN *= cd_f.T_D[cd_f.cToS.at(s)];
        }

        s.first -= 1;
        if(DBG) std::cout <<"C_RD["<< s.first <<","<< s2.second <<"] =>"
                <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;
        
        s.first = s.first % cd_f.sizeM;        
        tN *= cd_f.C_RD[cd_f.cToS.at(s)];

        if(DBG) std::cout <<">>>>> 3) contraction with right edge <<<<<"
            << std::endl;
        if(DBG) Print(tN);
    } else if(wBEh && !s1bs2) {
        sXdiff = s1.first - s2.first;
        sYdiff = s1.second - s2.second;

        if(DBG) { std::cout <<"Args: s1["<< s1.first <<","<< s1.second <<"]"<< std::endl;
            std::cout <<"Args: s2["<< s2.first <<","<< s2.second <<"]"<< std::endl; }

        // shift s2 to supercell
        s2.first  = s2.first % cls.sizeM;
        s2.second = s2.second % cls.sizeN;
        // shift s1 wrt to new position of s2
        s1.first  = s2.first + sXdiff;
        s1.second = s2.second + sYdiff;

        if(DBG) { std::cout <<"After shift: "<< std::endl;
            std::cout <<"s1["<< s1.first <<","<< s1.second <<"]"<< std::endl;
            std::cout <<"s2["<< s2.first <<","<< s2.second <<"]"<< std::endl; }

        // start from
        std::pair< int, int > s{s2};

        // Construct LEFT edge
        if(DBG) std::cout << "C_LD["<< s.first <<","<< s.second <<"]"<<std::endl;

        tN = cd_f.C_LD[cd_f.cToS.at(s)];

        for ( int row=s2.second; row >= s1.second; s.second = --row ) {
            if(DBG) std::cout<<"["<< s.first <<","<< row <<"] =>"
                <<"["<< s.first <<","<< 
            (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN <<"]"<<std::endl;

            s.second = (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN;
            tN.prime(HSLINK,2);
            tN.mapprime(LLINK,0,1);
            tN *= cd_f.T_L[cd_f.cToS.at(s)];
        }
        s.second += 1;
        if(DBG) std::cout << "C_LU["<< s.first <<","<< s.second <<"] =>"
            <<"["<< s.first <<","<< 
        (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN <<"]"<<std::endl;

        s.second = (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN;
        tN *= cd_f.C_LU[cd_f.cToS.at(s)];

        if(DBG) std::cout <<">>>>> 1) Left edge constructed <<<<<"<< std::endl;
        if(DBG) Print(tN);

        for ( int col=s2.first; col <= s1.first; s.first = ++col ) {
            s.second = s2.second;
            if(DBG) std::cout<<"T_D["<< col <<","<< s.second <<"] =>"
                <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;

            s.first = s.first % cd_f.sizeM;
            tN.noprime(ULINK, DLINK);
            tN *= cd_f.T_D[cd_f.cToS.at(s)];
            
            if(DBG) Print(tN);

            for ( int row=s2.second; row >= s1.second; s.second = --row ) {
                if(DBG) std::cout<<"["<< col <<","<< s.second <<"] =>"
                    <<"["<< s.first <<","<< 
                (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN <<"]"<<std::endl;

                s.second = (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN; 

                if ((col==s1.first) && (row == s1.second)) {
                    // contract Op.first with bra and ket on-site tensors of site s1
                    auto mpo_s1 = getTOT(Op.first, cls.cToS.at(s), 0, DBG);
                    if(DBG) std::cout <<"Op.first inserted at ["<< s.first <<","
                        << s.second <<"] -> "<< cls.cToS.at(s) << std::endl;
                    if(DBG) std::cout <<"HSLINK primeLvl: "<< 2*abs(abs(sYdiff)+row-s2.second)
                            << std::endl; 
                    tN *= prime(mpo_s1.mpo[0], HSLINK, 2*abs(abs(sYdiff)+row-s2.second));
                } else if ((col==s2.first) && (row == s2.second)) {
                    // contract Op.second with bra and ket on-site tensors of site s2
                    auto mpo_s2 = getTOT(Op.second, cls.cToS.at(s), 0, DBG);
                    if(DBG) std::cout <<"Op.second inserted at ["<< s.first 
                        <<","<< s.second <<"] -> "<< cls.cToS.at(s) 
                        << std::endl;
                    if(DBG) std::cout <<"HSLINK primeLvl: "<< 2*abs(abs(sYdiff)+row-s2.second)
                            << std::endl;
                    tN *= prime(mpo_s2.mpo[0], HSLINK, 2*abs(abs(sYdiff)+row-s2.second));
                } else {
                    if(DBG) std::cout <<"HSLINK primeLvl: "<< 2*abs(abs(sYdiff)+row-s2.second)
                            << std::endl;
                    tN *= prime( cd_f.sites[cd_f.cToS.at(s)],
                        HSLINK, 2*abs(abs(sYdiff)+row-s2.second) );
                }

                tN.prime(VSLINK);
            }
            tN.prime(VSLINK,-1);
            tN.prime(HSLINK,-1);
            
            s.second -= 1;
            if(DBG) std::cout <<"T_U["<< col <<","<< s.second <<"] =>"
                <<"["<< s.first <<","<< 
            (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN <<"]"<<std::endl;

            s.second = (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN;
            tN *= cd_f.T_U[cd_f.cToS.at(s)];

            if(DBG) std::cout << ">>>>> Appended col X= "<< col 
                <<" col mod sizeM: "<< col % cd_f.sizeM <<" <<<<<"<< std::endl;
            if(DBG) Print(tN);
        }

        if(DBG) std::cout <<">>>>> 2) "<< sXdiff+1 <<" cols appended <<<<<"
            << std::endl;

        // Construct RIGHT edge
        s = std::make_pair(s1.first, s2.second); 
        if(DBG) std::cout << "C_RD["<< s1.first <<","<< s2.second <<"] =>"
            <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;
        
        s.first = s.first % cd_f.sizeM;
        tN *= cd_f.C_RD[cd_f.cToS.at(s)];
        if(DBG) Print(tN);
        
        for ( int row=s2.second; row >= s1.second; s.second = --row ) {
            tN.mapprime(2*abs(abs(sYdiff)+row-s2.second), 1,HSLINK);
            if(DBG) std::cout <<"HSLINK "<< 2*abs(abs(sYdiff)+row-s2.second) <<" -> "<<  
                1 << std::endl;

            if(DBG) std::cout<<"["<< s2.first <<","<< s.second <<"] =>"
                    <<"["<< s.first <<","<< 
                (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN <<"]"<<std::endl;

            s.second = (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN;

            tN *= cd_f.T_R[cd_f.cToS.at(s)];
            tN.prime(RLINK);
        }
        tN.noprime(RLINK);
        s.second -= 1;
        if(DBG) std::cout <<"C_RU["<< s2.first <<","<< s.second <<"] =>"
                <<"["<< s.first <<","<< 
            (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN <<"]"<<std::endl;
        
        s.second = (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN;        
        tN *= cd_f.C_RU[cd_f.cToS.at(s)];

        if(DBG) std::cout <<">>>>> 3) contraction with right edge <<<<<"
            << std::endl;
        if(DBG) Print(tN);
    }

    if ( tN.r() > 0 ) {
        std::cout <<"Unexpected rank r="<< tN.r() << std::endl;
        exit(EXIT_FAILURE);
    }
    if(DBG) std::cout <<"===== EVBuilder::contract2Smpo done ====="
        << std::string(36,'=') << std::endl;

    return sumels(tN);
}

double EVBuilder::eval2Smpo_redDenMat2x1(OP_2S op2s, std::pair<int,int> s1, std::pair<int,int> s2, 
        bool DBG) const 
{
    auto rdm = redDenMat2x1(s1,s2,DBG);
    if ( rdm.r() != 4 ) std::cout << "ERROR rdm rank: "<< rdm.r() << std::endl;

    // find corresponding sites in elementary cluster
    auto e_s1 = std::make_pair(s1.first % cls.sizeM, s1.second % cls.sizeN);
    auto e_s2 = std::make_pair(s2.first % cls.sizeM, s2.second % cls.sizeN);
    // find the index of site given its elem position within cluster
    auto pI1 = noprime(findtype(cls.sites.at(cls.cToS.at(e_s1)), PHYS));
    auto pI2 = noprime(findtype(cls.sites.at(cls.cToS.at(e_s2)), PHYS));

    auto op = get2SiteSpinOP(op2s, pI1, pI2, DBG);

    auto valT = rdm * op.first * op.second;
    if (valT.r() > 0) std::cout << "ERROR valT rank" << std::endl;
    double val  = sumels(rdm * op.first * op.second);

    auto normT = rdm * delta(pI1,prime(pI1)) * delta(pI2,prime(pI2));
    if (normT.r() > 0) std::cout << "ERROR normT rank" << std::endl;
    double n = sumels(rdm * delta(pI1,prime(pI1)) * delta(pI2,prime(pI2)) );

    return val/n;
}

ITensor EVBuilder::redDenMat2x1(std::pair<int,int> s1, std::pair<int,int> s2, 
    bool DBG) const 
{
    if(DBG) std::cout <<"===== EVBuilder::redDenMat2x1 called ====="
        << std::string(34,'=') << std::endl;
    /*
     *  Contract network defined as a rectangle by sites s1 and s2 
     *
     *  C T  T  C           C T  C
     *  T s1 s2 T           T s1 T 
     *  C T  T  C    or     T s2 T
     *                      C T  C
     */
    bool singleSite = false; // Assume s1 != s2
    bool s1bs2      = true;  // Assume s1.first <= s2.first
    bool wBEh       = true;  // Assume width of rectangle >= height 

    // Perform some coord manipulation (assumed coords are >= 0) and
    // s1.first <= s2.second && s1.second <= s2.second
    if ( (s1.first < 0) || (s1.second < 0) || (s2.first < 0) || (s2.second <0)
        || (s1.second > s2.second) ) {
        std::cout <<"Improper coordinates of sites s1, s2"<< std::endl;
        exit(EXIT_FAILURE);
    }

    if ( (s1.first == s2.first) && (s1.second == s2.second) ) {
        if(DBG) std::cout <<"s1 = s2 => Computing norm for single site"
            << std::endl;
        singleSite = true;
    }

    if ( s1.first > s2.first ) {
        if(DBG) std::cout <<"s1.first > s2.first => s1 defines upper right corner"
            << std::endl;
        s1bs2 = false;
    }

    int sXdiff = s2.first - s1.first;
    int sYdiff = s2.second - s1.second;
    if ( !(abs(sXdiff) >= abs(sYdiff)) ) {
        if(DBG) std::cout <<"TN Width < Height => contracting row by row"
            << std::endl;
        wBEh = false;
    }

    ITensor tN;

    if (wBEh && s1bs2) 
    { // width >= height AND s1 is on the left of s2
        // shift s1 to supercell
        s1.first  = s1.first % cls.sizeM;
        s1.second = s1.second % cls.sizeN;
        // shift s2 wrt to new position of s1
        s2.first  = s1.first + sXdiff;
        s2.second = s1.second + sYdiff;

        std::pair< int, int > s(s1);

        // Construct LEFT edge
        if(DBG) std::cout << "C_LU["<< s.first <<","<< s.second <<"]"<<std::endl;

        tN = cd_f.C_LU[cd_f.cToS.at(s)];
        
        int row = s.second; 
        if(DBG) std::cout<<"["<< s.first <<","<< row <<"] =>"
            <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"<<std::endl;

        s.second = s.second % cd_f.sizeN;
        tN.noprime(LLINK);
        tN *= cd_f.T_L[cd_f.cToS.at(s)];
        
        if(DBG) std::cout << "C_LD["<< s.first <<","<< s.second <<"] =>"
            <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"<<std::endl;

        s.second = s.second % cd_f.sizeN;
        tN *= cd_f.C_LD[cd_f.cToS.at(s)];

        if(DBG) std::cout <<">>>>> 1) Left edge constructed <<<<<"<< std::endl;
        if(DBG) Print(tN);

        int col=s1.first;
            s.second = s1.second;
            if(DBG) std::cout<<"T_U["<< col <<","<< s.second <<"] =>"
                <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;

            s.first = s.first % cd_f.sizeM;
            tN.noprime(ULINK, DLINK);
            tN *= cd_f.T_U[cd_f.cToS.at(s)];
            
            if(DBG) Print(tN);

            row=s1.second;
                if(DBG) std::cout<<"["<< col <<","<< s.second <<"] =>"
                    <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"
                    <<std::endl;

                s.second = s.second % cd_f.sizeN; 
                tN.noprime(VSLINK);

                auto cmbL0 = combiner(prime(cls.aux[cls.SI.at(cls.cToS.at(s))],0), 
                    prime(cls.aux[cls.SI.at(cls.cToS.at(s))],4) );
                // cmbL0 *= delta(cd_f.I_XH, combinedIndex(cmbL0));
                auto cmbL1 = combiner(prime(cls.aux[cls.SI.at(cls.cToS.at(s))],1), 
                    prime(cls.aux[cls.SI.at(cls.cToS.at(s))],5) );
                // cmbL1 *= delta(cd_f.I_XV, combinedIndex(cmbL1));
                auto cmbL3 = combiner(prime(cls.aux[cls.SI.at(cls.cToS.at(s))],3), 
                    prime(cls.aux[cls.SI.at(cls.cToS.at(s))],7) );
                // cmbL3 *= delta(prime(cd_f.I_XV), combinedIndex(cmbL3));
                tN *= delta(cd_f.I_XH, combinedIndex(cmbL0));
                tN *= cmbL0;
                tN *= delta(cd_f.I_XV, combinedIndex(cmbL1));
                tN *= cmbL1;

                // if ((col==s1.first) && (row == s1.second)) {
                //     // contract Op.first with bra and ket on-site tensors of site s1
                //     auto mpo_s1 = getTOT(Op.first, cls.cToS.at(s), 0, DBG);
                //     if(DBG) std::cout <<"Op.first inserted at ["<< s.first <<","
                //         << s.second <<"] -> "<< cls.cToS.at(s) << std::endl;
                //     tN = tN * prime(mpo_s1.mpo[0], HSLINK, 2*(sYdiff-row+s1.second));
                // } else if ((col==s2.first) && (row == s2.second)) {
                //     // contract Op.second with bra and ket on-site tensors of site s2
                //     auto mpo_s2 = getTOT(Op.second, cls.cToS.at(s), 0, DBG);
                //     if(DBG) std::cout <<"Op.second inserted at ["<< s.first 
                //         <<","<< s.second <<"] -> "<< cls.cToS.at(s) 
                //         << std::endl;
                //     tN = tN * prime(mpo_s2.mpo[0], HSLINK, 2*(sYdiff-row+s1.second));
                // } else {
                    tN *= cls.sites.at(cls.cToS.at(s));
                    tN *= prime(prime(conj(cls.sites.at(cls.cToS.at(s))), AUXLINK, 4 ), PHYS, 1);
                // }
                tN *= cmbL3;
                tN *= delta(prime(cd_f.I_XV), combinedIndex(cmbL3));
                
                auto aIs1 = cls.aux[cls.SI.at(cls.cToS.at(s))];

            // tN.prime(HSLINK,-1);
            
            if(DBG) std::cout <<"T_D["<< col <<","<< s.second <<"] =>"
                <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"<<std::endl;

            s.second = s.second % cd_f.sizeN;
            tN *= cd_f.T_D[cd_f.cToS.at(s)];

            if(DBG) std::cout << ">>>>> Appended col X= "<< col 
                <<" col mod sizeM: "<< col % cd_f.sizeM <<" <<<<<"<< std::endl;
            if(DBG) Print(tN);
        // }

        if(DBG) std::cout <<">>>>> 2) "<< sXdiff+1 <<" cols appended <<<<<"
            << std::endl;

        // Construct RIGHT edge
        s = std::make_pair(s2.first, s1.second);
        if(DBG) std::cout << "C_RU["<< s2.first <<","<< s1.second <<"] =>"
            <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;
        
        s.first = s.first % cd_f.sizeM;
        auto tNR = cd_f.C_RU[cd_f.cToS.at(s)];
        if(DBG) Print(tNR);
        
        // for ( int row=s1.second; row <= s2.second; s.second = ++row ) {
            tNR.noprime(RLINK);
            // tN.mapprime(2*(sYdiff-row+s1.second), 1,HSLINK);
            // if(DBG) std::cout <<"HSLINK "<< 2*(sYdiff-row+s1.second) <<" -> "<<  
                // 1 << std::endl;

            if(DBG) std::cout<<"["<< s2.first <<","<< s.second <<"] =>"
                    <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"
                    <<std::endl;

            s.second = s.second % cd_f.sizeN;

            tNR *= cd_f.T_R[cd_f.cToS.at(s)];
        // }

        if(DBG) std::cout <<"C_RD["<< s2.first <<","<< s.second <<"] =>"
                <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"<<std::endl;
        
        s.second = s.second % cd_f.sizeN;        
        tNR *= cd_f.C_RD[cd_f.cToS.at(s)];


        if(DBG) std::cout <<">>>>> 3) contraction with right edge <<<<<"
            << std::endl;
        if(DBG) Print(tNR);


        // tNR.noprime(ULINK, DLINK);
        tNR *= cd_f.T_U[cd_f.cToS.at(s)];


        auto cmbR1 = combiner(prime(cls.aux[cls.SI.at(cls.cToS.at(s))],1), 
            prime(cls.aux[cls.SI.at(cls.cToS.at(s))],5) );
        // cmbR1 *= delta(cd_f.I_XV, combinedIndex(cmbR1));
        auto cmbR2 = combiner(prime(cls.aux[cls.SI.at(cls.cToS.at(s))],2), 
            prime(cls.aux[cls.SI.at(cls.cToS.at(s))],6) );
        // cmbR2 *= delta(prime(cd_f.I_XH), combinedIndex(cmbR2));
        auto cmbR3 = combiner( prime(cls.aux[cls.SI.at(cls.cToS.at(s))],3), 
            prime(cls.aux[cls.SI.at(cls.cToS.at(s))],7) );
        // cmbR3 *= delta(prime(cd_f.I_XV), combinedIndex(cmbR3));
        tNR *= delta(cd_f.I_XV, combinedIndex(cmbR1));
        tNR *= cmbR1;
        tNR *= delta(prime(cd_f.I_XH), combinedIndex(cmbR2));
        tNR *= cmbR2;


        tNR *= cls.sites.at(cls.cToS.at(s));
        tNR *= prime(prime(conj(cls.sites.at(cls.cToS.at(s))), AUXLINK, 4 ), PHYS, 1);

        tNR *= cmbR3;
        tNR *= delta(prime(cd_f.I_XV), combinedIndex(cmbR3));

        tNR *= cd_f.T_D[cd_f.cToS.at(s)];


        auto aIs2 = cls.aux[cls.SI.at(cls.cToS.at(s))];

        tN.noprime(ULINK,DLINK);
        tN *= delta( prime(aIs1,2) , prime(aIs2,0) );
        tN *= delta( prime(aIs1,6) , prime(aIs2,4) );

        return tN * tNR;
    } 
    else if (!wBEh && s1bs2) 
    { // width < height AND s1 is on the left of s2
        // shift s1 to supercell
        s1.first  = s1.first % cls.sizeM;
        s1.second = s1.second % cls.sizeN;
        // shift s2 wrt to new position of s1
        s2.first  = s1.first + sXdiff;
        s2.second = s1.second + sYdiff;

        std::pair< int, int > s(s1);

        // Construct UP edge
        if(DBG) std::cout <<"C_LU["<< s.first <<","<< s.second <<"]"<<std::endl;

        tN = cd_f.C_LU[cd_f.cToS.at(s)];

        //for ( int col=s1.first; col <= s2.first; s.first = ++col ) {
        int col = s1.first;
            if(DBG) std::cout<<"["<< col <<","<< s.second <<"] =>"
                <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;

            s.first = s.first % cd_f.sizeM;
            tN.prime(VSLINK,2);
            tN.noprime(ULINK);
            tN *= cd_f.T_U[cd_f.cToS.at(s)];
        //}
        
        if(DBG) std::cout << "C_RU["<< s.first <<","<< s.second <<"] =>"
            <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;

        s.first = s.first % cd_f.sizeM;
        tN *= cd_f.C_RU[cd_f.cToS.at(s)];

        if(DBG) std::cout <<">>>>> 1) up edge constructed <<<<<"<< std::endl;
        if(DBG) Print(tN);

        s = s1;
        //for ( int row=s1.second; row <= s2.second; s.second = ++row ) {
        int row=s1.second;

            s.first = s1.first;
            if(DBG) std::cout<<"T_L["<< s.first <<","<< row <<"] =>"
                <<"["<< s.first <<","<< s.second % cd_f.sizeN <<"]"<<std::endl;

            s.second = s.second % cd_f.sizeN;
            //tN.noprime(LLINK, RLINK);
            tN *= cd_f.T_L[cd_f.cToS.at(s)];
            
            if(DBG) Print(tN);

            //for ( int col=s1.first; col<=s2.first; s.first = ++col ) {
            col=s1.first;
                if(DBG) std::cout<<"["<< s.first <<","<< row <<"] =>"
                    <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"
                    <<std::endl;

                s.first = s.first % cd_f.sizeM;
                //tN.noprime(HSLINK);

                auto cmbT0 = combiner(prime(cls.aux[cls.SI.at(cls.cToS.at(s))],0), 
                    prime(cls.aux[cls.SI.at(cls.cToS.at(s))],4) );
                // cmbT0 *= delta(cd_f.I_XH, combinedIndex(cmbT0));
                auto cmbT1 = combiner(prime(cls.aux[cls.SI.at(cls.cToS.at(s))],1), 
                    prime(cls.aux[cls.SI.at(cls.cToS.at(s))],5) );
                // cmbT1 *= delta(cd_f.I_XV, combinedIndex(cmbT1));
                auto cmbT2 = combiner( prime(cls.aux[cls.SI.at(cls.cToS.at(s))],2), 
                    prime(cls.aux[cls.SI.at(cls.cToS.at(s))],6) );
                // cmbT2 *= delta(prime(cd_f.I_XH), combinedIndex(cmbT2));
                tN *= delta(cd_f.I_XH, combinedIndex(cmbT0));
                tN *= cmbT0;
                tN *= delta(cd_f.I_XV, combinedIndex(cmbT1));
                tN *= cmbT1;

                // if ((col==s1.first) && (row == s1.second)) {
                //     auto mpo_s1 = getTOT(Op.first, cls.cToS.at(s), 0, DBG);
                //     if(DBG) std::cout <<"Op.first inserted at ["<< s.first <<","
                //         << s.second <<"] -> "<< cls.cToS.at(s) << std::endl;
                //     tN *= prime(mpo_s1.mpo[0], VSLINK, 2*(sXdiff-col+s1.first));
                // } else if ((col==s2.first) && (row == s2.second)) {
                //     auto mpo_s2 = getTOT(Op.second, cls.cToS.at(s), 0, DBG);
                //     if(DBG) std::cout <<"Op.second inserted at ["<< s.first 
                //         <<","<< s.second <<"] -> "<< cls.cToS.at(s) 
                //         << std::endl;
                //     tN *= prime(mpo_s2.mpo[0], VSLINK, 2*(sXdiff-col+s1.first));
                // } else {
                    // tN *= prime( cd_f.sites[cd_f.cToS.at(s)],
                    //     VSLINK, 2*(sXdiff-col+s1.first) );
                    tN *= cls.sites.at(cls.cToS.at(s));
                    tN *= prime(prime(conj(cls.sites.at(cls.cToS.at(s))), AUXLINK, 4 ), PHYS, 1);
                // }
            
                tN *= cmbT2;
                tN *= delta(prime(cd_f.I_XH), combinedIndex(cmbT2));

            //}
            auto aIs1 = cls.aux[cls.SI.at(cls.cToS.at(s))];
            //tN.prime(VSLINK,-1);
            
            
            if(DBG) std::cout <<"T_R["<< s.first <<","<< row <<"] =>"
                <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;

            s.first = s.first % cd_f.sizeM;
            tN *= cd_f.T_R[cd_f.cToS.at(s)];

            if(DBG) std::cout <<">>>>> Appended row Y= "<< row <<
                " row mod sizeN: "<< row % cd_f.sizeN <<" <<<<<"<< std::endl;
            if(DBG) Print(tN);
        //}

        if(DBG) std::cout <<">>>>> 2) "<< sYdiff+1 <<" rows appended <<<<<"
            << std::endl;

        s = std::make_pair(s1.first, s2.second); 
        if(DBG) std::cout << "C_LD["<< s1.first <<","<< s2.second <<"] =>"
            <<"["<< s.first <<","<< s.second  % cd_f.sizeN <<"]"<<std::endl;
        
        s.second = s.second % cd_f.sizeN;
        auto tNB = cd_f.C_LD[cd_f.cToS.at(s)];
        if(DBG) Print(tNB);
        
        // for ( int col=s1.first; col <= s2.first; s.first = ++col ) {
        col = s1.first;
            //tNB.noprime(DLINK);
            //tNB.mapprime(2*(sXdiff-col+s1.first), 1,VSLINK);
            if(DBG) std::cout <<"VSLINK "<< 2*(sXdiff-col+s1.first) <<" -> "<<  
                1 << std::endl;

            if(DBG) std::cout<<"["<< s.first <<","<< s2.second <<"] =>"
                <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"
                <<std::endl;

            s.first = s.first % cd_f.sizeM;

            tNB *= cd_f.T_D[cd_f.cToS.at(s)];
        // }

        
        if(DBG) std::cout <<"C_RD["<< s.first <<","<< s2.second <<"] =>"
                <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;
        
        s.first = s.first % cd_f.sizeM;        
        tNB *= cd_f.C_RD[cd_f.cToS.at(s)];

        if(DBG) std::cout <<">>>>> 3) contraction with right edge <<<<<"
            << std::endl;
        if(DBG) Print(tNB);

        //tNB.noprime(LLINK, RLINK);
        tNB *= cd_f.T_L[cd_f.cToS.at(s)];


        auto cmbB0 = combiner(prime(cls.aux[cls.SI.at(cls.cToS.at(s))],0), 
            prime(cls.aux[cls.SI.at(cls.cToS.at(s))],4) );
        // cmbB0 *= delta(cd_f.I_XH, combinedIndex(cmbB0));
        auto cmbB2 = combiner(prime(cls.aux[cls.SI.at(cls.cToS.at(s))],2), 
            prime(cls.aux[cls.SI.at(cls.cToS.at(s))],6) );
        // cmbB0 *= delta(prime(cd_f.I_XH), combinedIndex(cmbB0));
        auto cmbB3 = combiner( prime(cls.aux[cls.SI.at(cls.cToS.at(s))],3), 
            prime(cls.aux[cls.SI.at(cls.cToS.at(s))],7) );
        // cmbB3 *= delta(prime(cd_f.I_XV), combinedIndex(cmbB3));
        tNB *= delta(cd_f.I_XH, combinedIndex(cmbB0));
        tNB *= cmbB0;
        tNB *= delta(prime(cd_f.I_XV), combinedIndex(cmbB3));
        tNB *= cmbB3;

        tNB *= cls.sites.at(cls.cToS.at(s));
        tNB *= prime(prime(conj(cls.sites.at(cls.cToS.at(s))), AUXLINK, 4 ), PHYS, 1);

        tNB *= cmbB2;
        tNB *= delta(prime(cd_f.I_XH), combinedIndex(cmbB2));

        tNB *= cd_f.T_R[cd_f.cToS.at(s)];

        auto aIs2 = cls.aux[cls.SI.at(cls.cToS.at(s))];
        tN.noprime(LLINK,RLINK);
        tN *= delta( prime(aIs1,3) , prime(aIs2,1) );
        tN *= delta( prime(aIs1,7) , prime(aIs2,5) );

        return tN * tNB;
    } 
    // else if(wBEh && !s1bs2) {
    //     sXdiff = s1.first - s2.first;
    //     sYdiff = s1.second - s2.second;

    //     if(DBG) { std::cout <<"Args: s1["<< s1.first <<","<< s1.second <<"]"<< std::endl;
    //         std::cout <<"Args: s2["<< s2.first <<","<< s2.second <<"]"<< std::endl; }

    //     // shift s2 to supercell
    //     s2.first  = s2.first % cls.sizeM;
    //     s2.second = s2.second % cls.sizeN;
    //     // shift s1 wrt to new position of s2
    //     s1.first  = s2.first + sXdiff;
    //     s1.second = s2.second + sYdiff;

    //     if(DBG) { std::cout <<"After shift: "<< std::endl;
    //         std::cout <<"s1["<< s1.first <<","<< s1.second <<"]"<< std::endl;
    //         std::cout <<"s2["<< s2.first <<","<< s2.second <<"]"<< std::endl; }

    //     // start from
    //     std::pair< int, int > s{s2};

    //     // Construct LEFT edge
    //     if(DBG) std::cout << "C_LD["<< s.first <<","<< s.second <<"]"<<std::endl;

    //     tN = cd_f.C_LD[cd_f.cToS.at(s)];

    //     for ( int row=s2.second; row >= s1.second; s.second = --row ) {
    //         if(DBG) std::cout<<"["<< s.first <<","<< row <<"] =>"
    //             <<"["<< s.first <<","<< 
    //         (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN <<"]"<<std::endl;

    //         s.second = (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN;
    //         tN.prime(HSLINK,2);
    //         tN.mapprime(LLINK,0,1);
    //         tN *= cd_f.T_L[cd_f.cToS.at(s)];
    //     }
    //     s.second += 1;
    //     if(DBG) std::cout << "C_LU["<< s.first <<","<< s.second <<"] =>"
    //         <<"["<< s.first <<","<< 
    //     (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN <<"]"<<std::endl;

    //     s.second = (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN;
    //     tN *= cd_f.C_LU[cd_f.cToS.at(s)];

    //     if(DBG) std::cout <<">>>>> 1) Left edge constructed <<<<<"<< std::endl;
    //     if(DBG) Print(tN);

    //     for ( int col=s2.first; col <= s1.first; s.first = ++col ) {
    //         s.second = s2.second;
    //         if(DBG) std::cout<<"T_D["<< col <<","<< s.second <<"] =>"
    //             <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;

    //         s.first = s.first % cd_f.sizeM;
    //         tN.noprime(ULINK, DLINK);
    //         tN *= cd_f.T_D[cd_f.cToS.at(s)];
            
    //         if(DBG) Print(tN);

    //         for ( int row=s2.second; row >= s1.second; s.second = --row ) {
    //             if(DBG) std::cout<<"["<< col <<","<< s.second <<"] =>"
    //                 <<"["<< s.first <<","<< 
    //             (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN <<"]"<<std::endl;

    //             s.second = (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN; 

    //             if ((col==s1.first) && (row == s1.second)) {
    //                 // contract Op.first with bra and ket on-site tensors of site s1
    //                 auto mpo_s1 = getTOT(Op.first, cls.cToS.at(s), 0, DBG);
    //                 if(DBG) std::cout <<"Op.first inserted at ["<< s.first <<","
    //                     << s.second <<"] -> "<< cls.cToS.at(s) << std::endl;
    //                 if(DBG) std::cout <<"HSLINK primeLvl: "<< 2*abs(abs(sYdiff)+row-s2.second)
    //                         << std::endl; 
    //                 tN *= prime(mpo_s1.mpo[0], HSLINK, 2*abs(abs(sYdiff)+row-s2.second));
    //             } else if ((col==s2.first) && (row == s2.second)) {
    //                 // contract Op.second with bra and ket on-site tensors of site s2
    //                 auto mpo_s2 = getTOT(Op.second, cls.cToS.at(s), 0, DBG);
    //                 if(DBG) std::cout <<"Op.second inserted at ["<< s.first 
    //                     <<","<< s.second <<"] -> "<< cls.cToS.at(s) 
    //                     << std::endl;
    //                 if(DBG) std::cout <<"HSLINK primeLvl: "<< 2*abs(abs(sYdiff)+row-s2.second)
    //                         << std::endl;
    //                 tN *= prime(mpo_s2.mpo[0], HSLINK, 2*abs(abs(sYdiff)+row-s2.second));
    //             } else {
    //                 if(DBG) std::cout <<"HSLINK primeLvl: "<< 2*abs(abs(sYdiff)+row-s2.second)
    //                         << std::endl;
    //                 tN *= prime( cd_f.sites[cd_f.cToS.at(s)],
    //                     HSLINK, 2*abs(abs(sYdiff)+row-s2.second) );
    //             }

    //             tN.prime(VSLINK);
    //         }
    //         tN.prime(VSLINK,-1);
    //         tN.prime(HSLINK,-1);
            
    //         s.second -= 1;
    //         if(DBG) std::cout <<"T_U["<< col <<","<< s.second <<"] =>"
    //             <<"["<< s.first <<","<< 
    //         (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN <<"]"<<std::endl;

    //         s.second = (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN;
    //         tN *= cd_f.T_U[cd_f.cToS.at(s)];

    //         if(DBG) std::cout << ">>>>> Appended col X= "<< col 
    //             <<" col mod sizeM: "<< col % cd_f.sizeM <<" <<<<<"<< std::endl;
    //         if(DBG) Print(tN);
    //     }

    //     if(DBG) std::cout <<">>>>> 2) "<< sXdiff+1 <<" cols appended <<<<<"
    //         << std::endl;

    //     // Construct RIGHT edge
    //     s = std::make_pair(s1.first, s2.second); 
    //     if(DBG) std::cout << "C_RD["<< s1.first <<","<< s2.second <<"] =>"
    //         <<"["<< s.first % cd_f.sizeM <<","<< s.second <<"]"<<std::endl;
        
    //     s.first = s.first % cd_f.sizeM;
    //     tN *= cd_f.C_RD[cd_f.cToS.at(s)];
    //     if(DBG) Print(tN);
        
    //     for ( int row=s2.second; row >= s1.second; s.second = --row ) {
    //         tN.mapprime(2*abs(abs(sYdiff)+row-s2.second), 1,HSLINK);
    //         if(DBG) std::cout <<"HSLINK "<< 2*abs(abs(sYdiff)+row-s2.second) <<" -> "<<  
    //             1 << std::endl;

    //         if(DBG) std::cout<<"["<< s2.first <<","<< s.second <<"] =>"
    //                 <<"["<< s.first <<","<< 
    //             (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN <<"]"<<std::endl;

    //         s.second = (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN;

    //         tN *= cd_f.T_R[cd_f.cToS.at(s)];
    //         tN.prime(RLINK);
    //     }
    //     tN.noprime(RLINK);
    //     s.second -= 1;
    //     if(DBG) std::cout <<"C_RU["<< s2.first <<","<< s.second <<"] =>"
    //             <<"["<< s.first <<","<< 
    //         (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN <<"]"<<std::endl;
        
    //     s.second = (s.second + abs(s.second)*cd_f.sizeN) % cd_f.sizeN;        
    //     tN *= cd_f.C_RU[cd_f.cToS.at(s)];

    //     if(DBG) std::cout <<">>>>> 3) contraction with right edge <<<<<"
    //         << std::endl;
    //     if(DBG) Print(tN);
    // }

    if ( tN.r() != 4 ) {
        std::cout <<"Unexpected rank r="<< tN.r() << std::endl;
        exit(EXIT_FAILURE);
    }
    if(DBG) std::cout <<"===== EVBuilder::redDenMat2x1 done ====="
        << std::string(36,'=') << std::endl;

    return ITensor();
} 

std::vector< std::complex<double> > EVBuilder::expVal_1sO1sO_H( 
        MPO_1S o1, MPO_1S o2,
        std::pair< int, int > site, int dist, bool dbg)
{
    ITensor N, NId;
    MpoNS op2;
    std::vector< std::complex<double> > ccVal;
    std::pair< int, int > site_op2;

    // Shift site to unit cell
    if(dbg) std::cout << "OP1 -> ["<< site.first <<","<< site.second <<"] => [";
    site.first  = site.first % cd_f.sizeM;
    site.second = site.second % cd_f.sizeN;
    if(dbg) std::cout << site.first <<","<< site.second <<"] = "
        << cls.cToS[site] << std::endl;
    auto op1 = getTOT(o1, cls.cToS[site], 0);
    
    /*
     * Construct the "left" part tensor L
     *  _    __                __
     * |C|--|T |--I(T_u)'     |  |--I(T_u)
     *  |    |            ==> |  |
     * |T|--|O1|--I(XH)'  ==> |L |--I(Xh)
     *  |    |            ==> |  |
     * |C|--|T |--I(T_d)'     |__|--I(T_d)
     *
     */
    auto idOp = getTOT(MPO_Id, cls.cToS[site], 0);

    auto L = (cd_f.C_LU[cd_f.cToS[site]]
        *cd_f.T_L[cd_f.cToS[site]])
        *cd_f.C_LD[cd_f.cToS[site]];
    auto LId = L;
    L.noprime();
    LId.noprime();
    L = ((L * cd_f.T_U[cd_f.cToS[site]] )
        * op1.mpo[0] ) 
        * cd_f.T_D[cd_f.cToS[site]];
    LId = ((LId * cd_f.T_U[cd_f.cToS[site]] )
        * idOp.mpo[0] )
        * cd_f.T_D[cd_f.cToS[site]];
    L.noprime();
    LId.noprime();
    Print(L);
    Print(LId);

    /*
     * Contract L with "dist" copies of a column
     *
     * I(T_u)--|T|--I(T_u)'
     *          |
     *  I(Xh)--|X|--I(Xh)'
     *          |
     * I(T_d)--|T|--I(T_d)'
     *
     */
    for(int i=0;i<dist;i++) {
        // (1) compute correlation at current distance i
        site_op2 = site;
        site_op2.first = site_op2.first + 1;
        if(dbg) std::cout <<"Inserting OP2 T_U--X["<< site_op2.first <<
            ","<< site_op2.second <<"]";
        site_op2.first = site_op2.first % cd_f.sizeM;
        if(dbg) std::cout <<"=>["<< site_op2.first <<","<< site_op2.second <<"] = "
            << cls.cToS[site_op2] <<" --T_D"<< std::endl; 
        op2 = getTOT(o2, cls.cToS[site_op2], 0);
        idOp = getTOT(MPO_Id, cls.cToS[site_op2], 0);

        N = ((L * cd_f.T_U[cd_f.cToS[site_op2]])
            * op2.mpo[0]) 
            * cd_f.T_D[cd_f.cToS[site_op2]];
        NId = ((LId * cd_f.T_U[cd_f.cToS[site_op2]])
            * idOp.mpo[0]) 
            * cd_f.T_D[cd_f.cToS[site_op2]];

        // normalize
        auto sqrtN = std::sqrt(norm(N));
        N   = N / sqrtN;
        NId = NId / sqrtN;

        // construct the right part
        /*    
         * Construct the "right" part tensor R
         *          __    _                 __
         * I(T_u)--|T |--|C|       I(T_u)--|  |
         *          |     |   ==>          |  |
         *  I(Xh)--|O2|--|T|  ==>   I(Xh)--|R |
         *          |     |   ==>          |  |
         * I(T_d)--|T |--|C|       I(T_d)--|__|
         */
        N = ((N * cd_f.C_RU[cd_f.cToS[site_op2]])
            * cd_f.T_R[cd_f.cToS[site_op2]]) 
            * cd_f.C_RD[cd_f.cToS[site_op2]];
        NId = ((NId * cd_f.C_RU[cd_f.cToS[site_op2]])
            * cd_f.T_R[cd_f.cToS[site_op2]]) 
            * cd_f.C_RD[cd_f.cToS[site_op2]];
    
        Print(N);
        Print(NId);

        // Assign value
        ccVal.push_back(sumelsC(N)/sumelsC(NId));

        // (2) Contract with single "transfer" matrix
        site.first = site.first + 1;
        if(dbg) std::cout <<"Inserting T_U--X["<< site.first <<
            ","<< site.second <<"]";
        site.first = site.first % cd_f.sizeM;
        if(dbg) std::cout <<"=>["<< site.first <<","<< site.second <<"] = "
            << cls.cToS[site] <<" --T_D"<< std::endl; 

        idOp = getTOT(MPO_Id, cls.cToS[site], 0);
        
        L = ((L * cd_f.T_U[cd_f.cToS[site]] )
            * idOp.mpo[0] ) 
            * cd_f.T_D[cd_f.cToS[site]];
        LId = ((LId * cd_f.T_U[cd_f.cToS[site]])
            * idOp.mpo[0] )
            * cd_f.T_D[cd_f.cToS[site]];
        L.noprime();
        LId.noprime();
    }

    for(int i=0;i<ccVal.size();i++) {
        std::cout << ccVal[i].real() <<" "<< ccVal[i].imag() << std::endl;
    }

    return ccVal;
}

void EVBuilder::analyseTransferMatrix(std::pair<int,int> const& s0, std::string dir, 
    std::string alg_type) 
{
    if(alg_type=="ARPACK") {
        TransferOpVecProd tvp( (*this), this->cd_f, s0, dir);

        int N = cd_f.auxDimSite * cd_f.auxDimEnv * cd_f.auxDimEnv;
        ARDNS<TransferOpVecProd> ardns(tvp);
        ardns.real_nonsymm_runner(N, 4, 100, 0.0, N * 10);
    } 
    else if (alg_type=="rsvd") {
        std::cout<<"===== Transfer operator RSVD ====="<<std::endl;
        auto cmbX = combiner(prime(cd_f.I_U),prime(cd_f.I_XH),prime(cd_f.I_D));
        auto cmbI = combinedIndex(cmbX);
        //cmbX.prime(cmbI);

        auto X = cd_f.T_U[cd_f.cToS.at(std::make_pair(1,0))];
        X *= cd_f.sites[cd_f.cToS.at(std::make_pair(1,0))];
        X *= cd_f.T_D[cd_f.cToS.at(std::make_pair(1,0))];

        X *= cmbX;
        X.prime();
        Print(X);
        {
            auto X2 = cd_f.T_U[cd_f.cToS.at(std::make_pair(0,0))];
            X2 *= cd_f.sites[cd_f.cToS.at(std::make_pair(0,0))];
            X2 *= cd_f.T_D[cd_f.cToS.at(std::make_pair(0,0))];
            X *= X2;
        }
        Print(X);
        X *= noprime(cmbX);

        auto argsSVDRRt = Args(
            "Cutoff",-1.0,
            "Maxm",3,
            "SVDThreshold",1E-2,
            "SVD_METHOD","rsvd",
            "rsvd_power",2,
            "rsvd_reortho",1
        );
        ITensor U(cmbI), S, V;
        svd_dd(X, U, S, V, argsSVDRRt);
    
        PrintData(S);
    }
    else if (alg_type=="gesdd") {
        std::cout<<"===== Transfer operator GESDD ====="<<std::endl;
        auto cmbX = combiner(prime(cd_f.I_U),prime(cd_f.I_XH),prime(cd_f.I_D));
        auto cmbI = combinedIndex(cmbX);
        //cmbX.prime(cmbI);

        auto X = cd_f.T_U[cd_f.cToS.at(std::make_pair(1,0))];
        X *= cd_f.sites[cd_f.cToS.at(std::make_pair(1,0))];
        X *= cd_f.T_D[cd_f.cToS.at(std::make_pair(1,0))];

        X *= cmbX;
        X.prime();
        Print(X);
        {
            auto X2 = cd_f.T_U[cd_f.cToS.at(std::make_pair(0,0))];
            X2 *= cd_f.sites[cd_f.cToS.at(std::make_pair(0,0))];
            X2 *= cd_f.T_D[cd_f.cToS.at(std::make_pair(0,0))];
            X *= X2;
        }
        Print(X);
        X *= noprime(cmbX);

        auto argsSVDRRt = Args(
            "Cutoff",-1.0,
            "Maxm",3,
            "SVDThreshold",1E-2,
            "SVD_METHOD","gesdd"
        );
        ITensor U(cmbI), S, V;
        svd_dd(X, U, S, V, argsSVDRRt);
    
        PrintData(S);
    }
    else {
        std::cout<<"[EVBuilder::analyseTransferMatrix] Unsupported option: "
            << alg_type <<std::endl;
    }
}

// Diagonal s1, s1+[1,1]
double EVBuilder::eval2x2Diag11(OP_2S op2s, std::pair<int,int> s1, 
        bool DBG) const
{
    return contract2x2Diag11(op2s, s1, DBG)/contract2x2Diag11(OP2S_Id, s1, DBG);
}

double EVBuilder::contract2x2Diag11(OP_2S op2s, std::pair<int,int> s1, 
    bool DBG) const 
{
    if(DBG) std::cout <<"===== EVBuilder::expVal2x2Diag11 called ====="
        << std::string(34,'=') << std::endl;

    //shift to unit cell
    auto e_s1 = std::make_pair(s1.first % cls.sizeM, s1.second % cls.sizeN);
    //diagonal s2 = s1 + [1,1]
    auto e_s2 = std::make_pair((s1.first+1) % cls.sizeM, (s1.second+1) % cls.sizeN);

    // s1 sX
    // sY s2
    auto sX = std::make_pair((s1.first+1) % cls.sizeM, s1.second % cls.sizeN);
    auto sY = std::make_pair(s1.first % cls.sizeM, (s1.second+1) % cls.sizeN);

    if(DBG) { std::cout <<"s1: ["<< e_s1.first <<","<< e_s1.second <<"]"<<std::endl;
    std::cout <<"sX: ["<< sX.first <<","<< sX.second <<"]"<<std::endl;
    std::cout <<"sY: ["<< sY.first <<","<< sY.second <<"]"<<std::endl;
    std::cout <<"s2: ["<< e_s2.first <<","<< e_s2.second <<"]"<<std::endl; }

    // find the index of site given its elem position within cluster
    auto pI1 = noprime(findtype(cls.sites.at(cls.cToS.at(e_s1)), PHYS));
    auto pI2 = noprime(findtype(cls.sites.at(cls.cToS.at(e_s2)), PHYS));

    auto op = get2SiteSpinOP(op2s, pI1, pI2, DBG);

    // build upper left corner
    auto tN = cd_f.C_LU[cd_f.cToS.at(e_s1)] * cd_f.T_U[cd_f.cToS.at(e_s1)] * 
        cd_f.T_L[cd_f.cToS.at(e_s1)];

    // get operator on site s1
    auto mpo1s = getTOT(op.first, cls.cToS.at(e_s1), 0, DBG);
    tN = tN * mpo1s.mpo[0];
    tN.mapprime(ULINK,1,0, HSLINK,1,0);
    if(DBG) { std::cout <<">>>>> 1) Upper Left corner done <<<<<"
            << std::endl;
        Print(tN); }

    // build upper right corner
    auto urc = cd_f.C_RU[cd_f.cToS.at(sX)] * cd_f.T_U[cd_f.cToS.at(sX)] * 
        cd_f.T_R[cd_f.cToS.at(sX)] * cd_f.sites[cd_f.cToS.at(sX)];
    urc.mapprime(VSLINK,1,2);
    if(DBG) Print(urc);

    tN = tN * urc;
    if(DBG) { std::cout <<">>>>> 2) Upper Right corner done <<<<<"
            << std::endl;
        Print(tN); }

    // build lower right corner
    urc = cd_f.C_RD[cd_f.cToS.at(e_s2)] * cd_f.T_R[cd_f.cToS.at(e_s2)] * 
        cd_f.T_D[cd_f.cToS.at(e_s2)];

    // get operator on site s1
    mpo1s = getTOT(op.second, cls.cToS.at(e_s2), 0, DBG);
    urc = urc * mpo1s.mpo[0];
    urc.mapprime(RLINK,0,1, VSLINK,0,2);
    if(DBG) Print(urc);

    tN = tN * urc;
    if(DBG) { std::cout <<">>>>> 3) Down Right corner done <<<<<"
            << std::endl;
        Print(tN); }

    urc = cd_f.C_LD[cd_f.cToS.at(sY)] * cd_f.T_D[cd_f.cToS.at(sY)] * 
        cd_f.T_L[cd_f.cToS.at(sY)] * cd_f.sites[cd_f.cToS.at(sY)];
    urc.mapprime(LLINK,0,1, VSLINK,0,1, DLINK,1,0, HSLINK,1,0);
    if(DBG) Print(urc);

    tN = tN * urc;
    if(DBG) std::cout <<">>>>> 4) Down Left corner done <<<<<"
            << std::endl;

    if(tN.r() > 0) {
        std::cout <<"Unexpected rank r="<< tN.r() << std::endl;
        exit(EXIT_FAILURE);
    }

    if(DBG) std::cout <<"===== EVBuilder::expVal2x2Diag11 done ====="
        << std::string(36,'=') << std::endl;

    return sumels(tN);
}


// Diagonal s1, s1+[1,-1]
// double EVBuilder::eval2x2Diag1N1(OP_2S op2s, std::pair<int,int> s1, 
//         bool DBG) const
// {
//     return contract2x2Diag1N1(op2s, s1, DBG)/contract2x2Diag1N1(OP2S_Id, s1, DBG);
// }

// double EVBuilder::contract2x2Diag1N1(OP_2S op2s, std::pair<int,int> s1, 
//     bool DBG) const 
// {
//     if(DBG) std::cout <<"===== EVBuilder::expVal2x2Diag1N1 called ====="
//         << std::string(34,'=') << std::endl;

//     //shift to unit cell
//     auto e_s1 = std::make_pair(s1.first % cls.sizeM, s1.second % cls.sizeN);
//     //diagonal s2 = s1 + [1,-1]
//     auto e_s2 = std::make_pair((s1.first+1) % cls.sizeM, (s1.second-1+cls.sizeN) % cls.sizeN);

//     // sY s2
//     // s1 sX
//     auto sX = std::make_pair((s1.first+1) % cls.sizeM, s1.second % cls.sizeN);
//     auto sY = std::make_pair(s1.first % cls.sizeM, (s1.second-1+cls.sizeN) % cls.sizeN);

//     if(DBG) { std::cout <<"s1: ["<< e_s1.first <<","<< e_s1.second <<"]"<<std::endl;
//     std::cout <<"sX: ["<< sX.first <<","<< sX.second <<"]"<<std::endl;
//     std::cout <<"sY: ["<< sY.first <<","<< sY.second <<"]"<<std::endl;
//     std::cout <<"s2: ["<< e_s2.first <<","<< e_s2.second <<"]"<<std::endl; }

//     // find the index of site given its elem position within cluster
//     auto pI1 = noprime(findtype(cls.sites.at(cls.cToS.at(e_s1)), PHYS));
//     auto pI2 = noprime(findtype(cls.sites.at(cls.cToS.at(e_s2)), PHYS));

//     auto op = get2SiteSpinOP(op2s, pI1, pI2, DBG);

//     // build lower left corner
//     auto tN = cd_f.C_LD[cd_f.cToS.at(e_s1)] * cd_f.T_D[cd_f.cToS.at(e_s1)] * 
//         cd_f.T_L[cd_f.cToS.at(e_s1)];

//     // get operator on site s1
//     auto mpo1s = getTOT(op.first, cls.cToS.at(e_s1), 0, DBG);
//     tN = tN * mpo1s.mpo[0];
//     tN.mapprime(DLINK,1,0, HSLINK,1,0);
//     if(DBG) { std::cout <<">>>>> 1) Lower Left corner done <<<<<"
//             << std::endl;
//         Print(tN); }

//     // build lower right corner
//     auto urc = cd_f.C_RD[cd_f.cToS.at(sX)] * cd_f.T_D[cd_f.cToS.at(sX)] * 
//         cd_f.T_R[cd_f.cToS.at(sX)] * cd_f.sites[cd_f.cToS.at(sX)];
//     urc.mapprime(VSLINK,0,2);
//     if(DBG) Print(urc);

//     tN = tN * urc;
//     if(DBG) { std::cout <<">>>>> 2) Lower Right corner done <<<<<"
//             << std::endl;
//         Print(tN); }

//     // build upper right corner
//     urc = cd_f.C_RU[cd_f.cToS.at(e_s2)] * cd_f.T_R[cd_f.cToS.at(e_s2)] * 
//         cd_f.T_U[cd_f.cToS.at(e_s2)];

//     // get operator on site s1
//     mpo1s = getTOT(op.second, cls.cToS.at(e_s2), 0, DBG);
//     urc = urc * mpo1s.mpo[0];
//     urc.mapprime(RLINK,1,0, VSLINK,1,2);
//     if(DBG) Print(urc);

//     tN = tN * urc;
//     if(DBG) { std::cout <<">>>>> 3) Upper Right corner done <<<<<"
//             << std::endl;
//         Print(tN); }

//     urc = cd_f.C_LU[cd_f.cToS.at(sY)] * cd_f.T_U[cd_f.cToS.at(sY)] * 
//         cd_f.T_L[cd_f.cToS.at(sY)] * cd_f.sites[cd_f.cToS.at(sY)];
//     urc.mapprime(LLINK,1,0, VSLINK,1,0, ULINK,1,0, HSLINK,1,0);
//     if(DBG) Print(urc);

//     tN = tN * urc;
//     if(DBG) std::cout <<">>>>> 4) Upper Left corner done <<<<<"
//             << std::endl;

//     if(tN.r() > 0) {
//         std::cout <<"Unexpected rank r="<< tN.r() << std::endl;
//         exit(EXIT_FAILURE);
//     }

//     if(DBG) std::cout <<"===== EVBuilder::expVal2x2Diag1N1 done ====="
//         << std::string(36,'=') << std::endl;

//     return sumels(tN);
// }

// Diagonal s1, s1+[-1,-1]
double EVBuilder::eval2x2DiagN11(OP_2S op2s, std::pair<int,int> s1, 
        bool DBG) const
{
    return contract2x2DiagN11(op2s, s1, DBG)/contract2x2DiagN11(OP2S_Id, s1, DBG);
}

double EVBuilder::contract2x2DiagN11(OP_2S op2s, std::pair<int,int> s1, 
    bool DBG) const 
{
    if(DBG) std::cout <<"===== EVBuilder::expVal2x2DiagN1N1 called ====="
        << std::string(34,'=') << std::endl;

    //shift to unit cell
    auto e_s1 = std::make_pair(s1.first % cls.sizeM, s1.second % cls.sizeN);
    //diagonal s2 = s1 + [-1,+1]
    auto e_s2 = std::make_pair((s1.first-1+cls.sizeM) % cls.sizeM, 
        (s1.second-1+cls.sizeN) % cls.sizeN);

    // sX s1
    // s2 sY
    auto sX = std::make_pair((s1.first-1+cls.sizeM) % cls.sizeM, s1.second % cls.sizeN);
    auto sY = std::make_pair(s1.first % cls.sizeM, (s1.second-1+cls.sizeN) % cls.sizeN);

    if(DBG) { std::cout <<"s1: ["<< e_s1.first <<","<< e_s1.second <<"]"<<std::endl;
    std::cout <<"sX: ["<< sX.first <<","<< sX.second <<"]"<<std::endl;
    std::cout <<"sY: ["<< sY.first <<","<< sY.second <<"]"<<std::endl;
    std::cout <<"s2: ["<< e_s2.first <<","<< e_s2.second <<"]"<<std::endl; }

    // find the index of site given its elem position within cluster
    auto pI1 = noprime(findtype(cls.sites.at(cls.cToS.at(e_s1)), PHYS));
    auto pI2 = noprime(findtype(cls.sites.at(cls.cToS.at(e_s2)), PHYS));

    auto op = get2SiteSpinOP(op2s, pI1, pI2, DBG);

    // build upper right corner
    auto tN = cd_f.C_RU[cd_f.cToS.at(e_s1)] * cd_f.T_U[cd_f.cToS.at(e_s1)] * 
        cd_f.T_R[cd_f.cToS.at(e_s1)];

    // get operator on site s1
    auto mpo1s = getTOT(op.first, cls.cToS.at(e_s1), 0, DBG);
    tN = tN * mpo1s.mpo[0];
    tN.mapprime(RLINK,1,0, VSLINK,1,0);
    if(DBG) { std::cout <<">>>>> 1) Upper Right corner done <<<<<"
            << std::endl;
        Print(tN); }

    // build down right corner
    auto urc = cd_f.C_RD[cd_f.cToS.at(sY)] * cd_f.T_R[cd_f.cToS.at(sY)] * 
        cd_f.T_D[cd_f.cToS.at(sY)] * cd_f.sites[cd_f.cToS.at(sY)];
    urc.mapprime(HSLINK,0,1);
    if(DBG) Print(urc);

    tN = tN * urc;
    if(DBG) { std::cout <<">>>>> 2) Lower Right corner done <<<<<"
            << std::endl;
        Print(tN); }

    // build lower left corner
    urc = cd_f.C_LD[cd_f.cToS.at(e_s2)] * cd_f.T_L[cd_f.cToS.at(e_s2)] * 
        cd_f.T_D[cd_f.cToS.at(e_s2)];

    // get operator on site s2
    mpo1s = getTOT(op.second, cls.cToS.at(e_s2), 0, DBG);
    urc = urc * mpo1s.mpo[0];
    urc.mapprime(DLINK,1,0);
    if(DBG) Print(urc);

    tN = tN * urc;
    if(DBG) { std::cout <<">>>>> 3) Lower Left corner done <<<<<"
            << std::endl;
        Print(tN); }

    urc = cd_f.C_LU[cd_f.cToS.at(sX)] * cd_f.T_U[cd_f.cToS.at(sX)] * 
        cd_f.T_L[cd_f.cToS.at(sX)] * cd_f.sites[cd_f.cToS.at(sX)];
    urc.mapprime(LLINK,1,0, VSLINK,1,0, ULINK,1,0, HSLINK,1,0);
    if(DBG) Print(urc);

    tN = tN * urc;
    if(DBG) std::cout <<">>>>> 4) Upper Left corner done <<<<<"
            << std::endl;

    if(tN.r() > 0) {
        std::cout <<"Unexpected rank r="<< tN.r() << std::endl;
        exit(EXIT_FAILURE);
    }

    if(DBG) std::cout <<"===== EVBuilder::expVal2x2DiagN1N1 done ====="
        << std::string(36,'=') << std::endl;

    return sumels(tN);
}



ITensor EVBuilder::getT(ITensor const& s, std::array<Index, 4> const& plToEnv, 
    bool dbg) const
{  

    Index aS(noprime(findtype(s, AUXLINK)));
    Index pS(noprime(findtype(s, PHYS)));

    // build |ket> part
    ITensor res = s;
    res = res * prime(conj(s), AUXLINK, 4);
    if (dbg) Print(res);

    // contract given indices into ENV compatible indices
    auto cmb = combiner(aS,prime(aS,4));
    for (int i=0; i<=3; i++) {
        if(plToEnv[i]) {
            res = res * prime(cmb,i);
            res = res * delta(plToEnv[i],commonIndex(res,prime(cmb,i)));    
            if (dbg) {
                Print(plToEnv[i]);
                Print(res);
            }
        }
    }

    return res;
}

double EVBuilder::contract3Smpo2x2(MPO_3site const& mpo3s,
    std::vector< std::pair<int,int> > siteSeq, bool dbg) const
{

    if(siteSeq.size() != 4) {
        std::cout<<"EVBuilder::contract3Smpo2x2: siteSeq.size() != 4"<<std::endl;
        exit(EXIT_FAILURE);
    }

    std::vector< std::string > tn(4);
    std::vector<int> pl(8);

    for (int i=0; i<4; i++) {
        // shift to unit cell
        auto pos = std::make_pair(
            (siteSeq[i].first + cls.sizeM * std::abs(siteSeq[i].first)) % cls.sizeM,
            (siteSeq[i].second + cls.sizeN * std::abs(siteSeq[i].second)) % cls.sizeN
        );

        tn[i] = cls.cToS.at(pos);

        int j = (i + 4 - 1) % 4; 
        auto sv = std::make_pair(siteSeq[i].first - siteSeq[j].first,
            siteSeq[i].second - siteSeq[j].second); 

        int plI0 = 2*j+1;
        int plI1 = 2*i; 
        if (sv == std::make_pair(1,0)) {
            pl[plI0] = 2;
            pl[plI1] = 0;
        } else if (sv == std::make_pair(-1,0)) {
            pl[plI0] = 0;
            pl[plI1] = 2;
        } else if (sv == std::make_pair(0,1)) {
            pl[plI0] = 3;
            pl[plI1] = 1;
        } else {
            pl[plI0] = 1;
            pl[plI1] = 3;
        }
    }

    return contract3Smpo2x2(mpo3s, tn, pl, dbg);
}

double EVBuilder::contract3Smpo2x2(MPO_3site const& mpo3s,
    std::vector<std::string> tn, std::vector<int> pl, bool dbg) const
{
    int dbgLvl = 3;

    std::chrono::steady_clock::time_point t_begin_int, t_end_int;

    if (dbg) {
        std::cout<<"GATE: ";
        for(int i=0; i<=3; i++) {
            std::cout<<">-"<<pl[2*i]<<"-> "<<tn[i]<<" >-"<<pl[2*i+1]<<"->"; 
        }
        std::cout<< std::endl;
    }

    if(dbg && (dbgLvl >= 2)) {
        std::cout<< mpo3s;
        PrintData(mpo3s.H1);
        PrintData(mpo3s.H2);
        PrintData(mpo3s.H3);
    }

    // ***** SET UP NECESSARY MAPS AND CONSTANT TENSORS ************************

    // map MPOs
    ITensor dummyMPO = ITensor();
    std::array<const ITensor *, 4> mpo({&mpo3s.H1, &mpo3s.H2, &mpo3s.H3, &dummyMPO});

    // find integer identifier of on-site tensors within CtmEnv
    std::vector<int> si;
    for (int i=0; i<=3; i++) {
        si.push_back(std::distance(cls.siteIds.begin(),
                std::find(std::begin(cls.siteIds), 
                    std::end(cls.siteIds), tn[i])));
    }
    if(dbg) {
        std::cout << "siteId -> CtmEnv.sites Index" << std::endl;
        for (int i = 0; i <=3; ++i) { std::cout << tn[i] <<" -> "<< si[i] << std::endl; }
    }

    // read off auxiliary and physical indices of the cluster sites
    std::array<Index, 4> aux({
        noprime(findtype(cls.sites.at(tn[0]), AUXLINK)),
        noprime(findtype(cls.sites.at(tn[1]), AUXLINK)),
        noprime(findtype(cls.sites.at(tn[2]), AUXLINK)),
        noprime(findtype(cls.sites.at(tn[3]), AUXLINK)) });

    std::array<Index, 4> auxRT({ aux[0], aux[1], aux[1], aux[2] });
    std::array<int, 4> plRT({ pl[1], pl[2], pl[3], pl[4] });

    std::array<Index, 4> phys({
        noprime(findtype(cls.sites.at(tn[0]), PHYS)),
        noprime(findtype(cls.sites.at(tn[1]), PHYS)),
        noprime(findtype(cls.sites.at(tn[2]), PHYS)),
        noprime(findtype(cls.sites.at(tn[3]), PHYS)) });

    std::array<Index, 3> opPI({
        noprime(findtype(mpo3s.H1, PHYS)),
        noprime(findtype(mpo3s.H2, PHYS)),
        noprime(findtype(mpo3s.H3, PHYS)) });

    // prepare map from on-site tensor aux-indices to half row/column T
    // environment tensors
    std::array<const std::vector<ITensor> * const, 4> iToT(
        {&cd_f.T_L, &cd_f.T_U, &cd_f.T_R ,&cd_f.T_D});

    // prepare map from on-site tensor aux-indices pair to half corner T-C-T
    // environment tensors
    const std::map<int, const std::vector<ITensor> * const > iToC(
        {{23, &cd_f.C_LU}, {32, &cd_f.C_LU},
        {21, &cd_f.C_LD}, {12, &cd_f.C_LD},
        {3, &cd_f.C_RU}, {30, &cd_f.C_RU},
        {1, &cd_f.C_RD}, {10, &cd_f.C_RD}});

    // for every on-site tensor point from primeLevel(index) to ENV index
    // eg. I_XH or I_XV (with appropriate prime level). 
    std::array< std::array<Index, 4>, 4> iToE; // indexToENVIndex => iToE

    // Find for site 0 through 3 which are connected to ENV
    std::vector<int> plOfSite({0,1,2,3}); // aux-indices (primeLevels) of on-site tensor 

    Index iQA, iQD, iQB;
    ITensor QA, eA(prime(aux[0],pl[1]), phys[0]);
    ITensor QD, eD(prime(aux[2],pl[4]), phys[2]);
    ITensor QB, eB(prime(aux[1],pl[2]), prime(aux[1],pl[3]), phys[1]);
    
    ITensor eRE;
    ITensor deltaBra, deltaKet;

    {
        // precompute 4 (proto)corners of 2x2 environment
        std::vector<ITensor> pc(4);
        for (int s=0; s<=3; s++) {
            // aux-indices connected to sites
            std::vector<int> connected({pl[s*2], pl[s*2+1]});
            // set_difference gives aux-indices connected to ENV
            std::sort(connected.begin(), connected.end());
            std::vector<int> tmp_iToE;
            std::set_difference(plOfSite.begin(), plOfSite.end(), 
                connected.begin(), connected.end(), 
                std::inserter(tmp_iToE, tmp_iToE.begin())); 
            tmp_iToE.push_back(pl[s*2]*10+pl[s*2+1]); // identifier for C ENV tensor
            if(dbg) { 
                std::cout <<"primeLevels (pl) of indices connected to ENV - site: "
                    << tn[s] << std::endl;
                std::cout << tmp_iToE[0] <<" "<< tmp_iToE[1] <<" iToC: "<< tmp_iToE[2] << std::endl;
            }

            // Assign indices by which site is connected to ENV
            if( findtype( (*iToT.at(tmp_iToE[0]))[si[s]], HSLINK ) ) {
                iToE[s][tmp_iToE[0]] = findtype( (*iToT.at(tmp_iToE[0]))[si[s]], HSLINK );
                iToE[s][tmp_iToE[1]] = findtype( (*iToT.at(tmp_iToE[1]))[si[s]], VSLINK );
            } else {
                iToE[s][tmp_iToE[0]] = findtype( (*iToT.at(tmp_iToE[0]))[si[s]], VSLINK );
                iToE[s][tmp_iToE[1]] = findtype( (*iToT.at(tmp_iToE[1]))[si[s]], HSLINK );
            }

            pc[s] = (*iToT.at(tmp_iToE[0]))[si[s]]*(*iToC.at(tmp_iToE[2]))[si[s]]*
                (*iToT.at(tmp_iToE[1]))[si[s]];
            if(dbg) Print(pc[s]);
            // set primeLevel of ENV indices between T's to 0 to be ready for contraction
            pc[s].noprime(LLINK, ULINK, RLINK, DLINK);
        }
        if(dbg) {
            for(int i=0; i<=3; i++) {
                std::cout <<"Site: "<< tn[i] <<" ";
                for (auto const& ind : iToE[i]) if(ind) std::cout<< ind <<" ";
                std::cout << std::endl;
            }
        }
        // ***** SET UP NECESSARY MAPS AND CONSTANT TENSORS DONE ******************* 

        // ***** COMPUTE "EFFECTIVE" REDUCED ENVIRONMENT ***************************
        t_begin_int = std::chrono::steady_clock::now();

        // C  D
        //    |
        // A--B
        // ITensor eRE;
        // ITensor deltaBra, deltaKet;

        // Decompose A tensor on which the gate is applied
        //ITensor QA, tempSA, eA(prime(aux[0],pl[1]), phys[0]);
        ITensor tempSA;
        svd(cls.sites.at(tn[0]), eA, tempSA, QA);
        iQA = Index("auxQA", commonIndex(QA,tempSA).m(), AUXLINK, 0);
        eA = (eA*tempSA) * delta(commonIndex(QA,tempSA), iQA);
        QA *= delta(commonIndex(QA,tempSA), iQA);

        // Prepare corner of A
        ITensor tempC = pc[0] * getT(QA, iToE[0], (dbg && (dbgLvl >= 3)) );
        if(dbg && (dbgLvl >=3)) Print(tempC);

        deltaKet = delta(prime(aux[0],pl[0]), prime(aux[3],pl[7]));
        deltaBra = prime(deltaKet,4);
        tempC = (tempC * deltaBra) * deltaKet;
        if(dbg && (dbgLvl >=3)) Print(tempC);

        eRE = tempC;

        // Prepare corner of C
        tempC = pc[3] * getT(cls.sites.at(tn[3]), iToE[3], (dbg && (dbgLvl >= 3)));
        if(dbg && (dbgLvl >=3)) Print(tempC);
        
        deltaKet = delta(prime(aux[3],pl[6]), prime(aux[2],pl[5]));
        deltaBra = prime(deltaKet,4);
        tempC = (tempC * deltaBra) * deltaKet;
        if(dbg && (dbgLvl >=3)) Print(tempC);

        eRE = eRE * tempC;

        // Decompose D tensor on which the gate is applied
        //ITensor QD, tempSD, eD(prime(aux[2],pl[4]), phys[2]);
        ITensor tempSD;
        svd(cls.sites.at(tn[2]), eD, tempSD, QD);
        iQD = Index("auxQD", commonIndex(QD,tempSD).m(), AUXLINK, 0);
        eD = (eD*tempSD) * delta(commonIndex(QD,tempSD), iQD);
        QD *= delta(commonIndex(QD,tempSD), iQD);

        // Prepare corner of D
        tempC = pc[2] * getT(QD, iToE[2], (dbg && (dbgLvl >= 3)));
        if(dbg && (dbgLvl >=3)) Print(tempC);

        eRE = eRE * tempC;

        // Decompose B tensor on which the gate is applied
        //ITensor QB, tempSB, eB(prime(aux[1],pl[2]), prime(aux[1],pl[3]), phys[1]);
        ITensor tempSB;
        svd(cls.sites.at(tn[1]), eB, tempSB, QB);
        iQB = Index("auxQB", commonIndex(QB,tempSB).m(), AUXLINK, 0);
        eB = (eB*tempSB) * delta(commonIndex(QB,tempSB), iQB);
        QB *= delta(commonIndex(QB,tempSB), iQB);

        tempC = pc[1] * getT(QB, iToE[1], (dbg && (dbgLvl >= 3)));
        if(dbg && (dbgLvl >=3)) Print(tempC);

        eRE = eRE * tempC;

        t_end_int = std::chrono::steady_clock::now();
        if (dbg) std::cout<<"Constructed reduced Env - T: "<< 
            std::chrono::duration_cast<std::chrono::microseconds>(t_end_int - t_begin_int).count()/1000000.0 <<" [sec]"<<std::endl;
        if(dbg && (dbgLvl >=3)) Print(eRE);
        // ***** COMPUTE "EFFECTIVE" REDUCED ENVIRONMENT DONE **********************
    }

    // ***** FORM "PROTO" ENVIRONMENTS FOR M and K ***************************** 
    t_begin_int = std::chrono::steady_clock::now();

    ITensor protoK = (eRE * eA) * delta(prime(aux[0],pl[1]), prime(aux[1],pl[2]));
    protoK = (protoK * eB) * delta(prime(aux[1],pl[3]), prime(aux[2],pl[4]));
    protoK = (protoK * eD);
    if(dbg && (dbgLvl >=3)) Print(protoK);

    auto protoKId = protoK;
    protoK = ( protoK * delta(opPI[0],phys[0]) ) * mpo3s.H1;
    protoK = ( protoK * delta(opPI[1],phys[1]) ) * mpo3s.H2;
    protoK = ( protoK * delta(opPI[2],phys[2]) ) * mpo3s.H3;
    protoK = (( protoK * delta(prime(opPI[0],1),phys[0]) ) * 
        delta(prime(opPI[1],1),phys[1]) ) * delta(prime(opPI[2],1),phys[2]);
    if(dbg && (dbgLvl >=3)) Print(protoK);

    // PROTOK - VARIANT 1
    protoKId *= conj(eA).prime(AUXLINK,4);
    protoKId *= delta(prime(aux[0],pl[1]+4), prime(aux[1],pl[2]+4));
    protoK = protoK * conj(eA).prime(AUXLINK,4);
    protoK *= delta(prime(aux[0],pl[1]+4), prime(aux[1],pl[2]+4));
    if(dbg && (dbgLvl >=3)) Print(protoK);

    protoKId *= conj(eB).prime(AUXLINK,4);
    protoKId *= delta(prime(aux[1],pl[3]+4), prime(aux[2],pl[4]+4));
    protoK = protoK * conj(eB).prime(AUXLINK,4);
    protoK *= delta(prime(aux[1],pl[3]+4), prime(aux[2],pl[4]+4));
    if(dbg && (dbgLvl >=3)) Print(protoK);

    protoKId *= conj(eD).prime(AUXLINK,4);
    protoK = protoK * conj(eD).prime(AUXLINK,4);
    if(dbg && (dbgLvl >=3)) Print(protoK);

    if (rank(protoK) > 0) std::cout<<"ERROR - protoK not a scalar"<<std::endl;
    if (rank(protoKId) > 0) std::cout<<"ERROR - protoKId not a scalar"<<std::endl;
    std::complex<double> ev   = sumelsC(protoK);
    std::complex<double> evId = sumelsC(protoKId);
    if (isComplex(protoK)) {
        std::cout<<"Expectation value is Complex: imag(ev)="<< ev.imag() << std::endl;
    }
    if (isComplex(protoKId)) {
        std::cout<<"evId is Complex: imag(evId)="<< evId.imag() << std::endl;
    }

    t_end_int = std::chrono::steady_clock::now();
    if (dbg) {
        std::cout<<"ev: "<< ev <<" evId: "<< evId << std::endl;
        std::cout<<"EVBuilder::contract3Smpo2x2 - T: "<< 
        std::chrono::duration_cast<std::chrono::microseconds>(t_end_int - t_begin_int).count()/1000000.0 <<" [sec]"<<std::endl;
    }

    return ev.real()/evId.real();
}

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

MPO_3site EVBuilder::get3Smpo(std::string mpo3s, bool DBG) const 
{
    int physDim = 2;
    Index s1 = Index("S1", physDim, PHYS);
    Index s2 = Index("S2", physDim, PHYS);
    Index s3 = Index("S3", physDim, PHYS);
    Index s1p = prime(s1);
    Index s2p = prime(s2);
    Index s3p = prime(s3);

    ITensor h123 = ITensor(s1,s2,s3,s1p,s2p,s3p);
    if (mpo3s == "3SZ") {
        h123 = SU2_getSpinOp(SU2_S_Z, s1) * SU2_getSpinOp(SU2_S_Z, s2) 
            * SU2_getSpinOp(SU2_S_Z, s3);
    } else {
        std::cout << "Invalid MPO selection mpo3s: "<< mpo3s << std::endl;
        exit(EXIT_FAILURE);
    }

    return symmMPO3Sdecomp(h123, s1, s2, s3);
}

std::pair< ITensor, ITensor > EVBuilder::get2SiteSpinOP(OP_2S op2s,
    Index const& sA, Index const& sB, bool dbg) 
{
    /*
     * 2-site operator acts on 2 physical indices
     * 
     *           A      B
     *   <bar|   s'     s'''
     *          _|______|_   
     *         |____OP____|
     *           |      |
     *           s      s''  |ket>    
     *
     */ 
    //auto s0 = findtype(TA.inds(), PHYS);
    auto s0 = sA;
    auto s1 = prime(s0,1);
    //auto s2 = prime(findtype(TB.inds(), PHYS), 2);
    auto s2 = sB;
    auto s3 = prime(s2,1);

    // Assume s0 is different then s2
    if( s0 == s2 ) {
        std::cout <<"On-site PHYS indices sA and sB are identitcal"<< std::endl;
        exit(EXIT_FAILURE);
    }

    // check dimensions of phys indices on TA and TB
    if( s0.m() != s2.m() ) {
        std::cout <<"On-site tensors TA and TB have different dimension of"
            <<" phys index"<< std::endl;
        exit(EXIT_FAILURE);
    }
    int dimS = s0.m();

    auto Op = ITensor(s0, s1, s2, s3);
    switch(op2s) {
        case OP2S_Id: { // Identity operator
            if(dbg) std::cout <<">>>>> 2) Constructing OP2S_Id <<<<<"<< std::endl;  
            for(int i=1;i<=dimS;i++) {
                for(int j=1;j<=dimS;j++){
                    Op.set(s0(i),s2(j),s1(i),s3(j), 1.+0._i);
                }
            }
            break;
        }
        case OP2S_AKLT_S2_H: { // H of AKLT-S2 on square lattice
            if(dbg) std::cout <<">>>>> 2) Constructing OP2S_AKLT-S2-H <<<<<"
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
                            if(dbg) std::cout <<"<"<< mbA <<","<< mbB <<"|"<< m 
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
            if(dbg) std::cout <<">>>>> 2) Constructing OP2S_SS <<<<<"<< std::endl;
    
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
        case OP2S_SZSZ: {
            if(dbg) std::cout <<">>>>> 2) Constructing OP2S_SZSZ <<<<<"<< std::endl;

            Index sBra = Index("sBra", dimS);
            Index sKet = prime(sBra);
            ITensor Sz = getSpinOp(MPO_S_Z, sBra);
            
            double hVal;
            // Loop over <bra| indices
            for(int bA=1;bA<=dimS;bA++) {
            for(int bB=1;bB<=dimS;bB++) {
                // Loop over |ket> indices
                for(int kA=1;kA<=dimS;kA++) {
                for(int kB=1;kB<=dimS;kB++) {
                
                    hVal = Sz.real(sBra(bA),sKet(kA))
                        *Sz.real(sBra(bB),sKet(kB));

                    Op.set(s0(kA),s2(kB),s1(bA),s3(bB),hVal);
                }}
            }}

            break;
        }
        default: {
            if(dbg) std::cout <<"Invalid OP_2S selection"<< std::endl;
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

    if(dbg) std::cout <<">>>>> 3) Performing SVD OP2S -> OpA * S * OpB <<<<<"
        << std::endl;
    svd(Op, OpA, S, OpB, {"Cutoff",1.0e-16});
    
    if(dbg) { Print(OpA);
        PrintData(S);
        Print(OpB); }
    
    //create a lambda function
    //which returns the square of its argument
    auto sqrt_T = [](Real r) { return sqrt(r); };
    S.apply(sqrt_T);

    // Absorb singular values (symmetrically) into OpA, OpB
    OpA = ( OpA*S )*delta(commonIndex(S,OpB), commonIndex(OpA,S));
    OpB = S*OpB;
    
    if(dbg) { std::cout <<">>>>> 4) Absorbing sqrt(S) to both OpA and OpB <<<<<"
        << std::endl;
        PrintData(OpA);
        PrintData(OpB); }

    return std::make_pair(OpA, OpB);
}

// TODO use getSpinOp defined in su2.h to get spin operator
ITensor EVBuilder::getSpinOp(MPO_1S mpo, Index const& s, bool DBG) {

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

    return SU2_getSpinOp(su2o, s, DBG);
}

void EVBuilder::setCluster(Cluster const& new_c) {
    cls = new_c;
}

void EVBuilder::setCtmData(CtmData const& new_cd) {
    cd = new_cd;
}

void EVBuilder::setCtmData_Full(CtmData_Full const& new_cd_f) {
    cd_f = new_cd_f;
}

std::ostream& EVBuilder::print(std::ostream& s) const {
    s << "ExpValBuilder("<< name <<")";
    return s;
}

std::ostream& operator<<(std::ostream& s, EVBuilder const& ev) {
    return ev.print(s);
}