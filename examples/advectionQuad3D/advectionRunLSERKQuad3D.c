#include "advectionQuad3D.h"

void advectionRunLSERKQuad3D(solver_t *solver){

  mesh_t *mesh = solver->mesh;

  // some sanity checks
  for(int e=0;e<mesh->Nelements;++e){
    for(int n=0;n<mesh->Np;++n){
      dfloat J = mesh->vgeo[e*mesh->Np*mesh->Nvgeo + JID*mesh->Np + n];
      if(J<1e-10)
	printf("J = %\g", J);
    }

    int isInternal = 1;
    int cubeFace = mesh->cubeFaceNumber[e];

    for(int f=0;f<mesh->Nfaces;++f){
      int cubeFaceNeighbor = mesh->cubeFaceNumber[mesh->EToE[mesh->Nfaces*e+f]];
      if(cubeFace!=cubeFaceNeighbor)
	isInternal = 0;
    }
    
    if(isInternal){
      printf("EToF[%d,*] = [", e);
      for(int f=0;f<mesh->Nfaces;++f){
	printf("%d ", mesh->EToF[mesh->Nfaces*e+f]);
      }
      printf("]\n");
    }
  }
  

  
  // MPI send buffer
  iint haloBytes = mesh->totalHaloPairs*mesh->Np*mesh->Nfields*sizeof(dfloat);
  dfloat *sendBuffer = (dfloat*) malloc(haloBytes);
  dfloat *recvBuffer = (dfloat*) malloc(haloBytes);

  dfloat * test_q = (dfloat *) calloc(mesh->Nelements*mesh->Np,sizeof(dfloat));
    
  //kernel arguments
  dfloat alpha = 1./mesh->N;

  mesh->o_qpre.copyTo(mesh->q);
  advectionErrorNormQuad3D(mesh, 0, "icPreFilter", 0);
  
  mesh->filterKernelq0H(mesh->Nelements,
			alpha,
			mesh->o_dualProjMatrix,
			mesh->o_cubeFaceNumber,
			mesh->o_EToE,
			mesh->o_x,
			mesh->o_y,
			mesh->o_z,
			mesh->o_qpre,
			mesh->o_qPreFilter);

  mesh->o_qPreFilter.copyTo(mesh->q);
  advectionErrorNormQuad3D(mesh, 0, "icMiddleFilter", 0);
  
  mesh->filterKernelq0V(mesh->Nelements,
			alpha,
			mesh->o_dualProjMatrix,
			mesh->o_cubeFaceNumber,
			mesh->o_EToE,
			mesh->o_x,
			mesh->o_y,
			mesh->o_z,
			mesh->o_qPreFilter,
			mesh->o_qpre);

  mesh->o_qpre.copyTo(mesh->q);
  advectionErrorNormQuad3D(mesh, 0, "icPostFilter", 0);
  
  for(iint tstep=0;tstep < mesh->NtimeSteps;++tstep){
    for (iint Ntick=0; Ntick < pow(2,mesh->MRABNlevels-1);Ntick++) {
      
      iint lev;
      for (lev=0;lev<mesh->MRABNlevels;lev++)
	if (Ntick % (1<<lev) !=0) break; //find the max lev to add to rhsq

      iint levS;
      for (levS=0;levS<mesh->MRABNlevels;levS++)
	if ((Ntick+1) % (1<<levS) !=0) break; //find the max lev to add to rhsq
	
      for (iint rk = 0; rk < mesh->Nrk; ++rk) {
	
	//synthesize actual stage time
	dfloat t = tstep*pow(2,mesh->MRABNlevels-1) + Ntick;
      
	if(mesh->totalHaloPairs>0){
	  // extract halo on DEVICE
	  iint Nentries = mesh->Np*mesh->Nfields;
	  
	  mesh->haloExtractKernel(mesh->totalHaloPairs,
				  Nentries,
				  mesh->o_haloElementList,
				  mesh->o_qpre,
				  mesh->o_haloBuffer);

	  // copy extracted halo to HOST 
	  mesh->o_haloBuffer.copyTo(sendBuffer);      
	
	  // start halo exchange
	  meshHaloExchangeStart(mesh,
				mesh->Np*mesh->Nfields*sizeof(dfloat),
				sendBuffer,
				recvBuffer);
	}

	// compute volume contribution to DG advection RHS
	mesh->volumePreKernel(mesh->Nelements,
			      mesh->o_vgeo,
			      mesh->o_D,
			      mesh->o_x,
			      mesh->o_y,
			      mesh->o_z,
			      mesh->o_qpre,
			      mesh->o_prerhsq);

	if(mesh->totalHaloPairs>0){
	  // wait for halo data to arrive
	  meshHaloExchangeFinish(mesh);
	
	  // copy halo data to DEVICE
	  size_t offset = mesh->Np*mesh->Nfields*mesh->Nelements*sizeof(dfloat); // offset for halo data
	  mesh->o_q.copyFrom(recvBuffer, haloBytes, offset);
	  mesh->device.finish();
	}
	
	mesh->surfacePreKernel(mesh->Nelements,
			       mesh->o_sgeo,
			       mesh->o_LIFTT,
			       mesh->o_vmapM,
			       mesh->o_vmapP,
			       t,
			       mesh->o_x,
			       mesh->o_y,
			       mesh->o_z,
			       mesh->o_qpre,
			       mesh->o_prerhsq);
	
	for (iint l = 0; l < mesh->MRABNlevels; ++l) {
	  iint saved = (l < lev)&&(rk == 0);
	  mesh->filterKernelHLSERK(mesh->MRABNelements[l],
				   mesh->o_MRABelementIds[l],
				   alpha,
				   saved,
				   mesh->MRABshiftIndex[l],
				   mesh->o_dualProjMatrix,
				   mesh->o_cubeFaceNumber,
				   mesh->o_EToE,
				   mesh->o_x,
				   mesh->o_y,
				   mesh->o_z,
				   mesh->o_prerhsq,
				   mesh->o_qPreFilter,
				   mesh->o_rhsq);
	}

	for (iint l = 0; l < mesh->MRABNlevels; ++l) {
	  iint saved = (l < lev)&&(rk == 0);
	  mesh->filterKernelVLSERK(mesh->MRABNelements[l],
				   mesh->o_MRABelementIds[l],
				   alpha,
				   saved,
				   mesh->MRABshiftIndex[l],
				   mesh->o_dualProjMatrix,
				   mesh->o_cubeFaceNumber,
				   mesh->o_EToE,
				   mesh->o_x,
				   mesh->o_y,
				   mesh->o_z,
				   mesh->o_qPreFilter,
				   mesh->o_qPreFiltered,
				   mesh->o_qFilter);
	}
	
	/*	for (iint l = 0; l < mesh->MRABNlevels; l++) {
		iint saved = (l < lev)&&(rk == 0);
		mesh->volumeCorrPreKernel(mesh->MRABNelements[l],
		mesh->o_MRABelementIds[l],
		saved,
		mesh->MRABshiftIndex[l],
		mesh->o_qpre,
		mesh->o_qCorr,
		mesh->o_qPreCorr);
		}*/
	for (iint l = 0; l < mesh->MRABNlevels; l++) {
	  iint saved = (l < lev)&&(rk == 0);
	  
	  if (mesh->MRABNelements[l]) {
	    mesh->updatePreKernel(mesh->MRABNelements[l],
				  mesh->o_MRABelementIds[l],
				  saved,
				  mesh->dt,
				  mesh->rka[rk],
				  mesh->rkb[rk],
				  mesh->MRABshiftIndex[l],
				  mesh->o_qPreFiltered,
				  //mesh->o_prerhsq,
				  mesh->o_qFiltered,
				  //mesh->o_rhsq,
				  mesh->o_qPreCorr,
				  mesh->o_resq,
				  mesh->o_qpre);
	    
	    saved = (l < levS)&&(rk == mesh->Nrk-1);
	    if (saved)
	      mesh->MRABshiftIndex[l] = (mesh->MRABshiftIndex[l]+mesh->Nrhs-1)%mesh->Nrhs;
	  }
	}
      }
      //runs after all rk iterations complete
      for (iint l = 0; l < levS; ++l) {
	mesh->traceUpdatePreKernel(mesh->MRABNelements[l],
				   mesh->o_MRABelementIds[l],
				   mesh->o_vmapM,
				   mesh->o_fQM,
				   mesh->o_qpre,
				   mesh->o_q);
      }
    }
    if (tstep==0 || (tstep+1) % mesh->errorStep == 0) {
      mesh->o_qpre.copyTo(mesh->q);
      dfloat t = mesh->dt*(tstep+1)*pow(2,mesh->MRABNlevels-1);
      advectionErrorNormQuad3D(mesh, t, "foo", (tstep+1)/mesh->errorStep);
    }

  }

  
  free(recvBuffer);
  free(sendBuffer);
}