/*

The MIT License (MIT)

Copyright (c) 2017 Tim Warburton, Noel Chalmers, Jesse Chan, Ali Karakus

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include "ellipticTet3D.h"

void ellipticOperator3D(solver_t *solver, dfloat lambda, occa::memory &o_q, occa::memory &o_Aq, const char *options){

  mesh_t *mesh = solver->mesh;

  occaTimerTic(mesh->device,"AxKernel");

  dfloat *sendBuffer = solver->sendBuffer;
  dfloat *recvBuffer = solver->recvBuffer;
  dfloat *gradSendBuffer = solver->gradSendBuffer;
  dfloat *gradRecvBuffer = solver->gradRecvBuffer;

  dfloat alpha = 0., alphaG =0.;
  dlong Nblock = solver->Nblock;
  dfloat *tmp = solver->tmp;
  occa::memory &o_tmp = solver->o_tmp;

  if(strstr(options, "CONTINUOUS")){
    ogs_t *ogs = solver->mesh->ogs;

    //pre-mask
    //if (solver->Nmasked) mesh->maskKernel(solver->Nmasked, solver->o_maskIds, o_q);

    if(solver->allNeumann)
      //solver->innerProductKernel(mesh->Nelements*mesh->Np, solver->o_invDegree,o_q, o_tmp);
      mesh->sumKernel(mesh->Nelements*mesh->Np, o_q, o_tmp);

    if(ogs->NhaloGather) {
      solver->partialAxKernel(solver->NglobalGatherElements, 
                              solver->o_globalGatherElementList,
                              mesh->o_ggeo, 
                              mesh->o_SrrT, mesh->o_SrsT, mesh->o_SrtT, 
                              mesh->o_SsrT, mesh->o_SssT, mesh->o_SstT, 
                              mesh->o_StrT, mesh->o_StsT, mesh->o_SttT, 
                              mesh->o_MM, lambda, 
                              o_q, o_Aq);
      //if (solver->Nmasked) mesh->maskKernel(solver->Nmasked, solver->o_maskIds, o_Aq);
      mesh->device.finish();
      mesh->device.setStream(solver->dataStream);
      mesh->gatherKernel(ogs->NhaloGather, ogs->o_haloGatherOffsets, ogs->o_haloGatherLocalIds, o_Aq, ogs->o_haloGatherTmp);
      ogs->o_haloGatherTmp.asyncCopyTo(ogs->haloGatherTmp);
      mesh->device.setStream(solver->defaultStream);
    }
    if(solver->NlocalGatherElements){
      solver->partialAxKernel(solver->NlocalGatherElements, 
                              solver->o_localGatherElementList,
                              mesh->o_ggeo, 
                              mesh->o_SrrT, mesh->o_SrsT, mesh->o_SrtT, 
                              mesh->o_SsrT, mesh->o_SssT, mesh->o_SstT, 
                              mesh->o_StrT, mesh->o_StsT, mesh->o_SttT, 
                              mesh->o_MM, lambda, 
                              o_q, o_Aq);
      //if (solver->Nmasked) mesh->maskKernel(solver->Nmasked, solver->o_maskIds, o_Aq);
    }
    if(solver->allNeumann) {
      o_tmp.copyTo(tmp);

      for(dlong n=0;n<Nblock;++n)
        alpha += tmp[n];

      MPI_Allreduce(&alpha, &alphaG, 1, MPI_DFLOAT, MPI_SUM, MPI_COMM_WORLD);
      alphaG *= solver->allNeumannPenalty*solver->allNeumannScale*solver->allNeumannScale;
    }

    // finalize gather using local and global contributions
    if(ogs->NnonHaloGather) mesh->gatherScatterKernel(ogs->NnonHaloGather, ogs->o_nonHaloGatherOffsets, ogs->o_nonHaloGatherLocalIds, o_Aq);

    // C0 halo gather-scatter (on data stream)
    if(ogs->NhaloGather) {
      mesh->device.setStream(solver->dataStream);
      mesh->device.finish();

      // MPI based gather scatter using libgs
      gsParallelGatherScatter(ogs->haloGsh, ogs->haloGatherTmp, dfloatString, "add");

      // copy totally gather halo data back from HOST to DEVICE
      ogs->o_haloGatherTmp.asyncCopyFrom(ogs->haloGatherTmp);
    
      // do scatter back to local nodes
      mesh->scatterKernel(ogs->NhaloGather, ogs->o_haloGatherOffsets, ogs->o_haloGatherLocalIds, ogs->o_haloGatherTmp, o_Aq);
      mesh->device.setStream(solver->defaultStream);
    }

    if(solver->allNeumann) {
      mesh->addScalarKernel((mesh->Nelements+mesh->totalHaloPairs)*mesh->Np, alphaG, o_Aq);
      //dfloat one = 1.f;
      //solver->scaledAddKernel(mesh->Nelements*mesh->Np, alphaG, solver->o_invDegree, one, o_Aq);
    }

    mesh->device.finish();    
    mesh->device.setStream(solver->dataStream);
    mesh->device.finish();    
    mesh->device.setStream(solver->defaultStream);

    //post-mask
    if (solver->Nmasked) mesh->maskKernel(solver->Nmasked, solver->o_maskIds, o_Aq);

  } else if(strstr(options, "IPDG")) {
    dlong offset = 0;
    dfloat alpha = 0., alphaG =0.;
    dlong Nblock = solver->Nblock;
    dfloat *tmp = solver->tmp;
    occa::memory &o_tmp = solver->o_tmp;

    ellipticStartHaloExchange3D(solver, o_q, mesh->Np, sendBuffer, recvBuffer);
    solver->partialGradientKernel(mesh->Nelements,
                                  offset,
                                  mesh->o_vgeo,
                                  mesh->o_DrT,
                                  mesh->o_DsT,
                                  mesh->o_DtT,
                                  o_q,
                                  solver->o_grad);
    ellipticInterimHaloExchange3D(solver, o_q, mesh->Np, sendBuffer, recvBuffer);

    //Start the rank 1 augmentation if all BCs are Neumann
    //TODO this could probably be moved inside the Ax kernel for better performance
    if(solver->allNeumann)
      mesh->sumKernel(mesh->Nelements*mesh->Np, o_q, o_tmp);

    if(mesh->NinternalElements) {
      solver->partialIpdgKernel(mesh->NinternalElements,
                                mesh->o_internalElementIds,
                                mesh->o_vmapM,
                                mesh->o_vmapP,
                                lambda,
                                solver->tau,
                                mesh->o_vgeo,
                                mesh->o_sgeo,
                                solver->o_EToB,
                                mesh->o_DrT,
                                mesh->o_DsT,
                                mesh->o_DtT,
                                mesh->o_LIFTT,
                                mesh->o_MM,
                                solver->o_grad,
                                o_Aq);
    }

    if(solver->allNeumann) {
      o_tmp.copyTo(tmp);

      for(dlong n=0;n<Nblock;++n)
        alpha += tmp[n];

      MPI_Allreduce(&alpha, &alphaG, 1, MPI_DFLOAT, MPI_SUM, MPI_COMM_WORLD);
      alphaG *= solver->allNeumannPenalty*solver->allNeumannScale*solver->allNeumannScale;
    }

    ellipticEndHaloExchange3D(solver, o_q, mesh->Np, recvBuffer);

    if(mesh->totalHaloPairs){
      offset = mesh->Nelements;
      solver->partialGradientKernel(mesh->totalHaloPairs,
                                    offset,
                                    mesh->o_vgeo,
                                    mesh->o_DrT,
                                    mesh->o_DsT,
                                    mesh->o_DtT,
                                    o_q,
                                    solver->o_grad);
    }

    if(mesh->NnotInternalElements) {
      solver->partialIpdgKernel(mesh->NnotInternalElements,
                                mesh->o_notInternalElementIds,
                                mesh->o_vmapM,
                                mesh->o_vmapP,
                                lambda,
                                solver->tau,
                                mesh->o_vgeo,
                                mesh->o_sgeo,
                                solver->o_EToB,
                                mesh->o_DrT,
                                mesh->o_DsT,
                                mesh->o_DtT,
                                mesh->o_LIFTT,
                                mesh->o_MM,
                                solver->o_grad,
                                o_Aq);
    }

    if(solver->allNeumann)
      mesh->addScalarKernel(mesh->Nelements*mesh->Np, alphaG, o_Aq);
  } 

  occaTimerToc(mesh->device,"AxKernel");
}

void ellipticScaledAdd(solver_t *solver, dfloat alpha, occa::memory &o_a, dfloat beta, occa::memory &o_b){

  mesh_t *mesh = solver->mesh;

  dlong Ntotal = mesh->Nelements*mesh->Np;

  // b[n] = alpha*a[n] + beta*b[n] n\in [0,Ntotal)
  occaTimerTic(mesh->device,"scaledAddKernel");
  solver->scaledAddKernel(Ntotal, alpha, o_a, beta, o_b);
  occaTimerToc(mesh->device,"scaledAddKernel");

}

dfloat ellipticWeightedInnerProduct(solver_t *solver,
            occa::memory &o_w,
            occa::memory &o_a,
            occa::memory &o_b,
            const char *options){

  mesh_t *mesh = solver->mesh;
  dfloat *tmp = solver->tmp;
  dlong Nblock = solver->Nblock;
  dlong Ntotal = mesh->Nelements*mesh->Np;

  occa::memory &o_tmp = solver->o_tmp;

  occaTimerTic(mesh->device,"weighted inner product2");

  if(strstr(options,"CONTINUOUS"))
    solver->weightedInnerProduct2Kernel(Ntotal, o_w, o_a, o_b, o_tmp);
  else
    solver->innerProductKernel(Ntotal, o_a, o_b, o_tmp);

  occaTimerToc(mesh->device,"weighted inner product2");

  o_tmp.copyTo(tmp);

  dfloat wab = 0;
  for(dlong n=0;n<Nblock;++n){
    wab += tmp[n];
  }

  dfloat globalwab = 0;
  MPI_Allreduce(&wab, &globalwab, 1, MPI_DFLOAT, MPI_SUM, MPI_COMM_WORLD);

  return globalwab;
}

dfloat ellipticInnerProduct(solver_t *solver,
			    occa::memory &o_a,
			    occa::memory &o_b){


  mesh_t *mesh = solver->mesh;
  dfloat *tmp = solver->tmp;
  dlong Nblock = solver->Nblock;
  dlong Ntotal = mesh->Nelements*mesh->Np;

  occa::memory &o_tmp = solver->o_tmp;

  occaTimerTic(mesh->device,"inner product");
  solver->innerProductKernel(Ntotal, o_a, o_b, o_tmp);
  occaTimerTic(mesh->device,"inner product");

  o_tmp.copyTo(tmp);

  dfloat ab = 0;
  for(dlong n=0;n<Nblock;++n){
    ab += tmp[n];
  }

  dfloat globalab = 0;
  MPI_Allreduce(&ab, &globalab, 1, MPI_DFLOAT, MPI_SUM, MPI_COMM_WORLD);

  return globalab;
}

int ellipticSolveTet3D(solver_t *solver, dfloat lambda, dfloat tol, 
                      occa::memory &o_r, occa::memory &o_x, const char *options){

  mesh_t *mesh = solver->mesh;

/*
  dfloat *t = (dfloat *) calloc(mesh->Np*mesh->Nelements,sizeof(dfloat));
  dfloat *Pt = (dfloat *) calloc(mesh->Np*mesh->Nelements,sizeof(dfloat));
  occa::memory o_t = mesh->device.malloc(mesh->Np*mesh->Nelements*sizeof(dfloat),t);
  occa::memory o_Pt = mesh->device.malloc(mesh->Np*mesh->Nelements*sizeof(dfloat),t);
  
  char fname[BUFSIZ];
  sprintf(fname, "Precon.m");
  FILE *fp = fopen(fname, "w");

  fprintf(fp, "P = [");
  agmgLevel **levels = solver->precon->parAlmond->levels;
  for (int n=0;n<mesh->Np*mesh->Nelements;n++) {
    for (int m=0;m<mesh->Np*mesh->Nelements;m++) t[m] =0;
    o_Pt.copyFrom(t);

    t[n] = 1;
    o_t.copyFrom(t);
    
    if (solver->Nmasked) mesh->maskKernel(solver->Nmasked, solver->o_maskIds, o_t);
    ellipticParallelGatherScatterTet3D(mesh, mesh->ogs, o_t, dfloatString, "add");
    //solver->dotMultiplyKernel(mesh->Nelements*mesh->Np, mesh->ogs->o_invDegree, o_t, o_t);

    o_t.copyTo(t);
    dfloat max = 0;
    for (int n=0;n<mesh->Np*mesh->Nelements;n++) max = mymax(max,t[n]);

    if (max>1E-5) ellipticPreconditioner3D(solver, lambda, o_t, o_Pt, options);
    //levels[0]->device_smooth(levels[0]->smoothArgs, o_t, o_Pt, false);
    //ellipticOperator2D(solver, lambda, o_t, o_Pt, options);


    //levels[0]->device_smooth(levels[0]->smoothArgs, o_t, levels[0]->o_x, true);

    // res = rhs - A*x
    //levels[0]->device_Ax(levels[0]->AxArgs,levels[0]->o_x,levels[0]->o_res);
    //dfloat one = 1.0, none = -1.0;
    //solver->scaledAddKernel(mesh->Np*mesh->Nelements, one, o_t, none, levels[0]->o_res);

    //solver->dotMultiplyKernel(mesh->Nelements*mesh->Np, mesh->ogs->o_invDegree, levels[0]->o_res, levels[0]->o_res);
    //levels[1]->device_coarsen(levels[1]->coarsenArgs, levels[0]->o_res, levels[1]->o_rhs);
    //levels[0]->device_gather (levels[0]->gatherArgs,  o_t, levels[0]->o_rhs);
    //levels[1]->device_smooth(levels[1]->smoothArgs, levels[1]->o_rhs, levels[1]->o_x, true);
    //levels[1]->o_x.copyFrom(levels[1]->o_rhs);
    //levels[0]->device_scatter(levels[0]->scatterArgs,  levels[0]->o_rhs, o_Pt);
    //levels[1]->device_prolongate(levels[1]->prolongateArgs, levels[1]->o_x, o_Pt);
    //solver->dotMultiplyKernel(mesh->Nelements*mesh->Np, mesh->ogs->o_invDegree, o_Pt, o_Pt);
    //levels[0]->device_smooth(levels[0]->smoothArgs, o_t, o_Pt, false);

    //solver->dotMultiplyKernel(mesh->Nelements*mesh->Np, mesh->ogs->o_invDegree, o_Pt, o_Pt);
    //ellipticParallelGatherScatterTri2D(mesh, mesh->ogs, o_Pt, dfloatString, "add");
    //if (solver->Nmasked) mesh->maskKernel(solver->Nmasked, solver->o_maskIds, o_Pt);

    //if (solver->Nmasked) mesh->maskKernel(solver->Nmasked, solver->o_maskIds, o_Pt);
    //o_Pt.copyFrom(o_t);

    o_Pt.copyTo(Pt);

    for (int m=0;m<mesh->Np*mesh->Nelements;m++) 
      fprintf(fp, "%.10e,", Pt[m]);

    fprintf(fp, "\n");
  }
  fprintf(fp, "];\n");
  fclose(fp);
*/

  int Niter =0;
  int maxIter = 5000; 

  double start =0.0, end =0.0;

  if(strstr(options,"VERBOSE")){
    mesh->device.finish();
    start = MPI_Wtime(); 
  }

  occaTimerTic(mesh->device,"Linear Solve");
  Niter = pcg(solver, options, lambda, o_r, o_x, tol, maxIter);
  occaTimerToc(mesh->device,"Linear Solve");

  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if(strstr(options,"VERBOSE")){
    mesh->device.finish();
    end = MPI_Wtime();
    double localElapsed = end-start;

    occa::printTimer();

    if(rank==0) printf("Solver converged in %d iters \n", Niter );

    int size;
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    hlong   localDofs = (hlong) mesh->Np*mesh->Nelements;
    hlong   localElements = (hlong) mesh->Nelements;
    double globalElapsed;
    hlong   globalDofs;
    hlong   globalElements;

    MPI_Reduce(&localElapsed, &globalElapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD );
    MPI_Reduce(&localDofs,    &globalDofs,    1, MPI_HLONG,   MPI_SUM, 0, MPI_COMM_WORLD );
    MPI_Reduce(&localElements,&globalElements,1, MPI_HLONG,   MPI_SUM, 0, MPI_COMM_WORLD );

    if (rank==0){
      printf("%02d %02d "hlongFormat" "hlongFormat" %d %17.15lg %3.5g \t [ RANKS N NELEMENTS DOFS ITERATIONS ELAPSEDTIME PRECONMEMORY] \n",
             size, mesh->N, globalElements, globalDofs, Niter, globalElapsed, solver->precon->preconBytes/(1E9));
    }
  }
  return Niter;
}