/**CFile****************************************************************

  FileName    [abcMfs.c]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [Network and node package.]

  Synopsis    [Optimization with don't-cares.]

  Author      [Alan Mishchenko]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - June 20, 2005.]

  Revision    [$Id: abcMfs.c,v 1.00 2005/06/20 00:00:00 alanmi Exp $]

***********************************************************************/

#include "base/abc/abc.h"
#include "bool/kit/kit.h"
#include "opt/sfm/sfm.h"

ABC_NAMESPACE_IMPL_START


////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////
 
////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

/**Function*************************************************************

  Synopsis    [Unrolls logic network while dropping some next-state functions.]

  Description [Returns the unrolled network.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Abc_Ntk_t * Abc_NtkUnrollAndDrop( Abc_Ntk_t * p, int nFrames, Vec_Int_t * vFlops, int * piPivot )
{
    Abc_Ntk_t * pNew; 
    Abc_Obj_t * pFanin, * pNode;
    Vec_Ptr_t * vNodes;
    int i, k, f, Value;
    assert( Abc_NtkIsLogic(p) );
    assert( Vec_IntSize(vFlops) == Abc_NtkLatchNum(p) );
    *piPivot = -1;
    // start the network
    pNew = Abc_NtkAlloc( p->ntkType, p->ntkFunc, 1 );
    pNew->pName = Extra_UtilStrsav(Abc_NtkName(p));
    // add CIs for the new network
    Abc_NtkForEachCi( p, pNode, i )
        pNode->pCopy = Abc_NtkCreatePi( pNew );
    // iterate unrolling
    vNodes = Abc_NtkDfs( p, 0 );
    for ( f = 0; f < nFrames; f++ )
    {
        if ( f > 0 )
        {
            Abc_NtkForEachPi( p, pNode, i )
                pNode->pCopy = Abc_NtkCreatePi( pNew );
        }
        Vec_PtrForEachEntry( Abc_Obj_t *, vNodes, pNode, i )
        {
            Abc_NtkDupObj( pNew, pNode, 0 );
            Abc_ObjForEachFanin( pNode, pFanin, k )
                Abc_ObjAddFanin( pNode->pCopy, pFanin->pCopy );
        }
        Abc_NtkForEachCo( p, pNode, i )
            pNode->pCopy = Abc_ObjFanin0(pNode)->pCopy;
        Abc_NtkForEachPo( p, pNode, i )
            Abc_ObjAddFanin( Abc_NtkCreatePo(pNew), pNode->pCopy );
        // add buffers
        if ( f == 0 )
        {
            *piPivot = Abc_NtkObjNum(pNew);
//            Abc_NtkForEachCo( p, pNode, i )
//                pNode->pCopy = Abc_NtkCreateNodeBuf( pNew, pNode->pCopy );
        }
        // transfer to flop outputs
        Abc_NtkForEachLatch( p, pNode, i )
            Abc_ObjFanout0(pNode)->pCopy = Abc_ObjFanin0(pNode)->pCopy;
    }
    Vec_PtrFree( vNodes );
    // add final POs
    Vec_IntForEachEntry( vFlops, Value, i )
    {
        if ( Value == 0 )
            continue;
        pNode = Abc_NtkCo( p, Abc_NtkPoNum(p) + i );
        Abc_ObjAddFanin( Abc_NtkCreatePo(pNew), pNode->pCopy );
    }
    Abc_NtkAddDummyPiNames( pNew );
    Abc_NtkAddDummyPoNames( pNew );
    // perform combinational cleanup
    Abc_NtkCleanup( pNew, 0 );
    if ( !Abc_NtkCheck( pNew ) )
        fprintf( stdout, "Abc_NtkCreateFromNode(): Network check has failed.\n" );
    return pNew;
}

/**Function*************************************************************

  Synopsis    [Updates the original network to include optimized nodes.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_NtkReinsertNodes( Abc_Ntk_t * p, Abc_Ntk_t * pNtk, int iPivot )
{
    Abc_Obj_t * pNode, * pNodeNew, * pFaninNew;
    Vec_Ptr_t * vNodes;
    int i, k;
    assert( Abc_NtkIsLogic(p) );
    assert( Abc_NtkCiNum(p) <= Abc_NtkCiNum(pNtk) );
    vNodes = Abc_NtkDfs( p, 0 );
    // clean old network
    Abc_NtkCleanCopy( p );
    Abc_NtkForEachNode( p, pNode, i )
    {
        Abc_ObjRemoveFanins( pNode );
        pNode->pData = Abc_SopRegister( (Mem_Flex_t *)p->pManFunc, (char *)" 0\n" );
    }
    // map CIs
    Abc_NtkForEachCi( p, pNode, i )
        Abc_NtkCi(pNtk, i)->pCopy = pNode;
    // map internal nodes
    assert( Vec_PtrSize(vNodes) + Abc_NtkCiNum(p) + Abc_NtkPoNum(p) == iPivot );
    Vec_PtrForEachEntry( Abc_Obj_t *, vNodes, pNode, i )
    {
        pNodeNew = Abc_NtkObj( pNtk, Abc_NtkCiNum(p) + i );
        if ( pNodeNew == NULL )
            continue;
        pNodeNew->pCopy = pNode;
    }
    // connect internal nodes
    Vec_PtrForEachEntry( Abc_Obj_t *, vNodes, pNode, i )
    {
        pNodeNew = Abc_NtkObj( pNtk, Abc_NtkCiNum(p) + i );
        if ( pNodeNew == NULL )
            continue;
        assert( pNodeNew->pCopy == pNode );
        Abc_ObjForEachFanin( pNodeNew, pFaninNew, k )
            Abc_ObjAddFanin( pNodeNew->pCopy, pFaninNew->pCopy );
        pNode->pData = Abc_SopRegister( (Mem_Flex_t *)p->pManFunc, (char *)pNodeNew->pData );
    }
    Vec_PtrFree( vNodes );
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Vec_Ptr_t * Abc_NtkAssignIDs( Abc_Ntk_t * pNtk )
{
    Vec_Ptr_t * vNodes;
    Abc_Obj_t * pObj;
    int i;
    vNodes = Abc_NtkDfs( pNtk, 0 );
    Abc_NtkCleanCopy( pNtk );
    Abc_NtkForEachCi( pNtk, pObj, i )
        pObj->iTemp = i;
    Vec_PtrForEachEntry( Abc_Obj_t *, vNodes, pObj, i )
    {
        pObj->iTemp = Abc_NtkCiNum(pNtk) + i;
//printf( "%d->%d ", pObj->Id, pObj->iTemp );
    }
//printf( "\n" );
    Abc_NtkForEachCo( pNtk, pObj, i )
        pObj->iTemp = Abc_NtkCiNum(pNtk) + Vec_PtrSize(vNodes) + i;
    return vNodes;
}
Vec_Ptr_t * Abc_NtkAssignIDs2( Abc_Ntk_t * pNtk )
{
    Vec_Ptr_t * vNodes;
    Abc_Obj_t * pObj;
    int i;
    Abc_NtkCleanCopy( pNtk );
    Abc_NtkForEachCi( pNtk, pObj, i )
        pObj->iTemp = i;
    vNodes = Vec_PtrAlloc( Abc_NtkNodeNum(pNtk) );
    Abc_NtkForEachNode( pNtk, pObj, i )
    {
        pObj->iTemp = Abc_NtkCiNum(pNtk) + Vec_PtrSize(vNodes);
        Vec_PtrPush( vNodes, pObj );
    }
    assert( Vec_PtrSize(vNodes) == Abc_NtkNodeNum(pNtk) );
    Abc_NtkForEachCo( pNtk, pObj, i )
        pObj->iTemp = Abc_NtkCiNum(pNtk) + Vec_PtrSize(vNodes) + i;
    return vNodes;
}

/**Function*************************************************************

  Synopsis    [Extracts information about the network.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Sfm_Ntk_t * Abc_NtkExtractMfs( Abc_Ntk_t * pNtk, int nFirstFixed )
{
    Vec_Ptr_t * vNodes;
    Vec_Wec_t * vFanins;
    Vec_Str_t * vFixed;
    Vec_Wrd_t * vTruths;
    Vec_Int_t * vArray;
    Abc_Obj_t * pObj, * pFanin;
    int i, k, nObjs;
    vNodes  = nFirstFixed ? Abc_NtkAssignIDs2(pNtk) : Abc_NtkAssignIDs(pNtk);
    nObjs   = Abc_NtkCiNum(pNtk) + Vec_PtrSize(vNodes) + Abc_NtkCoNum(pNtk);
    vFanins = Vec_WecStart( nObjs );
    vFixed  = Vec_StrStart( nObjs );
    vTruths = Vec_WrdStart( nObjs );
    Vec_PtrForEachEntry( Abc_Obj_t *, vNodes, pObj, i )
    {
        word uTruth = Abc_SopToTruth((char *)pObj->pData, Abc_ObjFaninNum(pObj));
        Vec_WrdWriteEntry( vTruths, pObj->iTemp, uTruth );
        if ( uTruth == 0 || ~uTruth == 0 )
            continue;
        vArray = Vec_WecEntry( vFanins, pObj->iTemp );
        Vec_IntGrow( vArray, Abc_ObjFaninNum(pObj) );
        Abc_ObjForEachFanin( pObj, pFanin, k )
            Vec_IntPush( vArray, pFanin->iTemp );
    }
    Abc_NtkForEachCo( pNtk, pObj, i )
    {
        vArray = Vec_WecEntry( vFanins, pObj->iTemp );
        Vec_IntGrow( vArray, Abc_ObjFaninNum(pObj) );
        Abc_ObjForEachFanin( pObj, pFanin, k )
            Vec_IntPush( vArray, pFanin->iTemp );
    }
    Vec_PtrFree( vNodes );
    for ( i = Abc_NtkCiNum(pNtk); i < Abc_NtkCiNum(pNtk) + nFirstFixed; i++ )
        Vec_StrWriteEntry( vFixed, i, (char)1 );
    // update fixed
    assert( nFirstFixed >= 0 && nFirstFixed < Abc_NtkNodeNum(pNtk) );
//    for ( i = Abc_NtkCiNum(pNtk); i + Abc_NtkCoNum(pNtk) < Abc_NtkObjNum(pNtk); i++ )
//        if ( rand() % 10 == 0 )
//            Vec_StrWriteEntry( vFixed, i, (char)1 );
    return Sfm_NtkConstruct( vFanins, Abc_NtkCiNum(pNtk), Abc_NtkCoNum(pNtk), vFixed, NULL, vTruths );
}
Sfm_Ntk_t * Abc_NtkExtractMfs2( Abc_Ntk_t * pNtk, int iPivot )
{
    Vec_Ptr_t * vNodes;
    Vec_Wec_t * vFanins;
    Vec_Str_t * vFixed;
    Vec_Wrd_t * vTruths;
    Vec_Int_t * vArray;
    Abc_Obj_t * pObj, * pFanin;
    int i, k, nObjs;
    vNodes  = Abc_NtkAssignIDs2(pNtk);
    nObjs   = Abc_NtkCiNum(pNtk) + Vec_PtrSize(vNodes) + Abc_NtkCoNum(pNtk);
    vFanins = Vec_WecStart( nObjs );
    vFixed  = Vec_StrStart( nObjs );
    vTruths = Vec_WrdStart( nObjs );
    Vec_PtrForEachEntry( Abc_Obj_t *, vNodes, pObj, i )
    {
        word uTruth = Abc_SopToTruth((char *)pObj->pData, Abc_ObjFaninNum(pObj));
        Vec_WrdWriteEntry( vTruths, pObj->iTemp, uTruth );
        if ( uTruth == 0 || ~uTruth == 0 )
            continue;
        vArray = Vec_WecEntry( vFanins, pObj->iTemp );
        Vec_IntGrow( vArray, Abc_ObjFaninNum(pObj) );
        Abc_ObjForEachFanin( pObj, pFanin, k )
            Vec_IntPush( vArray, pFanin->iTemp );
    }
    Abc_NtkForEachCo( pNtk, pObj, i )
    {
        vArray = Vec_WecEntry( vFanins, pObj->iTemp );
        Vec_IntGrow( vArray, Abc_ObjFaninNum(pObj) );
        Abc_ObjForEachFanin( pObj, pFanin, k )
            Vec_IntPush( vArray, pFanin->iTemp );
    }
    Vec_PtrFree( vNodes );
    // set fixed attributes
    Abc_NtkForEachNode( pNtk, pObj, i )
        if ( i >= iPivot )
            Vec_StrWriteEntry( vFixed, pObj->iTemp, (char)1 );
    return Sfm_NtkConstruct( vFanins, Abc_NtkCiNum(pNtk), Abc_NtkCoNum(pNtk), vFixed, NULL, vTruths );
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_NtkInsertMfs( Abc_Ntk_t * pNtk, Sfm_Ntk_t * p )
{
    Vec_Int_t * vCover, * vMap, * vArray;
    Abc_Obj_t * pNode;
    word * pTruth;
    int i, k, Fanin;
    // map new IDs into old nodes
    vMap = Vec_IntStart( Abc_NtkObjNumMax(pNtk) );
    Abc_NtkForEachCi( pNtk, pNode, i )
        Vec_IntWriteEntry( vMap, pNode->iTemp, Abc_ObjId(pNode) );
    Abc_NtkForEachNode( pNtk, pNode, i )
        if ( pNode->iTemp > 0 )
            Vec_IntWriteEntry( vMap, pNode->iTemp, Abc_ObjId(pNode) );
    // remove old fanins
    Abc_NtkForEachNode( pNtk, pNode, i )
        if ( !Sfm_NodeReadFixed(p, pNode->iTemp) )
            Abc_ObjRemoveFanins( pNode );
    // create new fanins
    vCover = Vec_IntAlloc( 1 << 16 );
    Abc_NtkForEachNode( pNtk, pNode, i )
    {
        if ( pNode->iTemp == 0 || Sfm_NodeReadFixed(p, pNode->iTemp) )
            continue;
        if ( !Sfm_NodeReadUsed(p, pNode->iTemp) )
        {
            Abc_NtkDeleteObj( pNode );
            continue;
        }
        // update fanins
        vArray = Sfm_NodeReadFanins( p, pNode->iTemp );
        Vec_IntForEachEntry( vArray, Fanin, k )
            Abc_ObjAddFanin( pNode, Abc_NtkObj(pNtk, Vec_IntEntry(vMap, Fanin)) );
        // update function
        pTruth = Sfm_NodeReadTruth( p, pNode->iTemp );
        if ( pTruth[0] == 0 )
            pNode->pData = Abc_SopRegister( (Mem_Flex_t *)pNtk->pManFunc, " 0\n" );
        else if ( ~pTruth[0] == 0 )
            pNode->pData = Abc_SopRegister( (Mem_Flex_t *)pNtk->pManFunc, " 1\n" );
        else
        {
            int RetValue = Kit_TruthIsop( (unsigned *)pTruth, Vec_IntSize(vArray), vCover, 1 );
            assert( Vec_IntSize(vArray) > 0 );
            assert( RetValue == 0 || RetValue == 1 );
            pNode->pData = Abc_SopCreateFromIsop( (Mem_Flex_t *)pNtk->pManFunc, Vec_IntSize(vArray), vCover );
            if ( RetValue )
                Abc_SopComplement( (char *)pNode->pData );
        }
        assert( Abc_SopGetVarNum((char *)pNode->pData) == Vec_IntSize(vArray) );
    }
    Vec_IntFree( vCover );
    Vec_IntFree( vMap );
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Abc_NtkPerformMfs( Abc_Ntk_t * pNtk, Sfm_Par_t * pPars )
{
    Sfm_Ntk_t * p;
    int nFaninMax, nNodes;
    assert( Abc_NtkIsLogic(pNtk) );
    // count fanouts
    nFaninMax = Abc_NtkGetFaninMax( pNtk );
    if ( nFaninMax > 6 )
    {
        Abc_Print( 1, "Currently \"mfs\" cannot process the network containing nodes with more than 6 fanins.\n" );
        return 0;
    }
    if ( !Abc_NtkHasSop(pNtk) )
        Abc_NtkToSop( pNtk, 0 );
    // collect information
    p = Abc_NtkExtractMfs( pNtk, pPars->nFirstFixed );
    // perform optimization
    nNodes = Sfm_NtkPerform( p, pPars );
    // call the fast extract procedure
    if ( nNodes == 0 )
    {
//        Abc_Print( 1, "The network is not changed by \"mfs\".\n" );
    }
    else
    {
        Abc_NtkInsertMfs( pNtk, p );
        if( pPars->fVerbose )
            Abc_Print( 1, "The network has %d nodes changed by \"mfs\".\n", nNodes );
    }
    Sfm_NtkFree( p );
    return 1;
}


/**Function*************************************************************

  Synopsis    [Performs MFS for the unrolled network.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Abc_NtkMfsAfterICheck( Abc_Ntk_t * p, int nFrames, Vec_Int_t * vFlops, Sfm_Par_t * pPars )
{
    Sfm_Ntk_t * pp;
    int nFaninMax, nNodes;
    Abc_Ntk_t * pNtk;
    int iPivot;
    assert( Abc_NtkIsLogic(p) );
    // count fanouts
    nFaninMax = Abc_NtkGetFaninMax( p );
    if ( nFaninMax > 6 )
    {
        Abc_Print( 1, "Currently \"mfs\" cannot process the network containing nodes with more than 6 fanins.\n" );
        return 0;
    }
    if ( !Abc_NtkHasSop(p) )
        Abc_NtkToSop( p, 0 );
    // derive unfolded network
    pNtk = Abc_NtkUnrollAndDrop( p, nFrames, vFlops, &iPivot );
    // collect information
    pp = Abc_NtkExtractMfs2( pNtk, iPivot );
    // perform optimization
    nNodes = Sfm_NtkPerform( pp, pPars );
    // call the fast extract procedure
    if ( nNodes == 0 )
    {
//        Abc_Print( 1, "The network is not changed by \"mfs\".\n" );
    }
    else
    {
        Abc_NtkInsertMfs( pNtk, pp );
        if( pPars->fVerbose )
            Abc_Print( 1, "The network has %d nodes changed by \"mfs\".\n", nNodes );
        Abc_NtkReinsertNodes( p, pNtk, iPivot );
    }
    Abc_NtkDelete( pNtk );
    Sfm_NtkFree( pp );
    return 1;

}


////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////


ABC_NAMESPACE_IMPL_END

