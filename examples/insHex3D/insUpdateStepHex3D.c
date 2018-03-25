#include "insHex3D.h"

void insUpdateStepHex3D(ins_t *ins, int tstep, char   * options){

  mesh3D *mesh = ins->mesh;
  dfloat t = tstep*ins->dt + ins->dt;

  dlong offset = (mesh->Nelements+mesh->totalHaloPairs);

  if (strstr(ins->pSolverOptions,"IPDG")) {
    if(mesh->totalHaloPairs>0){

      ins->pressureHaloExtractKernel(mesh->Nelements,
                                 mesh->totalHaloPairs,
                                 mesh->o_haloElementList,
                                 ins->o_PI,
                                 ins->o_pHaloBuffer);

      // copy extracted halo to HOST
      ins->o_pHaloBuffer.copyTo(ins->pSendBuffer);

      // start halo exchange
      meshHaloExchangeStart(mesh,
                           mesh->Np*sizeof(dfloat),
                           ins->pSendBuffer,
                           ins->pRecvBuffer);
    }
  }
  
  occaTimerTic(mesh->device,"GradientVolume");
  // Compute Volume Contribution of gradient of pressure gradient
  dlong ioffset = 0;
  ins->gradientVolumeKernel(mesh->Nelements,
                            mesh->o_vgeo,
                            mesh->o_D,
                            ioffset,  //no offset
                            ins->o_PI,
                            ins->o_PIx,
                            ins->o_PIy,
                            ins->o_PIz);
  occaTimerToc(mesh->device,"GradientVolume");

  if (strstr(ins->pSolverOptions,"IPDG")) {
    if(mesh->totalHaloPairs>0){
      meshHaloExchangeFinish(mesh);

      ins->o_pHaloBuffer.copyFrom(ins->pRecvBuffer);

      ins->pressureHaloScatterKernel(mesh->Nelements,
                                      mesh->totalHaloPairs,
                                      mesh->o_haloElementList,
                                      ins->o_PI,
                                      ins->o_pHaloBuffer);
    }
    
    const int solverid =1 ;

    occaTimerTic(mesh->device,"GradientSurface");
    // // Compute Surface Contribution of gradient of pressure increment
    ins->gradientSurfaceKernel(mesh->Nelements,
                                mesh->o_sgeo,
                                mesh->o_vmapM,
                                mesh->o_vmapP,
                                mesh->o_EToB,
                                mesh->o_x,
                                mesh->o_y,
                                mesh->o_z,
                                t,
                                ins->dt,
                                ins->c0,
                                ins->c1,
                                ins->c2,
                                ins->index,
                                offset,
                                solverid, // pressure increment BCs
                                ins->o_P,
                                ins->o_PI,
                                ins->o_PIx,
                                ins->o_PIy,
                                ins->o_PIz);
    
    occaTimerToc(mesh->device,"GradientSurface");
  }

  // U <= U - dt/g0 * d(pressure increment)/dx
  // V <= V - dt/g0 * d(pressure increment)/dy

  // occaTimerTic(mesh->device,"UpdateUpdate");
  ins->updateUpdateKernel(mesh->Nelements,
                              ins->dt,
                              ins->ig0,
                              ins->a0,
                              ins->a1,
                              ins->a2,
                              ins->c0,
                              ins->c1,
                              ins->c2,
                              ins->o_PI,
                              ins->o_PIx,
                              ins->o_PIy,
                              ins->o_PIz,
                              ins->index,
                              offset,
                              ins->o_U,
                              ins->o_V,
                              ins->o_W,
                              ins->o_P);
  occaTimerToc(mesh->device,"UpdateUpdate");

  ins->index = (ins->index+1)%3; //hard coded for 3 stages
}
