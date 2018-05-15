#include "bns.h"

void bnsRun(bns_t *bns, setupAide &options){

  mesh_t  *mesh = bns->mesh; 
    
  // MPI send buffer
  dfloat *sendBuffer;
  dfloat *recvBuffer;
  int haloBytes;

  if(options.compareArgs("TIME INTEGRATOR","MRSAAB"))
    haloBytes = mesh->totalHaloPairs*mesh->Nfp*bns->Nfields*mesh->Nfaces*sizeof(dfloat);
  else
    haloBytes = mesh->totalHaloPairs*mesh->Np*bns->Nfields*sizeof(dfloat);
  
   


  if (haloBytes) {
    occa::memory o_sendBufferPinned = mesh->device.mappedAlloc(haloBytes, NULL);
    occa::memory o_recvBufferPinned = mesh->device.mappedAlloc(haloBytes, NULL);
    sendBuffer = (dfloat*) o_sendBufferPinned.getMappedPointer();
    recvBuffer = (dfloat*) o_recvBufferPinned.getMappedPointer();
  }

  


  if(options.compareArgs("TIME INTEGRATOR","MRSAAB")){
  printf("Populating trace values\n");
  // Populate Trace Buffer
  dlong offset = mesh->Np*mesh->Nelements*bns->Nfields;
  for (int l=0; l<mesh->MRABNlevels; l++) {  
    const int id = 3*mesh->MRABNlevels*3 + 3*l;
    if (mesh->MRABNelements[l])
    bns->traceUpdateKernel(mesh->MRABNelements[l],
                      mesh->o_MRABelementIds[l],
                      offset,
                      mesh->MRABshiftIndex[l],
                      bns->MRSAAB_C[l-1], //
                      bns->MRAB_B[id+0], //
                      bns->MRAB_B[id+1],
                      bns->MRAB_B[id+2], //
                      bns->MRSAAB_B[id+0], //
                      bns->MRSAAB_B[id+1],
                      bns->MRSAAB_B[id+2],
                      mesh->o_vmapM,
                      bns->o_q,
                      bns->o_rhsq,
                      bns->o_fQM);

  if (mesh->MRABpmlNelements[l])
    bns->traceUpdateKernel(mesh->MRABpmlNelements[l],
                        mesh->o_MRABpmlElementIds[l],
                        offset,
                        mesh->MRABshiftIndex[l],
                        bns->MRSAAB_C[l-1], //
                        bns->MRAB_B[id+0], //
                        bns->MRAB_B[id+1],
                        bns->MRAB_B[id+2], //
                        bns->MRSAAB_B[id+0], //
                        bns->MRSAAB_B[id+1],
                        bns->MRSAAB_B[id+2],
                        mesh->o_vmapM,
                        bns->o_q,
                        bns->o_rhsq,
                        bns->o_fQM);

  }

  }

printf("N: %d Nsteps: %d dt: %.5e \n", mesh->N, bns->NtimeSteps, bns->dt);




double tic_tot = 0.f, elp_tot = 0.f; 
double tic_sol = 0.f, elp_sol = 0.f; 
double tic_out = 0.f, elp_out = 0.f;



occa::initTimer(mesh->device);
occaTimerTic(mesh->device,"BOLTZMANN");

tic_tot = MPI_Wtime();


if( bns->fixed_dt ){
 for(int tstep=0;tstep<bns->NtimeSteps;++tstep){
      
   // for(int tstep=0;tstep<10;++tstep){
      tic_out = MPI_Wtime();

      if(bns->reportFlag){
        if((tstep%bns->reportStep)==0){
          bnsReport(bns, tstep, options);
        }
      }

       if(bns->errorFlag){
        if((tstep%bns->errorStep)==0){
         bnsError(bns, tstep, options);
        }
      }
  

      elp_out += (MPI_Wtime() - tic_out);
      tic_sol = MPI_Wtime();

     
      if(options.compareArgs("TIME INTEGRATOR", "MRSAAB")){
        occaTimerTic(mesh->device, "MRSAAB"); 
         bnsMRSAABStep(bns, tstep, haloBytes, sendBuffer, recvBuffer, options);
        occaTimerToc(mesh->device, "MRSAAB"); 
      }

      if(options.compareArgs("TIME INTEGRATOR","LSERK")){
        occaTimerTic(mesh->device, "LSERK");  
        bnsLSERKStep(bns, tstep, haloBytes, sendBuffer, recvBuffer, options);
        occaTimerToc(mesh->device, "LSERK");  
      }

      if(options.compareArgs("TIME INTEGRATOR","SARK")){
        occaTimerTic(mesh->device, "SARK");
        dfloat time = tstep*bns->dt;  
        bnsSARKStep(bns, time, haloBytes, sendBuffer, recvBuffer, options);
        bns->o_q.copyFrom(bns->o_rkq);
        bns->o_pmlqx.copyFrom(bns->o_rkqx);
        bns->o_pmlqy.copyFrom(bns->o_rkqy);
        if(bns->dim==3)
          bns->o_pmlqz.copyFrom(bns->o_rkqz);

        occaTimerToc(mesh->device, "SARK");  
      }

      elp_sol += (MPI_Wtime() - tic_sol);
  }
}

 


  elp_tot += (MPI_Wtime() - tic_tot);    
  // occaTimerToc(mesh->device, "BOLTZMANN");

  // compute maximum over all processes
  double gelp_tot  = 0.f, gelp_sol = 0.f, gelp_out = 0.f;

  MPI_Allreduce(&elp_tot, &gelp_tot, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  MPI_Allreduce(&elp_out, &gelp_out, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  MPI_Allreduce(&elp_sol, &gelp_sol, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  
  
  int rank, size; 
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  if(rank==0){
    printf("ORDER\tSIZE\tTOTAL_TIME\tSOLVER_TIME\tOUTPUT TIME\n");
    printf("%2d %2d %.5e %.5e %.5e\n", mesh->N, size, gelp_tot, gelp_sol, gelp_out); 
  }



 
printf("writing Final data\n");  
// For Final Time
//bnsReport(bns, bns->NtimeSteps,options);

occa::printTimer();
}

